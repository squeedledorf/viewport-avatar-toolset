#!/usr/bin/env python3
"""Writes thumb-blank.png (4x4, colour everywhere but alpha 0, as the viewer's build 25 wrote thumbnails) and
thumb-drawn.png (the same with one opaque pixel), made by Pillow, for png_blank's test (tests/test_gif.cpp,
png_blank_finds_empty_thumbnails). Rerun instead of editing.

    python3 tests/data/make_thumb_pngs.py
"""
import os

from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
im = Image.new("RGBA", (4, 4), (200, 120, 90, 0))
im.save(os.path.join(here, "thumb-blank.png"))
im.putpixel((2, 1), (200, 120, 90, 255))
im.save(os.path.join(here, "thumb-drawn.png"))
