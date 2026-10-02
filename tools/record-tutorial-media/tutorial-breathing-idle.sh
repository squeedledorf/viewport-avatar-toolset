#!/usr/bin/env bash
# GIF of docs/wiki/tutorial-breathing-idle.md.
#   breathing.gif  the finished idle from the side, close on the chest and head: one 4-second breath, looping
source "$(dirname "$0")/common.sh"
PAGE=tutorial-breathing-idle

app_start "$WORK/data-breath" "$WIKI/examples/tutorial-breathing-idle.vat" --view right --tool select --select mChest
move_to 600 234; key f; sleep 0.3                    # centre on the chest
xdotool click --repeat 5 --delay 80 4; sleep 0.3     # zoom in
key Escape
click 174 12; click 238 356; sleep 0.2                # View > Show Body Bones: off
click 174 12; click 230 552; sleep 0.2                # View > Centre of Mass: off
park; sleep 0.5
[ -n "${SHOT:-}" ] && import -window root "$WORK/breathing-debug.png"
rec_start breathing 480 100 260 324
sleep 0.2
key Home space; sleep 4.4; key space
rec_stop
gif breathing $PAGE breathing 12 260 0.25 4.0
app_stop; sleep 1

#   chest-drag.gif  frame 48, from the side: a short drag on the chest's green ring lifts the ribs into the green target
#                   ghost (step 5), in the Second Life controls with the pointer shown
drag_setup
app_start "$WORK/data-chest" --pose body-stand --target "$WIKI/examples/tutorial-breathing-idle.vat" --tool rotate \
    --view right
move_to 800 300; key Escape; sleep 0.3
click 1164 176; xdotool click 1; sleep 0.3; key ctrl+a; xdotool type 120; key Return; sleep 0.3  # Last frame 120
click 516 670; sleep 0.3                                             # frame 48 on the ruler
click 172 36; sleep 0.4; click 112 169; sleep 0.3                    # Picker > Chest
move_to 800 300; key 3; sleep 0.6                                    # from the side (Esc above reset the camera)
move_to 600 160; xdotool click --repeat 3 --delay 120 4; sleep 0.6; park; sleep 0.3
rec_window chest-drag
sleep 0.6
slow_drag 686 110 686 103 12
move_to 860 330; sleep 1.4
rec_stop
gif_status chest-drag $PAGE chest-drag 12 420 23 480 330 480
