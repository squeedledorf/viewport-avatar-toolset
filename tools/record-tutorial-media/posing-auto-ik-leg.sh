#!/usr/bin/env bash
# docs/wiki/images/posing/auto-ik-leg.gif: the left ankle's dot dragged up and forward with Auto IK, from the side, onto
# the green target ghost of posing.md's worked example; the hip and knee follow. Recorded headless
# (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start

EX=$REPO/docs/wiki/examples
app_start "$WORK/data-auto-ik-leg" "$EX/posing-leg-lift.vat" --target "$EX/target-leg-lift.vat" --frame 12 \
    --select mKneeLeft --focus --tool select --view left --distance 2.6
park; sleep 0.8

rec_start posing-auto-ik-leg 440 45 320 290  # the legs, the ghost and the hover label
sleep 0.6
move_to 601 283; sleep 1.0                   # over the ankle: its dot and "Drag: Auto IK"
drag 601 283 561 223 45 0.03; sleep 1.4      # up and forward onto the ghost's ankle: the hip and knee follow
park; sleep 0.8
rec_stop
"$REPO/tools/optimize-wiki-gif.sh" "$WORK/posing-auto-ik-leg.mkv" "${GIF_OUT:-$REPO/docs/wiki/images/posing/auto-ik-leg.gif}"
