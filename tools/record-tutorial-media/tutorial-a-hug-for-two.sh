#!/usr/bin/env bash
# docs/wiki/images/tutorial-a-hug-for-two/: hug.gif (the finished hug, rocking) and slide.gif (the start: Lead's hands
# stay put as Partner's back bows under them). Recorded headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SIZE=1600x900 TL_Y=799
source "$REPO/tools/record-readme-media/lib.sh"
source "$REPO/tools/record-tutorial-media/tutorial-helpers.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
DIR=${GIF_DIR:-$REPO/docs/wiki/images/tutorial-a-hug-for-two}
EX=$REPO/docs/wiki/examples

# hug.gif: both actors in profile, a little round to the front, on a plain backdrop; half the loop (Partner leans one
# way and bows), 2 s.
start_app "$WORK/data-hug" "$EX/hug-finished.vat" --frame 24 --view right --select mChest --focus --distance 2.0 \
    --tool select --backdrop
clean_view
xdotool keydown alt; drag 750 300 720 300 10 0.02 1; xdotool keyup alt
park; sleep 0.6
rec_start hug 600 55 300 360
sleep 0.3
play; sleep 2.6; play
rec_stop
gif hug "$DIR/hug.gif" 12 0.4 2.0
app_stop

# slide.gif: the start, from behind Partner: as its back bows, Lead's hands stay put and sink into it.
start_app "$WORK/data-slide" "$EX/hug-start.vat" --frame 24 --view front --select mChest --focus --distance 1.4 \
    --tool select --backdrop
clean_view
park; sleep 0.6
rec_start slide 580 90 340 250
sleep 0.3
play; sleep 2.6; play
rec_stop
COLORS=32 gif slide "$DIR/slide.gif" 12 0.4 2.0
app_stop

# head-turn.gif: step 3, Lead's head at frame 18 seen from its left side, the Rotate tool's blue ring dragged: it lights
# yellow under the pointer and the face turns towards the camera, about 30 degrees.
start_app "$WORK/data-head" "$EX/hug-start.vat" --frame 18 --view left --select mHead --tool rotate --backdrop
move_to 950 350; sleep 0.3; key f; sleep 1                                          # Frame Selected: the head
xdotool mousemove 950 350 keydown alt sleep 0.2 mousedown 1 sleep 0.1 mousemove 950 340 sleep 0.05 \
    mousemove 950 325 sleep 0.05 mousemove 950 310 sleep 0.05 mousemove 950 300 sleep 0.1 mouseup 1 keyup alt
move_to 750 295; xdotool click 4; sleep 0.2; xdotool click 4                        # a little closer
park; sleep 0.8
rec_start head-turn 612 175 290 250
sleep 0.5
move_to 700 266; sleep 0.8                                                          # the ring lights up
drag 700 266 752 263 35 0.03; park; sleep 1.3                                       # about 30 degrees
rec_stop
COLORS=48 gif head-turn "$DIR/head-turn.gif" 15
