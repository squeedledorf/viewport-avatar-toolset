#!/usr/bin/env bash
# GIFs of docs/wiki/first-steps.md. ONLY="wave swing-out" records some of them.
#   wave.gif       the finished wave, from three-quarters in front, bones hidden, played twice
#   swing-out.gif  frame 10: the forearm's blue ring dragged until the forearm lies in the green target ghost, the
#                  status bar's distance going green (step 5)
source "$(dirname "$0")/common.sh"
PAGE=first-steps
want() { [ -z "${ONLY:-}" ] || [[ " $ONLY " == *" $1 "* ]]; }

if want wave; then
    app_start "$WORK/data-wave" "$WIKI/examples/first-wave.vat" --view front --tool select
    hide_bones
    xdotool keydown alt; drag 850 300 950 300 12 0.02; xdotool keyup alt  # Industry: Alt + left drag orbits
    park; sleep 0.5
    rec_start wave 470 25 300 395
    sleep 0.3
    key Home space; sleep 2.05; key space
    rec_stop
    app_stop; sleep 1
    gif wave $PAGE wave 15 300 0.1 2.0
fi

if want swing-out; then
    drag_setup
    app_start "$WORK/data-swing" --pose body-wave --target "$WIKI/examples/first-wave.vat" --frame 10 --select mElbowRight \
        --tool rotate --view front
    move_to 800 380; key f; sleep 0.8; park; sleep 0.4
    rec_window swing-out
    sleep 0.6
    slow_drag 535 166 514 210 40   # along the blue ring, away from the head: onto the ghost
    move_to 790 330; sleep 1.4      # off the gizmo, the result held
    rec_stop
    app_stop; sleep 1
    gif_status swing-out $PAGE swing-out 12 400 110 420 290 420
fi
