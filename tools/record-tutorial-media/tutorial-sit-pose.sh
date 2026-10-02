#!/usr/bin/env bash
# Viewport Avatar Toolset - GIFs of the "A sit pose for furniture" tutorial (docs/wiki/tutorial-sit-pose.md).
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#
# Steps 1 and 2 come from sit-start.vat (the chair) and --sit (Sit on This); the rest follows the page in the Second
# Life controls a new user starts with, the pointer shown, and records:
#   bind-hand.gif      from her left side: the left hand's right-click menu, Bind mWristLeft · Left Hand to..., a click
#                      on the left thigh, and the status bar naming both (step 4)
#   sit-lean-back.gif  from the side: the torso's green ring dragged back until the shoulders lie in the green target
#                      ghost, against the chair's back rest, the bound hands staying on the thighs (step 5)
#   sit-result.gif     the finished pose on a turntable (What you will make)
# Stills go to $WORK/sit-*.png for checking against the page.
source "$(dirname "$0")/common.sh"
PAGE=tutorial-sit-pose
want() { [ -z "${ONLY:-}" ] || [[ " $ONLY " == *" $1 "* ]]; }

drag_setup
snap() { ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -video_size 1280x720 -i ":$DISP" -frames:v 1 "$WORK/$1.png"; }

if want bind-hand; then
    app_start "$WORK/data-bind" "$WIKI/examples/sit-start.vat" --sit --target "$WIKI/examples/sit-chair.vat" --view left
    move_to 600 300; xdotool click --repeat 2 --delay 120 4; sleep 0.6; park; sleep 0.3   # from her left side, closer
    rec_window bind-hand
    sleep 0.6
    move_to 500 300; sleep 0.8; xdotool click 3; sleep 0.9                       # right-click the left hand (a finger)
    move_to 600 367; sleep 0.7; xdotool click 1; sleep 0.8                       # Bind mWristLeft · Left Hand to...
    move_to 570 336; sleep 1.0; xdotool click 1; sleep 0.5                       # the left thigh, clear of the hands
    move_to 860 380; sleep 1.6
    rec_stop
    snap sit-4-bound
    app_stop; sleep 1
    gif_status bind-hand $PAGE bind-hand 12 400 23 560 400 560 0.8 8
fi

if want lean-back; then
    app_start "$WORK/data-lean" "$WIKI/examples/sit-start.vat" --sit --target "$WIKI/examples/sit-chair.vat" --view left
    # The left hand bound as in step 4, then the torso from the Picker, framed; still from her left side.
    move_to 600 300; xdotool click --repeat 2 --delay 120 4; sleep 0.6
    move_to 500 300; sleep 0.4; xdotool click 3; sleep 0.6; click 600 367; sleep 0.5; click 570 336; sleep 0.5
    click 172 36; sleep 0.4; click 112 187; sleep 0.4                            # Picker > Torso
    move_to 800 150; key e; key f; sleep 0.8; park; sleep 0.3
    rec_window sit-lean
    sleep 0.6
    slow_drag 508 236 511 217 20                                                # the green ring, in front of the belly, up
    move_to 860 380; sleep 1.4
    rec_stop
    snap sit-5-leaned
    app_stop; sleep 1
    gif_status sit-lean $PAGE sit-lean-back 12 400 23 460 400 460
fi

# The result: the finished example filmed by the app's own listing media (File > Export Listing Media..., turntable on).
if want result; then
    rm -rf "$WORK/sit-turn" && mkdir -p "$WORK/sit-turn"
    (vats_env "$VATS" --data-dir "$WORK/data-sit-turn" "$WIKI/examples/sit-chair.vat" --select mPelvis --focus \
        --distance 2.4 --listing "$WORK/sit-turn/turn.png" --screenshot "$WORK/sit-turn/last.png" >"$WORK/app.log" 2>&1)
    sleep 1
    ffmpeg -loglevel error -y -framerate 12 -pattern_type glob -i "$WORK/sit-turn/turn_*.png" \
        -filter_complex "color=c=0x2b2d33:s=512x512:r=12[bg];[bg][0:v]overlay=shortest=1,crop=400:440:56:40" \
        -c:v libx264rgb -preset ultrafast -qp 0 "$WORK/sit-result.mkv"
    gif sit-result $PAGE sit-result 12 288
fi
