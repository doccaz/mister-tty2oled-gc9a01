#pragma once

// -----------------------------------------------------------------------
// GPIO pin assignments for the ESP32-C3-mini + GC9A01 round display.
// Edit this block to match your board's actual silkscreen/wiring - these
// are sane defaults that avoid the C3's strapping pins (GPIO2, 8, 9), its
// fixed native-USB pins (GPIO18/19, used automatically by Serial and never
// wired here), the in-package flash pins on FN4/FH4 modules (GPIO11-17),
// and GPIO5/6, which this board's onboard 0.42" SSD1306 OLED (72x40,
// driven as a 128x64 panel) uses as fixed I2C SDA/SCL - not
// user-selectable, since it's built into the board rather than wired by
// hand. GPIO20/21 are deliberately left unclaimed for the Serial0 debug
// UART (see protocol.cpp's DBG_ENABLED).
// -----------------------------------------------------------------------

#define PIN_LCD_SCLK 4
#define PIN_LCD_MOSI 0
#define PIN_LCD_CS   7
#define PIN_LCD_DC   1
#define PIN_LCD_RST  10
#define PIN_LCD_BL   3   // backlight, must be PWM (LEDC) capable

// GC9A01 panels are write-only in this design; no MISO wiring needed.

// Onboard 0.42" SSD1306 OLED (72x40 visible, 128x64 controller RAM),
// status display. Fixed pins on this board variant.
#define PIN_OLED_SDA 5
#define PIN_OLED_SCL 6
