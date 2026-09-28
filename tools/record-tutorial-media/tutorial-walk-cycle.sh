#!/usr/bin/env bash
# Viewport Avatar Toolset - the "A walk cycle for your AO" tutorial, driven step by step
# (docs/wiki/tutorial-walk-cycle.md).
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#
# Follows the page's steps in the app from a new project, then records its GIFs:
#   walk-result.gif  the finished walk on the treadmill, three-quarter view (What you will make)
# Stills of each step go to $WORK/walk-*.png for checking against the page. ONLY_GIFS=1 skips the steps.
set -u
OUT=${OUT:-$(cd "$(dirname "$0")/../.." && pwd)/docs/wiki/images/tutorial-walk-cycle}
source "$(dirname "$0")/lib.sh"
mkdir -p "$OUT"
trap cleanup EXIT
xvfb_start

frame() { field 281 619 "$1"; }
# bone <name>: select it in the Bones tab by filtering; the row is its depth in the tree below World.
declare -A ROW=([mPelvis]=1 [mTorso]=4 [mHipLeft]=2 [mKneeLeft]=3 [mAnkleLeft]=4 [mHipRight]=2 [mKneeRight]=3
                [mAnkleRight]=4 [mShoulderLeft]=9 [mShoulderRight]=9 [mElbowLeft]=10 [mElbowRight]=10)
bone() { pick "$1" $((187 + 21 * ROW[$1])); }
rot() { bone "$1"; field 1090 118 "$2"; field 1157 118 "$3"; field 1225 118 "$4"; }       # rot <bone> x y z
off() { bone mPelvis; field 1090 147 "$1"; field 1157 147 "$2"; field 1225 147 "$3"; }     # the hips' Offset (m)

if [ -z "${ONLY_GIFS:-}" ]; then
app_start "$WORK/data-walk"
# Step 1: 16 frames, looping, eases 0.25 s.
field 1157 176 16; click 1070 226; sleep 0.3; field 1157 342 0.25; field 1157 371 0.25
snap walk-1-setup
# Step 2: contact at frame 0.
bone mPelvis; click 1043 143; sleep 0.3  # Animate Position
snap walk-2-animate-position
rot mPelvis 0 0 -4; off 0 0 -0.025
rot mTorso 0 0 8
rot mHipLeft 0 -18 0; rot mKneeLeft 0 4 0; rot mAnkleLeft 0 -5 0
rot mHipRight 0 11 0; rot mKneeRight 0 35 0; rot mAnkleRight 0 4 0
rot mShoulderLeft -75 20 0; rot mShoulderRight 75 -20 0
rot mElbowLeft 0 0 -10; rot mElbowRight 0 0 10
move_to 600 200; key 3; sleep 0.3
snap walk-2-contact
# Step 3: down, passing, up.
frame 2; off 0 0.012 -0.032
rot mHipLeft 0 -17 0; rot mKneeLeft 0 23 0; rot mAnkleLeft 0 -11 0
rot mHipRight 0 5 0; rot mKneeRight 0 48 0; rot mAnkleRight 0 35 0
frame 4; off 0 0.018 0.012; rot mPelvis 0 0 0; rot mTorso 0 0 0
rot mHipLeft 0 3 0; rot mKneeLeft 0 8 0; rot mAnkleLeft 0 -4 0
rot mHipRight 0 -14 0; rot mKneeRight 0 63 0; rot mAnkleRight 0 4 0
rot mShoulderLeft -75 0 0; rot mShoulderRight 75 0 0
frame 6; off 0 0.010 0.031
rot mHipLeft 0 9 0; rot mKneeLeft 0 17 0; rot mAnkleLeft 0 2 0
rot mHipRight 0 -22 0; rot mKneeRight 0 22 0; rot mAnkleRight 0 -8 0
snap walk-3-up
# Step 4: nothing selected, copy frames 0-7, paste mirrored at 8.
select_none
xdotool mousemove 22 690 keydown shift mousedown 1 sleep 0.2 mousemove 300 690 sleep 0.1 mousemove 561 690 sleep 0.2 mouseup 1 keyup shift  # frame 7
click 62 12; move_to 90 405; sleep 0.4; move_to 300 405; click 458 503; sleep 0.3
snap walk-4-copied
frame 8
click 62 12; move_to 90 405; sleep 0.4; move_to 300 405; click 486 566; sleep 0.3
snap walk-4-pasted
# Step 5: Make Loop Seamless.
click 320 12; move_to 360 307; sleep 0.4; move_to 560 307; move_to 600 330; click 676 357; sleep 0.3
snap walk-5-seamless
# The walk as followed so far must be the example's, key for key.
verify "$WORK/data-walk" walk-cycle.vat
# Step 6: the treadmill, then travel added and taken away.
click 174 12; move_to 212 678; sleep 0.4; move_to 390 678; click 480 390; sleep 0.3
click 174 12; move_to 212 678; sleep 0.4; move_to 390 678; sleep 0.5
snap walk-6-treadmill
click 900 150; sleep 0.3  # a click on the view closes the menu
click 320 12; move_to 360 307; sleep 0.5; move_to 560 307; sleep 0.3; move_to 600 400; sleep 0.3
xdotool mousemove 649 410 click --repeat 2 --delay 60 1; sleep 0.25; key ctrl+a; xdotool type 3.02; key Return
click 792 410; sleep 0.3; click 900 150; sleep 0.3
snap walk-6-travel
click 320 12; move_to 360 307; sleep 0.4; move_to 560 307; move_to 600 380; click 698 385; sleep 0.3
snap walk-6-in-place
app_stop
fi

# walk-result.gif: the finished walk on the treadmill, from the default three-quarter view.
sleep 2
app_start "$WORK/data-walk-result" examples/walk-cycle.vat
click 174 12; move_to 212 678; sleep 0.4; move_to 390 678; click 480 390; sleep 0.3  # View > Treadmill > Show Treadmill
park; sleep 0.5
snap walk-result-start
rec_start walk-result 0 0 1280 720
sleep 0.3
play; sleep 3.3; play
rec_stop
# Two cycles, 1.07 s: the GIF loops as the walk does. The treadmill scrolls with the playhead, so the seam holds.
COLORS=48 BAYER=5 SS=1 DUR=1.067 gif walk-result "$OUT/walk-result.gif" 450 60 300 350
