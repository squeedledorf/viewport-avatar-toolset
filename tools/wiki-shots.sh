#!/usr/bin/env bash
# Regenerates the help wiki's screenshots from docs/wiki/images/shots.json (docs/wiki/STYLE.md, Images).
#
#   tools/wiki-shots.sh [filter]   every shot, or those whose page folder or whole "out" is filter, a glob:
#                                  loop-tools, loop-tools/loop-assist.png, 'tutorial-*' (quoted), '*/timeline*.png'
#
# Each shot runs the app headless on a private Xvfb display (never your desktop), with a throwaway --data-dir
# (never your settings), crops the picture, runs tools/optimize-wiki-images.sh on it and writes it to
# docs/wiki/images/<out>. The app is $VATS_BIN, else build/app/vats or build-release/app/vats.
#
# Manifest:
#   {"defaults": {"theme": "Dusk", "size": "1200x1000"},
#    "shots": [{"out": "graph-editor/graph-panel.png",
#               "args": ["examples/graph-basics.vat", "--frame", "22", "--select", "mElbowRight"],
#               "theme": "Studio Grey", "size": "1600x900",
#               "crop": "Graph"}]}
# args are the app's command line (docs/wiki/command-line.md) before --screenshot; paths in them are relative
# to docs/wiki. crop is optional: a window's name as its title bar shows it (--shot-rect), or [x, y, w, h] in
# the window's pixels. With a named crop, "inset": [x, y, w, h] takes that part of the window instead (w or h
# 0 = to its edge). The app runs with a fake firewall state and network address (VATS_FAKE_FIREWALL,
# VATS_FAKE_LAN), so a shot never shows this computer's. Needs Xvfb, xdotool, python3, and Pillow or ImageMagick.
set -euo pipefail
VATS="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIKI="$VATS/docs/wiki"
BIN="${VATS_BIN:-}"
for b in "$VATS/build/app/vats" "$VATS/build-release/app/vats"; do [[ -z "$BIN" && -x "$b" ]] && BIN="$b"; done
[[ -x "$BIN" ]] || { echo "No vats binary: build it or set VATS_BIN" >&2; exit 1; }
for t in Xvfb xdotool; do command -v $t >/dev/null || { echo "Needs $t" >&2; exit 1; }; done

# A display nobody uses: no socket and no lock file.
n=90
while [[ -e /tmp/.X11-unix/X$n || -e /tmp/.X$n-lock ]]; do ((++n)); done
Xvfb ":$n" -screen 0 2560x1600x24 -nolisten tcp >/dev/null 2>&1 &
xvfb=$!
work="$(mktemp -d)"
trap 'kill "$xvfb" 2>/dev/null; wait "$xvfb" 2>/dev/null; rm -rf "$work"' EXIT
# The socket shows up before the server takes clients: wait until one can ask it something (10 s at most).
ready=
for _ in $(seq 100); do DISPLAY=":$n" xdotool getdisplaygeometry >/dev/null 2>&1 && ready=1 && break; sleep 0.1; done
[[ -n "$ready" ]] || { echo "Xvfb did not start on :$n" >&2; exit 1; }

DISPLAY=":$n" BIN="$BIN" WIKI="$WIKI" WORK="$work" FILTER="${1:-}" OPT="$VATS/tools/optimize-wiki-images.sh" python3 - <<'PY'
import fnmatch, json, os, re, shutil, subprocess, sys

wiki, work = os.environ["WIKI"], os.environ["WORK"]
manifest = json.load(open(os.path.join(wiki, "images", "shots.json")))
defaults = manifest.get("defaults", {})
try:
    from PIL import Image
except ImportError:
    Image = None

def crop(src, dst, rect):
    x, y, w, h = rect
    if Image:
        Image.open(src).crop((x, y, x + w, y + h)).save(dst)
    else:
        subprocess.run(["magick", src, "-crop", f"{w}x{h}+{x}+{y}", "+repage", dst], check=True)

# X11 on the private display at 100% scale, whatever the desktop session sets.
env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "GDK_SCALE", "GDK_DPI_SCALE")}
env.update(GDK_BACKEND="x11", SDL_VIDEO_DRIVER="x11", SDL_VIDEO_X11_SCALING_FACTOR="1", VATS_HELP_DIR=wiki,
           VATS_FAKE_FIREWALL="none", VATS_FAKE_LAN="192.168.1.10")
failed = matched = 0
flt = os.environ["FILTER"]
for i, shot in enumerate(manifest["shots"]):
    out = shot["out"]
    # the whole "out" or its page folder, as a glob (fnmatch matches all of the name, so "tutorial-" is no prefix)
    if flt and not (fnmatch.fnmatchcase(out, flt) or fnmatch.fnmatchcase(out.split("/", 1)[0], flt)):
        continue
    matched += 1
    theme, size = shot.get("theme", defaults.get("theme", "Dusk")), shot.get("size", defaults.get("size", "1200x1000"))
    raw, data = os.path.join(work, f"{i}.png"), os.path.join(work, f"data{i}")
    cmd = [os.environ["BIN"], "--data-dir", data, "--size", size, "--theme", theme, *shot.get("args", [])]
    c = shot.get("crop")
    if isinstance(c, str):
        cmd += ["--shot-rect", c]
    cmd += ["--screenshot", raw]
    r = subprocess.run(cmd, cwd=wiki, env=env, capture_output=True, text=True, timeout=120)
    sys.stderr.write(r.stderr)
    if r.returncode or not os.path.exists(raw):
        print(f"{out}: the app failed ({r.returncode})", file=sys.stderr)
        failed += 1
        continue
    if isinstance(c, str):
        m = re.search(r"^shot-rect (\d+) (\d+) (\d+) (\d+)$", r.stdout, re.M)
        if not m:
            print(f"{out}: no window named {c!r} in the shot", file=sys.stderr)
            failed += 1
            continue
        c = [int(v) for v in m.groups()]
        if inset := shot.get("inset"):  # a part of the window: offsets from its top left, 0 = to its edge
            x, y, w, h = inset
            c = [c[0] + x, c[1] + y, w or c[2] - x, h or c[3] - y]
    dst = os.path.join(wiki, "images", out)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if c:
        crop(raw, dst, c)
    else:
        shutil.copyfile(raw, dst)
    subprocess.run([os.environ["OPT"], dst], check=True)
if not matched:
    print(f"no shot's out or page is {flt!r}", file=sys.stderr)
sys.exit(1 if failed or not matched else 0)
PY
