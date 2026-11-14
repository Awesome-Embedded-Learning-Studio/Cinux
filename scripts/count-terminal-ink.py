#!/usr/bin/env python3
"""Judges one PPM screendump: is the desktop's terminal showing text, and
where is it?

The terminal is the screen's big dark box; its glyphs are the theme's
light ink. Finding the box first makes the check independent of where
the window sits -- the desk can move one -- and counting the ink inside
it answers "did the text reach the screen" without trusting any log.

Prints the box and the ink count, then exits 0 when the box carries
text, 1 when it does not.
"""
from __future__ import annotations

import sys

# The theme's inks, as the desk paints them.
GROUND = (43, 38, 32)
INK = (246, 242, 234)
TOLERANCE = 24

# A terminal content box this large is a window, not a title bar or a
# panel strip; and this much ink is words, not a lone cursor block.
MIN_BOX_PIXELS = 20000
MIN_INK_PIXELS = 40


def read_ppm(path: str) -> tuple[int, int, bytes]:
    data = open(path, "rb").read()
    fields = data.split(b"\n", 3)
    if len(fields) != 4 or fields[0] != b"P6":
        raise SystemExit(f"{path}: not a binary PPM")
    width, height = (int(part) for part in fields[1].split())
    return width, height, fields[3]


def near(pixel: bytes, want: tuple[int, int, int]) -> bool:
    return all(abs(pixel[i] - want[i]) <= TOLERANCE for i in range(3))


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: count-terminal-ink.py SHOT.ppm")
    width, height, pixels = read_ppm(sys.argv[1])

    # Rows of the dark ground, the terminal's paint.
    dark_rows: list[int] = []
    for y in range(height):
        row = y * width * 3
        dark = 0
        for x in range(width):
            at = row + x * 3
            if near(pixels[at : at + 3], GROUND):
                dark += 1
        if dark > width // 2:
            dark_rows.append(y)
    if not dark_rows:
        print("  terminal box: none")
        return 1

    top, bottom = dark_rows[0], dark_rows[-1] + 1
    # The columns the box spans, taken from its middle row.
    middle = (top + bottom) // 2
    row = middle * width * 3
    columns = [x for x in range(width) if near(pixels[row + x * 3 : row + x * 3 + 3], GROUND)]
    left, right = columns[0], columns[-1] + 1

    box_pixels = (right - left) * (bottom - top)
    ink = 0
    for y in range(top, bottom):
        base = y * width * 3
        for x in range(left, right):
            at = base + x * 3
            if near(pixels[at : at + 3], INK):
                ink += 1

    print(f"  terminal box: {left},{top} {right - left}x{bottom - top}")
    print(f"  terminal ink pixels: {ink} (need {MIN_INK_PIXELS})")
    if box_pixels < MIN_BOX_PIXELS:
        print("  terminal box: too small to be a window")
        return 1
    return 0 if ink >= MIN_INK_PIXELS else 1


if __name__ == "__main__":
    sys.exit(main())
