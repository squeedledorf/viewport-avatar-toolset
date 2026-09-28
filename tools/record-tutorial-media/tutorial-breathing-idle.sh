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
