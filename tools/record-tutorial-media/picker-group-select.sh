#!/usr/bin/env bash
# docs/wiki/images/picker/group-select.gif: group selection on the Picker. Hovering R LEG lights the leg and a click
# selects it, Shift+click on L ARM adds the arm, Ctrl+click on the leg's label takes the leg away again.
# Recorded headless (tools/record-readme-media/lib.sh); see README.md here. PROBE=1 writes one still of the window to
# $WORK/probe.png instead, for finding the places to point at.
set -u
source "$(dirname "$0")/common.sh"

app_start "$WORK/data-picker-group" "$WIKI/examples/blocking-arm.vat" --frame 24 --picker body --picker-style silhouette
if [ -n "${PROBE:-}" ]; then
    ffmpeg -loglevel error -y -f x11grab -video_size "$SIZE" -i ":$DISP" -frames:v 1 "$WORK/probe.png"
    exit 0
fi
# The Picker panel at 1280 x 720 (the README layout): R LEG, L ARM on the canvas's sides.
RLEG_X=${RLEG_X:-35} RLEG_Y=${RLEG_Y:-221} LARM_X=${LARM_X:-186} LARM_Y=${LARM_Y:-143}
rec_start picker-group-select 0 56 226 344
sleep 0.5
move_to "$RLEG_X" "$RLEG_Y"; sleep 0.9                               # hovering the label lights the whole leg
xdotool click 1; sleep 0.8                                           # a click selects it
move_to "$LARM_X" "$LARM_Y"; sleep 0.6
xdotool keydown shift click 1 keyup shift; sleep 1.0                 # Shift adds the arm
move_to "$RLEG_X" "$RLEG_Y"; sleep 0.5
xdotool keydown ctrl click 1 keyup ctrl; sleep 0.9                   # Ctrl takes the leg away
park; sleep 1.0
rec_stop
gif picker-group-select picker group-select 12 226
