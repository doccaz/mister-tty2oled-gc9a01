# CLAUDE.md — tty2oled-gc9a01

A protocol-compatible re-implementation of [venice1200/MiSTer_tty2oled](https://github.com/venice1200/MiSTer_tty2oled)
targeting an **ESP32-C3-mini + GC9A01 240×240 round SPI display**, plus a
standalone **web app** for browsing/editing the per-core marquee library and
previewing it live on the physical display over WebSerial.

Design decisions and full protocol research are in
`/home/erico/.claude/plans/prancy-bouncing-manatee.md` (the approved plan this
was built from). Summary below.

## Scope

- **Per-core** marquee library (~100 images: NES, SNES, Arcade, ao486, …),
  matching the original tty2oled's actual design — not per-game.
- **Protocol superset**: same line-based command grammar and `ttyrdy;`/
  `ttyack;` handshake as the original, so an unmodified `tty2oled.sh` on a
  real MiSTer still works (text corename, contrast, rotation, and even
  legacy 2048/8192-byte grayscale `.xbm`/`.gsc` pictures, scaled/letterboxed
  onto the round display). New commands (`CMDCORC`) add full-color,
  round-native JPEG art for the web app.
- **WebSerial** is the only web-app transport in v1 — no WiFi/AP firmware.

## Local marquee library (imported from the reference project)

`reference/Pictures/ZIPs/` bundles the tty2oled community's marquee packs
(per-core system logos plus several per-game arcade packs, e.g. Cave/Neo
Geo/CPS titles) as `.gsc` (4bpp grayscale) / `.xbm` (1bpp) 256×64 text
files — a C-array-style header followed by hex byte literals (some variants
prefix 6 extra header bytes; see `tools/convert_library.py`'s comment/size
handling). `tools/build_library.sh` extracts the archives and converts
every file to PNG:

```bash
tools/build_library.sh
# -> library/converted/<pack>/<name>.png + index.json
# -> synced into web/public/library/ for the web app
```

The web app's editor has a "Browse local packs" button
(`web/src/importLibrary.ts`) that fetches `/library/index.json`, shows a
searchable thumbnail grid, and loads a selection straight into the Canvas
editor for the currently selected core — from there it's cropped/exported
exactly like any other imported image. These are third-party fan-made
assets from the reference repo, kept local to this project for personal
preview/development use only; they are not redistributed anywhere.

Cores whose id matches a pack filename exactly (case-insensitive - `NES`,
`SNES`, `C64`, …), or matches one of that core's known alternate spellings
in `web/src/aliases.ts` (`CORE_ALIASES` - e.g. pack file `GAMEBOY2P` →
core `Gameboy`, `GBA2P` → `GBA`, `Spectrum` → `ZXSpectrum`), auto-fill in
the library grid and editor at boot (`findAutoMatch` in
`importLibrary.ts`). The alias table is a best-effort guess, not verified
against a live MiSTer - the header's "Review core matches" button opens a
pre-filled, per-row-checkable table of every guessed match (with
already-saved cores unchecked by default so they aren't silently
overwritten) and a "Save checked matches" bulk action
(`openMatchReviewModal` in `main.ts`). Not every core has a guess; those
stay blank until assigned manually.

## Display profile & fit presets

`web/src/displays.ts` defines selectable target displays (`DISPLAY_PROFILES`):
round GC9A01 240×240 — **the default** (2026-08-13; previously the legacy
SSD1322 profile defaulted, to match most existing tty2oled setups, but
the round display is this project's actual, real-hardware-verified
firmware target) — plus a few rectangular options (240×240, 320×240, and
the legacy SSD1322 256×64, matching the reference project's actual
hardware). The header's "display" dropdown is a **global** preview
setting — it reshapes the editor canvas, the library thumbnails, and the
JPEG export resolution, but isn't stored per-core (a core's saved art is a
single blob; switching profiles doesn't keep per-profile versions). Only
the round profile is currently wired to real firmware/hardware — the
rectangular profiles are preview/export targets for a possible future
rect-display firmware variant.

`web/src/transforms.ts` defines fit presets (`cover`, `contain`,
`center-scale`) with a default per display **shape**
(`DEFAULT_TRANSFORM_BY_SHAPE`): round defaults to `contain` (2026-08-13 -
previously `center-scale` at 91%; changed so the whole image is visible,
uncropped, on first load), rect defaults to plain `cover`. Presets are a
one-shot starting point, applied on image load and on profile switch
(`MarqueeEditor.setProfile`/`applyPreset`) - the user can still pan/zoom
manually afterward via the editor's drag/scroll, and re-pick a preset any
time from the "fit" control to reset.

`DISPLAY_PROFILES` (`web/src/displays.ts`) lists round (GC9A01) first in
the dropdown, then the legacy SSD1322 rect profile, then the generic rect
profiles — `DEFAULT_DISPLAY_PROFILE` now matches (round/GC9A01), see
above.

## Layout

```
mister-tty2oled-gc9a01/
├── reference/     cloned upstream MiSTer_tty2oled repo — read-only protocol/format reference, never built
├── library/       converted/ - PNG marquees + index.json, generated by tools/build_library.sh
├── tools/         build_library.sh + convert_library.py (archive extraction / .gsc-.xbm-to-PNG conversion)
├── firmware/      PlatformIO project, ESP32-C3
│   └── src/
│       ├── main.cpp       setup()/loop()
│       ├── pins.h         GPIO pin block — edit to match your board's wiring
│       ├── gc9a01.h/.cpp  minimal direct-SPI GC9A01 driver (built on Arduino_GFX + plain SPIClass)
│       ├── display.h/.cpp GC9A01 rendering (gc9a01.h + JPEGDEC)
│       ├── oled_status.h/.cpp onboard 0.42" SSD1306 status display (I2C, separate from GC9A01)
│       └── protocol.h/.cpp serial command parser/dispatcher
└── web/           Vite + TypeScript SPA
    └── src/
        ├── serial.ts   WebSerial wrapper + wire protocol encode/decode (mirrors protocol.cpp)
        ├── cores.ts    seed list of ~90 MiSTer core names, editable in-app
        ├── db.ts        IndexedDB persistence for per-core art
        ├── editor.ts    Canvas2D crop/pan/zoom + circular safe-area overlay, JPEG export
        └── main.ts      UI wiring: library grid + editor panel + live preview
```

## Wire protocol (firmware ⇄ MiSTer or web app)

115200 baud, line-based ASCII commands terminated by `\n`. Firmware sends
`ttyrdy;` once after boot, then `ttyack;` (no trailing newline) after each
command unless `CMDSTTYACK,0` disables it.

Compatible commands (same behavior as upstream): `CMDCLS`/`cls`, `CMDCLSWU`,
`CMDSORG`/`sorg`, `CMDCON,<0-255>`, `CMDROT,<0|1>`, `CMDTXT,x,y,size,text`,
`CMDGEO,type,x,y,w,h,fill`, `CMDSNAM`, `CMDDOFF`, `CMDDON`, `CMDDUPD`,
`CMDSECD,<ms>`, `CMDSHCD`, `CMDSTTYACK,<0|1>`, `CMDRESET`, and a bare line
with no `CMD` prefix (legacy plain-corename fallback).

Additional compatible commands, added 2026-08-13 and verified on real
hardware (see `protocol.cpp`'s `dispatch()`):

- `CMDBYE`, `CMDTEST`, `CMDSHSYSHW` — cosmetic/diagnostic screens
  (farewell text, concentric-ring test pattern, FW version/chip
  model/free heap). Built entirely from existing `display_draw_text`/
  `draw_circle`-style primitives, not ported bitmap assets — RAM is
  already tight (see "RAM headroom") and the reference's icon/test
  bitmaps are separate-provenance assets we don't have license to reuse.
- `CMDHWINF` — replies over `Serial` with `HW<id>;<version>;` (no
  trailing newline; `web/src/serial.ts` tokenizes on `;`, not `\n`, same
  as `ttyack;`/`ttyrdy;`). Our hardware id is `HWGC9A01C` — a new value
  outside the reference's enum, confirmed safe because only its
  `installer.sh` (a separate setup tool) keys off this string;
  `tty2oled.sh` itself never reads it.
- `CMDCLST,<transition>,<color 0-15>` — fill the screen with a solid
  4bpp-grayscale color and reveal it with a transition. Implemented by
  filling `legacyBuf` with the solid color and reusing the existing
  legacy-GSC draw path wholesale (exactly like the reference fills
  `logoBin`), so `CMDSPIC`/`CMDSSCP` redisplay of a `CMDCLST` screen falls
  out for free rather than needing a separate solid-fill code path.
- `CMDSPIC[,<effect>]` — redisplay the last-drawn picture (any kind) with
  a new transition. No redecode: JPEG-sourced pictures replay straight
  from `display.cpp`'s still-resident `g_frame`; legacy XBM/GSC pictures
  replay from `legacyBuf`, which already persists between commands.
  Tracked via `protocol.cpp`'s `PictureKind lastPictureKind` +
  `redisplayCurrent()`, set by every picture-drawing path (`CMDCOR`/
  `CMDAPD`/`CMDCORC`/`CMDCLST`). Matches the reference's own behavior of
  *not* clearing the screen first — `transitionReveal()`/`oled_drawlogo()`
  both only ever draw new pixels over whatever's already there, so
  replaying a transition over *unchanged* content is visually a no-op in
  both implementations, not a bug.
- `CMDSSCP` — redisplay the last-drawn picture at reduced ("1/4 area")
  size, no effect parameter (matching the reference). JPEG downsamples
  directly from `g_frame` (nearest-neighbor ×2, no new buffer — a 120×120
  scratch buffer would cost another ~28KB of static RAM we don't have to
  spare); legacy XBM/GSC reuses `display.cpp`'s `drawLegacyScaled()`
  helper (shared with the full-size `display_draw_legacy_xbm/gsc`) with a
  smaller destination rect instead of duplicating the pixel-decode loop.
- `CMDSAVER,<mode>,<interval-sec>,<logotime-sec>` / `CMDSWSAVER,<0|1>` —
  **parses the reference's exact grammar and range clamps** (mode 0-255,
  interval 5-600s, logotime 20-600s) so a real `tty2oled.sh` never
  errors, but only implements simple blank-after-idle (`mode>0` ⇒
  enabled, blank both displays after `interval` seconds idle, wake and
  restore both on the next command or GPIO9 press — see "GPIO9 wake
  button" below for the idle-timer details) — **not** the reference's
  animated multi-screen/starfield/toaster screensaver system. `logotime`
  is parsed for grammar compatibility but intentionally unused. This is a
  deliberate scope cut, not an oversight — see `protocol.cpp`'s
  `CMDSAVER`/`CMDSWSAVER` handlers for the exact divergence. Blanks the
  onboard status OLED too (`oled_status_off()`/`_on()`), added
  2026-08-13 once a real burn-in concern was raised for its always-on
  static dashboard — an OLED is emissive, so unlike the GC9A01's
  backlight-only "off", blanking it by content actually stops pixel
  current, not just dims it.

While implementing these, also fixed a latent bug in the existing
`CMDCOR`/`CMDAPD` legacy path: `effect < 0` was silently mapped to a
hardcoded `1` instead of randomizing like the reference's `-1` convention
does. All effect-taking commands (`CMDCOR`/`CMDAPD`/`CMDSPIC`/`CMDCLST`)
now share one `resolveEffect()` helper in `protocol.cpp` that clamps to
our curated `1..5` set and randomizes on `-1`, so `-1` means the same
thing everywhere.

### Backlight PWM has no effect on at least one real GC9A01 module

Discovered 2026-08-13 while testing `CMDSAVER`: `CMDDOFF`/`CMDCON,0`
(both drive the same `ledcWrite()` PWM call on `PIN_LCD_BL`) were
confirmed on real hardware to have **zero visible effect**, even after a
full hardware reset — almost certainly this module's `BL` pin is wired
straight to VCC rather than through a GPIO-controlled transistor, not a
software bug. Since backlight control can't be relied on, `display_off()`
(`display.cpp`) now **blanks by content** (`fillScreenFast(0x0000)`) in
addition to the (harmless, possibly-ineffective) PWM call, and callers in
`protocol.cpp` (`CMDDON`, the screensaver wake path) explicitly call
`redisplayCurrent()` after `display_on()` to restore content -
`display_on()` itself has no generic "what was on screen" concept beyond
`g_frame`'s JPEG-only content, so it can't restore anything by itself.
Confirmed working end-to-end on real hardware for both `CMDDOFF`/`CMDDON`
and the full `CMDSAVER` blank/wake cycle.

Legacy picture transfer: `CMDCOR,<name>,<effect>` or `CMDAPD,...` followed
by a **blocking fixed-size** read of exactly 2048 (1bpp XBM) or 8192 (4bpp
grayscale "GSC") raw bytes, classified purely by byte count — identical to
upstream, including its truncation trap, which is why it's only used for
the legacy path and not for new art.

New color command: `CMDCORC,<name>,<effect>,<durationMs>,<length>\n` +
exactly `<length>` raw JPEG bytes, **length-prefixed** (not
size-classified), read in a loop with an overall timeout — avoids the
byte-count trap for variable-length payloads. Decoded on-device with
JPEGDEC into a full-frame RGB565 buffer (`firmware/src/display.cpp`'s
`g_frame`), then revealed onto the display using `effect` over
`durationMs` (`transitionReveal()`). Effect ids are shared with the web
app's `web/src/effects.ts` so the numbers mean the same thing on both
ends: `0` none/instant, `1`/`2` wipe left↔right, `3` wipe top→bottom, `4`
iris (expanding circle from center), `5` fade (cross-dissolve). The web
app's editor has a "▶ Preview effect" button (with a slow/normal/fast
speed picker) that plays the same reveal pattern locally on the canvas —
useful without a device connected, and representative but not
pixel-identical to the firmware's version (`MarqueeEditor.playEffectPreview`
in `web/src/editor.ts`).

## Firmware pins

Edit `firmware/src/pins.h` to match your board — it's the single place
for all display pins again (`PIN_LCD_SCLK/MOSI/CS/DC/RST/BL`), since the
custom `gc9a01.h`/`.cpp` driver (see below) reads them directly, unlike
the library-based approaches tried earlier that needed pins elsewhere.

Current wiring, **verified on real hardware 2026-08-13** (both the
GC9A01 startup screen and the onboard OLED status dashboard confirmed
showing correctly after physically moving the MOSI/DC jumpers and
reflashing): `SCLK=4, MOSI=0, CS=7, DC=1, RST=10, BL=3`. MOSI/DC were
moved off their original GPIO6/GPIO5 (verified working 2026-08-12) to
free those pins for `PIN_OLED_SDA=5, PIN_OLED_SCL=6` — this board's
onboard 0.42" SSD1306 status OLED (see "Onboard status OLED" below) uses
those two as fixed I2C pins, not user-selectable, since it's built into
the board. Moving these pins in `pins.h` only changes what the firmware
expects; the physical jumper wires must be moved to match, or the GC9A01
goes blank while still compiling and flashing cleanly. Avoid the C3's
strapping pins (GPIO2, 8, 9),
the FN4/FH4 module's in-package flash pins (GPIO11-17), and GPIO20/21
(left free for the `Serial0` debug UART, see `protocol.cpp`'s
`DBG_ENABLED`) if you change these further. The native USB-Serial/JTAG
peripheral is fixed to GPIO18/19 in hardware and used automatically by
`Serial` — never wired manually, and what both a real MiSTer and the
WebSerial web app talk to.

### Onboard status OLED

`firmware/src/oled_status.h`/`.cpp` drives this board's built-in 0.42"
SSD1306 (72×40 visible, addressed as a 128×64 controller with a
`kOffX=28, kOffY=24` window into that RAM — values copied from a working
WLED usermod for the same panel, not re-derived geometrically; a naive
`(128-72)/2, (64-40)/2` centering clips on real hardware) over I2C on
`PIN_OLED_SDA`/`PIN_OLED_SCL` (GPIO5/6). It's a **separate display from
the GC9A01** — shows firmware/connection status (core name, firmware
version, RX activity, uptime), not marquee art. `oled_status_loop()` is rate-limited to ~100ms
redraws and is called from `main.cpp`'s `loop()`. Because an I2C
`sendBuffer()` (~20ms) is long enough to starve `Serial`'s RX queue drain
if it runs mid-transfer (the exact class of bug that broke `CMDCORC`
originally — see below), `protocol.cpp` wraps every blocking picture-byte
read (`readFixed()`/`readExact()`, used by `handleLegacyPicture()` and
`handleColorPicture()`) in `oled_status_suspend()`/`oled_status_resume()`,
which skips redraws entirely for the duration. `protocol_get_corename()`
and `protocol_last_activity_ms()` are the read-only accessors
`oled_status.cpp` polls from `loop()` — cheap to call, never call them
from inside the suspend-guarded transfer path. If `u8g2.begin()` fails
(no OLED present on a given board), the module just stays permanently
inactive rather than erroring.

`oled_status_off()`/`_on()` (added 2026-08-13, driven by the same
`CMDSAVER` idle timer as the GC9A01's own screensaver - see "GPIO9 wake
button") blank the dashboard by content (`clearBuffer()`/`sendBuffer()`),
**not** u8g2's `setPowerSave()` - that API is known from firsthand
experience porting the WLED usermod below to hang this exact SSD1306
panel, requiring a full re-init to recover, not just a mode switch. An
always-on static dashboard is exactly the kind of content that burns in
on a real (emissive) OLED over time, so blanking it isn't optional
long-term the way it might be for an LCD.

Ported from a WLED usermod the same author wrote for this exact
ESP32-C3-mini + 0.42" OLED combo
(https://github.com/wled/WLED/pull/5475) — reused for the I2C
init/offset/font conventions, not the WLED-specific dashboard content
(effect name/brightness graph) or its config/button/LED-heartbeat layer,
none of which apply here.

### Firmware version and repo URL

`firmware/src/version.h` defines `FW_VERSION` and `REPO_URL` as the
single place to bump on release — shared by `protocol.cpp` (`CMDHWINF`'s
reply, `CMDSHSYSHW`'s screen), `display.cpp` (GC9A01 boot splash), and
`oled_status.cpp` (status dashboard, appended to the same line as the
existing RX indicator to fit the tiny OLED's ~31px of usable vertical
space). `REPO_URL` deliberately omits the `github.com/` prefix — the full
URL is ~240px wide at the boot splash's 6px/char font, wider than the
240px display itself; the shorter `owner/repo` form fits within the
round display's ~91%-inset safe area at all three splash-screen line
positions (verified on real hardware 2026-08-13, after an initial version
that centered "tty2oled" with a fixed pixel offset - now `getTextBounds()`
on every line instead, like `display_show_corename()` already did).

### GPIO9 wake button

This board's onboard "BOOT" pushbutton (GPIO9) is repurposed at runtime
as a screensaver wake button (`protocol.cpp`'s `protocol_button_check()`,
polled from `main.cpp`'s `loop()`). GPIO9 is a C3 strapping pin (pulled
low at reset = download mode), which is why it's on `pins.h`'s
"avoid wiring anything else to this" list — but that only matters during
boot, not while running, so reading it as a plain input afterward is
safe and doesn't interfere with normal flashing/boot. No debounce logic:
touching the idle timer repeatedly while the button is held is
idempotent, and releasing it just lets normal idle timing resume on its
own.

Screensaver idle timing runs off a separate `lastWakeMs` timestamp, not
the pre-existing `lastActivityMs` — `lastActivityMs` is also what
`oled_status.cpp`'s dashboard uses for its serial-RX indicator, and a
button press isn't a received command, so touching the shared timestamp
would make that indicator lie. `protocol_process()` keeps both in sync on
every dispatched line; only the button touches `lastWakeMs` alone.
Verified end-to-end on real hardware 2026-08-13: screensaver blanks after
idle, GPIO9 press wakes it and restores the last picture. Since
`lastWakeMs` also drives the onboard OLED's blank/wake (see "Onboard
status OLED" and `CMDSAVER` above), the same button press wakes both
displays together - also confirmed on real hardware.

### Why a custom GC9A01 driver, not a display library

Two libraries were tried and both had real ESP32-C3 bugs, discovered only
by actually flashing hardware (not by code review — both looked fine and
built without errors):

1. **`moononournation/GFX Library for Arduino`** (`Arduino_GC9A01`): its
   fast-path SPI databus (`Arduino_ESP32SPI`) references
   `DR_REG_SPI3_BASE`, a hardware register that only exists on classic
   ESP32/S2/S3 (the C3 has fewer SPI peripherals) - and the library's own
   master header unconditionally pulls that file in for every consumer
   regardless of which databus class you actually instantiate, so
   restricting our own `#include`s to just the pieces we needed didn't
   help. Newer releases separately require `esp32-hal-periman.h`, a header
   no resolvable Arduino-ESP32 core version provided either. **Compile-time
   failure** - never got as far as flashing.
2. **`TFT_eSPI`**: compiled and flashed cleanly, but `tft.init()` hung the
   chip on every boot (`rst:0x8 TG1WDT_SYS_RST` - task watchdog, in a
   crash loop). Bisected by adding `Serial.println()` checkpoints through
   `setup()`/`display_init()` to find which call never returned, then
   independently proved the wiring/SPI hardware itself was fine with a
   bare-`SPIClass` diagnostic sketch (worked instantly, no hang) before
   concluding the bug was in TFT_eSPI's ESP32-C3-specific
   `driver/spi_master.h`-based init path, not our code or wiring. **Runtime
   hang** - looked correct at compile time, only showed up on real hardware.

Given both, wrote a **~150-line direct driver** (`gc9a01.h`/`.cpp`) on top
of plain Arduino `SPIClass` (the exact thing proven working in the
diagnostic sketch) - manual CS/DC/RST toggling, the GC9A01's standard
vendor register-init sequence (adapted from TFT_eSPI's
`TFT_Drivers/GC9A01_Init.h`, MIT licensed - functional hardware-bringup
data, reused for interoperability with this exact chip, not display-library
code), and a fast `pushRect()` bulk blit for JPEG frames/transitions.
Subclasses `Adafruit_GFX` *only* for its software text/shape routines (no
display-specific driver code in Adafruit_GFX itself, so nothing
C3-specific left to break) - `drawPixel()` is the only thing it strictly
needs from us.

### RAM headroom

Build reports **~87.1% RAM usage** (285248 / 327680 bytes, `env:esp32c3`
with the onboard OLED) vs **~86.6%** (283808 / 327680 bytes,
`env:esp32c3_nooled`, see "Build variants" above) — only ~1.4KB
difference (2026-08-13, measured directly by building both envs).
`Wire`/the ESP-IDF I2C driver is **not** what the OLED adds: it's already
a hard transitive dependency of Adafruit GFX Library (`Adafruit_GFX.h`
includes `Adafruit_BusIO`'s `Adafruit_I2CDevice.h`, which requires
`Wire.h` to even compile `display.cpp`/`gc9a01.cpp`) — confirmed by
trying `lib_ignore = Wire` in `env:esp32c3_nooled`, which broke the build
with `fatal error: Wire.h: No such file or directory` from
`Adafruit_I2CDevice.h`. So Wire's cost was already baked into the
firmware before this session's OLED work; only U8g2 itself (excluded
from `env:esp32c3_nooled`'s `lib_deps`) plus a couple of small globals
account for the real difference. Both static 240×240×2-byte (112.5KB)
full-frame buffers in `display.cpp` (`g_frame` and the fade effect's
`blended` scratch buffer) remain the dominant cost either way. Leaves
**~42-44KB** free for stack/heap at runtime in both variants — fits the
32KB `ARDUINO_LOOP_STACK_SIZE` plus Serial's 4KB/2KB RX/TX queues plus
JPEGDEC's own small working buffers, but this has **not yet been
re-tested on hardware** with a full `CMDCORC` transfer under either
variant — do that before trusting it, since this exact class of problem
(silent allocation failure under tight/fragmented heap) is what caused
the original CMDCORC bring-up bugs.

## Build & flash

The same ESP32-C3-mini board is sold both with and without the built-in
0.42" status OLED, so `firmware/platformio.ini` has two build variants
controlled by the `HAS_ONBOARD_OLED` build flag — GC9A01 pins and
behavior are identical in both, only the status OLED (and its U8g2
library dependency) is compiled in or out (see "Onboard status OLED"
above):

```bash
cd firmware
pio run -e esp32c3 --target upload           # default: includes the onboard OLED
pio run -e esp32c3_nooled --target upload    # board variant without the OLED
pio device monitor          # 115200 baud, watch for "ttyrdy;"
```

Both variants verified on real hardware 2026-08-13: `esp32c3` shows the
status dashboard on the onboard OLED, `esp32c3_nooled` leaves it dark
(and off entirely — `Wire.begin()`/`u8g2.begin()` are never called) while
the GC9A01 behaves identically either way.

## Web app

```bash
cd web
npm install
npm run dev                 # open in Chrome or Edge (WebSerial required)
```

Select a core, drop in an image, pan/zoom to fit the circular safe area,
"Send to device" (or just edit — live preview streams automatically while
connected), "Save to library" persists it locally (IndexedDB) for next time.

### Command console

The header's "Command console" button opens a modal (`openCommandConsole()`
in `main.ts`) that documents and exercises every wire-protocol command the
firmware understands - both the original tty2oled grammar and the new
commands added for this round display. `web/src/commands.ts` is the
catalog (kept in sync with `firmware/src/protocol.cpp`'s `dispatch()` and
this file's "Wire protocol" section) - one `CommandDef` per command, with
its syntax, a plain-English summary, and a typed param list the modal
turns into input fields. Picture-transfer commands (`CMDCOR`/`CMDAPD`/
`CMDCORC`) are listed as `sendable: false` with a note pointing at the
main gallery/editor instead, since they need raw binary payloads a
generic command-line console has no business constructing. `serial.ts`'s
`SerialLink` gained a `"message"` event (any device output that isn't
`ttyack;`/`ttyrdy;`, e.g. `CMDHWINF`'s reply) so the console's log panel
can show raw device responses, not just sent commands.

### About modal + GitHub ribbon

The header's "About" button opens a short write-up of what the project
is, its architecture, and links (with full URLs shown) to this repo, the
original `venice1200/MiSTer_tty2oled`, the WLED OLED usermod the status
display was ported from, and the sibling `tty2tft` project. A CSS-only
diagonal "Fork me on GitHub" ribbon sits fixed in the page's bottom-right
corner (`.github-ribbon` in `style.css`) - built from scratch rather than
reusing the common SVG-icon ribbon snippet, to avoid pulling in art of
uncertain provenance.

### "Blank first" checkbox (2026-08-13)

The editor panel's "Send to device" row has a "blank first" checkbox
(`#blank-first-checkbox`, checked by default) - when checked, an explicit
"Send to device" click sends `CMDCLS` before the picture. Exists because
`transitionReveal()` on the firmware only ever writes new pixels over
whatever's already on the physical screen (never clears first,
deliberately matching the original tty2oled's own behavior - see
`CMDSPIC` above) - so re-sending pixel-identical content with only the
effect changed shows no visible transition at all, which is confusing
when using this button specifically to evaluate different effects.
Deliberately scoped to explicit sends only, not `scheduleLivePreview()`'s
debounced auto-send during pan/zoom - clearing on every drag update would
flash distractingly. Root-caused on real hardware 2026-08-13: three
back-to-back `CMDCORC` sends of the *same* JPEG with different effects
showed a transition only on the first (screen already matched); the same
sends with three *different* JPEGs showed all three transitions
correctly, confirming the framebuffer-reveal design was working exactly
as intended, not a bug.

## WiFi/AP feature — implemented and verified on real hardware (2026-08-13)

User request: AP mode with an mDNS/avahi hostname (like the sibling
`chromecast-esp32`/"KnobCast" project's `CastWebServer` pattern -
`WiFi.softAP()`, `DNSServer` wildcard-redirect captive portal,
`WebServer` on port 80, `MDNS.begin()`), plus: while in AP mode, pressing
the existing GPIO9 wake button (see "GPIO9 wake button" above) shows a
QR code on the GC9A01 for the AP's WiFi credentials instead of its normal
screensaver-wake behavior.

**Before any design work, RAM feasibility was measured on real hardware**
(not assumed) - this project's own CMDCORC bring-up history (silent
malloc failures, a heap-fragmented false-negative) made "does this even
fit" the load-bearing question, not an implementation detail. A temporary
build (`WiFi.h` + `WebServer.h` + `DNSServer.h` + `ESPmDNS.h`, reporting
`ESP.getFreeHeap()`/`getMaxAllocHeap()` over the `Serial0` debug UART -
same channel as `DBG_ENABLED`, see "Debug channel" in protocol.cpp)
found two things:

1. **As originally written, WiFi did not fit at all** - linking `WiFi.h`
   into `env:esp32c3` overflowed the DRAM segment by ~39984 bytes before
   a single byte of heap was even requested, i.e. static allocations
   alone (the two 240x240 framebuffers plus everything else) left no
   room for WiFi's own static/BSS footprint.
2. **The fade effect's `blended[DISP_W*DISP_H]` static buffer
   (`display.cpp`, ~112.5KB) was the single largest reclaimable chunk.**
   It only ever existed to hold a full-frame per-step blend of `g_frame`
   before pushing it - rewritten to blend one row at a time into a
   `uint16_t blendedRow[DISP_W]` scratch buffer and push each row inside
   one `beginBatch()/pushRectBatched()/endBatch()` span (same pattern
   already used by the iris effect), functionally identical output, just
   without the full-frame buffer. This alone dropped static RAM usage
   from ~87% to **58.4%** (191248/327680 bytes, `env:esp32c3`) -
   confirmed correct on real hardware (no seams/banding vs. the old
   full-buffer version).

With that fix in place, the full stack was measured end-to-end on real
hardware, holding the AP up continuously while also exercising the
existing serial protocol under load (a real `CMDCORC` JPEG transfer sent
over USB-CDC while WiFi/AP/WebServer/DNSServer/mDNS were all running):

| Stage | Free heap | Largest alloc block |
|---|---|---|
| Before `WiFi.mode(WIFI_AP)` | 79848–84216 | 57332–61428 |
| After `WiFi.softAP()` | 40480–44848 | 21492–26612 |
| After `WebServer.begin()` + `DNSServer.start()` + `MDNS.begin()` | 31680 | 13300 |
| Steady-state, 10s later | 31940 (stable, no leak) | 13300 |
| After a full `CMDCORC` transfer (6399-byte JPEG) with AP+server+mDNS all active | 31940 (unchanged) | 13300 |

`readExact()` completed the 6399-byte transfer in 155ms with no
slowdown, no dropped bytes, and no heap movement - the existing
`CMDCORC` pipeline is unaffected by WiFi running concurrently.
**Conclusion: WiFi/AP/mDNS/web-server is feasible on this hardware, but
only after the fade-buffer fix above** - ~31.9KB free heap / ~13.3KB
largest contiguous block is the working budget any new WiFi code has to
live within. `colorBuf` (20000 bytes) and `legacyBuf` (8192 bytes) are
already static, not heap, so they don't compete with this budget - new
WiFi-side buffers (HTTP request bodies, QR code matrix, WiFi credential
storage) should default to static/fixed-size for the same reason
(`colorBuf`'s malloc-failure history above), not `malloc`/`new`.

With that budget confirmed, the feature itself was built and verified
end-to-end on real hardware. Design decisions confirmed with the user
before implementation: **AP network is open** (no password, matching
`chromecast-esp32`'s `WiFi.softAP(ssid)`), **mDNS hostname is
MAC-suffixed** (`tty2oled-XXXX.local`, same convention as the AP SSID -
not a fixed shared name), and **GPIO9 fully splits by mode** (WiFi-QR
toggle in AP mode, existing screensaver-wake in normal operation, no new
hardware).

### New files

- `firmware/src/wifi_config.h` - `WifiConfigStore`/`WifiConfig`, NVS
  namespace `"tty2oled"` (`Preferences`), directly modeled on
  `chromecast-esp32/src/config.h`'s `ConfigStore` but trimmed to just
  `wifiSsid`/`wifiPass`/`configured` - no device-control state, that
  already lives in `protocol.cpp`/`display.cpp`.
- `firmware/src/wifi_manager.h`/`.cpp` - `WifiState` state machine
  (`INIT`/`AP_MODE`/`CONNECTING`/`CONNECTED`), directly modeled on
  `chromecast-esp32/src/main.cpp`'s `AppState` handling. No saved config
  → `AP_MODE`; saved config → `CONNECTING` with a 20s timeout (same
  constant as the reference) falling back to `AP_MODE` on failure;
  success → `CONNECTED`, which starts `MDNS.begin()` +
  `MDNS.addService("http","tcp",80)` and the web portal.
  `wifi_manager_is_ap_mode()` is protocol.cpp/display.cpp's only real
  coupling point to this module.
- `firmware/src/web_portal.h`/`.cpp` - `WebPortal` class (`WebServer(80)`
  + `DNSServer`), directly modeled on `chromecast-esp32/src/web_server.h`'s
  `CastWebServer` but trimmed to WiFi bootstrap only (no device-control
  routes): `GET /` (setup form in AP mode, status+forget page once
  connected), `POST /save` (writes config, `ESP.restart()`), `POST
  /reset` (clears config, `ESP.restart()`), `GET /status` (small
  hand-built JSON, no ArduinoJson dependency needed). HTML is an embedded
  `PROGMEM` C string, same "no LittleFS/SPIFFS" approach as the
  reference's `_handleRoot()`.
- `ricmoo/QRCode` (`lib_deps`) - small pure-C QR encoder, no dynamic
  allocation (caller-supplied fixed buffer via `qrcode_getBufferSize()`),
  used only for the AP-mode WiFi-join QR.

### Display additions (`display.cpp`)

`display_show_ap_mode(ssid, ip)`, `display_show_connecting_wifi(ssid)`,
`display_show_wifi_connected(ssid, ip)`, and `display_toggle_wifi_qr()`
(no args - remembers the AP ssid/ip set by `display_show_ap_mode()`
internally via `g_apSsid`/`g_apIp`/`g_qrShown`, so `protocol.cpp` doesn't
need to know them). The QR renderer (`drawWifiQrScreen()`, anonymous
namespace) builds the `WIFI:T:nopass;S:<ssid>;;` payload, encodes it at a
fixed version 4/ECC LOW, and reuses `g_frame` as scratch (safe to clobber
- AP mode never has a decoded CMDCORC JPEG on screen at the same time) so
no extra static buffer is needed; the whole QR is pushed in **one**
`pushRect()` call, not one `fillRect()`/SPI-transaction per module -
matching the "batch, don't do per-call transactions" lesson from the
iris/wipe stall fix earlier in this doc.

### `protocol.cpp` changes

`protocol_button_check()` gained real press-edge detection (a `static
bool wasPressed`) on top of its existing level-based `lastWakeMs` touch -
the QR toggle needs a single flip per press, not a re-trigger on every
loop() tick the button stays held. `protocol_saver_check()` now returns
early when `wifi_manager_is_ap_mode()` - no idle-blank during WiFi setup,
so the AP-status/QR screens stay up until the button is pressed or WiFi
gets configured, instead of a second idle timer fighting the QR toggle.

### Two real bugs found only by testing on actual hardware (2026-08-13)

Both exactly the kind of thing this project's CLAUDE.md keeps emphasizing
- looked correct in review, only broke on real hardware:

1. **QR square clipped by the round bezel.** The QR's pixel size was
   sized off a naive "200px safe area" constant with no relationship to
   the display actually being round - a square that size, centered, put
   its corners (exactly where the QR's finder patterns live) past the
   physical circular glass, visibly cropping them. Fixed by sizing the
   square so it's fully inscribed in a circle of conservative radius
   100px (`side <= R*sqrt(2)`, so `qrPx = floor(100*1.4142 / qrcode.size)
   * qrcode.size`) and removing a vertical offset that had been added to
   leave room for SSID text below - that offset pushed the square
   further off-center and made the clipping worse on one edge. Confirmed
   fixed on real hardware: a phone camera scanned the QR and prompted to
   join the open network correctly.
2. **`enterConnected()` never redrew the display.** On the first
   end-to-end STA-connect test, the physical screen stayed on
   "Connecting to <ssid>" indefinitely even though the device had
   actually connected in well under the 20s timeout - `WiFi.status()`
   correctly returned `WL_CONNECTED`, mDNS registered
   (`tty2oled-XXXX.local` resolved via `avahi-resolve`/`avahi-browse`),
   and `/status` served correctly over the new STA IP, all confirmed
   independently of the (stale) screen. The bug was simply that
   `enterConnected()` started mDNS/the portal but never called any
   `display_show_*` function, so the last screen drawn (`enterConnecting()`'s
   "Connecting to...") never changed. Fixed by adding
   `display_show_wifi_connected(ssid, ip)` and calling it from
   `enterConnected()`. This is a reminder that "the network side works"
   and "the screen reflects it" are two separate claims - verify both,
   not just the one that's easier to check from a laptop.

### Verified end-to-end on real hardware (2026-08-13)

- Cold boot with no saved WiFi config → device broadcasts
  `tty2oled-XXXX` (open network), captive-portal setup page loads
  automatically for any path (`onNotFound` → root), `/status` returns
  correct AP-mode JSON.
- WiFi credentials submitted through the real setup form (not curl, to
  keep the password out of any debug channel) → device restarts, joins
  the target network, `tty2oled-XXXX.local` resolves via mDNS
  (`avahi-resolve`/`avahi-browse` both confirmed the `_http._tcp`
  service advertisement), `/status` and `/` both serve correctly over
  both the direct IP and the mDNS hostname.
- GPIO9 press in AP mode → QR code renders fully inside the round bezel
  (post-fix), a real phone camera scans it and prompts to join the open
  AP network; second press reverts to the AP-status screen.
- Both `env:esp32c3` and `env:esp32c3_nooled` build clean with the
  feature included, at ~59.2-59.7% static RAM - comfortably inside the
  ~58.4% post-fade-fix baseline plus the feature's own footprint, nowhere
  near the pre-fade-fix 87% danger zone.

### Follow-ups added after initial hardware verification (2026-08-13)

- **mDNS hostname now shown on the "Connected" screen**
  (`display_show_wifi_connected()` gained a third `hostname` param,
  `<deviceName>.local`) alongside SSID/IP, so the hostname to type into a
  browser is visible without needing a laptop/mDNS tooling. Not shown in
  AP mode - mDNS isn't started until `enterConnected()`, and AP mode's
  status screen already shows the device's identity via its SSID (which
  *is* the device name).
- **WiFi network scan added to the setup form** - a "Scan for networks"
  button calls the new `GET /scan` route (`web_portal.cpp`'s
  `handleScan()`, `WiFi.scanNetworks()`, same synchronous pattern as
  `chromecast-esp32`'s `/wifiscan`), populating a `<select>` that fills
  the SSID text field on selection. Required changing `enterApMode()`
  from `WiFi.mode(WIFI_AP)` to `WIFI_AP_STA` - a pure-AP-mode scan simply
  fails, since `WiFi.scanNetworks()` needs the STA interface active even
  though STA isn't actually connecting to anything yet. Verified
  end-to-end on real hardware: scan populated real nearby SSIDs, and a
  full "scan → select → save" round trip successfully reconfigured the
  device's WiFi.

### Scope cuts (deliberate, not yet built)

- No password-protected AP - open network only, per the confirmed
  decision.
- STA-mode web portal is intentionally minimal (status + forget-WiFi) -
  this project's actual marquee control already lives in the WebSerial
  `web/` app; the new portal's only job is WiFi bootstrapping.
- `wifi_manager_status_line()` exists (meant for `oled_status.cpp`'s
  dashboard) but isn't wired in yet - the onboard OLED's 72x40 dashboard
  is already at 4 lines filling all ~40px of usable height, and adding a
  5th would mean redesigning the layout, not just appending a line.

## Wire protocol over WiFi (WebSocket transport, 2026-08-13)

The same `CMDxxx` command grammar the serial path speaks is now also
reachable over the network, so the web app can drive the display without
a USB cable from any machine on the LAN. Browsers can't open raw TCP
sockets (only WebSocket/HTTP), so this is a **WebSocket server on port
81** (`firmware/src/ws_protocol.h`/`.cpp`), started only once WiFi is
in the `CONNECTED` (STA) state - AP mode's only job stays WiFi
bootstrapping, matching the setup portal's existing scope cut.

**RAM was the design constraint, not an afterthought.** `arduinoWebSockets`
reassembles each binary frame into one contiguous heap allocation before
handing it to the app, and the measured largest-allocatable-block budget
(13.3KB, see the WiFi/AP feasibility table above) couldn't safely
guarantee a whole 6-15KB JPEG frame at once - the same failure class as
this project's own `malloc(40000)` bug. Rather than measure-and-hope, the
art transfer (`CMDCORC` equivalent) is **chunked at the protocol layer
from the start**: a text header frame (`CMDCORC,<name>,<effect>,
<durationMs>,<totalLength>`) followed by the JPEG split into 2048-byte
binary frames, each appended into the existing static `colorBuf`
(`protocol.cpp`) at a running offset. Every single allocation the
WebSockets library ever needs to make stays small and constant regardless
of total JPEG size - the risk is designed out, not measured around.
`web/src/wifiLink.ts`'s `sendColorArt()` does the client-side chunking to
match.

**Grammar is shared with the serial path, not duplicated.** `protocol.cpp`'s
`dispatch()` was renamed to `protocol_dispatch_line()`, moved out of its
anonymous namespace, and declared in `protocol.h` - both
`protocol_process()` (serial) and `ws_protocol.cpp`'s WS handler call it
for every command except `CMDHWINF` (writes a reply directly to `Serial`
in the shared function, so the WS handler intercepts and replies on the
socket instead) and the picture commands (each transport sources their
bytes differently). A new `protocol_note_activity()` (touches both
`lastActivityMs`/`lastWakeMs`) must be called by any transport dispatching
a command, or `oled_status.cpp`'s RX indicator lies and the screensaver
can blank mid-session under an active WiFi client - the same distinction
this project's GPIO9 button code already had to get right.

**Concurrent-transfer guard.** A WS transfer spans multiple `loop()`
iterations while chunks arrive (unlike the serial path's single blocking
`readExact()` call), so a serial `CMDCORC` landing mid-WS-transfer would
race the shared `colorBuf` without a guard. `protocol_ws_xfer_begin()`
rejects a second transfer outright, and `handleColorPicture()`/
`handleLegacyPicture()` (serial) each gained one guard line checking
`protocol_ws_xfer_in_progress()` before touching `colorBuf`/`legacyBuf` -
the only change made to either function's body.

**No legacy XBM/GSC transfer over WS** - that grammar exists for real-
MiSTer-over-serial compatibility, which doesn't apply to a WS client (the
web app only ever produces JPEG art). `WifiLink.sendLegacyPicture()`
throws rather than attempting it.

### Web app (`web/src/`)

`deviceLink.ts` defines the `DeviceLink` interface both `SerialLink`
(unchanged behavior, now `implements DeviceLink`) and the new `WifiLink`
satisfy - `main.ts`'s send/preview code only depends on this shape, not
on which transport is active. `main.ts` gained a header transport
selector (`USB (WebSerial)` / `WiFi`) plus a host field (persisted via
`localStorage`); switching transport disconnects the old one and swaps
`link` to a fresh instance, re-attaching the one "permanent" listener
(`statechange` → `renderConnectionState`) - short-lived listener
pairs like the command console's already register/deregister within
their own scope against whatever `link` was active when they opened, so
they didn't need any special handling.

### A PlatformIO/LDF gotcha worth remembering

`links2004/WebSockets` unconditionally `#include`s `<WiFiClientSecure.h>`
for the ESP32 target, even though this project never uses `wss://`.
That header is a genuine framework-bundled library (same
`framework-arduinoespressif32/libraries/` folder as `WiFi`/`Wire`/
`DNSServer`, which all resolve automatically with no `lib_deps` entry) -
but PlatformIO's LDF failed to find it **no matter where it was
`#include`d from** (this project's own source, a bare `lib_deps` entry,
`lib_ldf_mode = deep`/`deep+`) while every other bundled library kept
resolving fine. Root cause not fully explained, but the working fix was
`lib_extra_dirs = ${platformio.packages_dir}/framework-arduinoespressif32/libraries`
in `platformio.ini` - pointing LDF straight at the folder sidesteps
whatever the auto-discovery gap is for this one library.

### Verified end-to-end on real hardware (2026-08-13)

- Both `env:esp32c3`/`env:esp32c3_nooled` build clean at ~59.6-60.1%
  static RAM with the WS stack included - comfortably inside the
  ~58.4% post-fade-fix baseline, nowhere near the pre-fix 87% danger
  zone.
- A real WS client (both a Python test script and the actual `web/` app
  in a browser) connected, sent plain commands (`CMDCLS`, `CMDTXT`),
  `CMDHWINF` (replied correctly over the socket, not Serial), and
  multiple full chunked `CMDCORC` transfers back-to-back (6-8KB JPEGs) -
  all rendered correctly, `ttyack;` round-tripped every time, heap stayed
  in a healthy 20-28KB range across the whole session with no leak or
  crash.
- `MDNS.addService("ws", "tcp", 81)` (alongside the existing `"http"`
  service) confirmed advertised and resolvable via `avahi-browse`/
  `dns-sd` - **note this only makes the service discoverable to tools
  with real mDNS/Bonjour support, not to the browser-based web app
  itself**: browsers have no JavaScript API for mDNS discovery, so
  `wifiLink.ts` still requires the user to type in a host/IP either way.
- A mid-session WiFi disconnect/reconnect was observed and recovered
  from automatically (confirmed via the same real-hardware debug
  capture) - `WifiLink`'s `ws.onclose` now also resolves any pending
  `sendCommand()`/`sendColorArt()` ack wait immediately instead of
  leaving the UI looking stuck for the full 6s ack timeout.

## MQTT notifications (2026-08-13)

A third, independent input channel - text and image-by-URL notifications
pushed from Home Assistant or anything else on the local network that
speaks MQTT (`firmware/src/mqtt_client.h`/`.cpp`, `mqtt_config.h`), with
no web app or MiSTer involved. Explicitly scoped narrower than the
earlier "unknown core → search a catalog → cache" idea (see the "Follow-
up research" note under full-color marquee support below) - notifications
are one-shot and don't need a persistent cache or a real art catalog,
neither of which exist yet.

**Topics**: `<prefix>/text` (payload is the notification text verbatim)
and `<prefix>/image` (payload is a URL to fetch and show), where
`<prefix>` defaults to `tty2oled/<deviceName>` (overridable via the STA-
mode web portal's new MQTT section, `POST /mqtt-save`). Both show for a
configurable duration (default 8000ms, `MqttConfig.durationMs`) then
revert to whatever was on screen before - `protocol_redisplay_current()`,
newly exposed in `protocol.h` for this (previously `redisplayCurrent()`
was file-local in `protocol.cpp`, used only internally by the
screensaver wake path).

**Image payloads are URLs, not raw bytes** - deliberately, to avoid
reintroducing the large-contiguous-allocation risk this project already
solved for the WS command protocol: MQTT has no built-in payload
chunking the way our own `CMDCORC` grammar does, so a raw JPEG in one
MQTT publish would need `PubSubClient`'s receive buffer sized to the
whole image at once. A URL means the device does a normal HTTP GET,
streamed into the same static `colorBuf` `CMDCORC` already uses via the
same guard (`protocol_ws_xfer_begin()`/`_append()`/`_complete()`) - no
new buffer, no new allocation risk.

**No `HTTPClient` - a hand-rolled HTTP/1.1 GET instead.** `HTTPClient.h`
unconditionally references `WiFiClientSecure`'s TLS methods in its own
`.cpp` (not gated by the runtime URL scheme), and this framework's
mbedtls config has PSK cipher suites disabled - `ssl_client.cpp`'s real
implementation compiles out behind an `#if` guard that isn't satisfied,
leaving a stub with none of the symbols `WiFiClientSecure.cpp` calls,
which fails at *link* time (not the "header not found" LDF gap
`links2004/WebSockets` hit - a different, worse failure mode: it
compiles, and only fails when the linker tries to resolve
`start_ssl_client`/etc). Since this project only ever needs plain
`http://` for local-network image URLs, `mqtt_client.cpp`'s `httpGet()`
is a small hand-rolled GET over plain `WiFiClient` instead - in the same
spirit as this project's other hand-rolled wire parsing
(`protocol.cpp`'s `splitField()`). No `https://` support - a deliberate
v1 scope cut, not a limitation worth chasing given the mbedtls
reconfiguration it would require.

**The picture-buffer guard widened from two writers to three.** The WS
feature added `wsXferActive`/`protocol_ws_xfer_in_progress()` to stop a
serial `CMDCORC` racing a chunked WS transfer on `colorBuf`. MQTT's image
fetch is a third writer spanning multiple `loop()` iterations (an
`WiFiClient` stream, polled), so it claims/releases the same flag before
touching `colorBuf` too - same function names (`protocol_ws_xfer_*`,
kept despite now covering three callers, not worth renaming across every
call site for no functional change), just called from a third place.

### Two real bugs found only by testing on real hardware (2026-08-13)

1. **Image notifications never reverted - because they overwrote the
   very state they should have reverted back to.** The first
   implementation used the existing `protocol_ws_xfer_finish()` (same as
   `CMDCORC`), which decodes into the shared `g_frame` and sets
   `lastPictureKind = JPEG` - meaning after a notification image, "the
   last shown picture" *was* the notification, so
   `protocol_redisplay_current()` just redrew the notification again,
   forever. Root-caused by testing (confirmed: it never reverted, stayed
   on the fetched image). Fixed with a new `display_draw_jpeg_transient()`
   (`display.cpp`) that decodes and pushes straight to the physical
   display without touching `g_frame` at all, and a matching
   `protocol_ws_xfer_finish_transient()` that skips the
   `lastPictureKind`/`actCorename` update - so the underlying marquee
   art's `g_frame` content is never disturbed, and reverting later just
   re-reveals it unchanged. Confirmed fixed on real hardware: a real
   `CMDCORC` picture pushed first, then a notification, correctly
   reverted to the original picture afterward - both for text and image
   notifications.
2. **The first version of `display_draw_jpeg_transient()` hung the
   device outright** (confirmed via the `Serial0` debug channel: a
   `[mqtt] calling finish_transient` line printed, `finish_transient
   returned` never did, no crash-reboot banner either - a true hang, not
   a crash). Root cause: it wrapped `jpeg.decode()` in
   `beginBatch()`/`endBatch()`, holding an SPI transaction open across
   JPEGDEC's own internal per-block decode compute time, not just the
   `pushRectBatched()` calls themselves - unlike every other batched-push
   case in this file (fade, iris), which only ever wrap a loop *we*
   fully control. Fixed by pushing one `pushRect()` per JPEG block
   instead (its own transaction each), which - because JPEGDEC calls
   back once per MCU block (~15-30 times for a 240x240 image), not once
   per row - still avoids the older "per-row transaction stall" class of
   problem (see that section above) without needing to hold a transaction
   open across third-party decode timing. Confirmed fixed: the function
   now returns and the image displays correctly.

Also found and fixed in the same session: the MQTT client ID was
accidentally `"tty2oled-" + wifi_manager_device_name()`, doubling to
`tty2oled-tty2oled-B610` in the broker's connection log -
`wifi_manager_device_name()` already returns `"tty2oled-XXXX"`.

### Verified end-to-end on real hardware (2026-08-13)

Both build variants clean at ~60% static RAM (`knolleary/PubSubClient` is
small; no `lib_extra_dirs`-style LDF workaround needed since this feature
avoids `WiFiClientSecure` entirely, unlike WebSockets). Tested against a
real broker (a throwaway `eclipse-mosquitto:2` Docker container, not a
production Home Assistant instance, but a real MQTT implementation, not a
mock) - `mosquitto_pub`-equivalent publishes to `<prefix>/text` and
`<prefix>/image` both round-tripped correctly: banner/image appeared,
woke a blanked display, counted as activity, and reverted to the actual
prior marquee content (verified by pushing a real `CMDCORC` picture over
WS first, then triggering notifications, confirming revert restored that
exact picture) after the configured duration - repeated successfully
multiple times after all fixes landed.

## Planned: full-color marquee support (deferred)

Current priority is **hardware bring-up** (firmware flashed + verified on
the real ESP32-C3/GC9A01, driven end-to-end by the web app's gallery).
Full-color art sourcing is explicitly **deferred** until that's working -
tracked here so it isn't lost.

Findings from investigating "is there an existing color equivalent of our
grayscale pack" (2026-08-12):

- **tty2tft** (ojaksch/venice1200, same community/author as tty2oled) is
  the closest sibling project — built specifically for color TFT displays.
  Worth borrowing its **conventions**, not its assets:
  - JPG, non-progressive, ~60% quality, ~20–35KB/file target, 320×240 or
    480×320 depending on display variant.
  - Per-game matching by MAME setname, falling back to **progressively
    shortening the name** on a miss (`jackalfixed` → `jackalfixe` → … →
    `jackal`), plus `_alt1`..`_alt5` variants diced at random - a better
    arcade-matching strategy than our current exact/alias-only approach
    in `aliases.ts`/`findAutoMatch`, worth adopting regardless of art
    source.
  - License: CC BY-NC-SA 4.0 (non-commercial, share-alike, attribution
    required). Repo archived/unmaintained since 2025-04.
- **No bundled/downloadable color art pack exists** for tty2tft - despite
  `PICTURE_REPOSITORY_URL=https://www.tty2tft.de` appearing in its ini,
  that's for optional WiFi *streaming* from a self-hosted server, not a
  distributable archive (and the site 403s automated fetches). Users are
  expected to source their own JPGs and organize them into its SD-card
  folder convention by hand.
- Broader (non-MiSTer-specific) color marquee sources exist if we go
  looking outside this community: Progetto SNAPS, LaunchBox/EmuMovies
  "MAME Marquees" packs, Pixelcade's remastered marquee library. Each
  would need its own size/license/format check before importing - none
  of this has been vetted yet.

When this work resumes: reuse tty2tft's fallback-matching pattern in the
web app's arcade auto-match, and revisit which external source (if any)
to pull real color art from - that choice was explicitly not made yet.

### Follow-up research (2026-08-13): still no public art catalog, still unresolved

Re-checked with the WiFi/WS work now in place, since a device-side pull
feature would only be worth it if a real source existed to point it at.
Confirmed via the actual tty2tft source (`Code/MiSTer_tty2tft.ino`,
GitHub) and forum research, not just recollection:

- tty2tft is small/niche by GitHub metrics (13 stars, 3 forks, archived
  since 2025-04-23) but had real community discussion (9+ forum pages) -
  GitHub stars undersell MiSTer-community project usage generally.
- Its actual mechanism: config from a plain-text `wifi.txt` on the SD
  card (SSID/pass/base URL/timeout/country, one per line); on `CMDCOR`,
  it checks the SD card first (same shortened-name/`_alt` matching as
  above), and only falls back to an `HTTPClient` GET
  (`baseURL + folderjpg + dirletter + filename`) when nothing local
  matches - fetched images are cached permanently to SD, so it's a
  "local library with lazy-fill fallback," not continuous streaming. A
  separate always-on FTP server (hardcoded `esp32`/`esp32`) handles bulk
  library management. `www.tty2tft.de` (the default repository URL in
  its config) still 403s even a browser-UA `curl` - can't confirm what,
  if anything, it actually serves.
- Still **no real public catalog**: EmuMovies is subscription-gated, MAME
  project marquee art is a legitimate free raw source but not
  tty2tft-packaged, and a community member's "700 new pictures" for
  `tty2rpi` was mentioned in a forum thread with no download link found.
  The devs are explicit that they don't manage/bundle art - users source
  and convert it themselves.

**Design sketch for a device-side "pull on unknown core" feature**
(discussed, not built - contingent on a catalog existing, which it
doesn't yet):
- Trigger: the existing bare-corename fallback branch in
  `protocol_dispatch_line()` (not `CMDCOR`/`CMDAPD`, which are already
  committed to "raw picture bytes follow immediately" by the legacy
  protocol grammar - a stock `tty2oled.sh` only sends those when it
  already has local art to send, so it never naturally emits a "no art,
  please look it up" signal on its own; this would need either a
  modified script - already anticipated as a future step, see "Known
  gaps" below - or serve the web app's own flow rather than a real
  MiSTer).
- Catalog: an ordered list of base URLs in NVS (one delimited
  `Preferences` string), tried in order via `HTTPClient` GET, same
  `colorBuf`/`display_draw_jpeg()` draw path already used for `CMDCORC`.
  No new RAM design needed - the fetch streams into the same static
  buffer.
- **Blocking requirement, not optional polish**: without an on-device
  persistent cache, "a core we don't know" is meaningless - the device
  has zero memory of anything shown across boots/pushes today, so every
  single core selection would pay a network round-trip forever, which is
  a bad experience for something as frequent as MiSTer core-switching.
  LittleFS (flash headroom is fine, ~52% free) is the tractable option -
  no SD card hardware needed - but is real, unbuilt work (custom
  partition table, mount/read/write, a corename→file mapping) that
  should ideally be written through by *both* the push path (`CMDCORC`)
  and the pull path, so either one satisfies future lookups.
- Remaining quality gap even with all the above solved: catalog images
  would arrive full-rectangle with none of `editor.ts`'s round-safe
  crop/fit applied (the device has no such transform, it just centers/
  crops blind) - fine for a catalog curated specifically for this
  round display, ugly for a generic rectangular one.

Net: still blocked on "no catalog exists," same as before - this section
just records that the pull-side design is otherwise sound and what it
would take, so it isn't re-derived from scratch next time this comes up.

## Known gaps / follow-ups

- Legacy grayscale crop numbers (`LEGACY_W/H` in `display.cpp`) were spot
  checked against real converted `.gsc` samples (edge-vs-center pixel
  brightness on 15 random images): most have blank/background margins in
  the outer ~8px as assumed, but a handful (e.g. images with a full-bleed
  background pattern) don't — worth an eyeball pass over the actual
  library once art is being assigned for real, not just a geometric
  guarantee.
- Transition effects are a small curated set (basic wipe), not the
  original's 23.
- No MiSTer-side script changes (a `tty2oled.sh` fork sending `CMDCORC`)
  are included yet — the web app's WebSerial previewer doesn't need a
  MiSTer at all. Natural next step once firmware/web app are validated on
  real hardware.
- Pin defaults in `pins.h` (`SCLK=4, MOSI=0, CS=7, DC=1, RST=10, BL=3`,
  plus `PIN_OLED_SDA=5, PIN_OLED_SCL=6`) are **verified on real hardware**
  (2026-08-13): GC9A01 shows its startup screen and the onboard OLED
  shows the "tty2oled" status dashboard, both after physically moving the
  MOSI/DC jumpers from their original GPIO6/GPIO5 and reflashing.
- Onboard status OLED (`oled_status.h`/`.cpp`) is confirmed showing the
  live dashboard on real hardware (2026-08-13), **including with a
  `CMDCORC` transfer in flight** (2026-08-13: a 5365-byte JPEG sent with
  the OLED active completed and rendered cleanly) — the
  `oled_status_suspend()`/`resume()` guard around `readFixed()`/
  `readExact()` is confirmed working under real load, not just reasoned
  about, and the reduced RAM headroom (see "RAM headroom" above) held up.
- **`CMDCORC` JPEG rendering is verified end-to-end on real hardware**
  (2026-08-12, re-confirmed 2026-08-13 with the OLED active and the new
  protocol commands in place): JPEGs transfer and decode correctly. All
  six transition effects (wipe ×3, iris, fade, instant) are now
  individually verified correct on real hardware (2026-08-13) - three of
  them (wipe left→right, wipe right→left, iris) had real bugs before that
  (see "transitionReveal() pushRect stride bug" above) that this specific
  wording previously glossed over as just "wipe/iris/fade" working.
  Legacy `CMDCOR`/`CMDAPD` grayscale rendering over the wire still hasn't
  been exercised on real hardware yet (only `CMDCLST`'s reuse of the same
  `display_draw_legacy_gsc()` draw path, and the web app's local canvas
  preview / unit-level reasoning for the actual byte transfer).
- **New protocol commands verified end-to-end on real hardware
  (2026-08-13)**: `CMDBYE`, `CMDTEST`, `CMDSHSYSHW`, `CMDHWINF` (reply
  format confirmed over the wire), `CMDCLST`, `CMDSPIC` (both JPEG- and
  GSC-sourced redisplay), `CMDSSCP`, and the full `CMDSAVER`/`CMDSWSAVER`
  blank-after-idle/wake cycle (see "Wire protocol" above). This testing
  is also what surfaced and fixed the backlight-PWM hardware issue (see
  "Backlight PWM has no effect on at least one real GC9A01 module"
  above) — `CMDDOFF`/`CMDDON` are now confirmed working too, which they
  weren't before (backlight PWM alone was silently a no-op on this
  hardware).
- The test image used for hardware validation was scaled/cropped
  incorrectly (truncated at the edges) because it was a raw ad hoc
  240×240 JPEG made directly for the test script, not run through the
  web app's actual editor (which applies the round display's
  `center-scale` 91% inset). The firmware itself does no scaling — it
  just centers whatever pixel dimensions it decodes. Next real-world test
  should go through the actual web app export path, not a hand-made test
  file.

### Root causes of the CMDCORC bring-up bugs (found 2026-08-12)

Three independent bugs combined to make `CMDCORC` completely non-functional
on real hardware, manifesting as raw JPEG bytes scrolling across the
display as if misread as commands. All three are now fixed and confirmed
on hardware:

1. **Dispatch order**: `dispatch()` checked `cmd.startsWith("CMDCOR")`
   before `cmd.startsWith("CMDCORC")`, so every `CMDCORC` line was
   swallowed by the legacy fixed-8192-byte handler (byte-count mismatch
   against a variable-length JPEG → "Picture xfer error"). Fixed by
   checking `CMDCORC` first (`protocol.cpp`'s `dispatch()`).
2. **`colorBuf` malloc failure**: `colorBuf` was `malloc(40000)`'d per
   call. Heap fragmentation on the C3 made that malloc fail
   *intermittently* despite tens of KB of nominally free heap — no single
   40KB contiguous block was available. The failure was silent:
   `handleColorPicture()` returned before ever reading the incoming JPEG
   bytes off the wire, leaving them unconsumed for the main loop to
   misread as garbage commands — this was the literal cause of the "binary
   data scrolling on screen" symptom. Fixed by making `colorBuf` a static
   20000-byte array (`protocol.cpp`), eliminating the malloc dependency.
3. **RX queue silently undersized**: `Serial.setRxBufferSize(16384)`
   **silently fails and returns 0** if the requested size doesn't fit in
   available heap at that point in boot (~28KB free, most of the C3's RAM
   already consumed by `display.cpp`'s static 240×240 frame buffers) —
   `HWCDC::begin()` then falls back to its own tiny 256-byte default queue
   whenever the queue is still NULL, with the ISR silently dropping bytes
   once it's full. This was undetected for a long debugging stretch
   because the return value wasn't checked. Found via an independent
   hardware UART0 (GPIO20/21) debug channel wired through an external
   FTDI adapter, bypassing the very USB-CDC channel under suspicion.
   Fixed by checking the return value and falling back to 4096 (which
   allocates reliably).
4. **Sender-side burst overflow** (the last piece, found after the above
   three): even with a correctly-sized ~4KB RX queue, the web app's
   `sendColorArt()` wrote the *entire* multi-KB JPEG in one
   `writer.write()` call, which overflows a 4KB queue long before the
   firmware's `loop()` gets a chance to drain it — the queue has no
   backpressure back to the browser. Fixed by `writePaced()` in
   `web/src/serial.ts`, which chunks writes to 512 bytes with a 10ms gap.
   Confirmed on hardware: a full 10352-byte transfer that previously timed
   out at 5s (having received as little as ~1.4–4.6KB) now completes in
   ~215ms once paced.

`readExact()` in `protocol.cpp` also switched from a single
`Serial.readBytes()` call to an explicit `yield()`-based read loop, since
`Stream::readBytes()`'s busy-spin starves the FreeRTOS task that feeds
bytes from USB hardware into HWCDC's queue on this single-core chip.

### transitionReveal() pushRect stride bug + per-row transaction stall (found 2026-08-13)

Reported by the user: wipe left→right showed visible artifacts while
drawing; wipe right→left and iris didn't work at all; fade and instant
were fine; the web app's own local canvas preview of the same effects
was unaffected. That last detail pointed straight at `display.cpp`'s
`transitionReveal()`/`gc9a01.cpp`'s `pushRect()` - firmware-only, nothing
wrong with the effect logic itself. Two independent bugs, both in
`GC9A01Display::pushRect()`:

1. **Stride bug (the artifacts)**: `pushRect(x, y, w, h, data)` read
   `w*h` pixels from `data` assuming it was a densely-packed `w`-wide
   buffer. `display.cpp`'s `g_frame` is always 240 pixels wide -
   `transitionReveal()`'s left→right wipe calls
   `pushRect(0, 0, w, DISP_H, g_frame)` with `w < 240` during the
   animation, so every row after the first read starting `240-w` pixels
   too early, into the *previous* row's trailing pixels - visible as
   exactly the kind of artifact reported. The top→bottom wipe and
   full-frame draws were never affected: they always pass `w == 240`,
   which happens to equal `g_frame`'s real stride, so the bug was silent
   there. Fixed by adding a `srcStride` parameter (default `w`, so
   existing full-width/single-row callers are unaffected) and having
   every partial-width caller pass `g_frame`'s real width (`DISP_W`)
   explicitly.
2. **Per-row transaction stall (the "doesn't work" effects)**: the
   right→left wipe and the iris effect pushed one row at a time in a
   loop (`for y: gfx.pushRect(x0, y, w, 1, ...)`), and `pushRect()` opens
   and closes a *full* SPI transaction (`SPI.beginTransaction()`/CS
   toggle/`SPI.endTransaction()`) on every call - up to 240 separate
   transactions per animation step, on hardware this project has already
   found to be sensitive to exactly this class of timing issue (see the
   `CMDCORC` bring-up bugs above). Fixed by adding
   `beginBatch()`/`pushRectBatched()`/`endBatch()`: one transaction/CS-low
   span wraps the whole per-row loop, and `pushRectBatched()` only issues
   the (cheap) address-window command per row instead of a full
   transaction. The right→left wipe no longer needs a per-row loop at
   all once the stride fix lets it collapse into one
   `pushRect(x0, 0, w, DISP_H, &g_frame[x0], DISP_W)` call; only iris still
   loops per row, since its visible x-range genuinely differs per row
   (circular mask), but now inside one batch.

All six effects (wipe ×3, iris, fade, instant) re-verified individually
on real hardware after the fix.
