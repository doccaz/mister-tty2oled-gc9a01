#pragma once

#include <Arduino.h>

// Serial command protocol: a superset of venice1200/MiSTer_tty2oled's
// wire format. See ../reference/MiSTer_SSD1322_USB/MiSTer_SSD1322_USB.ino
// and ../CLAUDE.md for the original grammar this stays compatible with.
//
// - protocol_init() sends "ttyrdy;" once, exactly like the original.
// - protocol_process() must be called every loop() iteration; it reads at
//   most one line per call and dispatches it, sending "ttyack;" afterward
//   (unless disabled via CMDSTTYACK,0), exactly like the original.

void protocol_init();
void protocol_process();

// The shared command grammar - every CMDxxx case except CMDHWINF (which
// replies directly on its transport, so has no single "reply channel" to
// share) and the picture-transfer commands (which need transport-specific
// byte sourcing - blocking Serial reads here, chunked WS binary frames in
// ws_protocol.cpp). protocol_process() (serial) and ws_protocol.cpp (WiFi)
// both call this for everything else, so the grammar itself lives in
// exactly one place.
void protocol_dispatch_line(const String &cmd);

// Touches both activity timestamps a dispatched command should count as -
// see protocol_last_activity_ms()/protocol_saver_check() below. Any
// transport dispatching a command must call this itself (protocol_process()
// already does, for the serial path); skipping it makes oled_status.cpp's
// RX indicator lie and lets the screensaver blank mid-session under an
// active WiFi client.
void protocol_note_activity();

// Shared picture-transfer state for ws_protocol.cpp's chunked CMDCORC-
// equivalent (see CLAUDE.md "Wire protocol over WiFi"). Binary frames are
// capped small by the WS protocol design itself, appended into the same
// static colorBuf the serial CMDCORC path already uses - so this is state
// tracking, not a second buffer. protocol_ws_xfer_begin() rejects
// (returns false) an oversized totalLen or a transfer already in
// progress; handleColorPicture()/handleLegacyPicture() (serial) check
// protocol_ws_xfer_in_progress() before touching colorBuf/legacyBuf
// themselves, so the two transports can't race on the same buffer.
bool protocol_ws_xfer_begin(const String &name, uint8_t effect,
                             uint16_t durationMs, size_t totalLen);
bool protocol_ws_xfer_append(const uint8_t *data, size_t len);
bool protocol_ws_xfer_complete(); // true once all totalLen bytes have arrived
void protocol_ws_xfer_finish();   // draws the completed transfer, clears state
void protocol_ws_xfer_abort();
bool protocol_ws_xfer_in_progress();

// Read-only status accessors for oled_status.cpp. Cheap to call from
// loop() - do not call from inside protocol_process()'s dispatch/transfer
// path (see oled_status.cpp).
const String &protocol_get_corename();
unsigned long protocol_last_activity_ms(); // millis() timestamp of last dispatched line

// CMDSAVER/CMDSWSAVER idle-blank check - call every loop() iteration,
// after protocol_process(), never from inside a transfer path. No-op
// when the screensaver is disabled (the default).
void protocol_saver_check();

// Polls the GPIO9 wake button (see pins.h's PIN_WAKE_BTN) - call every
// loop() iteration, before protocol_saver_check() so a press this
// iteration can unblank in the same iteration. Only affects the
// screensaver's idle timer, not oled_status.cpp's serial-RX indicator.
void protocol_button_check();
