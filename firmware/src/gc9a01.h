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

  // Fast bulk blit: pushes RGB565 pixel data into the rectangle
  // (x, y, w, h). `data` points at the first pixel of the sub-rectangle's
  // first row - not necessarily the start of a buffer - and each
  // subsequent row is read `srcStride` pixels further along (defaults to
  // `w`, i.e. a densely-packed w*h buffer with no gaps between rows).
  // Pass the *source* buffer's actual row width whenever it's wider than
  // `w` (e.g. display.cpp's 240-wide g_frame when pushing a narrower
  // partial-width slice) - otherwise every row after the first reads from
  // the wrong offset. This was a real bug (see CLAUDE.md "transitionReveal
  // pushRect stride bug").
  void pushRect(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data, int16_t srcStride = 0);
  void fillScreenFast(uint16_t color);

  // For callers doing many small pushRect() calls back to back (e.g.
  // transitionReveal()'s per-row iris mask) - wrapping them in one
  // beginBatch()/endBatch() pair keeps a single SPI transaction/CS-low
  // span open instead of paying full transaction setup/teardown on every
  // single row, which was expensive enough on this hardware to make the
  // right-to-left wipe and iris effects visibly stall (see CLAUDE.md).
  // pushRect() itself is unaffected and still opens/closes its own
  // transaction when called outside a batch.
  void beginBatch();
  void pushRectBatched(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data, int16_t srcStride = 0);
  void endBatch();

 private:
  void writeCommand(uint8_t cmd);
  void writeData(uint8_t data);
  void setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
};
