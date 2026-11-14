#!/usr/bin/env python3
"""Judges one PPM screendump: is the desktop's terminal showing text?

The terminal window opens at a fixed place on the 640x480 desk, its
ground is the theme's dark Foreground, and its glyphs are the light
Background ink. Counting light pixels strictly inside the window's
content box answers "did the text reach the screen" without trusting
the serial log.

Exit status is 0 when the ink is there, 1 when it is not.
"""
from __future__ import annotations

import sys

# The desk's terminal window: WindowFrame at (20,40), 600x400, with the
# title bar and the frame's one-pixel border cut away.
CONTENT_LEFT = 26
CONTENT_TOP = 58
CONTENT_RIGHT = 613
CONTENT_BOTTOM = 432

# The theme's light ink and its dark ground, as the desk paints them.
GROUND = (43, 38, 32)
INK = (246, 242, 234)
INK_TOLERANCE = 24

# Enough light cells to be words, not the cursor block or a stray pixel,
# and enough dark ground under them to know the terminal is really open:
# a closed terminal leaves the desk's own cream ground in that box, which
# would otherwise read as ink.
MIN_INK_PIXELS = 40
MIN_GROUND_FRACTION = 0.5


def read_ppm(path: str) -> tuple[int, int, bytes]:
    data = open(path, "rb").read()
    fields = data.split(b"\n", 3)
    if len(fields) != 4 or fields[0] != b"P6":
        raise SystemExit(f"{path}: not a binary PPM")
    width, height = (int(part) for part in fields[1].split())
    return width, height, fields[3]


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: count-terminal-ink.py SHOT.ppm")
    width, height, pixels = read_ppm(sys.argv[1])
    if (width, height) != (640, 480):
        raise SystemExit(f"{sys.argv[1]}: unexpected size {width}x{height}")

    ink = 0
    ground = 0
    box = 0
    for y in range(CONTENT_TOP, min(CONTENT_BOTTOM, height)):
        row = y * width * 3
        for x in range(CONTENT_LEFT, min(CONTENT_RIGHT, width)):
            at = row + x * 3
            r, g, b = pixels[at], pixels[at + 1], pixels[at + 2]
            box += 1
            if (
                abs(r - INK[0]) <= INK_TOLERANCE
                and abs(g - INK[1]) <= INK_TOLERANCE
                and abs(b - INK[2]) <= INK_TOLERANCE
            ):
                ink += 1
            elif (
                abs(r - GROUND[0]) <= INK_TOLERANCE
                and abs(g - GROUND[1]) <= INK_TOLERANCE
                and abs(b - GROUND[2]) <= INK_TOLERANCE
            ):
                ground += 1
    share = ground / box if box else 0.0
    print(f"  terminal ground: {share:.2%} of the box (need {MIN_GROUND_FRACTION:.0%})")
    print(f"  terminal ink pixels: {ink} (need {MIN_INK_PIXELS})")
    return 0 if share >= MIN_GROUND_FRACTION and ink >= MIN_INK_PIXELS else 1


if __name__ == "__main__":
    sys.exit(main())
