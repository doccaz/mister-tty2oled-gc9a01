#!/usr/bin/env python3
"""
Convert tty2oled .gsc (4bpp grayscale, 8192 bytes) / .xbm (1bpp, 2048 bytes)
marquee files (256x64) into PNG thumbnails, plus a JSON index.

Source files are C-array text: 3 header lines, then comma-separated
"0X.." hex byte literals, terminated by "};". Format confirmed against
reference/Pictures/c2gsc.sh and MiSTer_SSD1322_USB.ino's oled_readlogo()/
draw4bppBitmap decode order (see project CLAUDE.md).
"""
import json
import re
import sys
from pathlib import Path

from PIL import Image

SRC_ROOT = Path(sys.argv[1])
OUT_ROOT = Path(sys.argv[2])
OUT_ROOT.mkdir(parents=True, exist_ok=True)

HEX_RE = re.compile(rb"0[xX]([0-9a-fA-F]{2})")

W, H = 256, 64


COMMENT_RE = re.compile(rb"/\*.*?\*/", re.DOTALL)


def parse_bytes(path: Path) -> bytes:
    data = path.read_bytes()
    data = COMMENT_RE.sub(b"", data)  # some headers embed a commented-out byte preview, e.g. "{ /* 0X00,0X04,... */"
    return bytes(int(m.group(1), 16) for m in HEX_RE.finditer(data))


def decode_xbm(buf: bytes) -> Image.Image:
    img = Image.new("L", (W, H), 0)
    px = img.load()
    line_bytes = W // 8
    for y in range(H):
        for xb in range(line_bytes):
            byte = buf[xb + y * line_bytes]
            for bit in range(8):
                x = xb * 8 + bit
                px[x, y] = 255 if (byte >> bit) & 1 else 0
    return img


def decode_gsc(buf: bytes) -> Image.Image:
    img = Image.new("L", (W, H), 0)
    px = img.load()
    line_bytes = W // 2
    for y in range(H):
        for xb in range(line_bytes):
            byte = buf[xb + y * line_bytes]
            hi = (byte >> 4) & 0xF
            lo = byte & 0xF
            px[xb * 2, y] = hi * 17
            px[xb * 2 + 1, y] = lo * 17
    return img


def main():
    entries = []
    files = sorted(SRC_ROOT.rglob("*"))
    candidates = [f for f in files if f.suffix.lower() in (".gsc", ".xbm") and f.is_file()]
    print(f"Found {len(candidates)} candidate files", file=sys.stderr)

    for f in candidates:
        try:
            buf = parse_bytes(f)
        except Exception as e:
            print(f"SKIP (parse error) {f}: {e}", file=sys.stderr)
            continue

        # A minority of files declare `_bits[8198]`/`_bits[2054]` - 6 extra
        # leading bytes (image2lcd metadata, e.g. width/height/type) before
        # the actual 8192/2048-byte pixel payload, sometimes as a "/* */"
        # comment (stripped above) and sometimes inline in the array itself.
        if len(buf) in (8192 + 6, 2048 + 6):
            buf = buf[6:]

        kind = None
        if len(buf) == 8192:
            img = decode_gsc(buf)
            kind = "gsc"
        elif len(buf) == 2048:
            img = decode_xbm(buf)
            kind = "xbm"
        else:
            print(f"SKIP (unexpected size {len(buf)}) {f}", file=sys.stderr)
            continue

        pack = f.parent.relative_to(SRC_ROOT).as_posix() or "root"
        safe_pack = re.sub(r"[^A-Za-z0-9_.-]+", "_", pack)
        safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", f.stem)
        out_dir = OUT_ROOT / safe_pack
        out_dir.mkdir(parents=True, exist_ok=True)
        out_path = out_dir / f"{safe_name}.png"
        img.save(out_path)

        entries.append(
            {
                "pack": pack,
                "name": f.stem,
                "kind": kind,
                "png": str(out_path.relative_to(OUT_ROOT)),
            }
        )

    index_path = OUT_ROOT / "index.json"
    index_path.write_text(json.dumps(entries, indent=2))
    print(f"Converted {len(entries)} images -> {OUT_ROOT}", file=sys.stderr)
    print(f"Index: {index_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
