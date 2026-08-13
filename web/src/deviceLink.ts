// Shared transport interface - SerialLink (WebSerial, serial.ts) and
// WifiLink (WebSocket, wifiLink.ts) both implement this, so the rest of
// the app (main.ts's send/preview logic) only ever depends on this shape,
// not on which transport is actually connected.

export type ConnectionState = "disconnected" | "connecting" | "connected";

export interface DeviceLink extends EventTarget {
  readonly state: ConnectionState;

  // `target` is transport-specific and optional: SerialLink ignores it
  // (WebSerial's browser port picker needs no address), WifiLink requires
  // it (the device's hostname/IP). Kept as one uniform signature rather
  // than diverging per-transport connect() shapes.
  connect(target?: string): Promise<void>;
  disconnect(): Promise<void>;

  sendCommand(line: string): Promise<void>;
  sendColorArt(coreName: string, effect: number, durationMs: number, jpegBytes: Uint8Array): Promise<void>;
  sendLegacyPicture(coreName: string, effect: number, bytes: Uint8Array): Promise<void>;
}
