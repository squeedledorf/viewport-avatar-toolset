#!/usr/bin/env python3
"""Writes the help's beat loop: four seconds of a 120 BPM click track, eight beats, the first of each bar of four
higher, so the dance tutorial and the audio track's example play with sound. Made for VATs, released under CC0 1.0.

  make-beat-loop.py [out.wav]     default: docs/wiki/examples/beat-120bpm.wav
"""
import math, os, struct, sys, wave

RATE, BPM, BEATS = 8000, 120, 8  # 4 s, mono 16-bit: 64 KB; a beat is exactly 4000 samples


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "docs", "wiki", "examples", "beat-120bpm.wav")
    beat = RATE * 60 // BPM
    click = int(RATE * 0.05)
    samples = []
    for b in range(BEATS):
        freq = 1600 if b % 4 == 0 else 1000
        for i in range(beat):  # a short decaying tone right on the beat, then silence
            x = math.sin(2 * math.pi * freq * i / RATE) * math.exp(-i / (click / 5)) if i < click else 0.0
            samples.append(int(round(x * 0.7 * 32767)))
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))


if __name__ == "__main__":
    main()
