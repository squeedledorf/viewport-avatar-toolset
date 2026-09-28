#!/usr/bin/env python3
# Viewport Avatar Toolset - compares the keys of two projects (a tutorial followed in the app, from its autosave,
# against the tutorial's shipped example): every bone's channels, keyed frames and values, rounded to the
# precision the page gives. Prints the differences; exits 1 when there are any.
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#
#   compare-keys.py done.vat example.vat [clip-index]
import json
import sys


def curves(path, clip):
    doc = json.load(open(path))
    found = []

    def walk(o):
        if isinstance(o, dict):
            if 'curves' in o:
                found.append(o['curves'])
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(doc)
    out = {}
    for bone, track in found[clip].items():
        for ch, curve in track.items():
            keys = curve['keys'] if isinstance(curve, dict) else curve
            vals = []
            for k in keys:
                f, v = (k[0], k[1]) if isinstance(k, list) else (k['frame'], k['value'])
                vals.append((round(f, 1), round(v + 0.0, 3) + 0.0))
            out[(bone, ch)] = vals
    return out


a = curves(sys.argv[1], int(sys.argv[3]) if len(sys.argv) > 3 else 0)
b = curves(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 0)
bad = 0
for k in sorted(set(a) | set(b)):
    if a.get(k) != b.get(k):
        bad += 1
        print(f'{k[0]} {k[1]}\n  followed: {a.get(k)}\n  example:  {b.get(k)}')
print(f'{len(set(a) | set(b))} channels, {bad} differ')
sys.exit(1 if bad else 0)
