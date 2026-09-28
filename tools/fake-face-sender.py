#!/usr/bin/env python3
# Viewport Avatar Toolset - a synthetic face stream for trying Tools > Motion Capture without a phone.
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#
#   tools/fake-face-sender.py vts    [--host H] [--phone-port 21412]   act as VTube Studio on an iPhone: wait for
#                                                                     VATs' request, then stream to the port it names
#   tools/fake-face-sender.py llf    [--host H] [--port 11111]         Live Link Face, Live Link (ARKit) mode
#   tools/fake-face-sender.py rokoko [--host H] [--port 14043]         Rokoko Studio JSON v3, a face-only actor
#
# Sends to 127.0.0.1 unless --host says otherwise. The face blinks, opens its jaw, smiles and turns its head.
# Packets are built from the public formats (the VTube Studio docs, PyLiveLinkFace, Rokoko's JSON v3), as the
# parsers in core/src/facecap.cpp and mocap.cpp read them. Stop with Ctrl+C, or --seconds N.
import argparse, json, math, socket, struct, time

ARKIT = ("eyeBlinkLeft eyeLookDownLeft eyeLookInLeft eyeLookOutLeft eyeLookUpLeft eyeSquintLeft eyeWideLeft "
         "eyeBlinkRight eyeLookDownRight eyeLookInRight eyeLookOutRight eyeLookUpRight eyeSquintRight eyeWideRight "
         "jawForward jawRight jawLeft jawOpen mouthClose mouthFunnel mouthPucker mouthRight mouthLeft mouthSmileLeft "
         "mouthSmileRight mouthFrownLeft mouthFrownRight mouthDimpleLeft mouthDimpleRight mouthStretchLeft "
         "mouthStretchRight mouthRollLower mouthRollUpper mouthShrugLower mouthShrugUpper mouthPressLeft "
         "mouthPressRight mouthLowerDownLeft mouthLowerDownRight mouthUpperUpLeft mouthUpperUpRight browDownLeft "
         "browDownRight browInnerUp browOuterUpLeft browOuterUpRight cheekPuff cheekSquintLeft cheekSquintRight "
         "noseSneerLeft noseSneerRight tongueOut").split()


def face(t):
    """ARKit weights 0..1 and head yaw/pitch in degrees at time t."""
    w = dict.fromkeys(ARKIT, 0.0)
    blink = 1.0 if (t % 3.0) < 0.15 else 0.0
    w["eyeBlinkLeft"] = w["eyeBlinkRight"] = blink
    w["jawOpen"] = 0.5 + 0.5 * math.sin(t * 2.0)
    w["mouthSmileLeft"] = w["mouthSmileRight"] = 0.5 + 0.5 * math.sin(t * 0.7)
    w["browInnerUp"] = 0.5 + 0.5 * math.sin(t * 1.3)
    return w, 20 * math.sin(t * 0.9), 8 * math.sin(t * 1.1)


def vts_frame(t):
    w, yaw, pitch = face(t)
    shapes = [{"k": k[0].upper() + k[1:], "v": round(v, 4)} for k, v in w.items()]  # VTS spells them EyeBlinkLeft
    return json.dumps({"Timestamp": time.time(), "Hotkey": -1, "FaceFound": True,
                       "Rotation": {"x": pitch, "y": yaw, "z": 0}, "Position": {"x": 0, "y": 0, "z": 0},
                       "EyeLeft": {"x": 0, "y": yaw / 3, "z": 0}, "EyeRight": {"x": 0, "y": yaw / 3, "z": 0},
                       "BlendShapes": shapes}).encode()


def llf_packet(t, frame):
    w, yaw, pitch = face(t)
    rad = math.radians  # the receiver assumes radians (kLiveLinkFaceDegreesPerUnit)
    values = [w[k] for k in ARKIT] + [rad(yaw), rad(pitch), 0, rad(yaw / 3), 0, 0, rad(yaw / 3), 0, 0]
    name = b"FakePhone"
    return (struct.pack("<I", 6) + b"$00000000-0000-0000-0000-000000000000" + struct.pack(">i", len(name)) + name +
            struct.pack(">ifii", frame, 0.0, 60, 1) + struct.pack(">B61f", 61, *values))


def rokoko_packet(t):
    w, _, _ = face(t)
    f = {k: round(v * 100, 2) for k, v in w.items()}
    f["faceId"] = "fake"
    return json.dumps({"version": 3, "fps": 60, "scene": {"timestamp": t, "actors": [
        {"name": "Fake Face", "meta": {"hasBody": False, "hasFace": True}, "face": f}], "props": []}}).encode()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("protocol", choices=["vts", "llf", "rokoko"])
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=0, help="where to send (llf, rokoko)")
    ap.add_argument("--phone-port", type=int, default=21412, help="vts: the port the fake phone listens on")
    ap.add_argument("--fps", type=float, default=60)
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long (0 = never)")
    a = ap.parse_args()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    start = time.monotonic()
    frame = 0
    until, target = 0.0, None  # vts: stream to target until `until`, as the request asked
    if a.protocol == "vts":
        s.bind((a.host, a.phone_port))
        s.setblocking(False)
        print(f"vts: waiting for a request on {a.host}:{a.phone_port}")
    else:
        target = (a.host, a.port or {"llf": 11111, "rokoko": 14043}[a.protocol])
    while not a.seconds or time.monotonic() - start < a.seconds:
        t = time.monotonic() - start
        if a.protocol == "vts":
            try:
                while True:
                    data, addr = s.recvfrom(4096)
                    req = json.loads(data)
                    if req.get("messageType") == "iOSTrackingDataRequest" and req.get("ports"):
                        if target is None:
                            print(f"vts: request from {addr[0]}, streaming to port {req['ports'][0]}")
                        target = (addr[0], int(req["ports"][0]))
                        until = time.monotonic() + float(req.get("time", 5))
            except (BlockingIOError, ValueError):
                pass
            if target and time.monotonic() < until:
                s.sendto(vts_frame(t), target)
        elif a.protocol == "llf":
            s.sendto(llf_packet(t, frame), target)
        else:
            s.sendto(rokoko_packet(t), target)
        frame += 1
        time.sleep(1 / a.fps)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
