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

# The beginner tutorials' drag GIFs: the app in the Second Life controls a new user starts with, the pointer drawn so
# the reader sees where to press and which way to drag. Record the whole window (rec_window) and cut it with
# gif_status.
drag_setup() { PRESET=secondlife POINTER=1; export PRESET POINTER; }
rec_window() { rec_start "$1" 0 0 1280 720; }

# gif_status <recording> <page> <name> <fps> <x> <y> <w> <h> [width] [trim-start] [duration]: a region of the
# whole-window recording, with the status bar's left end (the last command, the Target chip and its distance) as a
# strip under it, so the GIF shows the drag and what the status bar says about it.
gif_status() {
    local x=$5 y=$6 w=$7 h=$8
    ffmpeg -loglevel error -y -i "$WORK/$1.mkv" -filter_complex \
        "[0]crop=$w:$h:$x:$y[a];[0]crop=$w:22:0:698[b];[a][b]vstack" -c:v libx264rgb -preset ultrafast -qp 0 \
        "$WORK/$1-status.mkv"
    FPS=$4 SS=${10:-0} DUR=${11:-} "$ROOT/tools/optimize-wiki-gif.sh" "$WORK/$1-status.mkv" "$WIKI/images/$2/$3.gif" \
        "${9:-$w}"
}

# hide_bones: View > Bones > Show Body Bones and Show Hand Bones off, and View > Centre of Mass off, for a picture of
# the motion alone. Positions are the 1280 x 720 layout's menus.
hide_bones() {
    local row
    for row in 217 238; do
        click 174 12; sleep 0.3; move_to 203 217; sleep 0.2; move_to 230 217; sleep 0.4; click 480 "$row"; sleep 0.3
    done
    click 174 12; sleep 0.3; click 230 238; sleep 0.3
    park
}

# slow_drag <x1> <y1> <x2> <y2> [steps]: a drag a reader can follow: the pointer comes to the start, rests there so
# the ring under it lights up, then moves at an even, unhurried pace.
slow_drag() {
    move_to "$1" "$2"; sleep 0.7
    DRAG_PRE=0.3 drag "$1" "$2" "$3" "$4" "${5:-40}" 0.035
    sleep 0.3
}
