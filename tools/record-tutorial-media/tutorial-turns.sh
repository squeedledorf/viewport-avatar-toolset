#!/usr/bin/env bash
# The Turns for an AO tutorial's GIF (docs/wiki/tutorial-turns.md), recorded headless with the README media's
# helpers (tools/record-readme-media/lib.sh); see README.md here.
#   tools/record-tutorial-media/tutorial-turns.sh
# turns.gif: turn_left playing, from the front and a little above: the steps, and the head, chest and hips turned.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
DEST=${GIF_OUT:-$REPO/docs/wiki/images/tutorial-turns}
mkdir -p "$DEST"

app_start "$WORK/data-turns" "$REPO/docs/wiki/examples/turn-left.vat" --tool select --frame 0 --backdrop
qclick 174 12 640 712; sleep 0.4; qclick 229 552 640 712; sleep 0.3   # View > Centre of Mass off
qclick 174 12 640 712; sleep 0.4; qclick 238 356 640 712; sleep 0.3   # View > Show Body Bones off
qclick 174 12 640 712; sleep 0.4; qclick 238 377 640 712; sleep 0.3   # View > Show Hand Bones off
xdotool mousemove 600 300 keydown alt sleep 0.1 mousedown 1 sleep 0.1 mousemove 590 315 sleep 0.1 \
    mousemove 580 330 sleep 0.2 mouseup 1 keyup alt                    # orbit: the front right, from above
move_to 600 250; xdotool click 4; sleep 0.3
park; sleep 0.5
play; sleep 0.6
rec_start turns 470 50 260 370
sleep 1.9
rec_stop
COLORS=40 to_gif turns "$WORK/turns-40.gif" 12 260 0 1.6  # two loops of 24 frames; few colours: a body on a plain ground
FPS=12 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/turns-40.gif" "$DEST/turns.gif" 260
ls -l "$DEST"
