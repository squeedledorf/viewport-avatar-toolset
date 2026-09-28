# Viewport Avatar Toolset - shared setup for recording the tutorials' GIFs. Source this file.
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#
# Each tutorial-*.sh script here records the GIFs of one tutorial page (docs/wiki/tutorial-<name>.md) into
# docs/wiki/images/<page>/, with the helpers of tools/record-readme-media/lib.sh: a private Xvfb display, a throwaway
# data folder, xdotool driving the app and ffmpeg capturing it. The recorded window is 1280 x 720 in the README
# layout: Viewport 226,23 to 974,424; Graph 0,426 (curves from x 205); Timeline 0,574 (ruler 22 to 1258 at y 647).
#
# Environment (all optional): VATS (default build/app/vats), WORK (scratch, default $TMPDIR/vats-tutorial-media),
# DISP (X display).
TUT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$TUT/../.." && pwd)
WIKI=$ROOT/docs/wiki
VATS=${VATS:-$ROOT/build/app/vats}
WORK=${WORK:-${TMPDIR:-/tmp}/vats-tutorial-media}
source "$ROOT/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
for _ in $(seq 50); do xdotool getdisplaygeometry >/dev/null 2>&1 && break; sleep 0.1; done  # the server answers

# gif <recording> <page> <name> <fps> <width> [trim-start] [duration]: the recording as
# docs/wiki/images/<page>/<name>.gif through tools/optimize-wiki-gif.sh, which keeps it within 450 KB and 640 px.
gif() {
    FPS=$4 SS=${6:-0} DUR=${7:-} "$ROOT/tools/optimize-wiki-gif.sh" "$WORK/$1.mkv" "$WIKI/images/$2/$3.gif" "$5"
}
