# tty2oled-gc9a01

An ESP32-C3 + GC9A01 (240×240 round SPI display) reimplementation of
[venice1200/MiSTer_tty2oled](https://github.com/venice1200/MiSTer_tty2oled) —
a physical marquee/status display for a [MiSTer FPGA](https://github.com/MiSTer-devel/Main_MiSTer/wiki)
that shows per-core artwork on a small screen.

It's **protocol-compatible** with the original: an unmodified
`tty2oled.sh` daemon on a real MiSTer talks to this firmware exactly as it
would to the original SSD1322-based hardware over USB serial (handshake,
contrast/rotation commands, and the legacy fixed-size grayscale picture
transfer all work unchanged). On top of that, it adds full-color JPEG
marquee art suited to the round display, a standalone web app for
building and previewing that art, WiFi (its own access point + a small
setup portal, no cable required to get it onto your network), the same
command protocol reachable over WiFi via WebSocket, and MQTT
notifications so things like Home Assistant can push text/image alerts
to the display.

## What's in this repo

- **`firmware/`** — PlatformIO project for the ESP32-C3. Speaks the wire
  protocol below (over USB serial *and* WiFi), decodes JPEGs on-device,
  drives the GC9A01 panel with a small hand-rolled SPI driver (see [Why a
  custom display driver](#why-a-custom-gc9a01-driver)), and optionally
  drives an onboard 0.42" status OLED.
- **`web/`** — a Vite + TypeScript single-page app. Lets you browse a
  library of per-core marquee art, crop/pan/zoom it to fit the round
  display, preview transition effects locally, and push it live to a
  connected device over [WebSerial](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
  (Chrome/Edge only) or WiFi (WebSocket).
- **`tools/`** — a small Python conversion pipeline that turns the
  community's legacy `.gsc`/`.xbm` marquee packs into PNGs for the web
  app's library browser.
- **`reference/`** — the upstream `MiSTer_tty2oled` project, included as a
  git submodule. It's the protocol/format reference this firmware was
  built to be compatible with; nothing in it is built or copied into this
  project's own code.

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32-C3-mini (RISC-V, WiFi, native USB-CDC) |
| Display | GC9A01 240×240 round SPI TFT |
| Status display (optional) | Onboard 0.42" SSD1306 OLED, 72×40 visible |
| Wake button | Onboard "BOOT" pushbutton (GPIO9), repurposed at runtime |

The same ESP32-C3-mini board is sold both with and without the built-in
0.42" OLED — the firmware has a build-time flag for each variant (see
[Build & flash](#build--flash-the-firmware)).

### Pins

Edit `firmware/src/pins.h` to match your wiring — it's the single place
all display pins are defined. Verified working on real hardware:

| Signal | GPIO |
|---|---|
| SCLK | 4 |
| MOSI | 0 |
| CS | 7 |
| DC | 1 |
| RST | 10 |
| BL (backlight, PWM) | 3 |
| OLED SDA | 5 (fixed on boards with the onboard OLED) |
| OLED SCL | 6 (fixed on boards with the onboard OLED) |
| Wake button | 9 |

Avoid the C3's strapping pins (GPIO2, 8, 9 — GPIO9 is deliberately used
anyway for the wake button, safe post-boot) and whatever pins your
specific board's onboard peripherals use if you change these. The native
USB-Serial/JTAG peripheral is fixed to GPIO18/19 in hardware and is used
automatically by `Serial` — this is what both a real MiSTer and the web
app's WebSerial connection talk to; it's never wired manually. GPIO20/21
are left free for an optional hardware debug UART (see `protocol.cpp`'s
`DBG_ENABLED`).

## Build & flash the firmware

```bash
cd firmware
pio run -e esp32c3 --target upload          # default: includes the onboard OLED
pio run -e esp32c3_nooled --target upload   # board variant without the OLED
pio device monitor          # 115200 baud, watch for "ttyrdy;"
```

Requires [PlatformIO](https://platformio.org/) (CLI or the VS Code
extension). All dependencies (`JPEGDEC`, `Adafruit GFX Library`,
`ricmoo/QRCode`, `links2004/WebSockets`, `knolleary/PubSubClient`, plus
`U8g2` in the OLED variant) are pulled automatically via `platformio.ini`.

## First boot: WiFi setup

With no WiFi configured, the device starts its own open access point,
`tty2oled-XXXX` (last two bytes of its MAC address). Connect to it with
a phone or laptop — a captive-portal setup page should open
automatically, or browse to `http://192.168.4.1/`. It can scan for
nearby networks or you can type the SSID/password in by hand.

While in AP mode, pressing the wake button shows a scannable WiFi-join
QR code on the round display (encoding the open network's SSID) instead
of its normal screensaver-wake behavior.

Once configured, the device joins your network and is reachable at
`tty2oled-XXXX.local` (mDNS) — the round display's "Connected" screen
shows this hostname, along with the current IP. Visiting that address in
a browser shows a small status page with a "Forget WiFi" button (which
restarts the device back into AP mode) and, once configured, MQTT broker
settings (see [MQTT notifications](#mqtt-notifications) below).

## Run the web app

```bash
cd web
npm install
npm run dev                 # open in Chrome or Edge
```

Pick a display profile (round GC9A01, or a rectangular preview profile —
see [Display profiles](#display-profiles)), select a core, drop in an
image, pan/zoom to fit, and either let the live preview stream to a
connected device automatically or click "Send to device". "Save to
library" persists the art locally in the browser (IndexedDB) so it's
there next time.

The header's transport selector switches between **USB (WebSerial)** and
**WiFi** — for WiFi, enter the device's hostname (`tty2oled-XXXX.local`)
or IP address, then Connect. Both transports speak the identical command
protocol; the app doesn't care which one is active. The header also has
a **Command console** (documents and lets you exercise every command the
firmware understands) and an **About** panel with links back to this
project and the ones it's built on.

### Building the local marquee library

The editor's "Browse local packs" button needs a converted image index,
built from the community packs bundled in `reference/Pictures/ZIPs`
(pulled in via the `reference/` submodule):

```bash
git submodule update --init reference
tools/build_library.sh
# -> library/converted/<pack>/<name>.png + index.json
# -> synced into web/public/library/ for the web app to fetch
```

This step is optional and its output isn't tracked in this repo (see
[License](#license) — the bundled packs are third-party fan-made assets
of unclear licensing, kept local-only rather than redistributed). Cores
whose id matches a pack filename (or a known alias in
`web/src/aliases.ts`) auto-fill in the library grid once this is run.

## Wire protocol

115200 baud, line-based ASCII commands terminated by `\n`. The firmware
sends `ttyrdy;` once after boot, then `ttyack;` (no trailing newline)
after each processed command, unless `CMDSTTYACK,0` disables it — the
same handshake the original `tty2oled.sh` daemon expects.

**Commands compatible with the original protocol** (same behavior as
upstream, so an unmodified MiSTer daemon works unchanged):
`CMDCLS`/`cls`, `CMDCLSWU`, `CMDSORG`/`sorg`, `CMDCON,<0-255>`,
`CMDROT,<0|1>`, `CMDTXT,x,y,size,text`, `CMDGEO,type,x,y,w,h,fill`,
`CMDSNAM`, `CMDDOFF`, `CMDDON`, `CMDDUPD`, `CMDSECD,<ms>`, `CMDSHCD`,
`CMDSTTYACK,<0|1>`, `CMDRESET`, `CMDSAVER,<mode>,<interval>,<logotime>`
(parses the original's full grammar; implements simple blank-after-idle,
not its animated multi-screen screensaver), `CMDSWSAVER,<0|1>`, and a
bare line with no `CMD` prefix (legacy plain-corename fallback).

**Also implemented, new but original-protocol-shaped**: `CMDBYE`,
`CMDTEST`, `CMDSHSYSHW` (cosmetic/diagnostic screens built from
primitives, not the original's bitmap assets), `CMDHWINF` (replies
`HWGC9A01C;<version>;`), `CMDCLST,<transition>,<color>` (solid-color
fill), `CMDSPIC[,<effect>]` (redisplay the last picture with a new
transition), `CMDSSCP` (redisplay at reduced size).

**Legacy picture transfer**: `CMDCOR,<name>,<effect>` or `CMDAPD,...`
followed by a blocking, fixed-size read of exactly 2048 (1bpp XBM) or
8192 (4bpp grayscale "GSC") raw bytes, classified purely by byte count —
identical to upstream. Source images are 256×64; on the round display
they're scaled to fit (not cropped), since the safe display area is the
same 4:1 aspect ratio as the source.

**Full-color art**: `CMDCORC,<name>,<effect>,<durationMs>,<length>\n`
followed by exactly `<length>` raw JPEG bytes, length-prefixed (not
size-classified, so it isn't subject to the legacy path's fixed-size
truncation behavior). Decoded on-device and revealed with a transition
effect over `durationMs`. Effect ids (shared between firmware and web
app):

| id | effect |
|---|---|
| 0 | none (instant cut) |
| 1 | wipe left → right |
| 2 | wipe right → left |
| 3 | wipe top → bottom |
| 4 | iris (expand from center) |
| 5 | fade (cross-dissolve) |

### Over WiFi

The same command grammar is also reachable over a WebSocket on port 81
once the device has joined your network (see [First boot: WiFi
setup](#first-boot-wifi-setup)) — `ws://tty2oled-XXXX.local:81/`,
advertised via mDNS as `_ws._tcp`. `CMDCORC`'s JPEG payload is sent as a
text header frame followed by the image chunked into small binary
frames, rather than one big length-prefixed blob — this keeps every
allocation the device's WebSocket library needs to make small and
constant regardless of image size. Legacy `CMDCOR`/`CMDAPD` (raw
XBM/GSC) aren't supported over this transport — that grammar exists for
real-MiSTer-over-serial compatibility, and a WiFi client always has the
modern JPEG path available instead.

## MQTT notifications

Point the device at an MQTT broker (Home Assistant's built-in Mosquitto
add-on, or any other local broker) via the web status page's MQTT
section, and it'll show:

- A temporary text banner on any message published to `<prefix>/text`.
- A fetched image on any message published to `<prefix>/image` (the
  payload is a URL to a JPEG, not the image bytes themselves).

`<prefix>` defaults to `tty2oled/<device name>` and is configurable.
Both notification types show for a configurable duration (default 8s),
wake the display if the screensaver had blanked it, and then revert to
whatever marquee art was showing before them. Plain MQTT only (port
1883, no TLS) — suited to a local-network broker, not a cloud one.

## Display profiles

The web app's editor targets a selectable display profile
(`web/src/displays.ts`): the round GC9A01 this firmware drives, the
legacy 256×64 SSD1322 (real original hardware), and a couple of generic
rectangular sizes kept as preview/export targets for a possible future
firmware variant. Only the profiles with matching real firmware get a
live "Send to device" — GC9A01 sends `CMDCORC` JPEGs, the legacy SSD1322
profile sends `CMDCOR` grayscale GSC data; the generic rect profiles are
preview-only since no real hardware exists for them.

## Compatibility notes

This firmware was checked against the actual `tty2oled.sh` daemon script
(not just its documentation) to confirm real-world compatibility:

- The daemon never reads anything back from the device in its main
  loop — it just writes commands with fixed sleeps between them, so this
  firmware's `ttyack;` replies are harmlessly ignored, not required.
- **Device path matters.** This firmware uses the ESP32-C3's native
  USB-CDC peripheral, which Linux enumerates as `/dev/ttyACM*` — the
  original hardware used a CP2102/FTDI-style chip, enumerating as
  `/dev/ttyUSB*`. Point `TTYDEV` at the right one in
  `tty2oled-system.ini`, and make sure `USBMODE="yes"` (otherwise the
  daemon only ever sends plain core names, no picture data).
- `CMDSETTIME`, sent once at daemon startup, isn't implemented — it
  falls through to the bare-line fallback and briefly flashes as garbled
  "core name" text before the first real core name overwrites it.
  Cosmetic only. (`CMDSAVER` *is* implemented, see [Wire
  protocol](#wire-protocol) above — this note used to say otherwise.)

## Why a custom GC9A01 driver

Two existing Arduino display libraries were tried first and both had real
ESP32-C3 bugs, only discovered by flashing actual hardware:

1. **`moononournation/GFX Library for Arduino`** — its fast SPI databus
   references a hardware register (`DR_REG_SPI3_BASE`) that doesn't exist
   on the C3, pulled in unconditionally regardless of which databus class
   is actually used. Compile-time failure, never got as far as flashing.
2. **`TFT_eSPI`** — compiled and flashed cleanly, but hung the chip on
   every boot inside its ESP32-C3-specific SPI init path (task watchdog
   reset, confirmed via bisection with checkpoint logging, with the
   underlying SPI wiring separately proven fine via a bare `SPIClass`
   diagnostic sketch).

Given both, `firmware/src/gc9a01.h`/`.cpp` is a ~150-line direct driver
on top of plain Arduino `SPIClass` — manual CS/DC/RST toggling, the
GC9A01's standard vendor register-init sequence (adapted from
TFT_eSPI's MIT-licensed `GC9A01_Init.h` — reused as functional
hardware-bring-up data, not as library code), and a fast `pushRect()`
bulk blit for JPEG frames and transitions. It subclasses `Adafruit_GFX`
only for its software text/shape routines.

## License

The code in this repository (`firmware/`, `web/`, `tools/`) is licensed
under the [MIT License](LICENSE).

A few things are explicitly **not** covered by that grant:

- **`reference/`** is a git submodule pointing at the upstream
  [`MiSTer_tty2oled`](https://github.com/venice1200/MiSTer_tty2oled)
  project, which is GPLv3-licensed. It's included purely as a
  protocol/format reference — this firmware is an independent, from-
  scratch reimplementation of the same wire protocol, not a derivative of
  that codebase. Interoperating with a GPL project's protocol doesn't
  require this project to be GPL, but the submodule's own contents
  remain under its original license.
- The community marquee art packs referenced by `tools/build_library.sh`
  (fetched from `reference/Pictures/ZIPs` via the submodule above) are
  third-party fan-made assets of unclear/unverified licensing. They are
  **not** included in this repository — `library/converted/` and
  `web/public/library/` are build output, gitignored, and only ever
  generated locally on your own machine if you choose to run that
  script.

## Acknowledgments

Built on the protocol and hardware design work of
[venice1200 and ojaksch](https://github.com/venice1200/MiSTer_tty2oled),
the original tty2oled project's authors. The onboard status OLED support
was ported from a [WLED usermod](https://github.com/wled/WLED/pull/5475)
for the same panel.
