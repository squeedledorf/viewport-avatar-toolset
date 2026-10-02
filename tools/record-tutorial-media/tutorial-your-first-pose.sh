#!/usr/bin/env bash
# GIFs of docs/wiki/tutorial-your-first-pose.md. ONLY="raise-arm save-pose" records some of them.
#   victory.gif     the finished pose on a turntable (the app's listing media, as numbered pictures)
#   lift-chest.gif  from the side, the chest's green ring dragged up onto the green target ghost (step 3)
#   raise-arm.gif   from the front, the left upper arm clicked and its red ring dragged round into the ghost's raised
#                   arm, past it and back (step 4)
#   save-pose.gif   Save Pose... from the view's menu, named Victory; Reset Whole Pose; Poses > Victory brings it back
source "$(dirname "$0")/common.sh"
PAGE=tutorial-your-first-pose
want() { [ -z "${ONLY:-}" ] || [[ " $ONLY " == *" $1 "* ]]; }

if want victory; then  # one turn in 31 pictures, played at 12 per second
    rm -rf "$WORK/victory" && mkdir -p "$WORK/victory"
    (vats_env "$VATS" --data-dir "$WORK/data-victory" "$WIKI/examples/tutorial-first-pose.vat" --select mTorso --focus --distance 2.8 \
        --listing "$WORK/victory/turn.png" --screenshot "$WORK/victory/last.png" >"$WORK/app.log" 2>&1)
    sleep 1
    ffmpeg -loglevel error -y -framerate 12 -pattern_type glob -i "$WORK/victory/turn_*.png" \
        -filter_complex "color=c=0x2b2d33:s=512x512:r=12[bg];[bg][0:v]overlay=shortest=1,crop=300:512:106:0" \
        -c:v libx264rgb -preset ultrafast -qp 0 "$WORK/victory.mkv"  # the pictures are transparent
    gif victory $PAGE victory 12 220
fi

drag_setup
# start <name> [args]: Contrapposto with the finished pose as the target ghost, from the front.
start() {
    app_start "$WORK/data-$1" --pose body-contrapposto --target "$WIKI/examples/tutorial-first-pose.vat" --view front "${@:2}"
    sleep 0.5
}

if want lift-chest; then
    start chest --tool rotate
    click 172 36; sleep 0.4; click 112 152; sleep 0.4                     # Picker > Chest
    move_to 800 380; key 3; sleep 0.6; park; sleep 0.4                    # from the side
    rec_window lift-chest
    sleep 0.6
    slow_drag 688 176 688 162 20                                          # the green ring, in front of the chest, up
    move_to 860 330; sleep 1.4
    rec_stop
    app_stop; sleep 1
    gif_status lift-chest $PAGE lift-chest 12 430 30 420 300 420
fi

if want raise-arm; then
    start arm --tool rotate
    rec_window raise-arm
    sleep 0.6
    move_to 630 160; sleep 0.6; xdotool click 1; sleep 0.8               # the left upper arm: the rings
    slow_drag 655 215 862 133 50                                          # the red ring, up and round: onto the ghost
    move_to 880 330; sleep 1.4
    rec_stop
    app_stop; sleep 1
    gif_status raise-arm $PAGE raise-arm 12 420 23 460 330 460
fi

if want save-pose; then
    PRESET=secondlife POINTER=1 app_start "$WORK/data-save" "$WIKI/examples/tutorial-first-pose.vat" --view front --tool select
    rec_window save-pose
    sleep 0.3
    # The view's right-click menu on empty space (opened at 720,90): Save Pose..., Poses, ..., Reset Whole Pose.
    move_to 720 90; sleep 0.3; xdotool click 3; sleep 0.5
    click 830 136; sleep 0.5                                              # Save Pose...
    xdotool type --delay 80 Victory; sleep 0.3; key Return; sleep 0.7
    move_to 720 90; sleep 0.2; xdotool click 3; sleep 0.5
    click 830 430; sleep 0.8                                              # Reset Whole Pose: the T shape
    move_to 720 90; sleep 0.2; xdotool click 3; sleep 0.5
    move_to 830 157; sleep 0.4; move_to 1000 157; sleep 0.2; click 1062 157  # Poses > Victory
    move_to 900 330; sleep 1.2
    rec_stop
    app_stop; sleep 1
    gif_status save-pose $PAGE save-pose 12 420 23 740 420 560 1.0 8   # the start, before the first click, trimmed
fi
