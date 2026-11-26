#!/usr/bin/env python3
"""Generate the 8-bit home icons in src/components/icons/.

Each icon is drawn on a 16x16 grid and scaled 2x to the 32x32 bitmap the
firmware expects, so every pixel of the art is a 2x2 block on the panel.

Bitmap layout (see GfxRenderer::drawIcon): 1 bit per pixel, MSB first, a 0 bit
is ink, and the stored rows are the image turned a quarter turn: display pixel
(X, Y) is stored at row 31-X, column Y.

Usage: python3 scripts/gen_pixel_icons.py
"""
from pathlib import Path

GRID = 16
SCALE = 2
SIZE = GRID * SCALE
OUT = Path(__file__).resolve().parent.parent / "src" / "components" / "icons"


class Art:
    def __init__(self):
        self.px = [[0] * GRID for _ in range(GRID)]

    def dot(self, x, y, v=1):
        if 0 <= x < GRID and 0 <= y < GRID:
            self.px[y][x] = v

    def fill(self, x, y, w, h, v=1):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.dot(xx, yy, v)

    def box(self, x, y, w, h):
        self.fill(x, y, w, h)
        self.fill(x + 1, y + 1, w - 2, h - 2, 0)

    def hline(self, x, y, w):
        self.fill(x, y, w, 1)

    def vline(self, x, y, h):
        self.fill(x, y, 1, h)

    def center(self):
        rows = [y for y in range(GRID) if any(self.px[y])]
        cols = [x for x in range(GRID) if any(self.px[y][x] for y in range(GRID))]
        top, left = min(rows), min(cols)
        h, w = max(rows) - top + 1, max(cols) - left + 1
        dy, dx = (GRID - h) // 2 - top, (GRID - w) // 2 - left
        out = Art()
        for y in range(GRID):
            for x in range(GRID):
                if self.px[y][x]:
                    out.dot(x + dx, y + dy)
        return out

    def ascii(self):
        return "\n".join("".join("#" if v else "." for v in row) for row in self.px)


def folder():
    a = Art()
    a.box(1, 2, 6, 3)
    a.box(1, 4, 14, 9)
    a.hline(2, 4, 4)
    a.fill(2, 4, 4, 1, 0)
    return a


def library():
    a = Art()
    a.box(1, 2, 4, 11)
    a.hline(1, 5, 4)
    a.hline(1, 10, 4)
    a.box(6, 4, 4, 9)
    a.hline(6, 7, 4)
    a.box(11, 3, 4, 10)
    a.hline(11, 6, 4)
    a.fill(0, 13, 16, 1)
    return a


def stats():
    a = Art()
    a.vline(1, 2, 12)
    a.hline(1, 13, 14)
    a.fill(4, 9, 2, 4)
    a.fill(7, 6, 2, 7)
    a.fill(10, 3, 2, 10)
    a.fill(13, 7, 2, 6)
    return a


def blocks():
    a = Art()
    a.box(1, 8, 6, 6)
    a.box(8, 8, 6, 6)
    a.box(1, 1, 6, 6)
    # A diamond that sits apart from the squares, as in the Lucide original.
    for i in range(4):
        a.hline(11 - i, 1 + i, 1 + 2 * i)
        a.hline(11 - i, 7 - i, 1 + 2 * i) if i < 3 else None
    return a


def transfer():
    a = Art()
    a.vline(4, 2, 12)
    for i in range(1, 4):
        a.dot(4 - i, 2 + i)
        a.dot(4 + i, 2 + i)
    a.vline(11, 2, 12)
    for i in range(1, 4):
        a.dot(11 - i, 13 - i)
        a.dot(11 + i, 13 - i)
    return a


def settings():
    a = Art()
    for y in range(GRID):
        for x in range(GRID):
            d2 = (x - 7.5) ** 2 + (y - 7.5) ** 2
            if 9 <= d2 <= 30:
                a.dot(x, y)
    a.fill(6, 0, 4, 3)
    a.fill(6, 13, 4, 3)
    a.fill(0, 6, 3, 4)
    a.fill(13, 6, 3, 4)
    for x, y in ((2, 2), (12, 2), (2, 12), (12, 12)):
        a.fill(x, y, 2, 2)
    a.fill(6, 6, 4, 4, 0)
    return a


def book():
    a = Art()
    a.box(1, 3, 7, 10)
    a.box(8, 3, 7, 10)
    a.vline(8, 3, 10)
    for y in (6, 8, 10):
        a.hline(3, y, 3)
        a.hline(10, y, 3)
    return a


ICONS = {
    "folder": ("FolderIcon", folder),
    "library": ("LibraryIcon", library),
    "stats": ("StatsIcon", stats),
    "blocks": ("BlocksIcon", blocks),
    "transfer": ("TransferIcon", transfer),
    "settings2": ("Settings2Icon", settings),
    "book": ("BookIcon", book),
}


def to_bytes(art):
    big = [[art.px[y // SCALE][x // SCALE] for x in range(SIZE)] for y in range(SIZE)]
    row_bytes = SIZE // 8
    data = []
    for row in range(SIZE):
        for byte in range(row_bytes):
            value = 0xFF
            for bit in range(8):
                col = byte * 8 + bit
                if big[col][SIZE - 1 - row]:  # image[Y=col][X=31-row]
                    value &= ~(0x80 >> bit)
            data.append(value)
    return data


def header(name, data):
    lines = ["#pragma once", "#include <cstdint>", "", f"// size: {SIZE}x{SIZE}",
             f"static const uint8_t {name}[] = {{"]
    for i in range(0, len(data), 19):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 19]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    for stem, (name, draw) in ICONS.items():
        art = draw().center()
        (OUT / f"{stem}.h").write_text(header(name, to_bytes(art)))
        print(f"{stem}.h\n{art.ascii()}\n")


if __name__ == "__main__":
    main()
