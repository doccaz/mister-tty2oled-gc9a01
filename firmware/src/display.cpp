#include "display.h"
#include "gc9a01.h"
#include "pins.h"
#include <JPEGDEC.h>
#include <cmath>
#include <cstring>

namespace {

constexpr int16_t DISP_W = 240;
constexpr int16_t DISP_H = 240;
constexpr int16_t DISP_CX = DISP_W / 2;
constexpr int16_t DISP_CY = DISP_H / 2;

// Largest 4:1 (256:64) rect inscribed in the 240px circle, used for legacy
// grayscale art. LEGACY_W:LEGACY_H (240:60) is exactly the same 4:1 ratio
// as the source LEGACY_SRC_W:LEGACY_SRC_H (256:64), so the whole source
// image is scaled down uniformly (16:15) to fit - no cropping and no
// letterbox bars needed, since the aspect ratios already match exactly.
constexpr int16_t LEGACY_W = 240;
constexpr int16_t LEGACY_H = 60;
constexpr int16_t LEGACY_Y = DISP_CY - LEGACY_H / 2;
constexpr int16_t LEGACY_SRC_W = 256;
constexpr int16_t LEGACY_SRC_H = 64;
constexpr int16_t LEGACY_SRC_LINE_1BPP = LEGACY_SRC_W / 8; // 32
constexpr int16_t LEGACY_SRC_LINE_4BPP = LEGACY_SRC_W / 2; // 128

GC9A01Display gfx;
JPEGDEC jpeg;

uint8_t g_contrast = 128;
uint8_t g_rotation = 0;

// LEDC backlight PWM channel. Using the legacy ledcSetup/ledcAttachPin/
// ledcWrite(channel,...) API (not the newer ledcAttach(pin,...) API) since
// this targets arduino-esp32 core 2.0.x, which doesn't have the latter.
constexpr int LEDC_CHANNEL_BL = 0;

// Full-frame decode target for CMDCORC JPEGs, so a transition effect can
// reveal it onto the glass in a pattern other than JPEG's natural
// top-to-bottom MCU decode order. 240x240x2 bytes = 112.5KB - fits the
// C3's ~320KB usable DRAM alongside JPEGDEC's own (much smaller) working
// buffers. Colors are stored byte-swapped (big-endian on the wire) ready
// for GC9A01Display::pushRect(), which writes bytes as-is over SPI.
uint16_t g_frame[DISP_W * DISP_H];

inline uint16_t swap16(uint16_t v) {
  return (v >> 8) | (v << 8);
}

int jpegDrawCallback(JPEGDRAW *pDraw) {
  for (int row = 0; row < pDraw->iHeight; row++) {
    int y = pDraw->y + row;
    if (y < 0 || y >= DISP_H) continue;
    int rowLen = pDraw->iWidth;
    int x0 = pDraw->x;
    if (x0 < 0) rowLen += x0, x0 = 0; // clip left overhang
    if (x0 + rowLen > DISP_W) rowLen = DISP_W - x0; // clip right overhang
    if (rowLen <= 0) continue;
    memcpy(&g_frame[y * DISP_W + x0], &pDraw->pPixels[row * pDraw->iWidth + (x0 - pDraw->x)], rowLen * sizeof(uint16_t));
  }
  return 1;
}

// delay(stepDelay) on its own assumes the pushRect()/compute work above it
// is free, which it isn't - a full-frame pushRect() alone costs ~92ms at
// the driver's 10MHz SPI clock (240*240*2 bytes), so a naive per-step
// delay() on top of that lets a "slow" (1200ms/30-step) fade balloon to
// 4+ seconds of real wall time, blowing well past the 3s ack timeout
// web/src/serial.ts uses to decide a send finished - which then let the
// browser start writing the NEXT queued send while this device was still
// stuck here not reading Serial, wedging its write() on backpressure with
// no timeout of its own. Subtracting elapsed step time keeps total
// transitionReveal() wall time close to the requested durationMs
// regardless of how expensive the step's own draw work is.
void stepDelay(uint32_t stepStart, uint16_t targetStepMs) {
  uint32_t elapsed = millis() - stepStart;
  if (elapsed < targetStepMs) delay(targetStepMs - elapsed);
}

// Curated transition set, mirrored in web/src/effects.ts so the id numbers
// sent over CMDCORC mean the same thing on both ends. Reveals g_frame onto
// the display over durationMs; effect 0 (or durationMs == 0) draws
// instantly. Step counts are capped so effects stay responsive even at
// long durations - each step still redraws its whole visible region
// rather than tracking per-frame deltas, which is simpler and cheap
// enough at 240x240.
void transitionReveal(uint8_t effect, uint16_t durationMs) {
  if (effect == 0 || durationMs == 0) {
    gfx.pushRect(0, 0, DISP_W, DISP_H, g_frame);
    return;
  }

  const int steps = constrain(durationMs / 30, 4, 30);
  const uint16_t stepMs = durationMs / steps;

  switch (effect) {
    case 1: // wipe left -> right
      for (int s = 1; s <= steps; s++) {
        uint32_t t0 = millis();
        int w = (DISP_W * s) / steps;
        gfx.pushRect(0, 0, w, DISP_H, g_frame);
        stepDelay(t0, stepMs);
      }
      break;

    case 2: // wipe right -> left
      for (int s = 1; s <= steps; s++) {
        uint32_t t0 = millis();
        int w = (DISP_W * s) / steps;
        int x0 = DISP_W - w;
        for (int y = 0; y < DISP_H; y++) {
          gfx.pushRect(x0, y, w, 1, &g_frame[y * DISP_W + x0]);
        }
        stepDelay(t0, stepMs);
      }
      break;

    case 3: // wipe top -> bottom
      for (int s = 1; s <= steps; s++) {
        uint32_t t0 = millis();
        int h = (DISP_H * s) / steps;
        gfx.pushRect(0, 0, DISP_W, h, g_frame);
        stepDelay(t0, stepMs);
      }
      break;

    case 4: { // iris: expanding circle from center
      const int cx = DISP_W / 2;
      const int cy = DISP_H / 2;
      const float maxR = sqrtf((float)(cx * cx + cy * cy));
      for (int s = 1; s <= steps; s++) {
        uint32_t t0 = millis();
        float r = maxR * s / steps;
        int yStart = max(0, (int)(cy - r));
        int yEnd = min(DISP_H - 1, (int)(cy + r));
        for (int y = yStart; y <= yEnd; y++) {
          float dy = y - cy;
          float dxf = sqrtf(max(0.0f, r * r - dy * dy));
          int x0 = max(0, (int)(cx - dxf));
          int x1 = min(DISP_W - 1, (int)(cx + dxf));
          int w = x1 - x0 + 1;
          if (w > 0) gfx.pushRect(x0, y, w, 1, &g_frame[y * DISP_W + x0]);
        }
        stepDelay(t0, stepMs);
      }
      break;
    }

    case 5: { // fade: cross-dissolve from black via per-pixel RGB565 blend
      // Unlike the wipes/iris (which only ever push a partial region), fade
      // redraws the FULL frame every step - at the driver's 10MHz SPI
      // clock that's ~92ms of pure SPI time per step regardless of
      // durationMs, so steps is capped much lower here (30 full frames
      // would be a ~2.76s SPI floor alone, uncomfortably close to
      // web/src/serial.ts's ack timeout even before any compute time).
      const int fadeSteps = min(steps, 12);
      const uint16_t fadeStepMs = durationMs / fadeSteps;
      static uint16_t blended[DISP_W * DISP_H];
      for (int s = 1; s <= fadeSteps; s++) {
        uint32_t t0 = millis();
        int t256 = (256 * s) / fadeSteps; // fixed-point 0..256 blend factor
        for (int i = 0; i < DISP_W * DISP_H; i++) {
          uint16_t c = swap16(g_frame[i]); // unswap to compute, reswap to store
          uint8_t r = ((c >> 11) & 0x1F) * t256 / 256;
          uint8_t g = ((c >> 5) & 0x3F) * t256 / 256;
          uint8_t b = (c & 0x1F) * t256 / 256;
          blended[i] = swap16((r << 11) | (g << 5) | b);
        }
        gfx.pushRect(0, 0, DISP_W, DISP_H, blended);
        stepDelay(t0, fadeStepMs);
      }
      break;
    }

    default:
      gfx.pushRect(0, 0, DISP_W, DISP_H, g_frame);
      break;
  }
}

} // namespace

void display_init() {
  pinMode(PIN_LCD_BL, OUTPUT);
  ledcSetup(LEDC_CHANNEL_BL, 5000, 8); // 5kHz, 8-bit duty
  ledcAttachPin(PIN_LCD_BL, LEDC_CHANNEL_BL);
  ledcWrite(LEDC_CHANNEL_BL, g_contrast);

  gfx.begin();
  gfx.fillScreenFast(0x0000);
}

void display_clear() {
  gfx.fillScreenFast(0x0000);
}

void display_flush() {
  // Draws happen immediately; nothing to flush. Present for CMDDUPD compat.
}

void display_set_rotation(uint8_t rot) {
  g_rotation = rot ? 1 : 0;
  gfx.setRotation(g_rotation ? 2 : 0); // 180 degree flip, matches CMDROT semantics
}

void display_set_contrast(uint8_t level) {
  g_contrast = level;
  ledcWrite(LEDC_CHANNEL_BL, g_contrast);
}

void display_on() {
  ledcWrite(LEDC_CHANNEL_BL, g_contrast);
}

void display_off() {
  ledcWrite(LEDC_CHANNEL_BL, 0);
}

void display_show_start_screen() {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xFFFF);
  gfx.setTextSize(2);
  gfx.setCursor(DISP_CX - 60, DISP_CY - 20);
  gfx.print("tty2oled");
  gfx.setTextSize(1);
  gfx.setCursor(DISP_CX - 30, DISP_CY + 10);
  gfx.print("GC9A01");
}

void display_show_corename(const String &name) {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xFFFF);
  gfx.setTextSize(2);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(name, 0, 0, &x1, &y1, &w, &h);
  int16_t x = DISP_CX - w / 2;
  int16_t y = DISP_CY - h / 2;
  if (x < 10) x = 10; // keep inside the circle's safe area
  gfx.setCursor(x, y);
  gfx.print(name);
}

void display_show_error(const String &msg) {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xF800); // red
  gfx.setTextSize(1);
  gfx.setCursor(20, DISP_CY);
  gfx.print(msg);
}

void display_draw_text(int16_t x, int16_t y, uint8_t fontSize, const String &text) {
  gfx.setTextColor(0xFFFF);
  gfx.setTextSize(fontSize == 0 ? 1 : fontSize);
  gfx.setCursor(x, y);
  gfx.print(text);
}

void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, bool fill) {
  if (fill) gfx.fillRect(x, y, w, h, 0xFFFF);
  else gfx.drawRect(x, y, w, h, 0xFFFF);
}

void display_draw_circle(int16_t x, int16_t y, int16_t r, bool fill) {
  if (fill) gfx.fillCircle(x, y, r, 0xFFFF);
  else gfx.drawCircle(x, y, r, 0xFFFF);
}

void display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  gfx.drawLine(x0, y0, x1, y1, 0xFFFF);
}

void display_draw_legacy_xbm(const uint8_t *buf, uint8_t effect) {
  gfx.fillScreenFast(0x0000);
  for (int16_t y = 0; y < LEGACY_H; y++) {
    // Nearest-neighbor scale (not crop) - see LEGACY_W/H comment above.
    int16_t srcY = (y * LEGACY_SRC_H) / LEGACY_H;
    for (int16_t x = 0; x < LEGACY_W; x++) {
      int16_t srcX = (x * LEGACY_SRC_W) / LEGACY_W;
      uint8_t byte = buf[srcX / 8 + srcY * LEGACY_SRC_LINE_1BPP];
      bool on = bitRead(byte, srcX % 8);
      gfx.drawPixel(x, LEGACY_Y + y, on ? 0xFFFF : 0x0000);
    }
    if (effect) delay(1); // crude wipe pacing; curated-effect placeholder
  }
}

void display_draw_legacy_gsc(const uint8_t *buf, uint8_t effect) {
  gfx.fillScreenFast(0x0000);
  for (int16_t y = 0; y < LEGACY_H; y++) {
    // Nearest-neighbor scale (not crop) - see LEGACY_W/H comment above.
    int16_t srcY = (y * LEGACY_SRC_H) / LEGACY_H;
    for (int16_t x = 0; x < LEGACY_W; x++) {
      int16_t srcX = (x * LEGACY_SRC_W) / LEGACY_W;
      int16_t byteIdx = (srcX / 2) + srcY * LEGACY_SRC_LINE_4BPP;
      uint8_t byte = buf[byteIdx];
      uint8_t nibble = (srcX % 2 == 0) ? (byte >> 4) : (byte & 0x0F); // 4-bit grayscale
      uint8_t gray = nibble * 17; // 0..15 -> 0..255
      uint16_t color = ((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3); // RGB565
      gfx.drawPixel(x, LEGACY_Y + y, color);
    }
    if (effect) delay(1);
  }
}

void display_draw_jpeg(const uint8_t *buf, size_t len, uint8_t effect, uint16_t durationMs) {
  memset(g_frame, 0, sizeof(g_frame));
  if (!jpeg.openRAM(const_cast<uint8_t *>(buf), len, jpegDrawCallback)) {
    display_show_error("JPEG decode failed");
    return;
  }
  int imgW = jpeg.getWidth();
  int imgH = jpeg.getHeight();
  jpeg.setPixelType(RGB565_BIG_ENDIAN);
  // Center-crop: the web app is expected to export already-square,
  // circle-safe 240x240 JPEGs, but center anyway to tolerate other sizes.
  int offX = (DISP_W - imgW) / 2;
  int offY = (DISP_H - imgH) / 2;
  jpeg.decode(offX, offY, 0);
  jpeg.close();

  transitionReveal(effect, durationMs);
}

void display_self_test() {
  const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFE0, 0xF81F, 0x07FF, 0xFFFF, 0x0000};
  const char *names[] = {"RED", "GREEN", "BLUE", "YELLOW", "MAGENTA", "CYAN", "WHITE", "BLACK"};
  int cycle = 0;
  while (true) {
    for (int i = 0; i < 8; i++) {
      gfx.fillScreenFast(colors[i]);
      gfx.setTextColor(colors[i] == 0x0000 ? 0xFFFF : 0x0000);
      gfx.setTextSize(2);
      gfx.setCursor(20, 100);
      gfx.print(names[i]);
      gfx.setTextSize(1);
      gfx.setCursor(20, 130);
      gfx.print("cycle ");
      gfx.print(cycle);
      // Also exercise the exact pushRect() path used for real JPEG frames
      // (not just fillScreenFast's per-row loop), same buffer size as a
      // full 240x240 frame.
      for (int p = 0; p < DISP_W * DISP_H; p++) g_frame[p] = colors[i];
      gfx.pushRect(0, 0, DISP_W, DISP_H, g_frame);
      delay(600);
    }
    cycle++;
  }
}
