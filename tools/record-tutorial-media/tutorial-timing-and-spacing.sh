#!/usr/bin/env bash
# GIFs of docs/wiki/tutorial-timing-and-spacing.md.
#   nod.gif              the finished, eased nod, from the side
#   linear-vs-eased.gif  the linear nod (left) and the eased nod (right), played side by side
source "$(dirname "$0")/common.sh"
PAGE=tutorial-timing-and-spacing


# side <recording> <project>: the nod from the side, close on the head and shoulders, bones hidden, played twice.
side() {
    app_start "$WORK/data-$1" "$WIKI/examples/$2" --view right --tool select --select mHead
    move_to 600 234; key f; sleep 0.3                    # centre on the head (--distance acts only with --screenshot)
    xdotool click --repeat 9 --delay 80 4; sleep 0.3     # zoom in
    key Escape
    click 174 12; click 238 356; sleep 0.2                # View > Show Body Bones: off
    click 174 12; click 230 552; sleep 0.2                # View > Centre of Mass: off
    park; sleep 0.5
    [ -n "${SHOT:-}" ] && import -window root "$WORK/$1-debug.png"
    rec_start "$1" 485 130 250 250
    sleep 0.2
    key Home space; sleep 2.2; key space Home; sleep 0.4
    rec_stop
    app_stop
    sleep 1  # Xvfb resets once its last client has gone
}

side nod-eased tutorial-nod.vat
side nod-linear tutorial-nod-linear.vat
gif nod-eased $PAGE nod 15 250 0.2 2.2

# The two side by side, the same frames of each, named at the top.
label() { echo "drawtext=fontfile=$ROOT/app/assets/fonts/Inter-SemiBold.ttf:text=$1:fontsize=18:fontcolor=white@0.85:x=(w-tw)/2:y=10"; }
ffmpeg -loglevel error -y -i "$WORK/nod-linear.mkv" -i "$WORK/nod-eased.mkv" \
    -filter_complex "[0:v]scale=220:-1,$(label Linear)[a];[1:v]scale=220:-1,$(label Eased)[b];[a][b]hstack=inputs=2" \
    -c:v libx264rgb -preset ultrafast -qp 0 "$WORK/linear-vs-eased.mkv"
gif linear-vs-eased $PAGE linear-vs-eased 15 440 0.2 2.2

# The drags (step 4 and step 7), in the Second Life controls with the pointer shown.
drag_setup
#   nod-drag.gif     frame 8, from the side: the head's green ring dragged down into the green target ghost
app_start "$WORK/data-nod-drag" --pose body-stand --target "$WIKI/examples/tutorial-nod.vat" --frame 0 --select mHead \
    --tool rotate --view right
move_to 800 300; key s; sleep 0.3                                    # the key at frame 0
click 354 670; sleep 0.3                                             # frame 8 on the ruler
move_to 800 300; key f; sleep 0.6
move_to 600 215; xdotool click --repeat 4 --delay 120 4; sleep 0.6; park; sleep 0.3
rec_window nod-drag
sleep 0.6
slow_drag 690 215 684 250 30   # on the green ring (inside the pale view ring)
move_to 860 330; sleep 1.4
rec_stop
app_stop; sleep 1
gif_status nod-drag $PAGE nod-drag 12 430 60 400 330 400

#   retime-drag.gif  the Dope Sheet: the key at frame 8 dragged to 4, then 14, played after each
app_start "$WORK/data-retime" "$WIKI/examples/tutorial-nod.vat" --select mHead --tool select --view right
move_to 800 300; key f; sleep 0.6
move_to 600 160; xdotool click --repeat 3 --delay 120 4; sleep 0.5
click 125 438; sleep 0.3; click 125 438; sleep 0.5                  # the Dope Sheet tab (the first click focuses)
park; sleep 0.3
rec_window retime-drag
sleep 0.5
# The dope sheet's ruler in this layout: frame 0 at x 297, 47.2 px a frame; the Head row at y 546.
click 675 546; sleep 0.4
slow_drag 675 546 486 546 20; play; sleep 1.1; play; sleep 0.2      # to 4: an eager nod
slow_drag 486 546 958 546 25; play; sleep 1.1; play; sleep 0.2      # to 14: a slow one
sleep 0.6
rec_stop
app_stop; sleep 1
gif_status retime-drag $PAGE retime-drag 12 200 23 800 547 560 0 8
