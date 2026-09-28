#!/usr/bin/env python3
"""Writes mixamo_walk.bvh: a two-second walk on a Mixamo-named skeleton (Y up, centimetres, T-pose rest),
made from sine waves. It is the retargeting fixture for the help wiki's screenshots (docs/wiki/retargeting.md)
and for tools/make_wiki_examples.cpp; rerun this script instead of editing the BVH.

    python3 tests/data/make_mixamo_walk.py
"""
import math
import os

FPS, FRAMES = 30, 61          # 2 s, the last frame repeats the first
STRIDE_S = 1.0                # one full stride (both steps) per second
SPEED_CM_S = 137.0            # hips travel forward (+Z) as fast as 15-degree steps sweep: the planted foot stands still

# name, parent, offset (cm). Mixamo faces +Z; +X is its left.
JOINTS = [
    ("Hips", None, (0, 98, 0)),
    ("Spine", "Hips", (0, 10, 0)),
    ("Spine1", "Spine", (0, 12, 0)),
    ("Spine2", "Spine1", (0, 12, 0)),
    ("Neck", "Spine2", (0, 14, 0)),
    ("Head", "Neck", (0, 10, 0)),
    ("LeftShoulder", "Spine2", (6, 10, 0)),
    ("LeftArm", "LeftShoulder", (12, 0, 0)),
    ("LeftForeArm", "LeftArm", (28, 0, 0)),
    ("LeftHand", "LeftForeArm", (26, 0, 0)),
    ("RightShoulder", "Spine2", (-6, 10, 0)),
    ("RightArm", "RightShoulder", (-12, 0, 0)),
    ("RightForeArm", "RightArm", (-28, 0, 0)),
    ("RightHand", "RightForeArm", (-26, 0, 0)),
    ("LeftUpLeg", "Hips", (9, -5, 0)),
    ("LeftLeg", "LeftUpLeg", (0, -42, 0)),
    ("LeftFoot", "LeftLeg", (0, -42, 0)),
    ("LeftToeBase", "LeftFoot", (0, -8, 12)),
    ("RightUpLeg", "Hips", (-9, -5, 0)),
    ("RightLeg", "RightUpLeg", (0, -42, 0)),
    ("RightFoot", "RightLeg", (0, -42, 0)),
    ("RightToeBase", "RightFoot", (0, -8, 12)),
]
END_SITES = {"Head": (0, 12, 0), "LeftHand": (10, 0, 0), "RightHand": (-10, 0, 0),
             "LeftToeBase": (0, 0, 8), "RightToeBase": (0, 0, 8)}


def rotation(name, t):
    """Local Z, X, Y rotation in degrees at time t (seconds). Zero is the T-pose."""
    w = 2 * math.pi * t / STRIDE_S
    swing = math.sin(w)            # left leg forward at +1
    if name == "Hips":
        return (0, 0, 4 * math.sin(w))                       # a little yaw with the stride
    if name in ("Spine", "Spine1"):
        return (0, 0, -3 * math.sin(w))                      # the torso counter-turns
    if name == "LeftUpLeg":                                  # -X pitches the leg forward
        return (0, -15 * swing, 0)
    if name == "RightUpLeg":
        return (0, 15 * swing, 0)
    if name == "LeftLeg":                                    # the knee bends while the leg swings through
        return (0, 35 * max(0.0, -math.sin(w - 1.2)) ** 2, 0)
    if name == "RightLeg":
        return (0, 35 * max(0.0, math.sin(w - 1.2)) ** 2, 0)
    if name in ("LeftFoot", "RightFoot"):
        return (0, -8 * (swing if name == "LeftFoot" else -swing), 0)
    # Arms hang down from the T-pose (Z) and swing opposite to the legs (Y).
    if name == "LeftArm":
        return (-70, 0, -20 * swing)
    if name == "RightArm":
        return (70, 0, 20 * swing)
    if name in ("LeftForeArm", "RightForeArm"):
        return (0, 0, (15 if name == "LeftForeArm" else -15) * (1 - swing) / 2)
    return (0, 0, 0)


def main():
    out = []
    indent = 0

    def line(s):
        out.append("\t" * indent + s)

    children = {n: [c for c, p, _ in JOINTS if p == n] for n, _, _ in JOINTS}

    def write_joint(name, parent, offset):
        nonlocal indent
        line(("ROOT " if parent is None else "JOINT ") + "mixamorig:" + name)
        line("{")
        indent += 1
        line("OFFSET %.4f %.4f %.4f" % offset)
        line("CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation" if parent is None
             else "CHANNELS 3 Zrotation Xrotation Yrotation")
        for child in children[name]:
            write_joint(child, name, next(o for n, _, o in JOINTS if n == child))
        if name in END_SITES:
            line("End Site")
            line("{")
            indent += 1
            line("OFFSET %.4f %.4f %.4f" % END_SITES[name])
            indent -= 1
            line("}")
        indent -= 1
        line("}")

    line("HIERARCHY")
    write_joint(*JOINTS[0])
    line("MOTION")
    line("Frames: %d" % FRAMES)
    line("Frame Time: %.6f" % (1 / FPS))
    for f in range(FRAMES):
        t = f / FPS
        w = 2 * math.pi * t / STRIDE_S
        values = [0.0, 98 - 2.5 * abs(math.sin(w)), SPEED_CM_S * t]   # the hips bob and travel forward
        for name, parent, _ in JOINTS:
            values.extend(rotation(name, t))
        line(" ".join("%.4f" % v for v in values))
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mixamo_walk.bvh")
    with open(path, "w") as fh:
        fh.write("\n".join(out) + "\n")
    print("wrote", path, len(JOINTS), "joints,", FRAMES, "frames")


if __name__ == "__main__":
    main()
