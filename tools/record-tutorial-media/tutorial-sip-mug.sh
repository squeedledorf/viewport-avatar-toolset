#!/usr/bin/env bash
# Viewport Avatar Toolset - the "Sip from a mug" tutorial, driven step by step (docs/wiki/tutorial-sip-mug.md).
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#
# Follows the page's steps in the app from a new project and records its GIFs:
#   sip-pinky-out.gif  the right pinky dragged out in the Hands window (step 3)
#   sip-drink.gif      the drink: the head tips back and the bound hand goes with it (step 6)
#   sip-result.gif     the finished sip played, close on the upper body (What you will make)
# Stills of each step go to $WORK/sip-*.png for checking against the page.
set -u
OUT=${OUT:-$(cd "$(dirname "$0")/../.." && pwd)/docs/wiki/images/tutorial-sip-mug}
source "$(dirname "$0")/lib.sh"
mkdir -p "$OUT"
trap cleanup EXIT
xvfb_start  # its own display, DISP or the first free one

frame() { field 281 619 "$1"; }                                  # the timeline's frame box
rot() { field 1090 "${4:-118}" "$1"; field 1157 "${4:-118}" "$2"; field 1225 "${4:-118}" "$3"; }  # Rotation X Y Z
bone() { pick "m$1" "$2"; }  # bone <name without m> <row>: rows from 187, 21 px apart

if [ -z "${ONLY_GIFS:-}" ]; then
app_start "$WORK/data-sip"

# Step 1: Relaxed Stand, 72 frames, priority 4, short eases.
click 103 35; filter "Relaxed Stand"; scroll 15; click 90 398; sleep 0.4
field 1157 176 72; field 1157 255 4; field 1157 284 0.3; field 1157 313 0.3
snap sip-1-stand
# Step 2: the Mug, at its grip in the right hand.
click 103 35; filter Mug; scroll 30; xdotool mousemove 60 125 click --repeat 2 --delay 60 1; sleep 0.5
snap sip-2-mug
# Step 3: the grip on the right hand, then the pinky out with the Hands window.
filter Grip; scroll 15; snap sip-3-grip-list
xdotool mousemove 90 398 keydown shift click 1 keyup shift; sleep 0.4
move_to 600 250; key h; sleep 0.5
snap sip-3-hands
park; sleep 0.3; DRAG_PRE=0.3 drag 934 328 962 296 20 0.04; sleep 0.3
snap sip-3-pinky
move_to 600 250; key ctrl+z; sleep 0.3  # the optional pinky undone, as the example leaves it
snap sip-3-undone
key h; sleep 0.3
# Step 4: the hold pose at frame 0.
bone ShoulderRight 376; rot 72 0 20
bone ElbowRight 397; rot 0 0 100
bone WristRight 418; move_to 600 200; key s  # at rest already: S keys it
bone Head 376; move_to 600 200; key s
snap sip-4-hold
# Step 5: anticipation at frame 8, the lips at frame 20.
frame 8
bone ShoulderRight 376; rot 74 0 18
bone ElbowRight 397; rot 0 0 92
bone Head 376; rot 0 4 0
frame 20
bone ShoulderRight 376; rot -13 -48 63
bone ElbowRight 397; rot 0 -46 112
bone WristRight 418; rot -6 -25 -35
bone Head 376; rot 0 0 0
snap sip-5-lips
# Step 6: bind the hand to the head at frame 20, tip the head back and forward, release at 48.
bone Head 376
click 34 35; filter WristRight; scroll 15; xdotool mousemove 170 407 keydown shift click 1 keyup shift; sleep 0.2
tools 223
snap sip-6-bound
bone Head 376
frame 34; rot 0 -14 0
frame 40; move_to 600 200; key s  # the head holds -14 already: S keys the hold
frame 46; rot 0 0 0
frame 48
bone WristRight 418; tools 244
snap sip-6-released
# Step 7: down past the hold at 60, settle at 68.
frame 60
bone ShoulderRight 376; rot 72 0 20
bone ElbowRight 397; rot 0 0 92
bone WristRight 418; rot 0 0 0
frame 68
bone ShoulderRight 376; move_to 600 200; key s  # at 72, 0, 20 already
bone ElbowRight 397; rot 0 0 100
snap sip-7-settle
verify "$WORK/data-sip" sip-mug.vat
app_stop
fi

# The GIFs, each from the example it shows, framed as the page describes.
# sip-pinky-out.gif: the grip's pinky dragged out in the Hands window, beside a close view of the hand.
app_start "$WORK/data-sip-pinky" examples/sip-start.vat --select mWristRight --tool select
move_to 600 235; key f; sleep 0.6; xdotool click --repeat 12 4; key h; sleep 0.4
xdotool keydown alt; DRAG_PRE=0.05 drag 600 235 470 120 6 0.05 2; xdotool keyup alt  # pan: the hand up and left, clear of the Hands window
park; sleep 0.8
rec_start sip-pinky 0 0 1280 720
sleep 0.6
DRAG_PRE=0.3 drag 934 328 962 296 24 0.04; park; sleep 1.2
rec_stop
# The hand twice its size beside the Hands window's Right half, so the finger's move reads.
ffmpeg -loglevel error -y -i "$WORK/sip-pinky.mkv" -filter_complex \
    "[0]crop=160:120:450:80,scale=320:240:flags=lanczos[a];[0]crop=180:140:790:262,pad=180:240:0:50:color=0x1c1d22[b];[a][b]hstack" \
    -c:v libx264rgb -preset ultrafast -qp 0 "$WORK/sip-pinky-crop.mkv"
COLORS=64 _small_gif sip-pinky "$OUT/sip-pinky-out.gif" 0

# sip-drink.gif: frames 20 to 48 scrubbed from the right, the head tipping back with the mug riding along.
app_stop
app_start "$WORK/data-sip-drink" examples/sip-mug.vat --select mHead --tool select
move_to 600 235; key 3; sleep 0.3; key f; sleep 0.6; xdotool click --repeat 10 4; select_none
field 281 619 20; park; sleep 0.6
rec_start sip-drink 0 0 1280 720
sleep 0.5
DRAG_PRE=0.3 drag 363 685 841 685 60 0.04; park; sleep 1
rec_stop
gif sip-drink "$OUT/sip-drink.gif" 450 90 320 280

# sip-result.gif: the whole sip played, close on the upper body from the front, turned a little to the mug's side.
app_stop
app_start "$WORK/data-sip-result" examples/sip-mug.vat --select mNeck --tool select
move_to 600 235; key 1; sleep 0.3; key f; sleep 0.6; xdotool click --repeat 6 4
xdotool keydown alt; DRAG_PRE=0.05 drag 600 100 520 100 10 0.03 1; xdotool keyup alt  # towards the right hand
select_none; menu 219 181  # Light > Plain Backdrop
key Home; park; sleep 0.6
rec_start sip-result 0 0 1280 720
sleep 0.4
key space; sleep 3.2; key space
rec_stop
gif sip-result "$OUT/sip-result.gif" 400 120 400 304
