#pragma once

// WiFi command protocol - the same CMDxxx grammar as protocol.cpp's
// serial path, tunneled over a WebSocket (port 81) instead of USB-CDC.
// See CLAUDE.md's "Wire protocol over WiFi" for the full design
// (chunked art transfer, why no legacy XBM/GSC over this transport, why
// it's STA-only).

void ws_protocol_init(); // starts the WebSocketsServer; call once, from wifi_manager's enterConnected()
void ws_protocol_loop();  // pumps the server; call every loop() iteration, no-op before ws_protocol_init()
