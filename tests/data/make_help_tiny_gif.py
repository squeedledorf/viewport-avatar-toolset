#!/usr/bin/env python3
"""Writes help-tiny.gif: three 4x3 frames, red 70 ms, green 0 ms, blue 250 ms, looping, made by Pillow rather than
VATs' own GIF writer, for the GIF reader's test (tests/test_gif.cpp, gif_reads_fixture). Rerun instead of editing.

    python3 tests/data/make_help_tiny_gif.py
"""
import os

from PIL import Image

frames = [Image.new("RGB", (4, 3), c) for c in [(255, 0, 0), (0, 255, 0), (0, 0, 255)]]
frames[0].save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "help-tiny.gif"), save_all=True,
               append_images=frames[1:], duration=[70, 0, 250], loop=0, optimize=False, disposal=1)
