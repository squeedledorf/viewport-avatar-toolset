#!/usr/bin/env bash
# The pistol hold tutorial's GIF (docs/wiki/tutorial-pistol-hold.md): over-walk.gif, the hold over a walk as Second
# Life combines them per bone (examples/pistol-over-walk.vat), from the front right, on the treadmill at SL's walking
# speed so the ground runs back under the feet: the walk reads as forward. Four strides.
# tutorial-rifle-hold.sh runs this with PAGE and EXAMPLE set for the rifle.
#   VATS=build/app/vats WORK=~/Projects/.vats-media/tutorial tools/record-tutorial-media/tutorial-pistol-hold.sh
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
PAGE=${PAGE:-tutorial-pistol-hold} EXAMPLE=${EXAMPLE:-pistol-over-walk}
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start

app_start "$WORK/data-$EXAMPLE" "$REPO/docs/wiki/examples/$EXAMPLE.vat" --tool select --view front --window treadmill
xdotool keydown alt; drag 600 250 500 250 12 0.02 1; xdotool keyup alt  # orbit to the front right
move_to 600 230; xdotool click 4; sleep 0.3  # a little closer
qclick 174 12 640 712; sleep 0.4; qclick 240 356 640 712; sleep 0.3  # View > Show Body Bones off
qclick 174 12 640 712; sleep 0.4; qclick 229 552 640 712; sleep 0.3  # View > Centre of Mass off
park; sleep 0.5

rec_start "$EXAMPLE" ${REC:-420 45 360 380}  # REC: the region, wider for the rifle
sleep 0.3
play; sleep 2.3; play                                                # 30 frames, twice
sleep 0.3
rec_stop
FPS=12 SS=0.3 DUR=2.1 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/$EXAMPLE.mkv" "$REPO/docs/wiki/images/$PAGE/over-walk.gif" 300
