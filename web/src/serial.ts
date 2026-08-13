// WebSerial transport + wire protocol encoding, mirroring the firmware's
// protocol.cpp exactly (see ../../firmware/src/protocol.cpp and
// ../../CLAUDE.md for the full command grammar this implements).
//
// Handshake: firmware sends "ttyrdy;" once after boot, then "ttyack;"
// (no trailing newline) after each processed command. We track ack state
// with a simple pending-resolve queue so callers can await confirmation,
// same as the original MiSTer-side shell script's
// `read -d ";" ttyresponse < ${TTYDEVICE}`.

const BAUD_RATE = 115200;
// Generous margin over the slowest legitimate transitionReveal() case
// (fade at the "slow" preset: ~12 full-frame SPI pushes at the firmware's
// 10MHz SPI clock is already a ~1.1s floor before any compute/delay - see
// firmware/src/display.cpp's fadeSteps cap). A timeout that fires while the
// firmware is still legitimately busy is worse than one that's generous:
// the fallback here makes enqueue() advance to the next queued send, whose
// write() has no timeout of its own and will then block on backpressure
// against a device that hasn't gotten back to reading Serial yet.
const ACK_TIMEOUT_MS = 6000;

// The ESP32-C3's native USB-CDC RX queue is small (falls back to ~4KB at
// runtime - a 16KB request silently fails when free heap is tight, see
// CLAUDE.md/protocol.cpp's readExact). Writing a whole multi-KB JPEG in one
// write() call overflows that queue before the firmware's loop() gets a
// chance to drain it, which is what caused every CMDCORC transfer to time
// out on real hardware. Pacing in small chunks with a brief gap fixes it -
// confirmed against real hardware (full 10352-byte transfer completed in
// 214ms once paced, vs. timing out entirely when sent in one shot).
const CHUNK_BYTES = 512;
const CHUNK_DELAY_MS = 10;

export type ConnectionState = "disconnected" | "connecting" | "connected";

export class SerialLink extends EventTarget {
  private port: SerialPort | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private readLoopPromise: Promise<void> | null = null;
  private textBuffer = "";
  private pendingAck: (() => void) | null = null;
  private _state: ConnectionState = "disconnected";

  // Serializes sendCommand()/sendColorArt() calls onto a single queue. Both
  // methods write in multiple awaited steps (header, then paced JPEG chunks,
  // then wait for ttyack) - without this, two overlapping calls (e.g. a
  // debounced live-preview send still in flight when the user clicks "Send
  // to device", or drags the image again before the previous send's ack
  // arrives) interleave their bytes on the same underlying writer, which the
  // firmware then misparses as garbage. Confirmed against real hardware:
  // this is what caused "corrupts after a few sends" even after removing
  // the firmware-side debug delays that made it more likely to reproduce.
  private queue: Promise<void> = Promise.resolve();

  private enqueue<T>(fn: () => Promise<T>): Promise<T> {
    const result = this.queue.then(fn, fn);
    this.queue = result.then(
      () => undefined,
      () => undefined,
    );
    return result;
  }

  get state(): ConnectionState {
    return this._state;
  }

  private setState(s: ConnectionState) {
    this._state = s;
    this.dispatchEvent(new CustomEvent("statechange", { detail: s }));
  }

  static isSupported(): boolean {
    return "serial" in navigator;
  }

  async connect(): Promise<void> {
    if (!SerialLink.isSupported()) {
      throw new Error("Web Serial API not available (use Chrome or Edge)");
    }
    this.setState("connecting");
    this.port = await navigator.serial.requestPort();
    await this.port.open({ baudRate: BAUD_RATE });
    this.writer = this.port.writable!.getWriter();
    this.reader = this.port.readable!.getReader();
    this.readLoopPromise = this.readLoop();
    this.setState("connected");
  }

  async disconnect(): Promise<void> {
    this.reader?.cancel().catch(() => {});
    await this.readLoopPromise?.catch(() => {});
    this.writer?.releaseLock();
    this.reader?.releaseLock();
    await this.port?.close().catch(() => {});
    this.port = null;
    this.writer = null;
    this.reader = null;
    this.setState("disconnected");
  }

  private async readLoop(): Promise<void> {
    const decoder = new TextDecoder();
    try {
      while (this.reader) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (!value) continue;
        this.textBuffer += decoder.decode(value, { stream: true });
        this.consumeTokens();
      }
    } catch {
      // port closed / disconnected mid-read; disconnect() handles cleanup
    }
  }

  private consumeTokens() {
    let idx: number;
    while ((idx = this.textBuffer.indexOf(";")) !== -1) {
      const token = this.textBuffer.slice(0, idx);
      this.textBuffer = this.textBuffer.slice(idx + 1);
      if (token.endsWith("ttyack")) {
        this.pendingAck?.();
        this.pendingAck = null;
      } else if (token.endsWith("ttyrdy")) {
        this.dispatchEvent(new CustomEvent("ready"));
      }
    }
  }

  private async write(bytes: Uint8Array): Promise<void> {
    if (!this.writer) throw new Error("Not connected");
    await this.writer.write(bytes);
  }

  /** Write in small chunks with a brief gap, see CHUNK_BYTES/CHUNK_DELAY_MS above. */
  private async writePaced(bytes: Uint8Array): Promise<void> {
    if (!this.writer) throw new Error("Not connected");
    for (let offset = 0; offset < bytes.length; offset += CHUNK_BYTES) {
      await this.writer.write(bytes.subarray(offset, offset + CHUNK_BYTES));
      await new Promise((resolve) => setTimeout(resolve, CHUNK_DELAY_MS));
    }
  }

  private waitForAck(): Promise<void> {
    return new Promise((resolve) => {
      this.pendingAck = resolve;
      setTimeout(() => {
        if (this.pendingAck === resolve) {
          this.pendingAck = null;
          resolve(); // don't hang the UI forever if CMDSTTYACK,0 is set device-side
        }
      }, ACK_TIMEOUT_MS);
    });
  }

  /** Send a plain command line, e.g. "CMDCLS" or "CMDCON,128". */
  async sendCommand(line: string): Promise<void> {
    return this.enqueue(async () => {
      const ackPromise = this.waitForAck();
      await this.write(new TextEncoder().encode(line + "\n"));
      await ackPromise;
    });
  }

  /**
   * Send new-protocol color art: CMDCORC,<name>,<effect>,<durationMs>,<length>\n
   * followed by exactly <length> raw JPEG bytes. Length-prefixed by design
   * (see protocol.cpp's readExact) so chunked writes here never truncate.
   * durationMs controls the on-device transition speed (see effects.ts).
   */
  async sendColorArt(coreName: string, effect: number, durationMs: number, jpegBytes: Uint8Array): Promise<void> {
    return this.enqueue(async () => {
      const header = `CMDCORC,${coreName},${effect},${durationMs},${jpegBytes.length}\n`;
      const ackPromise = this.waitForAck();
      await this.write(new TextEncoder().encode(header));
      await this.writePaced(jpegBytes);
      await ackPromise;
    });
  }

  /**
   * Send legacy-protocol picture data: CMDCOR,<name>,<effect>\n followed by
   * exactly `bytes.length` raw bytes (2048 for 1bpp XBM, 8192 for 4bpp GSC -
   * see legacyEncode.ts). Unlike CMDCORC this is NOT length-prefixed - the
   * original firmware (and ours) classifies the picture purely by byte
   * count, matching the original tool's fixed-size transfer. Still paced
   * the same way as sendColorArt, since the original hardware's RX buffer
   * size is unknown and pacing costs nothing when the buffer is plenty big.
   */
  async sendLegacyPicture(coreName: string, effect: number, bytes: Uint8Array): Promise<void> {
    return this.enqueue(async () => {
      const header = `CMDCOR,${coreName},${effect}\n`;
      const ackPromise = this.waitForAck();
      await this.write(new TextEncoder().encode(header));
      await this.writePaced(bytes);
      await ackPromise;
    });
  }
}
