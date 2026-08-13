#pragma once

#include <Adafruit_GFX.h>
#include <Arduino.h>

// Minimal direct-SPI GC9A01 driver, built on plain Arduino SPIClass.
//
// Why this exists instead of a display library: both
// moononournation/GFX Library for Arduino and TFT_eSPI have real ESP32-C3
// compatibility bugs in the versions available for this project (see
// CLAUDE.md "Why a custom GC9A01 driver"). Plain SPIClass was verified
// working on this exact board via a standalone diagnostic before writing
// this. The init command sequence is the GC9A01's standard vendor
// register-init sequence (adapted from TFT_eSPI's TFT_Drivers/GC9A01_Init.h,
// MIT licensed) - functional hardware-bringup data, not display-library
// code, so this stays a thin ~150-line driver rather than pulling in a
// whole library's worth of untested C3 codepaths again.
//
// Subclasses Adafruit_GFX purely for its software text/shape routines
// (no display-specific driver code in Adafruit_GFX itself, so nothing
// C3-specific to break) - drawPixel() is the only primitive it strictly
// needs from us. pushRect()/fillScreenFast() are our own fast-path
// additions for bulk blits (full JPEG frames, transition effects), which
// would be far too slow going through per-pixel drawPixel() calls.
class GC9A01Display : public Adafruit_GFX {
 public:
  GC9A01Display() : Adafruit_GFX(240, 240) {}

  void begin();
  void setRotation(uint8_t r) override;

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;

  // Fast bulk blit: pushes `data` (RGB565, big-endian, w*h pixels) into
  // the rectangle (x, y, w, h). Used for JPEG frames and transition effects.
  void pushRect(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data);
  void fillScreenFast(uint16_t color);

 private:
  void writeCommand(uint8_t cmd);
  void writeData(uint8_t data);
  void setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
};
