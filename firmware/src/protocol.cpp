#include "protocol.h"
#include "display.h"
#include "oled_status.h"
#include <Arduino.h>
#include <cstring>

namespace {

// Legacy fixed-size picture buffer: 8192 bytes covers both the 2048-byte
// 1bpp XBM and 8192-byte 4bpp GSC payloads (256x64 source), exactly as in
// the original firmware's logoBin[8192].
constexpr size_t LEGACY_BUF_SIZE = 8192;
uint8_t legacyBuf[LEGACY_BUF_SIZE];

// New color art buffer: length-prefixed, so unlike the legacy path this
// isn't classified by byte count. Static, not malloc'd - found by testing
// against real hardware that malloc(40000) failed intermittently despite
// tens of KB of nominally "free" heap, because the C3's heap was
// fragmented enough that no single 40KB contiguous block was available.
// The failure was silent to the actual bug hunt: handleColorPicture()
// returned immediately on OOM *before* ever reading the incoming JPEG
// bytes off the wire, leaving them to be misread as garbage commands by
// the main loop - which is what all the earlier "corruption" actually
// was. 20000 bytes comfortably covers a compressed 240x240 JPEG (test
// images ran 1.5-10KB) while leaving more RAM headroom than the original
// 40000 cap did.
constexpr size_t COLOR_BUF_MAX = 20000;
uint8_t colorBuf[COLOR_BUF_MAX];

String actCorename = "No Core loaded";
int cDelay = 15;         // ms delay before ttyack;, mirrors original cDelay
bool sendTTYACK = true;  // CMDSTTYACK toggle
unsigned long lastActivityMs = 0; // millis() of last dispatched line, for oled_status.cpp

constexpr const char *FW_VERSION = "0.1.0";
constexpr uint16_t DEFAULT_TRANSITION_MS = 600; // CMDSPIC has no duration param in the wire format

// Tracks which buffer/kind the last successfully-drawn picture came from,
// so CMDSPIC/CMDSSCP can redisplay it without needing the MiSTer to
// resend the bytes - legacyBuf/colorBuf/g_frame already persist between
// commands, this just remembers which one is current.
enum class PictureKind { NONE, XBM, GSC, JPEG };
PictureKind lastPictureKind = PictureKind::NONE;

// CMDSAVER/CMDSWSAVER: parses the reference's full <mode>,<interval>,
// <logotime> grammar (so a real tty2oled.sh never errors) but only
// implements simple blank-on-idle, not the original's animated
// multi-screen/starfield/toaster screensaver system - see CLAUDE.md.
bool saverEnabled = false;
unsigned long saverIntervalMs = 60000;
bool saverBlanked = false;

// Debug channel: hardware UART0 (GPIO20/21, via an external USB-serial
// adapter), completely independent of the native USB-CDC "Serial" object
// used for the actual protocol - useful for diagnosing anything wrong with
// Serial/HWCDC itself without the debug output being affected by the same
// bug. Disabled by default now that the CMDCORC pipeline is confirmed
// working end-to-end on real hardware; flip to 1 to bring it back.
#define DBG_ENABLED 0
#if DBG_ENABLED
#define DBG(...) Serial0.printf(__VA_ARGS__)
#else
#define DBG(...)
#endif

String splitField(const String &s, int index) {
  int start = 0;
  int comma = -1;
  for (int i = 0; i <= index; i++) {
    comma = s.indexOf(',', start);
    if (comma == -1) {
      if (i == index) return s.substring(start);
      return "";
    }
    if (i == index) return s.substring(start, comma);
    start = comma + 1;
  }
  return "";
}

// Clamps to our curated 1..5 effect set (see transitionReveal() in
// display.cpp) and resolves -1 ("no parameter"/"random", matching the
// reference's convention) to a random pick - shared by every command that
// takes a transition effect, so -1 means the same thing everywhere.
uint8_t resolveEffect(int t) {
  if (t < -1) t = -1;
  if (t > 5) t = 5;
  if (t == -1) t = random(1, 6); // random(min, max+1)
  return (uint8_t)t;
}

// Like splitField(), but returns -1 (not 0) when the field is absent -
// the convention CMDSPIC/CMDCOR/CMDAPD use for "no effect given" before
// resolveEffect() turns that into a random pick.
int splitFieldEffect(const String &cmd, int index) {
  String f = splitField(cmd, index);
  return f.length() ? f.toInt() : -1;
}

// Redraws whatever was last shown (picture or corename text), with the
// given transition effect. Backs CMDSPIC directly, and is also what makes
// CMDDON/the screensaver's wake-up actually restore content: display_off()
// blanks by content (see its comment in display.cpp - backlight PWM alone
// is confirmed to have no effect on at least one real GC9A01 module), so
// display_on() alone doesn't bring anything back - this does.
void redisplayCurrent(uint8_t effect) {
  switch (lastPictureKind) {
    case PictureKind::JPEG:
      display_replay_jpeg(effect, DEFAULT_TRANSITION_MS);
      break;
    case PictureKind::XBM:
      display_draw_legacy_xbm(legacyBuf, effect);
      break;
    case PictureKind::GSC:
      display_draw_legacy_gsc(legacyBuf, effect);
      break;
    case PictureKind::NONE:
      display_show_corename(actCorename);
      break;
  }
}

// Reads exactly `size` bytes into buf, matching the original's blocking
// Serial.readBytes() + fixed 500ms timeout behavior.
size_t readFixed(uint8_t *buf, size_t size) {
  Serial.setTimeout(500);
  return Serial.readBytes(reinterpret_cast<char *>(buf), size);
}

// Reads exactly `size` bytes into buf, looping until satisfied or an
// overall timeout elapses. Used for the new length-prefixed color command
// so slow/chunked writes from a browser don't truncate like the legacy
// fixed-size read would.
bool readExact(uint8_t *buf, size_t size, uint32_t timeoutMs = 5000) {
  // Stream::readBytes()/timedRead() busy-spin calling read() with no
  // yield() when nothing's immediately available. On this single-core
  // chip that can starve whatever FreeRTOS task actually feeds bytes from
  // the USB hardware into HWCDC's rx_queue in the first place, since our
  // own tight loop never gives it a chance to run - found by testing
  // against real hardware: host-side write() of the full JPEG completed
  // in ~18ms, but the device only received a couple KB over a 5 SECOND
  // timeout, a pathological slowdown consistent with exactly this kind of
  // task starvation, not a real transfer-rate limit. Explicitly yielding
  // whenever no data is immediately available fixes it.
  DBG("readExact: requesting %u bytes, avail=%d\n", (unsigned)size, Serial.available());
  size_t got = 0;
  uint32_t start = millis();
  uint32_t lastProgress = start;
  while (got < size) {
    if (Serial.available()) {
      int n = Serial.readBytes(reinterpret_cast<char *>(buf + got), min((size_t)Serial.available(), size - got));
      if (n > 0) {
        got += n;
        lastProgress = millis();
        continue;
      }
    }
    if (millis() - lastProgress > timeoutMs) break; // timeout since last byte, not since start
    yield();
  }
  uint32_t t1 = millis();
  DBG("readExact: got %u of %u in %u ms\n", (unsigned)got, (unsigned)size, (unsigned)(t1 - start));
  if (got != size) {
    display_show_error("got " + String((unsigned)got) + " of " + String((unsigned)size));
  }
  return got == size;
}

void handleLegacyPicture(const String &cmd, int prefixLen) {
  String rest = cmd.substring(prefixLen);
  int comma = rest.indexOf(',');
  int effect = -1;
  if (comma == -1) {
    actCorename = rest;
  } else {
    actCorename = rest.substring(0, comma);
    effect = rest.substring(comma + 1).toInt();
  }

  oled_status_suspend();
  size_t n = readFixed(legacyBuf, LEGACY_BUF_SIZE);
  oled_status_resume();
  if (n == 2048) {
    display_draw_legacy_xbm(legacyBuf, resolveEffect(effect));
    lastPictureKind = PictureKind::XBM;
  } else if (n == 8192) {
    display_draw_legacy_gsc(legacyBuf, resolveEffect(effect));
    lastPictureKind = PictureKind::GSC;
  } else {
    display_show_error("Picture xfer error: " + String(n));
  }
}

// CMDCORC,<name>,<effect>,<durationMs>,<length>\n + <length> raw JPEG bytes.
// Length-prefixed by design (see readExact) - the legacy byte-count
// classification trick doesn't work for variable-length JPEG.
void handleColorPicture(const String &cmd) {
  DBG("handleColorPicture: free heap=%u cmd='%s' len=%u\n", (unsigned)ESP.getFreeHeap(), cmd.c_str(), (unsigned)cmd.length());
  String name = splitField(cmd, 1);
  int effect = splitField(cmd, 2).toInt();
  long durationMs = splitField(cmd, 3).toInt();
  long length = splitField(cmd, 4).toInt();
  DBG("parsed name='%s' effect=%d durationMs=%ld length=%ld\n", name.c_str(), effect, durationMs, length);

  if (length <= 0 || (size_t)length > COLOR_BUF_MAX) {
    DBG("REJECT bad length\n");
    display_show_error("Bad CMDCORC length: " + String(length));
    return;
  }

  DBG("calling readExact...\n");
  oled_status_suspend();
  bool ok = readExact(colorBuf, (size_t)length);
  oled_status_resume();
  DBG("readExact returned %d\n", ok);
  if (!ok) {
    return; // readExact() already showed+froze on the got/size mismatch
  }

  actCorename = name;
  display_draw_jpeg(colorBuf, (size_t)length, (uint8_t)effect, (uint16_t)durationMs);
  lastPictureKind = PictureKind::JPEG;
}

void dispatch(const String &cmd) {
  if (cmd == "CMDNULL") {
    // no-op test command
  } else if (cmd == "cls" || cmd == "CMDCLS") {
    display_clear();
    display_flush();
  } else if (cmd == "CMDCLSWU") {
    display_clear();
  } else if (cmd == "sorg" || cmd == "CMDSORG") {
    display_show_start_screen();
  } else if (cmd.startsWith("CMDSECD")) {
    cDelay = cmd.substring(8).toInt();
  } else if (cmd == "CMDSHCD") {
    display_show_error("cDelay: " + String(cDelay));
  } else if (cmd == "CMDSNAM") {
    display_show_corename(actCorename);
  } else if (cmd == "CMDDOFF") {
    display_off();
  } else if (cmd == "CMDDON") {
    display_on();
    redisplayCurrent(0); // instant - display_off() blanked by content, not just backlight, see its comment
  } else if (cmd == "CMDDUPD") {
    display_flush();
  } else if (cmd.startsWith("CMDTXT")) {
    // CMDTXT,x,y,fontSize,text
    int16_t x = splitField(cmd, 1).toInt();
    int16_t y = splitField(cmd, 2).toInt();
    uint8_t fs = (uint8_t)splitField(cmd, 3).toInt();
    String text = splitField(cmd, 4);
    display_draw_text(x, y, fs, text);
  } else if (cmd.startsWith("CMDGEO")) {
    // CMDGEO,type,x,y,w_or_r,h,fill  (type: 0=rect,1=circle,2=line(x2 in w,y2 in h))
    int type = splitField(cmd, 1).toInt();
    int16_t x = splitField(cmd, 2).toInt();
    int16_t y = splitField(cmd, 3).toInt();
    int16_t w = splitField(cmd, 4).toInt();
    int16_t h = splitField(cmd, 5).toInt();
    int fill = splitField(cmd, 6).toInt();
    if (type == 0) display_draw_rect(x, y, w, h, fill != 0);
    else if (type == 1) display_draw_circle(x, y, w, fill != 0);
    else if (type == 2) display_draw_line(x, y, w, h);
  } else if (cmd.startsWith("CMDCORC")) {
    // Must be checked before the CMDCOR/CMDAPD legacy branch below:
    // "CMDCORC,..." also starts with the substring "CMDCOR", so with the
    // checks in the other order every CMDCORC command was silently
    // swallowed by the legacy handler (reading a fixed 8192-byte legacy
    // buffer against a variable-length JPEG payload - byte-count mismatch,
    // "Picture xfer error"). Found by testing against real hardware.
    handleColorPicture(cmd);
  } else if (cmd.startsWith("CMDAPD") || cmd.startsWith("CMDCOR")) {
    handleLegacyPicture(cmd, 6);
  } else if (cmd.startsWith("CMDCON")) {
    display_set_contrast((uint8_t)splitField(cmd, 1).toInt());
  } else if (cmd.startsWith("CMDROT")) {
    display_set_rotation((uint8_t)splitField(cmd, 1).toInt());
  } else if (cmd.startsWith("CMDSTTYACK")) {
    sendTTYACK = splitField(cmd, 1).toInt() != 0;
  } else if (cmd == "CMDBYE") {
    display_show_bye();
  } else if (cmd == "CMDTEST") {
    display_show_test_pattern();
  } else if (cmd == "CMDSHSYSHW") {
    display_show_sysinfo(FW_VERSION);
  } else if (cmd == "CMDHWINF") {
    // Reply grammar matches the reference (<hwid>;<version>;), but the id
    // itself is our own - a new device type outside the reference's
    // enum. Only its own installer.sh keys off this string, not
    // tty2oled.sh itself (confirmed against reference/), so an unknown id
    // is safe. No trailing newline: web/src/serial.ts tokenizes on ";",
    // not "\n", matching how ttyack;/ttyrdy; are already sent.
    Serial.print("HWGC9A01C;");
    Serial.print(FW_VERSION);
    Serial.print(";");
  } else if (cmd.startsWith("CMDCLST")) {
    // CMDCLST,<transition>,<color 0-15> - fill screen with a solid
    // 4bpp-grayscale color and reveal it with a transition. Reused
    // wholesale through the existing legacy-GSC draw path (fill legacyBuf
    // with the solid color, exactly like the reference fills logoBin)
    // rather than adding a separate solid-fill code path, so CMDSPIC/
    // CMDSSCP redisplay of a CMDCLST screen falls out for free.
    String tT = splitField(cmd, 1);
    String cT = splitField(cmd, 2);
    if (tT.length() == 0 || cT.length() == 0) {
      display_show_error("CMDCLST needs transition,color");
    } else {
      int c = cT.toInt();
      if (c < 0) c = 0;
      if (c > 15) c = 15;
      memset(legacyBuf, (uint8_t)((c << 4) | c), LEGACY_BUF_SIZE);
      actCorename = "No Core";
      uint8_t effect = resolveEffect(tT.toInt());
      display_draw_legacy_gsc(legacyBuf, effect);
      lastPictureKind = PictureKind::GSC;
    }
  } else if (cmd.startsWith("CMDSPIC")) {
    // CMDSPIC[,<effect>] - redisplay the last-drawn picture (any kind)
    // with a (possibly random) transition. No redecode: JPEG replays
    // straight from display.cpp's still-resident g_frame, legacy XBM/GSC
    // replays from legacyBuf, which persists between commands already.
    redisplayCurrent(resolveEffect(splitFieldEffect(cmd, 1)));
  } else if (cmd == "CMDSSCP") {
    // Show the last-drawn picture at reduced ("1/4 area") size - no
    // effect parameter, matching the reference.
    switch (lastPictureKind) {
      case PictureKind::JPEG:
        display_draw_jpeg_small();
        break;
      case PictureKind::XBM:
        display_draw_legacy_xbm_small(legacyBuf);
        break;
      case PictureKind::GSC:
        display_draw_legacy_gsc_small(legacyBuf);
        break;
      case PictureKind::NONE:
        display_show_corename(actCorename);
        break;
    }
  } else if (cmd.startsWith("CMDSWSAVER")) {
    int x = splitField(cmd, 1).toInt();
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    saverEnabled = (x == 1);
  } else if (cmd.startsWith("CMDSAVER")) {
    // CMDSAVER,<mode>,<interval>,<logotime> - grammar and range clamps
    // match the reference exactly (mode 0-255, interval 5-600s, logotime
    // 20-600s) so a real tty2oled.sh never errors, but only simple
    // blank-on-idle is implemented (mode>0 => enabled, blank after
    // <interval> seconds idle via the existing display_off()/on()), not
    // the reference's animated starfield/toaster/multi-screen system -
    // see CLAUDE.md "CMDSAVER" for the divergence.
    int m = splitField(cmd, 1).toInt();
    int iv = splitField(cmd, 2).toInt();
    // logotime is parsed for grammar compatibility but intentionally unused.
    if (m < 0) m = 0;
    if (m > 255) m = 255;
    if (iv < 5) iv = 5;
    if (iv > 600) iv = 600;
    saverIntervalMs = (unsigned long)iv * 1000UL;
    saverEnabled = (m > 0);
  } else if (cmd == "CMDRESET") {
    ESP.restart();
  } else if (cmd == "QWERTZ") {
    // First transmission from MiSTer after boot; clears the buffer, ignore.
  } else {
    // Bare line, no CMD prefix: legacy text-only corename fallback.
    // This is what makes an unmodified tty2oled.sh usable with zero
    // MiSTer-side changes.
    actCorename = cmd;
    display_show_corename(actCorename);
  }
}

} // namespace

void protocol_init() {
  // The C3's native USB-CDC (HWCDC) defaults to a 256-byte RX queue -
  // nowhere near enough for a multi-KB CMDCORC JPEG burst; the ISR drops
  // bytes silently once it's full (xQueueSendFromISR failing on a full
  // queue), which readExact() sees as bytes that just never arrive.
  //
  // An earlier attempt requested 16384 bytes here, which *silently
  // failed* every single boot (setRxBufferSize() returns 0 on failure,
  // easy to not check) - free heap at this point in setup() is only
  // ~32KB (most of the C3's ~320KB RAM is consumed by our own two large
  // static 240x240 frame buffers in display.cpp, plus ESP-IDF's own
  // startup allocations), and a 16KB queue doesn't reliably fit. Because
  // HWCDC::begin() falls back to creating its own 256-byte default
  // whenever the queue is still NULL, the failed 16384 request left us
  // silently running with the tiny default this entire time - which is
  // the actual root cause of what looked like a mysterious
  // corruption/throughput bug spanning a very long debugging session:
  // real transfers were mostly being dropped by ISR queue overflow, not
  // "corrupted" or slow. 4096 bytes is confirmed (by testing against
  // real hardware, checking the return value) to allocate reliably and
  // is ample headroom for readExact()'s actively-draining read loop.
#if DBG_ENABLED
  Serial0.begin(115200); // independent hardware UART0 debug channel (GPIO20/21), see DBG() above
#endif
  DBG("free heap at protocol_init: %u\n", (unsigned)ESP.getFreeHeap());
  size_t rxSet = Serial.setRxBufferSize(16384);
  DBG("setRxBufferSize(16384) returned %u\n", (unsigned)rxSet);
  if (rxSet == 0) Serial.setRxBufferSize(4096); // fallback if 16384 still doesn't fit
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  Serial.setTimeout(500);
  delay(cDelay);
  Serial.print("ttyrdy;");
}

void protocol_process() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  DBG("dispatch line len=%u\n", (unsigned)line.length());

  dispatch(line);
  lastActivityMs = millis();

  if (sendTTYACK) {
    delay(cDelay);
    Serial.print("ttyack;");
  }
}

const String &protocol_get_corename() {
  return actCorename;
}

unsigned long protocol_last_activity_ms() {
  return lastActivityMs;
}

void protocol_saver_check() {
  if (!saverEnabled) {
    if (saverBlanked) {
      display_on();
      redisplayCurrent(0);
      saverBlanked = false;
    }
    return;
  }
  bool shouldBlank = (millis() - lastActivityMs) >= saverIntervalMs;
  if (shouldBlank && !saverBlanked) {
    display_off();
    saverBlanked = true;
  } else if (!shouldBlank && saverBlanked) {
    display_on();
    redisplayCurrent(0);
    saverBlanked = false;
  }
}
