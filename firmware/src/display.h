#pragma once

#include <Arduino.h>

// Round GC9A01 display: 240x240, RGB565. All legacy (rectangular 256x64)
// art is scaled/letterboxed into the circle's inscribed safe rect; new
// CMDCORC JPEG art is expected to already be authored round-safe by the
// web app and is drawn centered/cropped to the full 240x240 circle.

void display_init();

void display_clear();
void display_flush();                      // no-op placeholder (GFX draws immediately); kept for CMDDUPD compat
void display_set_rotation(uint8_t rot);     // 0 = normal, 1 = 180 degrees (mirrors CMDROT semantics)
void display_set_contrast(uint8_t level);   // 0..255 -> backlight PWM
void display_on();
void display_off();

void display_show_start_screen();
void display_show_corename(const String &name);
void display_show_error(const String &msg);
void display_show_bye();                        // CMDBYE - built-in text/shape screen, not the original's icon bitmap
void display_show_test_pattern();                // CMDTEST - concentric color rings, not the original's bitmap
void display_show_sysinfo(const String &fwVersion); // CMDSHSYSHW

// WiFi bootstrap screens (see wifi_manager.h). display_toggle_wifi_qr()
// flips between the AP-status screen most recently shown via
// display_show_ap_mode() and a scannable WiFi-join QR code for that same
// AP - takes no arguments, it remembers the ssid/ip itself.
void display_show_ap_mode(const String &ssid, const String &ip);
void display_show_connecting_wifi(const String &ssid);
void display_show_wifi_connected(const String &ssid, const String &ip, const String &hostname);
void display_toggle_wifi_qr();

// MQTT notification banner (see mqtt_client.h) - fills a banner region
// and draws a single truncated line (no word wrap for v1). Does not
// remember/restore prior content itself - mqtt_client.cpp's revert timer
// calls protocol_redisplay_current() for that, same as the screensaver
// wake path already does.
void display_show_notification_text(const String &text);

// Text/geometry primitives for CMDTXT / CMDGEO compatibility
void display_draw_text(int16_t x, int16_t y, uint8_t fontSize, const String &text);
void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool fill);
void display_draw_circle(int16_t x, int16_t y, int16_t r, bool fill);
void display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

// Legacy tty2oled picture formats (256x64 source, byte-count discriminated
// by the caller exactly as in the original protocol).
void display_draw_legacy_xbm(const uint8_t *buf, uint8_t effect); // 1bpp, 2048 bytes
void display_draw_legacy_gsc(const uint8_t *buf, uint8_t effect); // 4bpp grayscale, 8192 bytes

// Reduced-size ("1/4 area") redisplay of an already-received legacy
// buffer, for CMDSSCP. No effect parameter - the reference command takes
// none either.
void display_draw_legacy_xbm_small(const uint8_t *buf);
void display_draw_legacy_gsc_small(const uint8_t *buf);

// Re-reveals the already-decoded CMDCORC frame (g_frame) with a new
// transition, for CMDSPIC - no JPEG redecode needed since the last
// decoded frame is still sitting in display.cpp's static buffer.
void display_replay_jpeg(uint8_t effect, uint16_t durationMs);

// Reduced-size ("1/4 area") redisplay of the last-decoded CMDCORC frame,
// for CMDSSCP. Downsampled directly from g_frame, no redecode.
void display_draw_jpeg_small();

// New color/round-native art: raw JPEG bytes, decoded on-device, then
// revealed onto the display using the given transition effect (see
// ../../web/src/effects.ts for the shared id list) over durationMs.
// durationMs == 0 or effect 0 draws instantly.
void display_draw_jpeg(const uint8_t *buf, size_t len, uint8_t effect, uint16_t durationMs);

// Decodes and pushes straight to the display, WITHOUT touching g_frame -
// unlike display_draw_jpeg(), this does not become "the last shown
// picture" (see mqtt_client.h's notification-revert design: an overlay
// that clobbered g_frame would make reverting just redraw itself). No
// transition effect - always instant, matching the banner-overlay style
// display_show_notification_text() already uses for the text case.
void display_draw_jpeg_transient(const uint8_t *buf, size_t len);

// Hardware stability self-test: cycles solid full-screen colors + text via
// the same fillScreenFast()/pushRect() code paths used by the real
// rendering functions, repeatedly, entirely decoupled from JPEGDEC/serial
// parsing. Never returns - watch the physical display for the full run.
void display_self_test();
