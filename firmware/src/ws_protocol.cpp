#include "ws_protocol.h"
#include "protocol.h"
#include "oled_status.h"
#include "version.h"
#include <WebSocketsServer.h>

namespace {

WebSocketsServer wsServer(81);
bool wsReady = false;

// Tiny local comma-split helper for the one WS-specific text grammar
// (CMDCORC's header fields) - protocol.cpp's own splitField() is
// file-local there, and duplicating one small helper here is cheaper and
// safer than exposing protocol.cpp's internals just for this.
String field(const String &s, int index) {
  int start = 0;
  int comma = -1;
  for (int i = 0; i <= index; i++) {
    comma = s.indexOf(',', start);
    if (comma == -1) return (i == index) ? s.substring(start) : "";
    if (i == index) return s.substring(start, comma);
    start = comma + 1;
  }
  return "";
}

void handleText(uint8_t num, uint8_t *payload, size_t length) {
  // WebSockets malloc()s text payloads with one extra byte for a null
  // terminator (see WebSockets.cpp) - safe to treat as a C string.
  String line = String(reinterpret_cast<char *>(payload));
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("CMDCORC")) {
    String name = field(line, 1);
    int effect = field(line, 2).toInt();
    long durationMs = field(line, 3).toInt();
    long totalLen = field(line, 4).toInt();
    if (totalLen <= 0 || !protocol_ws_xfer_begin(name, (uint8_t)effect, (uint16_t)durationMs, (size_t)totalLen)) {
      wsServer.sendTXT(num, "ERR CMDCORC;");
      return;
    }
    // No ttyack here - one is sent once the chunked binary transfer
    // completes (see handleBinary()), matching the serial path's "ack
    // after the whole picture is drawn" behavior.
    return;
  }

  if (line == "CMDHWINF") {
    // The only command that replies - special-cased here instead of
    // going through protocol_dispatch_line(), which would write the
    // reply to Serial (the wrong transport for a WS client). Same
    // grammar as the serial reply, see protocol.cpp.
    wsServer.sendTXT(num, "HWGC9A01C;" FW_VERSION ";");
    protocol_note_activity();
    return;
  }

  protocol_dispatch_line(line);
  protocol_note_activity();
  wsServer.sendTXT(num, "ttyack;");
}

void handleBinary(uint8_t num, uint8_t *payload, size_t length) {
  if (!protocol_ws_xfer_in_progress()) return; // no transfer pending - malformed client, ignore

  // Same hazard as the serial picture path: an I2C sendBuffer() (~20ms)
  // can starve wsServer.loop()'s TCP servicing mid-transfer if it isn't
  // suspended around the append, exactly like protocol.cpp's readExact()
  // wraps its reads (see CLAUDE.md "Onboard status OLED").
  oled_status_suspend();
  bool ok = protocol_ws_xfer_append(payload, length);
  oled_status_resume();

  if (!ok) {
    wsServer.sendTXT(num, "ERR xfer overflow;");
    return;
  }
  if (protocol_ws_xfer_complete()) {
    protocol_ws_xfer_finish();
    protocol_note_activity();
    wsServer.sendTXT(num, "ttyack;");
  }
}

void onWsEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_TEXT:
      handleText(num, payload, length);
      break;
    case WStype_BIN:
      handleBinary(num, payload, length);
      break;
    case WStype_DISCONNECTED:
      // Don't leave a half-received transfer stuck waiting for chunks
      // that will never arrive from a client that's gone. Only one
      // transfer is ever in flight at a time (protocol_ws_xfer_begin()
      // rejects a second), so "any disconnect" is an adequate signal
      // without tracking which client owns the pending transfer.
      if (protocol_ws_xfer_in_progress()) protocol_ws_xfer_abort();
      break;
    default:
      break;
  }
}

} // namespace

void ws_protocol_init() {
  wsServer.begin();
  wsServer.onEvent(onWsEvent);
  wsReady = true;
}

void ws_protocol_loop() {
  if (wsReady) wsServer.loop();
}
