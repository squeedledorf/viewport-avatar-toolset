#!/usr/bin/env bash
# docs/wiki/images/posing/rotate-drag.gif: the head turned with the Rotate tool's Z ring, which keys the frame.
# Recorded headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start

app_start "$WORK/data-posing-rotate" "$REPO/docs/wiki/examples/posing-head-turn.vat" --frame 12 --select mHead --tool rotate
move_to 600 250; key f; sleep 0.8                                    # Frame Selected: the head in the middle
move_to 598 235; for _ in 1 2 3; do xdotool click 4; sleep 0.2; done  # a little closer
park; sleep 0.8

rec_start posing-rotate-drag 420 125 360 290
sleep 0.6
drag 545 258 652 256 45 0.03; sleep 0.9                              # along the Z ring: the head turns and keys
drag 590 260 548 258 30 0.03; park; sleep 1.2                             # and part of the way back
rec_stop
"$REPO/tools/optimize-wiki-gif.sh" "$WORK/posing-rotate-drag.mkv" "${GIF_OUT:-$REPO/docs/wiki/images/posing/rotate-drag.gif}"
