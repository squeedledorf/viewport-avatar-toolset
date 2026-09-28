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
