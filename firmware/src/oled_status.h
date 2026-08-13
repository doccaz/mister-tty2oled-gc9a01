#pragma once

// Onboard 0.42" SSD1306 OLED (72x40 visible area, 128x64 controller RAM)
// status display - separate I2C panel from the main GC9A01 SPI display,
// shows firmware/connection status rather than marquee art. See pins.h
// for PIN_OLED_SDA/PIN_OLED_SCL.
//
// The same ESP32-C3-mini board ships both with and without this OLED, so
// support is compiled in/out via the HAS_ONBOARD_OLED build flag (see
// platformio.ini's env:esp32c3 vs env:esp32c3_nooled). All four functions
// below are always declared and safe to call unconditionally from
// main.cpp/protocol.cpp; when HAS_ONBOARD_OLED=0 they compile to no-ops.

void oled_status_init();

// Call every loop() iteration. Non-blocking, internally rate-limited
// (redraws at most every ~100ms) and skips entirely while
// oled_status_suspend() is in effect.
void oled_status_loop();

// Must be held for the duration of any serial-critical section (JPEG/XBM/
// GSC picture transfer reads) - an I2C sendBuffer() takes long enough to
// risk dropping bytes out of Serial's RX queue if it runs mid-transfer.
// Nest-safe via a counter, not a bool.
void oled_status_suspend();
void oled_status_resume();
