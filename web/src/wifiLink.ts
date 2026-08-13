// WiFi transport for the same command protocol SerialLink speaks over
// WebSerial, tunneled over a WebSocket to the firmware's ws_protocol.cpp
// (port 81) instead. See ../../firmware/src/ws_protocol.cpp and
// ../../CLAUDE.md's "Wire protocol over WiFi" for the wire format this
// mirrors - each WS text frame is one complete command/reply (no ';'
// token-splitting needed like the continuous serial byte stream), and
// CMDCORC's JPEG payload is sent as a header frame followed by chunked
// binary frames, matching the firmware's chunked-append design (keeps
// every single allocation the firmware's WebSockets library needs to
// make small, see CLAUDE.md for why that mattered).

import type { DeviceLink, ConnectionState } from "./deviceLink";
export type { ConnectionState } from "./deviceLink";

const PORT = 81;
const ACK_TIMEOUT_MS = 6000; // same rationale as serial.ts's ACK_TIMEOUT_MS

// Matches ws_protocol.cpp's WStype_BIN handler, which appends each frame
// into the firmware's static colorBuf - kept small and fixed so the
// firmware's WebSockets library never needs a large contiguous heap
// allocation to reassemble one frame (see CLAUDE.md's RAM discussion).
const CHUNK_BYTES = 2048;

export class WifiLink extends EventTarget implements DeviceLink {
  private ws: WebSocket | null = null;
  private pendingAck: (() => void) | null = null;
  private _state: ConnectionState = "disconnected";

  // Same reasoning as SerialLink's queue: sendCommand()/sendColorArt()
  // each await multiple steps before resolving, and overlapping calls
  // would interleave frames on the same socket.
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

  /** `host` is the device's hostname (e.g. "tty2oled-XXXX.local") or IP - required. */
  async connect(host?: string): Promise<void> {
    if (!host) throw new Error("Device host/IP required for WiFi connection");
    this.setState("connecting");
    await new Promise<void>((resolve, reject) => {
      const ws = new WebSocket(`ws://${host}:${PORT}/`);
      this.ws = ws;
      ws.onopen = () => {
        // No literal "ttyrdy;" over this transport - the socket's own
        // open event is an unambiguous ready signal (see ws_protocol.cpp).
        this.setState("connected");
        this.dispatchEvent(new CustomEvent("ready"));
        resolve();
      };
      ws.onmessage = (ev) => this.onMessage(ev);
      ws.onclose = () => {
        if (this._state !== "disconnected") this.setState("disconnected");
        // Don't leave a pending sendCommand()/sendColorArt() call waiting
        // out the full ACK_TIMEOUT_MS - an unexpected close (WiFi blip,
        // device reboot) should resolve any in-flight wait immediately,
        // not make the UI look stuck for up to 6s before recovering.
        this.pendingAck?.();
        this.pendingAck = null;
      };
      ws.onerror = () => {
        if (this._state === "connecting") reject(new Error("WiFi connection failed"));
      };
    });
  }

  async disconnect(): Promise<void> {
    this.ws?.close();
    this.ws = null;
    this.setState("disconnected");
  }

  private onMessage(ev: MessageEvent) {
    if (typeof ev.data !== "string") return; // no binary replies expected from the firmware
    const token = ev.data.endsWith(";") ? ev.data.slice(0, -1) : ev.data;
    if (token.endsWith("ttyack")) {
      this.pendingAck?.();
      this.pendingAck = null;
    } else {
      // e.g. CMDHWINF's "HWGC9A01C;0.2.0;" reply, or an "ERR ...;" - same
      // "message" event shape SerialLink uses for anything that isn't an ack.
      this.dispatchEvent(new CustomEvent("message", { detail: token }));
    }
  }

  private waitForAck(): Promise<void> {
    return new Promise((resolve) => {
      this.pendingAck = resolve;
      setTimeout(() => {
        if (this.pendingAck === resolve) {
          this.pendingAck = null;
          resolve(); // don't hang the UI forever if the ack is somehow lost
        }
      }, ACK_TIMEOUT_MS);
    });
  }

  private send(data: string | Uint8Array) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) throw new Error("Not connected");
    this.ws.send(data);
  }

  /** Send a plain command line, e.g. "CMDCLS" or "CMDCON,128". */
  async sendCommand(line: string): Promise<void> {
    return this.enqueue(async () => {
      const ackPromise = this.waitForAck();
      this.send(line);
      await ackPromise;
    });
  }

  /**
   * Send color art over the CMDCORC-equivalent WS grammar: a text header
   * frame ("CMDCORC,<name>,<effect>,<durationMs>,<length>", no ack yet),
   * then the JPEG chunked into CHUNK_BYTES-sized binary frames - the
   * firmware only acks once the last chunk completes the transfer (see
   * ws_protocol.cpp's handleBinary()).
   */
  async sendColorArt(coreName: string, effect: number, durationMs: number, jpegBytes: Uint8Array): Promise<void> {
    return this.enqueue(async () => {
      const ackPromise = this.waitForAck();
      this.send(`CMDCORC,${coreName},${effect},${durationMs},${jpegBytes.length}`);
      for (let offset = 0; offset < jpegBytes.length; offset += CHUNK_BYTES) {
        // TCP has real backpressure, unlike the serial RX-queue problem
        // serial.ts's writePaced() works around - but a cheap bufferedAmount
        // check still avoids piling up frames faster than the socket can
        // actually drain them on a slow/congested WiFi link.
        while (this.ws && this.ws.bufferedAmount > CHUNK_BYTES * 4) {
          await new Promise((resolve) => setTimeout(resolve, 5));
        }
        this.send(jpegBytes.subarray(offset, offset + CHUNK_BYTES));
      }
      await ackPromise;
    });
  }

  /**
   * No legacy XBM/GSC transfer over WiFi - that grammar exists for real-
   * MiSTer-over-serial compatibility, which doesn't apply to a WS client
   * (see ws_protocol.cpp). Callers should route legacy-profile sends to a
   * SerialLink instead, or avoid offering them while WiFi is selected.
   */
  async sendLegacyPicture(): Promise<void> {
    throw new Error("Legacy picture transfer is not supported over WiFi - use WebSerial instead");
  }
}
