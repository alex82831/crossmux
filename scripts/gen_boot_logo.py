#!/usr/bin/env python3
"""Generate src/images/Logo120.h — the boot/sleep splash logo bitmap.

Design source: src/images/Logo120.svg — the FLASHAPPS mark: a lightning bolt
knocked out of a rounded app tile. Rendered for the 1-bit panel as tile ink
with a white bolt; deliberately just the two shapes, because anything finer
than that turns to mush at 120px on an e-ink refresh.

This rasterizes the SAME geometry with the Python standard library only (no
cairosvg / Pillow), so the asset is reproducible on any machine. Keep the
constants below in sync with Logo120.svg.

Output matches the firmware's drawImage() 1bpp convention:
  120x120, row-major, MSB-first, 8px/byte, white=0xFF / black=0x00
  (bit 1 -> white pixel, bit 0 -> black pixel). No rotation — BootActivity /
  SleepActivity draw it via drawImage(), not drawIcon(), so no 90deg pre-rotate.

Usage:
  python3 scripts/gen_boot_logo.py            # write src/images/Logo120.h
  python3 scripts/gen_boot_logo.py --ascii     # also print an ASCII preview
  python3 scripts/gen_boot_logo.py --png out.png  # also write a PNG preview
"""
import os
import struct
import sys
import zlib

SIZE = 120

# --- geometry (keep in sync with Logo120.svg) -------------------------------
TILE_INSET = 5          # margin from the bitmap edge to the tile
TILE_RADIUS = 24        # rounded-corner radius of the tile
# Lightning bolt, in bitmap coordinates. A single closed polygon so the
# scanline fill below needs no special cases.
BOLT = [
    (73, 14),
    (33, 66),
    (56, 66),
    (47, 106),
    (88, 52),
    (64, 52),
]


def in_rounded_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    # Inside the straight edges of the cross shape?
    if x0 + r <= x <= x1 - r or y0 + r <= y <= y1 - r:
        return True
    # Otherwise it must fall inside one of the four corner discs.
    cx = x0 + r if x < x0 + r else x1 - r
    cy = y0 + r if y < y0 + r else y1 - r
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def in_polygon(x, y, poly):
    """Even-odd scanline test on pixel centres."""
    inside = False
    px, py = x + 0.5, y + 0.5
    n = len(poly)
    for i in range(n):
        ax, ay = poly[i]
        bx, by = poly[(i + 1) % n]
        if (ay > py) != (by > py):
            t = (py - ay) / (by - ay)
            if px < ax + t * (bx - ax):
                inside = not inside
    return inside


def build_rows():
    """Rows of booleans; True = white pixel."""
    x1 = y1 = SIZE - 1 - TILE_INSET
    x0 = y0 = TILE_INSET
    rows = []
    for y in range(SIZE):
        row = []
        for x in range(SIZE):
            on_tile = in_rounded_rect(x, y, x0, y0, x1, y1, TILE_RADIUS)
            if not on_tile:
                row.append(True)                     # page white around the tile
            elif in_polygon(x, y, BOLT):
                row.append(True)                     # the bolt itself
            else:
                row.append(False)                    # tile ink
        rows.append(row)
    return rows


def pack(rows):
    out = bytearray()
    for row in rows:
        for byte_start in range(0, SIZE, 8):
            byte = 0
            for bit in range(8):
                x = byte_start + bit
                if x < SIZE and row[x]:
                    byte |= 1 << (7 - bit)
                elif x >= SIZE:
                    byte |= 1 << (7 - bit)           # pad beyond the edge as white
            out.append(byte)
    return bytes(out)


def write_header(data, path):
    lines = []
    per_line = 19
    for i in range(0, len(data), per_line):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + per_line])
        lines.append("    " + chunk)
    body = ",\n".join(lines)
    with open(path, "w") as f:
        f.write("#pragma once\n#include <cstdint>\n\n")
        f.write(f"// Image dimensions: {SIZE}x{SIZE}\n")
        f.write("// Source: src/images/Logo120.svg - regenerate via scripts/gen_boot_logo.py\n")
        f.write(f"static const uint8_t Logo{SIZE}[] = {{\n{body}}};\n")


def print_ascii(rows):
    step = 2
    for y in range(0, SIZE, step):
        print("".join("." if rows[y][x] else "#" for x in range(0, SIZE, step)))


def write_png(rows, path):
    scale = 1
    W = H = SIZE * scale
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        sy = y // scale
        for x in range(W):
            raw.append(255 if rows[sy][x // scale] else 0)

    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data
                + struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_h = os.path.join(root, "src", "images", "Logo120.h")
    rows = build_rows()
    write_header(pack(rows), out_h)
    print(f"Wrote {out_h}")
    if "--ascii" in sys.argv:
        print_ascii(rows)
    if "--png" in sys.argv:
        idx = sys.argv.index("--png") + 1
        if idx >= len(sys.argv):
            print("error: --png requires an output path, e.g. --png preview.png",
                  file=sys.stderr)
            sys.exit(2)
        png_path = sys.argv[idx]
        write_png(rows, png_path)
        print(f"Wrote {png_path}")


if __name__ == "__main__":
    main()
