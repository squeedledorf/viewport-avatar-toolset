#!/usr/bin/env bash
# docs/wiki/images/tutorial-dynamics-keys-and-export/: baked-key-drag.gif (a key of the tail tip's baked Rotate Y curve
# dragged up in the Graph), wag-ring.gif (the Rotate tool's blue ring, on Gimbal axes, swinging the base of the tail at
# frame 0) and stretch.gif (the dope sheet's Summary keys box-selected and scaled from 30 to 40 frames). Recorded
# headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SIZE=1600x900 TL_Y=799
source "$REPO/tools/record-readme-media/lib.sh"
source "$REPO/tools/record-tutorial-media/tutorial-helpers.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
DIR=${GIF_DIR:-$REPO/docs/wiki/images/tutorial-dynamics-keys-and-export}
EX=$REPO/docs/wiki/examples

# rec_with_row <name> <x> <y> <w> <h> <row-x> <row-y> <row-w>: one recorder for the region and a 26 px Properties row
# stacked under it, so the value and the motion stay in step.
rec_with_row() {
    REC_FILE=$WORK/$1.mkv
    ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -framerate 30 -video_size ${4}x${5} -i :$DISP+$2,$3 \
        -f x11grab -draw_mouse 0 -framerate 30 -video_size ${8}x26 -i :$DISP+$6,$7 \
        -filter_complex "[1]pad=$4:26:(ow-iw)/2:0:color=0x1f2126[r];[0][r]vstack=shortest=1" \
        -c:v libx264rgb -preset ultrafast -qp 0 "$REC_FILE" &
    REC_PID=$!
    sleep 0.5
}

# baked-key-drag.gif: the tail tip selected, Rotate Y shown alone, its key at frame 8 dragged up.
start_app "$WORK/data-keydrag" "$EX/dynamics-walk-tail.vat" --select mTail6
qclick 46 647 800 895; sleep 0.6                                     # the Rotate Y channel alone
park; sleep 0.5
rec_start baked-key-drag 400 598 620 144
sleep 0.6
qclick 610 669 800 895; sleep 0.5
DRAG_PRE=0.05 drag 610 669 610 630 30 0.03; park; sleep 1.4
rec_stop
FPS=12 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/baked-key-drag.mkv" "$DIR/baked-key-drag.gif"
app_stop

# wag-ring.gif: the unbaked walk, mTail1 from behind and a little above, Gimbal axes; the blue ring dragged out and
# eased back to a small swing. The Rotation boxes of Properties sit under the view.
start_app "$WORK/data-wag" "$EX/dynamics-walk-start.vat" --select-group Tail --select mTail1 --view back --focus \
    --distance 2.6 --tool rotate
move_to 750 300; key o; key o                                         # Local -> World -> Gimbal
xdotool keydown alt; drag 750 300 690 340 15 0.02; xdotool keyup alt  # look from behind and above
park; sleep 0.8
rec_with_row wag-ring 560 150 360 270 1220 105 360
sleep 0.6
drag 690 308 735 312 30 0.03; sleep 0.2
DRAG_PRE=0.05 drag 735 312 712 311 15 0.03; park; sleep 1.4
rec_stop
FPS=12 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/wag-ring.mkv" "$DIR/wag-ring.gif"
app_stop

# stretch.gif: nothing selected, the dope sheet on All animated bones, zoomed out; a box over the Summary row, then its
# right-hand handle dragged from 30 to 40.
start_app "$WORK/data-stretch" "$EX/dynamics-walk-wag.vat" --window dope-sheet
qclick 133 580 800 895; sleep 0.4; qclick 74 630 800 895; sleep 0.4  # All animated bones
move_to 900 700; for _ in 1 2 3 4 5 6; do xdotool click 5; sleep 0.2; done  # zoom out: 0 to 40 in view
park; sleep 0.6
rec_start stretch 640 598 670 144
sleep 0.6
DRAG_PRE=0.05 drag 640 625 1160 640 25 0.02; park; sleep 0.8
DRAG_PRE=0.3 drag 1133 690 1290 690 30 0.03; park; sleep 1.4
rec_stop
FPS=12 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/stretch.mkv" "$DIR/stretch.gif"
