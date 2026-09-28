#!/usr/bin/env python3
"""Writes click_track.wav: four seconds of clicks at 120 BPM (8 kHz, mono, 16-bit), the audio for the help
wiki's timeline screenshots (docs/wiki/audio-track.md). Rerun this script instead of editing the file.

    python3 tests/data/make_click_track.py
"""
import math
import os
import struct
import wave

RATE, SECONDS, BPM = 8000, 4, 120


def main():
    samples = []
    for i in range(RATE * SECONDS):
        t = i / RATE
        since_beat = t % (60 / BPM)
        beat = int(t // (60 / BPM))
        tone = 880 if beat % 4 == 0 else 660                     # the downbeat is higher
        v = math.sin(2 * math.pi * tone * t) * math.exp(-since_beat * 30)
        samples.append(int(v * 28000))
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "click_track.wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))
    print("wrote", path)


if __name__ == "__main__":
    main()
