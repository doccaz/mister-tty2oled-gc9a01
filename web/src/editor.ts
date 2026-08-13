// Canvas2D crop/pan/zoom editor for a single core's marquee art, targeting
// a selectable DisplayProfile (round or rectangular, various resolutions -
// see displays.ts). Round profiles get a circular safe-area overlay.
// Exports via canvas.toBlob('image/jpeg') - the same pipeline used both
// for the on-screen preview and for the bytes sent over WebSerial, so
// there's exactly one image path (see CLAUDE.md "Web app design").

import type { DisplayProfile } from "./displays";
import { DEFAULT_TRANSFORM_BY_SHAPE, applyTransform, type TransformPreset } from "./transforms";
import { encodeGsc } from "./legacyEncode";

// On-screen render size caps (px); actual export resolution always comes
// from the DisplayProfile, independent of how big the canvas looks here.
const MAX_DISPLAY_SIZE = 320;

export class MarqueeEditor {
  readonly canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private profile: DisplayProfile;
  private img: ImageBitmap | null = null;
  private scale = 1;
  private offsetX = 0;
  private offsetY = 0;
  private dragging = false;
  private lastX = 0;
  private lastY = 0;
  private onChange: (() => void) | null = null;

  constructor(canvas: HTMLCanvasElement, profile: DisplayProfile) {
    this.canvas = canvas;
    this.profile = profile;
    const ctx = canvas.getContext("2d");
    if (!ctx) throw new Error("2D canvas context unavailable");
    this.ctx = ctx;
    this.resizeCanvasToProfile();

    canvas.addEventListener("pointerdown", (e) => {
      this.dragging = true;
      this.lastX = e.clientX;
      this.lastY = e.clientY;
      canvas.setPointerCapture(e.pointerId);
    });
    canvas.addEventListener("pointermove", (e) => {
      if (!this.dragging) return;
      // canvas.width may differ from its on-screen CSS size, so translate
      // pointer deltas into canvas pixel space.
      const scaleX = this.canvas.width / this.canvas.clientWidth;
      const scaleY = this.canvas.height / this.canvas.clientHeight;
      this.offsetX += (e.clientX - this.lastX) * scaleX;
      this.offsetY += (e.clientY - this.lastY) * scaleY;
      this.lastX = e.clientX;
      this.lastY = e.clientY;
      this.render();
      this.onChange?.();
    });
    canvas.addEventListener("pointerup", () => (this.dragging = false));
    canvas.addEventListener("pointerleave", () => (this.dragging = false));
    canvas.addEventListener(
      "wheel",
      (e) => {
        e.preventDefault();
        const factor = e.deltaY < 0 ? 1.05 : 0.95;
        this.scale = Math.min(8, Math.max(0.05, this.scale * factor));
        this.render();
        this.onChange?.();
      },
      { passive: false },
    );
  }

  /** Called after every pan/zoom/load/transform so callers can push a live preview. */
  setOnChange(cb: () => void) {
    this.onChange = cb;
  }

  private resizeCanvasToProfile() {
    this.canvas.width = this.profile.width;
    this.canvas.height = this.profile.height;
    // Cap the on-screen CSS size so large/odd aspect profiles stay usable.
    const displayScale = Math.min(1, MAX_DISPLAY_SIZE / Math.max(this.profile.width, this.profile.height));
    this.canvas.style.width = `${this.profile.width * displayScale}px`;
    this.canvas.style.height = `${this.profile.height * displayScale}px`;
    this.canvas.style.borderRadius = this.profile.shape === "round" ? "50%" : "4px";
  }

  /** Switches the target display; re-applies that shape's default transform preset if an image is loaded. */
  setProfile(profile: DisplayProfile) {
    this.profile = profile;
    this.resizeCanvasToProfile();
    if (this.img) {
      this.applyPreset(DEFAULT_TRANSFORM_BY_SHAPE[profile.shape]);
    } else {
      this.render();
    }
  }

  getProfile(): DisplayProfile {
    return this.profile;
  }

  async loadFile(file: File | Blob): Promise<void> {
    this.img = await createImageBitmap(file);
    this.applyPreset(DEFAULT_TRANSFORM_BY_SHAPE[this.profile.shape]);
  }

  /** Applies a fit preset (cover/contain/center-scale) as the new pan/zoom starting point. */
  applyPreset(preset: TransformPreset) {
    if (!this.img) return;
    const t = applyTransform(preset, this.canvas.width, this.canvas.height, this.img.width, this.img.height);
    this.scale = t.scale;
    this.offsetX = t.offsetX;
    this.offsetY = t.offsetY;
    this.render();
    this.onChange?.();
  }

  hasImage(): boolean {
    return this.img !== null;
  }

  private render() {
    const ctx = this.ctx;
    const { width: w, height: h } = this.canvas;
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, w, h);
    if (this.img) {
      ctx.drawImage(this.img, this.offsetX, this.offsetY, this.img.width * this.scale, this.img.height * this.scale);
    }
    if (this.profile.shape === "round") this.drawCircularOverlay();
  }

  private drawCircularOverlay() {
    const ctx = this.ctx;
    const { width: w, height: h } = this.canvas;
    const cx = w / 2;
    const cy = h / 2;
    const r = Math.min(w, h) / 2;

    ctx.save();
    // Dim everything outside the circle so the round bezel crop is obvious.
    ctx.globalCompositeOperation = "destination-in";
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();

    ctx.save();
    ctx.strokeStyle = "rgba(255,255,255,0.35)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(cx, cy, r - 1, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
  }

  /**
   * Locally animates the given transition effect (see effects.ts ids) from
   * black to the currently-loaded image over durationMs, purely in the
   * browser - no device connection needed. Mirrors (not pixel-for-pixel,
   * but the same reveal pattern) firmware/src/display.cpp's
   * display_transition() so what you see here is representative of what
   * CMDCORC will play on real hardware.
   */
  async playEffectPreview(effectId: number, durationMs: number): Promise<void> {
    if (!this.img) return;
    const w = this.canvas.width;
    const h = this.canvas.height;

    const final = document.createElement("canvas");
    final.width = w;
    final.height = h;
    const fctx = final.getContext("2d")!;
    fctx.fillStyle = "#000";
    fctx.fillRect(0, 0, w, h);
    fctx.drawImage(this.img, this.offsetX, this.offsetY, this.img.width * this.scale, this.img.height * this.scale);

    if (effectId === 0 || durationMs <= 0) {
      this.render();
      return;
    }

    const ctx = this.ctx;
    // Capture the start time on the FIRST rAF callback, not before
    // requestAnimationFrame() is called - a rAF timestamp reflects when
    // that frame cycle began, which can predate a performance.now() taken
    // just before scheduling it. Using the earlier timestamp let t go
    // slightly negative on frame one, producing a negative iris radius
    // (Canvas2D's arc() throws IndexSizeError on that; the wipe effects
    // masked the same underlying bug because a tiny negative rect width
    // just draws nothing instead of throwing).
    let start: number | null = null;

    await new Promise<void>((resolve) => {
      const step = (now: number) => {
        if (start === null) start = now;
        const t = Math.min(1, Math.max(0, (now - start) / durationMs));
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = "#000";
        ctx.fillRect(0, 0, w, h);

        ctx.save();
        switch (effectId) {
          case 1: // wipe left -> right
            ctx.beginPath();
            ctx.rect(0, 0, w * t, h);
            ctx.clip();
            break;
          case 2: // wipe right -> left
            ctx.beginPath();
            ctx.rect(w * (1 - t), 0, w * t, h);
            ctx.clip();
            break;
          case 3: // wipe top -> bottom
            ctx.beginPath();
            ctx.rect(0, 0, w, h * t);
            ctx.clip();
            break;
          case 4: { // iris: expanding circle from center
            const cx = w / 2;
            const cy = h / 2;
            const maxR = Math.hypot(cx, cy);
            ctx.beginPath();
            ctx.arc(cx, cy, maxR * t, 0, Math.PI * 2);
            ctx.clip();
            break;
          }
          case 5: // fade: cross-dissolve via alpha ramp
            ctx.globalAlpha = t;
            break;
        }
        ctx.drawImage(final, 0, 0);
        ctx.restore();

        if (this.profile.shape === "round") this.drawCircularOverlay();

        if (t < 1) {
          requestAnimationFrame(step);
        } else {
          this.render(); // settle back into normal interactive state
          resolve();
        }
      };
      requestAnimationFrame(step);
    });
  }

  /** Renders a fresh off-screen canvas at the profile's resolution, no overlay - shared by all export paths. */
  private renderOffscreen(): HTMLCanvasElement {
    const out = document.createElement("canvas");
    out.width = this.profile.width;
    out.height = this.profile.height;
    const octx = out.getContext("2d")!;
    octx.fillStyle = "#000";
    octx.fillRect(0, 0, out.width, out.height);
    if (this.img) {
      octx.drawImage(this.img, this.offsetX, this.offsetY, this.img.width * this.scale, this.img.height * this.scale);
    }
    return out;
  }

  async exportJpegBlob(quality = 0.85): Promise<Blob> {
    const out = this.renderOffscreen();
    return new Promise((resolve, reject) =>
      out.toBlob((b) => (b ? resolve(b) : reject(new Error("toBlob failed"))), "image/jpeg", quality),
    );
  }

  /** Same render, returned as raw bytes ready for the wire protocol. */
  async exportJpeg(quality = 0.85): Promise<Uint8Array> {
    const blob = await this.exportJpegBlob(quality);
    return new Uint8Array(await blob.arrayBuffer());
  }

  /**
   * Renders and packs into the legacy 4bpp GSC format for CMDCOR - only
   * valid when the current profile is exactly 256x64 (see displays.ts's
   * legacyMono flag), since that's the original hardware's fixed
   * resolution and the format has no notion of scaling.
   */
  exportGsc(): Uint8Array {
    const out = this.renderOffscreen();
    const octx = out.getContext("2d")!;
    return encodeGsc(octx.getImageData(0, 0, out.width, out.height));
  }
}
