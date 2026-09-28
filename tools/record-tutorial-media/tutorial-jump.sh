#!/usr/bin/env bash
# The Standing jump tutorial's GIFs (docs/wiki/tutorial-jump.md), recorded headless with the README media's helpers
# (tools/record-readme-media/lib.sh); see README.md here.
#   tools/record-tutorial-media/tutorial-jump.sh [jump] [arc]   (default: both)
# jump.gif: the finished jump from the side. arc.gif: the key poses' floaty jump (jump-poses.vat), then the same after
# Tools > Jump Arc... from 14 to 26 (jump-arc.vat), side by side in time.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
EX=$REPO/docs/wiki/examples
DEST=${GIF_OUT:-$REPO/docs/wiki/images/tutorial-jump}
mkdir -p "$DEST"

# View > Centre of Mass, Show Body Bones and Show Hand Bones off; the viewport taller; the body panned down a
# little, so the jump has room above the head.
clean_view() {
    qclick 174 12 640 712; sleep 0.4; qclick 229 552 640 712; sleep 0.3
    qclick 174 12 640 712; sleep 0.4; qclick 238 356 640 712; sleep 0.3
    qclick 174 12 640 712; sleep 0.4; qclick 238 377 640 712; sleep 0.3
    drag 640 427 640 560 12 0.03; sleep 0.3
    xdotool mousemove 600 300 keydown alt sleep 0.1 mousedown 2 sleep 0.1 mousemove 600 285 sleep 0.1 \
        mousemove 600 270 sleep 0.2 mouseup 2 keyup alt
    sleep 0.3
}
# once <name> <project>: one play of a 48-frame jump from the side, into $WORK/<name>.mkv.
once() {
    app_start "$WORK/data-$1" "$EX/$2" --view right --tool select --frame 0 --backdrop
    clean_view
    park; sleep 0.5
    rec_start "$1" 440 44 320 490
    sleep 0.5
    first_frame; play; sleep 2.6
    rec_stop
    app_stop
}
# body_gif <in.mkv> <name> <start> <seconds> <width>: few colours for a body on a plain ground, then the optimiser.
body_gif() {
    COLORS=40 to_gif "$1" "$WORK/$2-40.gif" 15 "$5" "$3" "$4"
    "$REPO/tools/optimize-wiki-gif.sh" "$WORK/$2-40.gif" "$DEST/$2.gif" "$5"
}

rec_jump() {
    once jump jump-finished.vat
    body_gif jump jump 0.75 2.1 240  # the 48 frames once, and a moment standing
}

rec_arc() {
    once before jump-poses.vat
    once after jump-arc.vat
    # the crouch to the landing of each, one after the other
    ffmpeg -loglevel error -y -ss 1.05 -t 1.3 -i "$WORK/before.mkv" -ss 1.05 -t 1.3 -i "$WORK/after.mkv" \
        -filter_complex "[0:v][1:v]concat=n=2:v=1[v]" -map "[v]" -c:v libx264rgb -preset ultrafast -qp 0 \
        "$WORK/arc.mkv"
    body_gif arc arc 0 2.6 240
}

for name in ${@:-jump arc}; do "rec_$name"; done
ls -l "$DEST"
