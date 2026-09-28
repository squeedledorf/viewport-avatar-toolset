#!/usr/bin/env python3
"""Builds the starter props made for VATs (CC0 1.0, see app/assets/props/CREDITS.md) in the same format as the
converted ones: low-poly, flat colours from the shared palette, grip point at the origin.

  make-starter-props.py [out-dir]     writes spear.dae and magazine.dae (default: app/assets/props)
"""
import importlib.util, math, os, sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("glb_to_prop", os.path.join(HERE, "glb-to-prop.py"))
conv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(conv)


def loft(rings, colour, cap_start=True, cap_end=True):
    """Triangles joining closed rings of equal size (each a list of 3D points, counter-clockwise seen from the
    end the loft grows towards), with fan caps at both ends."""
    tris = []
    for a, b in zip(rings, rings[1:]):
        n = len(a)
        for i in range(n):
            j = (i + 1) % n
            tris += [(a[i], a[j], b[j]), (a[i], b[j], b[i])]
    if cap_start:
        c = np.mean(rings[0], axis=0)
        tris += [(c, rings[0][(i + 1) % len(rings[0])], rings[0][i]) for i in range(len(rings[0]))]
    if cap_end:
        c = np.mean(rings[-1], axis=0)
        tris += [(c, rings[-1][i], rings[-1][(i + 1) % len(rings[-1])]) for i in range(len(rings[-1]))]
    return [(np.array(t, float), colour) for t in tris]


def ring(z, rx, ry=None, n=8, x=0.0, phase=None):
    ry = rx if ry is None else ry
    phase = math.pi / n if phase is None else phase
    return [(x + rx * math.cos(phase + 2 * math.pi * k / n), ry * math.sin(phase + 2 * math.pi * k / n), z) for k in range(n)]


def spear():
    """A 2 m spear along +Z: steel butt cap, wooden shaft, a socket and a leaf-shaped blade. The origin is the
    right hand's grip, 0.8 m up the shaft, the point where it balances with the head."""
    parts = []
    parts += loft([ring(0.0, 0.012), ring(0.035, 0.016)], "gunmetal")
    parts += loft([ring(0.035, 0.015), ring(1.70, 0.015)], "wood", cap_start=False, cap_end=False)
    parts += loft([ring(1.70, 0.016), ring(1.72, 0.019), ring(1.80, 0.014)], "gunmetal")
    # The blade: a flat diamond section (thin on X, its edges on Y like the Sword's) that swells to a point.
    blade = [(1.80, 0.010, 0.008), (1.86, 0.040, 0.009), (1.93, 0.034, 0.008), (2.00, 0.0005, 0.0005)]
    parts += loft([ring(z, t, w, n=4, phase=0) for z, w, t in blade], "steel", cap_end=False)
    return parts, (0.0, 0.0, 0.8)


def magazine():
    """A curved rifle magazine, 21 cm tall, sized for the starter Rifle: feed lips at the top, a base plate at the
    bottom, curving forward (+X) as it goes down. The origin is the grip of the hand that holds it, halfway down."""
    parts, rings = [], []
    steps, h, sweep = 8, 0.18, math.radians(22)
    radius = h / sweep
    for k in range(steps + 1):  # along an arc: the spine goes down and forward
        a = sweep * k / steps
        cx, cz = radius * (1 - math.cos(a)), -radius * math.sin(a)
        d = 0.034 + 0.004 * k / steps  # front to back, a little deeper at the bottom
        tx, tz = math.cos(a), -math.sin(a)  # the depth axis turns with the arc
        rings.append([(cx + sx * d * tx, sy * 0.013, cz + sx * d * tz) for sx, sy in ((1, -1), (1, 1), (-1, 1), (-1, -1))])
    rings = [[(x, y, z) for x, y, z in r] for r in rings]
    parts += loft(rings[::-1], "dark")  # built top-down; reversed so the rings run bottom to top
    last = sweep
    cx, cz = radius * (1 - math.cos(last)), -radius * math.sin(last)
    tx, tz = math.cos(last), -math.sin(last)
    nx, nz = math.sin(last), math.cos(last)  # the arc's normal (out of the base)
    plate = [[(cx + sx * 0.042 * tx + o * nx, sy * 0.016, cz + sx * 0.042 * tz + o * nz)
              for sx, sy in ((1, -1), (1, 1), (-1, 1), (-1, -1))] for o in (-0.012, 0.0)]
    parts += loft(plate, "black")
    lips = [[(sx * 0.03, sy * 0.009, z) for sx, sy in ((1, -1), (1, 1), (-1, 1), (-1, -1))] for z in (0.0, 0.012)]
    parts += loft(lips, "gunmetal")
    mid = sweep / 2  # the grip: the spine halfway down
    return parts, (radius * (1 - math.cos(mid)), 0.0, -radius * math.sin(mid))


def write(parts, path, origin):
    tris = np.array([t for t, _ in parts])
    cols = [c for _, c in parts]
    lo, hi = tris.reshape(-1, 3).min(0), tris.reshape(-1, 3).max(0)
    o = np.array(origin)
    tris = tris - o
    lo, hi = lo - o, hi - o
    keys = {c: i for i, c in enumerate(dict.fromkeys(cols))}
    mats = [keys[c] for c in cols]
    nv, nt = conv.write_dae(path, os.path.basename(path)[:-4], tris, mats, {i: c for c, i in keys.items()})
    print(f"{path}: {nt} tris, {nv} verts; bbox {lo.round(4)} .. {hi.round(4)}, centre {((lo + hi) / 2).round(4)}")


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "app", "assets", "props")
    for name, fn in (("spear", spear), ("magazine", magazine)):
        parts, origin = fn()
        write(parts, os.path.join(out, name + ".dae"), origin)
