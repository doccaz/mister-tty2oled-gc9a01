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
  protocol commands in place): JPEGs transferred, decoded, and rendered
  correctly with wipe/iris/fade transition effects. Legacy `CMDCOR`/
  `CMDAPD` grayscale rendering over the wire still hasn't been exercised
  on real hardware yet (only `CMDCLST`'s reuse of the same
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
