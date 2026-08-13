// Selectable target displays for the web app's editor/preview. "round"
// gets a circular safe-area overlay and a circular thumbnail shape;
// "rect" gets neither. This is a preview/export concept independent of
// what's currently wired in firmware/ - exporting for a profile that
// doesn't match your connected hardware just previews as if it did (the
// firmware always centers whatever JPEG it receives - see
// firmware/src/display.cpp's display_draw_jpeg).

export type DisplayShape = "round" | "rect";

export interface DisplayProfile {
  id: string;
  label: string;
  shape: DisplayShape;
  width: number;
  height: number;
  // Whether this profile's real hardware understands CMDCORC (the
  // length-prefixed color-JPEG command this app's live-preview/"Send to
  // device" path uses) - see firmware/src/protocol.cpp. Only the GC9A01
  // firmware in this repo implements it. Sending CMDCORC to anything else
  // (the legacy SSD1322 tty2oled, or a hypothetical rect-display variant
  // that was never built) desyncs the legacy CMDCOR-prefixed handler on
  // real hardware exactly like the CMDCORC/CMDCOR dispatch-order bug this
  // project's own firmware had before it was fixed - see CLAUDE.md. The
  // other profiles remain valid preview/export targets, just not sendable.
  colorArt: boolean;
  // Whether this profile's real hardware understands the legacy CMDCOR
  // fixed-size 4bpp GSC picture transfer (see legacyEncode.ts /
  // SerialLink.sendLegacyPicture) - true only for the exact 256x64
  // resolution the original SSD1322 tty2oled hardware and firmware use.
  // The generic rect profiles are preview/export targets for a firmware
  // variant that doesn't exist, so there's no real device to send to.
  legacyMono: boolean;
}

export const DISPLAY_PROFILES: DisplayProfile[] = [
  { id: "gc9a01-240", label: "GC9A01 240×240 (round)", shape: "round", width: 240, height: 240, colorArt: true, legacyMono: false },
  { id: "ssd1322-256x64", label: "SSD1322 256×64 (rect, legacy tty2oled)", shape: "rect", width: 256, height: 64, colorArt: false, legacyMono: true },
  { id: "rect-240x240", label: "Generic 240×240 (rect)", shape: "rect", width: 240, height: 240, colorArt: false, legacyMono: false },
  { id: "rect-320x240", label: "Generic 320×240 (rect, 4:3)", shape: "rect", width: 320, height: 240, colorArt: false, legacyMono: false },
];

// Defaults to the round GC9A01 profile - this project's actual firmware
// target (see CLAUDE.md). Previously defaulted to the legacy SSD1322 rect
// profile to match most existing tty2oled setups; changed 2026-08-13 once
// the round display was the one actually verified on real hardware.
export const DEFAULT_DISPLAY_PROFILE = DISPLAY_PROFILES.find((p) => p.id === "gc9a01-240")!;

export function findDisplayProfile(id: string): DisplayProfile {
  return DISPLAY_PROFILES.find((p) => p.id === id) ?? DEFAULT_DISPLAY_PROFILE;
}
