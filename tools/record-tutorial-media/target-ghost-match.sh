#!/usr/bin/env bash
# docs/wiki/images/target-ghost/match.gif: the head turned with the Rotate tool's Z ring onto the green target ghost,
# past it and back, while the status bar's distance goes from 30 degrees to green. Recorded headless
# (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start

EX=$REPO/docs/wiki/examples
app_start "$WORK/data-target-ghost" "$EX/posing-head-turn.vat" --target "$EX/target-head-turn.vat" --frame 0 --select mHead \
    --tool rotate
move_to 600 250; key f; sleep 0.8                                            # Frame Selected: the head in the middle
move_to 598 225; for _ in 1 2 3 4 5 6; do xdotool click 4; sleep 0.2; done  # closer
park; sleep 0.8

rec_start target-ghost-match 100 45 640 675  # the view down to the status bar's chip and distance
sleep 0.8
drag 545 262 620 262 45 0.03; park; sleep 1.0  # along the Z ring: past the ghost, 17 degrees away
drag 600 262 572 262 30 0.03; park; sleep 1.6  # back onto it: the distance turns green
rec_stop
"$REPO/tools/optimize-wiki-gif.sh" "$WORK/target-ghost-match.mkv" "${GIF_OUT:-$REPO/docs/wiki/images/target-ghost/match.gif}"
