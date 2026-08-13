#pragma once

// -----------------------------------------------------------------------
// GPIO pin assignments for the ESP32-C3-mini + GC9A01 round display.
// Edit this block to match your board's actual silkscreen/wiring - these
// are sane defaults that avoid the C3's strapping pins (GPIO2, 8, 9) and
// its fixed native-USB pins (GPIO18/19, used automatically by Serial and
// never wired here). Also avoid whatever pins your board's onboard
// display (if it has one, e.g. a small I2C OLED) already uses.
// -----------------------------------------------------------------------

#define PIN_LCD_SCLK 4
#define PIN_LCD_MOSI 6
#define PIN_LCD_CS   7
#define PIN_LCD_DC   5
#define PIN_LCD_RST  10
#define PIN_LCD_BL   3   // backlight, must be PWM (LEDC) capable

// GC9A01 panels are write-only in this design; no MISO wiring needed.
