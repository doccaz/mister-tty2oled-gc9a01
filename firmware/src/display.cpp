#include "display.h"
#include "gc9a01.h"
#include "pins.h"
#include "version.h"
#include <JPEGDEC.h>
#include <qrcode.h>
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

// State for display_toggle_wifi_qr(): remembers the AP-status screen's
// ssid/ip (set by display_show_ap_mode()) so a button press can flip to
// the QR view and back without protocol.cpp needing to pass them through.
String g_apSsid;
String g_apIp;
bool g_qrShown = false;

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
        // srcStride=DISP_W required here: g_frame's real row width (240)
        // is wider than this partial-width slice (w), so each row after
        // the first must skip the leftover (240-w) pixels to stay
        // aligned - omitting it was a real bug (see CLAUDE.md).
        gfx.pushRect(0, 0, w, DISP_H, g_frame, DISP_W);
        stepDelay(t0, stepMs);
      }
      break;

    case 2: // wipe right -> left
      for (int s = 1; s <= steps; s++) {
        uint32_t t0 = millis();
        int w = (DISP_W * s) / steps;
        int x0 = DISP_W - w;
        // One stride-aware call instead of a 240-row loop (each iteration
        // of which used to pay a full SPI transaction's setup/teardown
        // cost) - same fix as case 1, plus the performance fix that
        // stopped this effect from stalling on real hardware.
        gfx.pushRect(x0, 0, w, DISP_H, &g_frame[x0], DISP_W);
        stepDelay(t0, stepMs);
      }
      break;

    case 3: // wipe top -> bottom
      for (int s = 1; s <= steps; s++) {
        uint32_t t0 = millis();
        int h = (DISP_H * s) / steps;
        gfx.pushRect(0, 0, DISP_W, h, g_frame, DISP_W); // w==DISP_W already, but explicit for clarity
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
        // The visible x-range differs per row (circular mask), so unlike
        // the wipes this can't collapse into one pushRect() call - but
        // wrapping the per-row calls in one beginBatch()/endBatch() still
        // avoids paying a full SPI transaction's setup/teardown per row,
        // which was expensive enough to stall this effect on real
        // hardware (see CLAUDE.md).
        gfx.beginBatch();
        for (int y = yStart; y <= yEnd; y++) {
          float dy = y - cy;
          float dxf = sqrtf(max(0.0f, r * r - dy * dy));
          int x0 = max(0, (int)(cx - dxf));
          int x1 = min(DISP_W - 1, (int)(cx + dxf));
          int w = x1 - x0 + 1;
          if (w > 0) gfx.pushRectBatched(x0, y, w, 1, &g_frame[y * DISP_W + x0]);
        }
        gfx.endBatch();
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
      // Blend one row at a time into a DISP_W-wide scratch buffer instead of
      // a full 240x240 static buffer (that was a static ~112.5KB - the
      // single biggest thing standing between this firmware and fitting
      // WiFi's DRAM footprint, see CLAUDE.md WiFi planning notes). Wrapped
      // in beginBatch()/pushRectBatched()/endBatch() so this stays one SPI
      // transaction per step instead of 240.
      static uint16_t blendedRow[DISP_W];
      for (int s = 1; s <= fadeSteps; s++) {
        uint32_t t0 = millis();
        int t256 = (256 * s) / fadeSteps; // fixed-point 0..256 blend factor
        gfx.beginBatch();
        for (int y = 0; y < DISP_H; y++) {
          const uint16_t *srcRow = &g_frame[y * DISP_W];
          for (int x = 0; x < DISP_W; x++) {
            uint16_t c = swap16(srcRow[x]); // unswap to compute, reswap to store
            uint8_t r = ((c >> 11) & 0x1F) * t256 / 256;
            uint8_t g = ((c >> 5) & 0x3F) * t256 / 256;
            uint8_t b = (c & 0x1F) * t256 / 256;
            blendedRow[x] = swap16((r << 11) | (g << 5) | b);
          }
          gfx.pushRectBatched(0, y, DISP_W, 1, blendedRow);
        }
        gfx.endBatch();
        stepDelay(t0, fadeStepMs);
      }
      break;
    }

    default:
      gfx.pushRect(0, 0, DISP_W, DISP_H, g_frame);
      break;
  }
}

// Shared nearest-neighbor scaler behind display_draw_legacy_xbm/gsc and
// their CMDSSCP "_small" counterparts - same loop, parameterized by
// destination rect instead of the fixed LEGACY_W/H/Y constants, so the
// reduced-size redisplay path doesn't duplicate the pixel-decode logic.
void drawLegacyScaled(const uint8_t *buf, uint8_t effect, bool isGsc,
                       int16_t dstW, int16_t dstH, int16_t dstX, int16_t dstY) {
  gfx.fillScreenFast(0x0000);
  for (int16_t y = 0; y < dstH; y++) {
    int16_t srcY = (y * LEGACY_SRC_H) / dstH;
    for (int16_t x = 0; x < dstW; x++) {
      int16_t srcX = (x * LEGACY_SRC_W) / dstW;
      uint16_t color;
      if (isGsc) {
        uint8_t byte = buf[(srcX / 2) + srcY * LEGACY_SRC_LINE_4BPP];
        uint8_t nibble = (srcX % 2 == 0) ? (byte >> 4) : (byte & 0x0F); // 4-bit grayscale
        uint8_t gray = nibble * 17; // 0..15 -> 0..255
        color = ((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3); // RGB565
      } else {
        uint8_t byte = buf[srcX / 8 + srcY * LEGACY_SRC_LINE_1BPP];
        color = bitRead(byte, srcX % 8) ? 0xFFFF : 0x0000;
      }
      gfx.drawPixel(dstX + x, dstY + y, color);
    }
    if (effect) delay(1); // crude wipe pacing; curated-effect placeholder
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
  // Backlight PWM alone is confirmed (on real hardware, 2026-08-13) to
  // have no visible effect on at least one GC9A01 module variant - almost
  // certainly BL wired straight to VCC rather than through a
  // GPIO-controlled transistor, not a software bug (CMDCON's contrast
  // level has the exact same ledcWrite() call and is equally unaffected).
  // Blanking by content works regardless of BL wiring, so do that too;
  // display_on() doesn't restore it directly (display.cpp has no
  // generic "last screen" concept beyond g_frame's JPEG-only content) -
  // protocol.cpp's callers are responsible for redrawing after display_on().
  gfx.fillScreenFast(0x0000);
}

void display_show_start_screen() {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xFFFF);
  int16_t x1, y1;
  uint16_t w, h;

  gfx.setTextSize(2);
  gfx.getTextBounds("tty2oled", 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY - 40);
  gfx.print("tty2oled");

  gfx.setTextSize(1);
  gfx.getTextBounds("GC9A01  v" FW_VERSION, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY - 5);
  gfx.print("GC9A01  v" FW_VERSION);

  gfx.getTextBounds(REPO_URL, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY + 20);
  gfx.print(REPO_URL);
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
  drawLegacyScaled(buf, effect, false, LEGACY_W, LEGACY_H, 0, LEGACY_Y);
}

void display_draw_legacy_gsc(const uint8_t *buf, uint8_t effect) {
  drawLegacyScaled(buf, effect, true, LEGACY_W, LEGACY_H, 0, LEGACY_Y);
}

void display_draw_legacy_xbm_small(const uint8_t *buf) {
  constexpr int16_t w = LEGACY_W / 2, h = LEGACY_H / 2; // half res each way = ~1/4 area
  drawLegacyScaled(buf, 0, false, w, h, DISP_CX - w / 2, DISP_CY - h / 2);
}

void display_draw_legacy_gsc_small(const uint8_t *buf) {
  constexpr int16_t w = LEGACY_W / 2, h = LEGACY_H / 2;
  drawLegacyScaled(buf, 0, true, w, h, DISP_CX - w / 2, DISP_CY - h / 2);
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

void display_replay_jpeg(uint8_t effect, uint16_t durationMs) {
  // g_frame already holds the last-decoded CMDCORC frame - no redecode
  // needed. Matches the reference's own CMDSPIC, which also just re-runs
  // its reveal routine over the still-resident picture data rather than
  // clearing first: transitionReveal() only ever draws g_frame's pixels
  // over whatever's already on the glass, so replaying it here reveals
  // the same content it's already showing, exactly as upstream does.
  transitionReveal(effect, durationMs);
}

void display_draw_jpeg_small() {
  // Downsampled directly from g_frame (nearest-neighbor, factor 2) rather
  // than through a new intermediate buffer - a 120x120 scratch buffer
  // would cost another ~28KB of static RAM we don't have to spare (see
  // CLAUDE.md "RAM headroom").
  constexpr int16_t w = DISP_W / 2, h = DISP_H / 2;
  constexpr int16_t dstX = (DISP_W - w) / 2, dstY = (DISP_H - h) / 2;
  gfx.fillScreenFast(0x0000);
  for (int16_t y = 0; y < h; y++) {
    int16_t srcY = y * 2;
    for (int16_t x = 0; x < w; x++) {
      int16_t srcX = x * 2;
      uint16_t c = swap16(g_frame[srcY * DISP_W + srcX]); // g_frame is byte-swapped, see its declaration comment
      gfx.drawPixel(dstX + x, dstY + y, c);
    }
  }
}

void display_show_bye() {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xFFFF);
  gfx.drawCircle(DISP_CX, DISP_CY - 30, 20, 0xFFFF);
  gfx.setTextSize(2);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds("Bye!", 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY + 20);
  gfx.print("Bye!");
}

void display_show_test_pattern() {
  const uint16_t colors[] = {0xF800, 0xFFE0, 0x07E0, 0x07FF, 0x001F, 0xF81F};
  constexpr int rings = 6;
  gfx.fillScreenFast(0x0000);
  for (int i = 0; i < rings; i++) {
    int16_t r = (DISP_CX * (rings - i)) / rings;
    gfx.fillCircle(DISP_CX, DISP_CY, r, colors[i]);
  }
}

void display_show_sysinfo(const String &fwVersion) {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xFFFF);
  gfx.setTextSize(1);
  gfx.setCursor(20, DISP_CY - 40);
  gfx.print("SysInfo");
  gfx.setCursor(20, DISP_CY - 20);
  gfx.print("FW: ");
  gfx.print(fwVersion);
  gfx.setCursor(20, DISP_CY);
  gfx.print("Chip: ");
  gfx.print(ESP.getChipModel());
  gfx.setCursor(20, DISP_CY + 20);
  gfx.print("Free heap: ");
  gfx.print(ESP.getFreeHeap());
}

namespace {
void drawApStatusScreen() {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xFFFF);
  int16_t x1, y1;
  uint16_t w, h;

  gfx.setTextSize(2);
  gfx.getTextBounds("AP Mode", 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY - 40);
  gfx.print("AP Mode");

  gfx.setTextSize(1);
  gfx.getTextBounds(g_apSsid, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY - 5);
  gfx.print(g_apSsid);

  gfx.getTextBounds(g_apIp, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY + 15);
  gfx.print(g_apIp);

  const char *hint = "Press button for QR";
  gfx.getTextBounds(hint, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY + 60);
  gfx.print(hint);
}

// Renders a WiFi-join QR (open network, per the confirmed AP design) for
// g_apSsid. Builds the module grid into g_frame as scratch (safe to
// clobber - AP mode never has a decoded CMDCORC JPEG on screen at the
// same time) and pushes it in one pushRect() call, rather than one
// fillRect()/SPI-transaction per module - that per-call transaction cost
// is exactly what made the iris/wipe effects stall before they were
// batched (see CLAUDE.md's "per-row transaction stall" section).
void drawWifiQrScreen() {
  String payload = "WIFI:T:nopass;S:" + g_apSsid + ";;";

  QRCode qrcode;
  uint8_t qrData[qrcode_getBufferSize(4)];
  qrcode_initText(&qrcode, qrData, 4, ECC_LOW, payload.c_str());

  // The QR is a square, but this is a ROUND display - a square sized to
  // the naive 200px "safe area" put its corners (exactly where the
  // finder patterns live) past the physical circular bezel, which
  // clipped them on real hardware. A square centered on a circle of
  // radius R fits entirely inside it only up to side <= R*sqrt(2); using
  // a conservative R (not the full 120px display radius) leaves margin
  // for the glass/bezel being a little smaller than the panel's nominal
  // active area. No vertical offset either - shifting the square off
  //-center for the SSID text below only made the clipping worse on one
  // edge; the SSID text is thin and tolerates sitting near the edge far
  // better than the QR's finder-pattern corners do.
  constexpr int kQrSafeRadius = 100;
  int scale = (int)((kQrSafeRadius * 1.4142f) / qrcode.size);
  if (scale < 1) scale = 1;
  int qrPx = qrcode.size * scale;
  int ox = DISP_CX - qrPx / 2;
  int oy = DISP_CY - qrPx / 2;

  memset(g_frame, 0, sizeof(g_frame));
  for (int y = 0; y < qrPx; y++) {
    uint16_t *row = &g_frame[(oy + y) * DISP_W + ox];
    for (int x = 0; x < qrPx; x++) {
      // 0x0000/0xFFFF are byte-swap invariant, so no swap16() needed here
      // unlike g_frame's usual JPEG-decoded content.
      row[x] = qrcode_getModule(&qrcode, x / scale, y / scale) ? 0xFFFF : 0x0000;
    }
  }

  gfx.fillScreenFast(0x0000);
  gfx.pushRect(ox, oy, qrPx, qrPx, &g_frame[oy * DISP_W + ox], DISP_W);

  gfx.setTextColor(0xFFFF);
  gfx.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(g_apSsid, 0, 0, &x1, &y1, &w, &h);
  int16_t textY = oy + qrPx + 16;
  if (textY > DISP_H - 20) textY = DISP_H - 20; // keep inside the circle's safe area
  gfx.setCursor(DISP_CX - w / 2, textY);
  gfx.print(g_apSsid);
}
} // namespace

void display_show_ap_mode(const String &ssid, const String &ip) {
  g_apSsid = ssid;
  g_apIp = ip;
  g_qrShown = false;
  drawApStatusScreen();
}

void display_show_connecting_wifi(const String &ssid) {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xFFFF);
  gfx.setTextSize(1);
  int16_t x1, y1;
  uint16_t w, h;
  String line = "Connecting to " + ssid;
  gfx.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
  int16_t x = w > DISP_W - 20 ? 10 : DISP_CX - w / 2; // keep inside the circle's safe area
  gfx.setCursor(x, DISP_CY);
  gfx.print(line);
}

void display_show_wifi_connected(const String &ssid, const String &ip) {
  gfx.fillScreenFast(0x0000);
  gfx.setTextColor(0xFFFF);
  int16_t x1, y1;
  uint16_t w, h;

  gfx.setTextSize(2);
  const char *title = "Connected";
  gfx.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY - 30);
  gfx.print(title);

  gfx.setTextSize(1);
  gfx.getTextBounds(ssid, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY);
  gfx.print(ssid);

  gfx.getTextBounds(ip, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor(DISP_CX - w / 2, DISP_CY + 20);
  gfx.print(ip);
}

void display_toggle_wifi_qr() {
  g_qrShown = !g_qrShown;
  if (g_qrShown) {
    drawWifiQrScreen();
  } else {
    drawApStatusScreen();
  }
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
