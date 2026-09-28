#!/usr/bin/env bash
# The Run cycle production tutorial's GIFs (docs/wiki/tutorial-run-cycle.md), recorded headless with the README
# media's helpers (tools/record-readme-media/lib.sh); see README.md here.
#   tools/record-tutorial-media/tutorial-run-cycle.sh [run] [blocked] [stretch]   (default: all three)
# run.gif: the finished run on the SL Run treadmill. blocked.gif: the stepped key poses playing. stretch.gif: View >
# Treadmill, SL Run, Stretch Time, and the cycle measured again.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
EX=$REPO/docs/wiki/examples
DEST=${GIF_OUT:-$REPO/docs/wiki/images/tutorial-run-cycle}
mkdir -p "$DEST"

# View > Centre of Mass, Show Body Bones and Show Hand Bones off, for a clean body; the viewport made taller.
clean_view() {
    qclick 174 12 640 712; sleep 0.4; qclick 229 552 640 712; sleep 0.3
    qclick 174 12 640 712; sleep 0.4; qclick 238 356 640 712; sleep 0.3
    qclick 174 12 640 712; sleep 0.4; qclick 238 377 640 712; sleep 0.3
    drag 640 427 640 560 12 0.03; sleep 0.3
}
# One step closer, and the body panned up so the feet and the ground show.
close_up() {
    move_to 600 250; xdotool click 4; sleep 0.2
    xdotool mousemove 600 300 keydown alt sleep 0.1 mousedown 2 sleep 0.1 mousemove 600 280 sleep 0.1 \
        mousemove 600 250 sleep 0.2 mouseup 2 keyup alt
    sleep 0.2
}
# body_gif <name> <seconds> <width>: a body on a plain ground needs few colours; 40 keeps the GIF small, then the
# help's optimiser checks the cap.
body_gif() {
    COLORS=40 to_gif "$1" "$WORK/$1-40.gif" 15 "$3" 0 "$2"
    "$REPO/tools/optimize-wiki-gif.sh" "$WORK/$1-40.gif" "$DEST/$1.gif" "$3"
}
# View > Treadmill, hovered open.
treadmill_menu() { qclick 174 12 640 712; sleep 0.4; move_to 213 678; sleep 0.5; }

rec_run() {
    app_start "$WORK/data-run" "$EX/run-cycle.vat" --view right --tool select --frame 0 --backdrop
    clean_view
    close_up
    treadmill_menu
    qclick 428 471 428 471; sleep 0.3  # SL Run (the menu stays open)
    qclick 485 390 640 712; sleep 0.3  # Show Treadmill
    park; sleep 0.5
    play; sleep 1.0
    rec_start run 390 44 440 480
    sleep 2.0
    rec_stop
    app_stop
    # two cycles exactly (16 frames at 30 fps, twice), so the GIF loops without a jump
    body_gif run 1.0667 280
}

rec_blocked() {
    app_start "$WORK/data-blocked" "$EX/run-blocked.vat" --view right --tool select --frame 0 --backdrop
    clean_view
    close_up
    park; sleep 0.5
    play; sleep 1.0
    rec_start blocked 390 44 440 480
    sleep 2.0
    rec_stop
    app_stop
    body_gif blocked 1.6 280  # two cycles of 24 frames
}

rec_stretch() {
    app_start "$WORK/data-stretch" "$EX/run-blocked.vat" --view right --tool select --frame 0
    qclick 61 12 640 712; sleep 0.4; qclick 153 223 640 712; sleep 0.3  # Edit > Convert Blocking to Spline
    qclick 174 12 640 712; sleep 0.4; qclick 229 552 640 712; sleep 0.3  # Centre of Mass off
    park; sleep 0.5
    rec_start stretch 20 360 640 360  # the Treadmill menu and the status bar
    sleep 0.3
    treadmill_menu; sleep 0.3
    qclick 428 471 428 471; sleep 1.6     # SL Run: 65% of 5.13
    qclick 478 679 640 712; sleep 1.0     # Stretch Time: the cycle is now 16 frames long
    treadmill_menu; sleep 1.9             # 98% of 5.13
    xdotool key Escape; park; sleep 0.4
    rec_stop
    app_stop
    FPS=12 DUR=7.5 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/stretch.mkv" "$DEST/stretch.gif" 640
}

for name in ${@:-run blocked stretch}; do "rec_$name"; done
ls -l "$DEST"
