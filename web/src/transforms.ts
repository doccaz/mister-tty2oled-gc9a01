import type { DisplayShape } from "./displays";

// Fit presets applied to an image against a canvas of a given size. Each
// produces an initial {scale, offsetX, offsetY}; the user can still
// pan/zoom manually afterward (see editor.ts) - presets just set the
// starting point, they aren't a locked mode.

export type TransformMode = "cover" | "contain" | "center-scale";

export interface TransformPreset {
  mode: TransformMode;
  /** Only used by "center-scale": percent of the cover-fit scale to render at, leaving a margin. */
  scalePercent: number;
}

// Per-shape defaults: round displays default to a slightly-inset centered
// scale so art doesn't get clipped hard by the circular bezel; rect
// displays default to a plain edge-to-edge cover fit.
export const DEFAULT_TRANSFORM_BY_SHAPE: Record<DisplayShape, TransformPreset> = {
  round: { mode: "center-scale", scalePercent: 91 },
  rect: { mode: "cover", scalePercent: 100 },
};

export interface Transform {
  scale: number;
  offsetX: number;
  offsetY: number;
}

export function applyTransform(
  preset: TransformPreset,
  canvasW: number,
  canvasH: number,
  imgW: number,
  imgH: number,
): Transform {
  const coverScale = Math.max(canvasW / imgW, canvasH / imgH);
  const containScale = Math.min(canvasW / imgW, canvasH / imgH);

  let scale: number;
  switch (preset.mode) {
    case "contain":
      scale = containScale;
      break;
    case "center-scale":
      scale = coverScale * (preset.scalePercent / 100);
      break;
    case "cover":
    default:
      scale = coverScale;
      break;
  }

  return {
    scale,
    offsetX: (canvasW - imgW * scale) / 2,
    offsetY: (canvasH - imgH * scale) / 2,
  };
}
