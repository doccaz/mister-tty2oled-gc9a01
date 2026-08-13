#include "gc9a01.h"
#include "pins.h"
#include <SPI.h>

namespace {
SPISettings spiSettings(10000000, MSBFIRST, SPI_MODE0);

// Shared by pushRect()/pushRectBatched(): writes h rows of w RGB565
// pixels each, reading row `r` starting at `data + r*srcStride` (not
// `data + r*w` - see gc9a01.h's pushRect() comment for why that
// distinction matters). Assumes the address window and DC=HIGH are
// already set by the caller.
void writeRectData(int16_t w, int16_t h, const uint16_t *data, int16_t srcStride) {
  if (srcStride <= 0) srcStride = w;
  // Chunked instead of one single transferBytes() call for the whole
  // buffer: a single ~115KB (240x240) transfer blocks for a long enough
  // stretch to trigger memory corruption on this hardware/core combo -
  // found by testing against real hardware, isolated by freezing the
  // firmware immediately after each stage of display_draw_jpeg() and
  // observing corruption only ever appeared once pushRect() was reached.
  // Small chunks with an explicit yield() between them avoid whatever the
  // underlying issue is (starved USB-CDC ISR, watchdog-adjacent timing,
  // or similar) without needing to root-cause the SPI/IDF internals.
  constexpr size_t CHUNK_PIXELS = 512;
  for (int16_t row = 0; row < h; row++) {
    const uint8_t *rowBytes = reinterpret_cast<const uint8_t *>(data + (size_t)row * srcStride);
    size_t rowBytesLen = (size_t)w * 2;
    size_t offset = 0;
    while (offset < rowBytesLen) {
      size_t chunk = min(CHUNK_PIXELS * 2, rowBytesLen - offset);
      SPI.transferBytes(rowBytes + offset, nullptr, chunk);
      offset += chunk;
      yield();
    }
  }
}
} // namespace

void GC9A01Display::writeCommand(uint8_t cmd) {
  digitalWrite(PIN_LCD_DC, LOW);
  SPI.transfer(cmd);
}

void GC9A01Display::writeData(uint8_t data) {
  digitalWrite(PIN_LCD_DC, HIGH);
  SPI.transfer(data);
}

void GC9A01Display::setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  writeCommand(0x2A); // column address set
  writeData(x0 >> 8);
  writeData(x0 & 0xFF);
  writeData(x1 >> 8);
  writeData(x1 & 0xFF);

  writeCommand(0x2B); // row address set
  writeData(y0 >> 8);
  writeData(y0 & 0xFF);
  writeData(y1 >> 8);
  writeData(y1 & 0xFF);

  writeCommand(0x2C); // memory write
}

void GC9A01Display::begin() {
  pinMode(PIN_LCD_CS, OUTPUT);
  pinMode(PIN_LCD_DC, OUTPUT);
  pinMode(PIN_LCD_RST, OUTPUT);
  digitalWrite(PIN_LCD_CS, HIGH);

  SPI.begin(PIN_LCD_SCLK, -1 /* MISO */, PIN_LCD_MOSI, PIN_LCD_CS);

  digitalWrite(PIN_LCD_RST, LOW);
  delay(20);
  digitalWrite(PIN_LCD_RST, HIGH);
  delay(120);

  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_LCD_CS, LOW);

  // GC9A01 vendor register-init sequence (see gc9a01.h header comment for provenance).
  writeCommand(0xEF);
  writeCommand(0xEB); writeData(0x14);
  writeCommand(0xFE);
  writeCommand(0xEF);
  writeCommand(0xEB); writeData(0x14);
  writeCommand(0x84); writeData(0x40);
  writeCommand(0x85); writeData(0xFF);
  writeCommand(0x86); writeData(0xFF);
  writeCommand(0x87); writeData(0xFF);
  writeCommand(0x88); writeData(0x0A);
  writeCommand(0x89); writeData(0x21);
  writeCommand(0x8A); writeData(0x00);
  writeCommand(0x8B); writeData(0x80);
  writeCommand(0x8C); writeData(0x01);
  writeCommand(0x8D); writeData(0x01);
  writeCommand(0x8E); writeData(0xFF);
  writeCommand(0x8F); writeData(0xFF);
  writeCommand(0xB6); writeData(0x00); writeData(0x20);
  writeCommand(0x3A); writeData(0x05); // COLMOD: 16bpp RGB565
  writeCommand(0x90); writeData(0x08); writeData(0x08); writeData(0x08); writeData(0x08);
  writeCommand(0xBD); writeData(0x06);
  writeCommand(0xBC); writeData(0x00);
  writeCommand(0xFF); writeData(0x60); writeData(0x01); writeData(0x04);
  writeCommand(0xC3); writeData(0x13);
  writeCommand(0xC4); writeData(0x13);
  writeCommand(0xC9); writeData(0x22);
  writeCommand(0xBE); writeData(0x11);
  writeCommand(0xE1); writeData(0x10); writeData(0x0E);
  writeCommand(0xDF); writeData(0x21); writeData(0x0C); writeData(0x02);
  writeCommand(0xF0); writeData(0x45); writeData(0x09); writeData(0x08); writeData(0x08); writeData(0x26); writeData(0x2A);
  writeCommand(0xF1); writeData(0x43); writeData(0x70); writeData(0x72); writeData(0x36); writeData(0x37); writeData(0x6F);
  writeCommand(0xF2); writeData(0x45); writeData(0x09); writeData(0x08); writeData(0x08); writeData(0x26); writeData(0x2A);
  writeCommand(0xF3); writeData(0x43); writeData(0x70); writeData(0x72); writeData(0x36); writeData(0x37); writeData(0x6F);
  writeCommand(0xED); writeData(0x1B); writeData(0x0B);
  writeCommand(0xAE); writeData(0x77);
  writeCommand(0xCD); writeData(0x63);
  writeCommand(0x70); writeData(0x07); writeData(0x07); writeData(0x04); writeData(0x0E); writeData(0x0F); writeData(0x09); writeData(0x07); writeData(0x08); writeData(0x03);
  writeCommand(0xE8); writeData(0x34);
  writeCommand(0x62); writeData(0x18); writeData(0x0D); writeData(0x71); writeData(0xED); writeData(0x70); writeData(0x70);
                       writeData(0x18); writeData(0x0F); writeData(0x71); writeData(0xEF); writeData(0x70); writeData(0x70);
  writeCommand(0x63); writeData(0x18); writeData(0x11); writeData(0x71); writeData(0xF1); writeData(0x70); writeData(0x70);
                       writeData(0x18); writeData(0x13); writeData(0x71); writeData(0xF3); writeData(0x70); writeData(0x70);
  writeCommand(0x64); writeData(0x28); writeData(0x29); writeData(0xF1); writeData(0x01); writeData(0xF1); writeData(0x00); writeData(0x07);
  writeCommand(0x66); writeData(0x3C); writeData(0x00); writeData(0xCD); writeData(0x67); writeData(0x45); writeData(0x45);
                       writeData(0x10); writeData(0x00); writeData(0x00); writeData(0x00);
  writeCommand(0x67); writeData(0x00); writeData(0x3C); writeData(0x00); writeData(0x00); writeData(0x00); writeData(0x01);
                       writeData(0x54); writeData(0x10); writeData(0x32); writeData(0x98);
  writeCommand(0x74); writeData(0x10); writeData(0x85); writeData(0x80); writeData(0x00); writeData(0x00); writeData(0x4E); writeData(0x00);
  writeCommand(0x98); writeData(0x3E); writeData(0x07);
  writeCommand(0x35);
  writeCommand(0x21);
  writeCommand(0x11); // sleep out
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
  delay(120);

  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_LCD_CS, LOW);
  writeCommand(0x29); // display on
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
  delay(20);

  setRotation(0);
}

void GC9A01Display::setRotation(uint8_t r) {
  rotation = r % 4;
  _width = WIDTH;
  _height = HEIGHT;

  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_LCD_CS, LOW);
  writeCommand(0x36); // MADCTL
  switch (rotation) {
    case 0: writeData(0x08); break;              // normal (BGR)
    case 1: writeData(0x68); break;               // 90 degree
    case 2: writeData(0xC8); break;               // 180 degree
    case 3: writeData(0xA8); break;               // 270 degree
  }
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
}

void GC9A01Display::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (x < 0 || y < 0 || x >= _width || y >= _height) return;
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_LCD_CS, LOW);
  setAddrWindow(x, y, x, y);
  digitalWrite(PIN_LCD_DC, HIGH);
  SPI.transfer16(color);
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
}

void GC9A01Display::pushRect(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data, int16_t srcStride) {
  if (w <= 0 || h <= 0) return;
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_LCD_CS, LOW);
  setAddrWindow(x, y, x + w - 1, y + h - 1);
  digitalWrite(PIN_LCD_DC, HIGH);
  writeRectData(w, h, data, srcStride);
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
}

void GC9A01Display::beginBatch() {
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_LCD_CS, LOW);
}

void GC9A01Display::pushRectBatched(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *data, int16_t srcStride) {
  if (w <= 0 || h <= 0) return;
  setAddrWindow(x, y, x + w - 1, y + h - 1);
  digitalWrite(PIN_LCD_DC, HIGH);
  writeRectData(w, h, data, srcStride);
}

void GC9A01Display::endBatch() {
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
}

void GC9A01Display::fillScreenFast(uint16_t color) {
  static uint16_t rowBuf[240];
  for (int i = 0; i < 240; i++) rowBuf[i] = (color >> 8) | (color << 8); // pre-swap once
  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_LCD_CS, LOW);
  setAddrWindow(0, 0, _width - 1, _height - 1);
  digitalWrite(PIN_LCD_DC, HIGH);
  for (int y = 0; y < _height; y++) {
    SPI.transferBytes(reinterpret_cast<const uint8_t *>(rowBuf), nullptr, _width * 2);
  }
  digitalWrite(PIN_LCD_CS, HIGH);
  SPI.endTransaction();
}
