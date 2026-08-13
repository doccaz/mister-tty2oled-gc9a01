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

// Text/geometry primitives for CMDTXT / CMDGEO compatibility
void display_draw_text(int16_t x, int16_t y, uint8_t fontSize, const String &text);
void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool fill);
void display_draw_circle(int16_t x, int16_t y, int16_t r, bool fill);
void display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1);

// Legacy tty2oled picture formats (256x64 source, byte-count discriminated
// by the caller exactly as in the original protocol).
void display_draw_legacy_xbm(const uint8_t *buf, uint8_t effect); // 1bpp, 2048 bytes
void display_draw_legacy_gsc(const uint8_t *buf, uint8_t effect); // 4bpp grayscale, 8192 bytes

// New color/round-native art: raw JPEG bytes, decoded on-device, then
// revealed onto the display using the given transition effect (see
// ../../web/src/effects.ts for the shared id list) over durationMs.
// durationMs == 0 or effect 0 draws instantly.
void display_draw_jpeg(const uint8_t *buf, size_t len, uint8_t effect, uint16_t durationMs);

// Hardware stability self-test: cycles solid full-screen colors + text via
// the same fillScreenFast()/pushRect() code paths used by the real
// rendering functions, repeatedly, entirely decoupled from JPEGDEC/serial
// parsing. Never returns - watch the physical display for the full run.
void display_self_test();
