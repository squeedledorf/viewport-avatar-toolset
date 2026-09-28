#!/usr/bin/env bash
# GIFs of docs/wiki/tutorial-your-first-pose.md. ONLY="raise-arm save-pose" records some of them.
#   victory.gif      the finished pose on a turntable (the app's listing media, as numbered pictures)
#   raise-arm.gif    the left upper arm raised with the red ring of the Rotate gizmo, then 50 typed
#   save-pose.gif    Save Pose..., named Victory; Reset Whole Pose; a double-click on Victory brings it back
source "$(dirname "$0")/common.sh"
PAGE=tutorial-your-first-pose
STAND=$WIKI/examples/posing-head-turn.vat  # Relaxed Stand at frame 0

want() { [ -z "${ONLY:-}" ] || [[ " $ONLY " == *" $1 "* ]]; }
dbg() { [ -n "${SHOT:-}" ] && import -window root "$WORK/$1-debug.png"; return 0; }

# type_into <x> <y> <text>: double-click a value box, replace its text, press Enter.
type_into() {
    xdotool mousemove "$1" "$2" sleep 0.15 click --repeat 2 --delay 60 1 sleep 0.3 key ctrl+a sleep 0.2
    xdotool type --delay 150 "$3"
    sleep 0.3
    xdotool key Return
}

# Relaxed Stand from the front, zoomed and panned so the raised arms stay in view.
front_close() {
    app_start "$WORK/data-$1" "$2" --view front "${@:3}"
    move_to 600 200; sleep 0.4; xdotool click --repeat 3 --delay 150 4; sleep 0.4
    xdotool keydown alt; drag 700 200 700 280 12 0.02 2; xdotool keyup alt
    park; sleep 0.4
}

if want victory; then  # one turn in 31 pictures, played at 12 per second
    rm -rf "$WORK/victory" && mkdir -p "$WORK/victory"
    (vats_env "$VATS" --data-dir "$WORK/data-victory" "$WIKI/examples/tutorial-first-pose.vat" \
        --listing "$WORK/victory/turn.png" --screenshot "$WORK/victory/last.png" >"$WORK/app.log" 2>&1)
    sleep 1
    ffmpeg -loglevel error -y -framerate 12 -pattern_type glob -i "$WORK/victory/turn_*.png" \
        -filter_complex "color=c=0x2b2d33:s=512x512:r=12[bg];[bg][0:v]overlay=shortest=1,crop=340:496:86:8" \
        -c:v libx264rgb -preset ultrafast -qp 0 "$WORK/victory.mkv"  # the pictures are transparent
    gif victory $PAGE victory 12 220
fi

if want raise-arm; then
    front_close raise "$STAND"
    dbg raise
    rec_start raise-arm 380 23 900 401
    sleep 0.5
    click 641 222; sleep 0.8                    # the left upper arm (on the right of the screen): the gizmo
    drag 677 260 828 173 40 0.03; sleep 0.8       # the red ring, dragged along: the arm rises
    type_into 1087 118 50                       # Rotation X, typed exactly
    park; sleep 1.2
    rec_stop
    app_stop; sleep 1
    gif raise-arm $PAGE raise-arm 12 640 0.3
fi

if want save-pose; then
    app_start "$WORK/data-save" "$WIKI/examples/tutorial-first-pose.vat" --view front --tab poses --tool select
    park; sleep 0.4
    rec_start save-pose 0 0 720 424
    sleep 0.5
    click 62 95; sleep 0.7                                          # Save Pose... (nothing selected: the whole pose)
    xdotool type --delay 120 Victory; sleep 0.4; key Return; sleep 0.8
    click 62 12; sleep 0.4; click 129 314; sleep 1.0                # Edit > Reset Whole Pose
    move_to 72 186; sleep 0.4; xdotool click --repeat 2 --delay 80 1; sleep 0.3   # double-click Victory
    park; sleep 1.2
    rec_stop
    app_stop; sleep 1
    gif save-pose $PAGE save-pose 12 540 0.3
fi
