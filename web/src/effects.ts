// Curated transition effect set - mirrored in firmware/src/display.cpp's
// display_draw_jpeg()/display_transition() so the id numbers sent over
// CMDCORC mean the same thing on both ends. Kept intentionally small
// (not the original tty2oled's 23) - see CLAUDE.md.

export interface EffectDef {
  id: number;
  label: string;
}

export const EFFECTS: EffectDef[] = [
  { id: 0, label: "none (instant cut)" },
  { id: 1, label: "wipe left → right" },
  { id: 2, label: "wipe right → left" },
  { id: 3, label: "wipe top → bottom" },
  { id: 4, label: "iris (expand from center)" },
  { id: 5, label: "fade (cross-dissolve)" },
];

export interface SpeedPreset {
  id: string;
  label: string;
  durationMs: number;
}

export const SPEED_PRESETS: SpeedPreset[] = [
  { id: "slow", label: "slow", durationMs: 1200 },
  { id: "normal", label: "normal", durationMs: 500 },
  { id: "fast", label: "fast", durationMs: 200 },
];

export const DEFAULT_SPEED = SPEED_PRESETS[1];
