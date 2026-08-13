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

// Shared picture-buffer claim, guarding colorBuf against three possible
// writers: the serial CMDCORC path (handleColorPicture(), a single
// blocking call), ws_protocol.cpp's chunked CMDCORC-equivalent (frames
// arrive across multiple loop() iterations), and mqtt_client.cpp's
// image-by-URL fetch (an HTTPClient stream, also spanning multiple
// loop() iterations). Despite the "ws_xfer" name (kept from when this
// only had two callers, see CLAUDE.md "Wire protocol over WiFi" - not
// worth renaming across every call site for no functional change), any
// of the three claims the same flag before touching colorBuf and
// releases it when done; protocol_ws_xfer_begin() rejects (returns
// false) an oversized totalLen or a claim already held, so the three
// writers can't race the same buffer no matter which pair overlaps.
bool protocol_ws_xfer_begin(const String &name, uint8_t effect,
                             uint16_t durationMs, size_t totalLen);
bool protocol_ws_xfer_append(const uint8_t *data, size_t len);
bool protocol_ws_xfer_complete(); // true once all totalLen bytes have arrived
void protocol_ws_xfer_finish();   // draws the completed transfer, clears state
// Same completion as protocol_ws_xfer_finish(), but draws via
// display_draw_jpeg_transient() and does NOT update lastPictureKind/
// actCorename - for mqtt_client.cpp's image notifications, which must
// leave "what to redisplay when the notification reverts" untouched
// (see CLAUDE.md's MQTT notifications section for why - this is the
// fix for a real bug found in testing where an image notification never
// reverted, because it had overwritten the very state it should have
// reverted back to).
void protocol_ws_xfer_finish_transient();
void protocol_ws_xfer_abort();
bool protocol_ws_xfer_in_progress();

// Redraws whatever was last shown (picture or corename text) - exposed
// for mqtt_client.cpp's notification-revert timer, which needs the same
// "restore previous content" behavior protocol_saver_check() already
// uses internally for the screensaver wake path.
void protocol_redisplay_current(uint8_t effect);

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
