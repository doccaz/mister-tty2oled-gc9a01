// Packs a 256x64 canvas render into the legacy tty2oled 4bpp "GSC"
// grayscale format: 8192 bytes, one nibble per pixel, two pixels per byte
// (high nibble = even x, low nibble = odd x), row-major. Byte layout
// confirmed against tools/convert_library.py's decode_gsc() (itself
// cross-checked against reference/Pictures/c2gsc.sh and the original
// firmware's draw4bppBitmap decode order - see CLAUDE.md) and mirrored by
// firmware/src/display.cpp's display_draw_legacy_gsc().

export const LEGACY_W = 256;
export const LEGACY_H = 64;
export const GSC_BYTES = (LEGACY_W * LEGACY_H) / 2;

export function encodeGsc(imageData: ImageData): Uint8Array {
  if (imageData.width !== LEGACY_W || imageData.height !== LEGACY_H) {
    throw new Error(`encodeGsc: expected ${LEGACY_W}x${LEGACY_H}, got ${imageData.width}x${imageData.height}`);
  }
  const src = imageData.data; // RGBA
  const out = new Uint8Array(GSC_BYTES);
  const lineBytes = LEGACY_W / 2;
  for (let y = 0; y < LEGACY_H; y++) {
    for (let xb = 0; xb < lineBytes; xb++) {
      const x0 = xb * 2;
      const x1 = x0 + 1;
      const hi = grayNibble(src, x0, y);
      const lo = grayNibble(src, x1, y);
      out[xb + y * lineBytes] = (hi << 4) | lo;
    }
  }
  return out;
}

function grayNibble(rgba: Uint8ClampedArray, x: number, y: number): number {
  const i = (y * LEGACY_W + x) * 4;
  const luminance = 0.299 * rgba[i] + 0.587 * rgba[i + 1] + 0.114 * rgba[i + 2];
  return Math.min(15, Math.round(luminance / 17)); // 0..255 -> 0..15
}
