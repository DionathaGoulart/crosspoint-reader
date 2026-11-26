#!/usr/bin/env python3
"""Give the generated line icons the 8-bit look, in place.

The icon headers in src/components/icons/ come from the SDK's gen_icons.py,
which draws smooth Lucide strokes. This script snaps each icon to a grid of
2x2 pixel blocks, so its pixels are as coarse as the home icons made by
scripts/gen_pixel_icons.py. Run it after every gen_icons.py regeneration.

Each icon tries both grid phases (a 1 px shift in x and y) and keeps the one
that differs least from the original, so a 2 px stroke stays 2 px instead of
growing to 4. Running it again on its own output changes nothing.

Bitmap layout (freeink::Icon): rows top to bottom, (w + 7) / 8 bytes per row,
MSB first, a 0 bit is ink. Not rotated.

Usage: python3 scripts/pixelate_icons.py [--check]
  --check  exit 1 if any header would change, and write nothing.
"""
import re
import sys
from pathlib import Path

ICON_DIR = Path(__file__).resolve().parent.parent / "src" / "components" / "icons"
BLOCK = 2
NOTE = "Pixelated afterwards by scripts/pixelate_icons.py."
# clang-format may wrap the note across comment lines.
NOTED = re.compile(r"Pixelated\W+afterwards")

# static const (or constexpr) uint8_t NAME[] = { ... };  then  freeink::Icon X = {w, h, oc, NAME};
ARRAY = re.compile(r"static (?:const|constexpr) uint8_t (\w+)\[\] = \{(.*?)\};", re.S)
ICON = re.compile(r"static (?:const|constexpr) freeink::Icon \w+ = \{(\d+), (\d+), (-?\d+), (\w+)\};")


def unpack(data, w, h):
    row_bytes = (w + 7) // 8
    return [[1 if not (data[y * row_bytes + (x >> 3)] >> (7 - (x & 7))) & 1 else 0 for x in range(w)]
            for y in range(h)]


def pack(img, w, h):
    row_bytes = (w + 7) // 8
    out = []
    for y in range(h):
        for b in range(row_bytes):
            value = 0xFF
            for bit in range(8):
                x = b * 8 + bit
                if x < w and img[y][x]:
                    value &= ~(0x80 >> bit)
            out.append(value)
    return out


def snap(img, w, h, dx, dy):
    """Blocks start at (dx, dy) - BLOCK, so the first block may be cut short."""
    out = [[0] * w for _ in range(h)]
    for by in range(dy - BLOCK if dy else 0, h, BLOCK):
        for bx in range(dx - BLOCK if dx else 0, w, BLOCK):
            ys = range(max(by, 0), min(by + BLOCK, h))
            xs = range(max(bx, 0), min(bx + BLOCK, w))
            cells = [img[y][x] for y in ys for x in xs]
            # Ink when at least half the block is ink; a cut-short block counts
            # against its own size.
            if sum(cells) * 2 >= len(cells):
                for y in ys:
                    for x in xs:
                        out[y][x] = 1
    return out


def pixelate(img, w, h):
    best, best_err = None, None
    for dy in range(BLOCK):
        for dx in range(BLOCK):
            cand = snap(img, w, h, dx, dy)
            err = sum(cand[y][x] != img[y][x] for y in range(h) for x in range(w))
            if best_err is None or err < best_err:
                best, best_err = cand, err
    return best


def process(text):
    sizes = {m.group(4): (int(m.group(1)), int(m.group(2))) for m in ICON.finditer(text)}
    count = 0

    def rewrite(m):
        nonlocal count
        name = m.group(1)
        if name not in sizes:
            return m.group(0)
        w, h = sizes[name]
        data = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(2))]
        if len(data) != ((w + 7) // 8) * h:
            raise SystemExit(f"{name}: {len(data)} bytes, expected {((w + 7) // 8) * h}")
        out = pack(pixelate(unpack(data, w, h), w, h), w, h)
        count += 1
        lines = [", ".join(f"0x{b:02X}" for b in out[i:i + 18]) + "," for i in range(0, len(out), 18)]
        qualifier = "constexpr" if m.group(0).startswith("static constexpr") else "const"
        return f"static {qualifier} uint8_t {name}[] = {{\n    " + "\n    ".join(lines) + "};"

    out = ARRAY.sub(rewrite, text)
    arrays = len(ARRAY.findall(text))
    if count != arrays:
        # An icon whose declaration the patterns miss would stay smooth, and
        # --check would not say so.
        raise SystemExit(f"{count} of {arrays} icon arrays have a freeink::Icon declaration")
    # Header comments from the SDK say "Do not edit"; say what happens next.
    if "Do not edit." in out and not NOTED.search(out):
        out = out.replace("Do not edit.", "Do not edit. " + NOTE, 1)
    return out, count


def main():
    check = "--check" in sys.argv[1:]
    changed = False
    for path in sorted(ICON_DIR.glob("*.h")):
        name = path.name
        text = path.read_text()
        # Only headers in the freeink::Icon format; the home icons are
        # drawn by scripts/gen_pixel_icons.py in a rotated layout.
        if "freeink::Icon" not in text:
            continue
        new, count = process(text)
        # Compare bytes, not layout: clang-format reflows these arrays.
        same = re.findall(r"0x[0-9A-Fa-f]{2}", text) == re.findall(r"0x[0-9A-Fa-f]{2}", new) and (
            "Do not edit." not in text or NOTED.search(text))
        print(f"{name}: {count} icons, {'unchanged' if same else 'pixelated'}")
        if not same:
            changed = True
            if not check:
                path.write_text(new)
    if check and changed:
        sys.exit(1)


if __name__ == "__main__":
    main()
