#!/usr/bin/env bash
# Viewport Avatar Toolset - the "A sit pose for furniture" tutorial, driven step by step (docs/wiki/tutorial-sit-pose.md).
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#
# Follows the page's steps in the app from a new project and records its GIFs:
#   sit-hips-to-seat.gif  the hips rise onto the seat while the held feet stay on the floor (step 5)
#   sit-lean-back.gif     the back leans into the chair while the bound hands stay on the thighs (step 7)
#   sit-result.gif        the finished pose on a turntable (What you will make)
# Stills of each step go to $WORK/sit-*.png for checking against the page.
set -u
OUT=${OUT:-$(cd "$(dirname "$0")/../.." && pwd)/docs/wiki/images/tutorial-sit-pose}
source "$(dirname "$0")/lib.sh"
mkdir -p "$OUT"
trap cleanup EXIT
xvfb_start  # its own display, DISP or the first free one

app_start "$WORK/data-sit"

# Step 1: the chair.
click 103 35; filter Chair; scroll 30; scroll 2 4; xdotool mousemove 60 145 click --repeat 2 --delay 60 1; sleep 0.5
snap sit-1-chair
# Step 2: the Sitting starter pose, seen from the right.
click 880 330 3; sleep 0.3; move_to 932 397; sleep 0.3; move_to 870 397; move_to 810 397; sleep 0.4; click 909 502; sleep 0.4
move_to 800 100; key 3; sleep 0.4
snap sit-2-sitting
# Step 3: Animation Check, fix the feet (Drop the Hips by 49.0 cm).
tools 615; sleep 0.4
snap sit-3-check
click 478 472; sleep 0.4; click 854 184; sleep 0.3
# Step 4: hold both ankles in the world.
click 34 35
filter AnkleLeft; click 104 271; tools 202
filter AnkleRight; click 104 271; tools 202
snap sit-4-held
# Step 5: the hips up onto the seat, the Select tool so no gizmo hides the legs.
filter ""; click 62 229; move_to 800 100; key q; park; sleep 0.3
rec_start sit-hips 0 0 1280 720
sleep 0.6
xdotool mousemove 1197 147 mousedown 1
for i in $(seq 1 63); do xdotool mousemove $((1197 + i)) 147 sleep 0.035; done
xdotool mouseup 1; sleep 0.8
rec_stop
field 1225 147 -0.43
snap sit-5-seat
# Step 6: bind each wrist to its thigh.
filter HipLeft; click 78 229; filter WristLeft; scroll 15; xdotool mousemove 170 407 keydown shift click 1 keyup shift; tools 223
filter HipRight; click 78 229; filter WristRight; scroll 15; xdotool mousemove 170 407 keydown shift click 1 keyup shift; tools 223
snap sit-6-bound
# Step 7: lean back, then level the head.
filter Torso; click 92 271; park; sleep 0.3
rec_start sit-lean 0 0 1280 720
sleep 0.6
xdotool mousemove 1170 118 mousedown 1
for i in $(seq 1 48); do xdotool mousemove $((1170 - i)) 118 sleep 0.04; done
xdotool mouseup 1; sleep 0.8
rec_stop
field 1157 118 -12
filter Head; click 141 376; field 1157 118 12
# Step 8: loop and priority 4, then the Check's fix for the eases.
click 1070 326; sleep 0.3
field 1157 413 4
tools 615; sleep 0.4
snap sit-8-check
click 478 298; sleep 0.3
snap sit-8-fixed
click 854 184; sleep 0.3
# Step 9: the export's name and heights.
move_to 700 100; key ctrl+e; sleep 0.5
click 676 99; xdotool type Sit; key Return; click 524 273; sleep 0.4
xdotool mousemove 700 500 click --repeat 12 5; sleep 0.4
snap sit-9-export
verify "$WORK/data-sit" sit-chair.vat
click 849 38; sleep 0.3

COLORS=48 gif_row sit-hips "$OUT/sit-hips-to-seat.gif" 480 130 320 280 147
COLORS=48 gif_row sit-lean "$OUT/sit-lean-back.gif" 480 130 320 280 118

# The result: the finished example filmed by the app's own listing media (File > Export Listing Media..., turntable
# on), slowed to 12 fps so the turn takes 2.5 s.
COLORS=32 LISTING_W=288 listing sit-chair.vat sit-result "$OUT/sit-result.gif" --select mPelvis --focus --distance 2.4
