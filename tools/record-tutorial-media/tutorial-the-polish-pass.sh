#!/usr/bin/env bash
# docs/wiki/images/tutorial-the-polish-pass/: throw.gif (the finished throw), blocking-to-spline.gif (the blocked throw
# played, Edit > Convert Blocking to Spline, played again) and dope-sheet.gif (the release keys moved from 18 to 15 in
# the Summary row). Recorded headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SIZE=1600x900 TL_Y=799
source "$REPO/tools/record-readme-media/lib.sh"
source "$REPO/tools/record-tutorial-media/tutorial-helpers.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
DIR=${GIF_DIR:-$REPO/docs/wiki/images/tutorial-the-polish-pass}
EX=$REPO/docs/wiki/examples
VIEW=(--view right --select mChest --focus --distance 2.8 --tool select --backdrop)  # the thrower in profile

# throw.gif: the finished throw from its first move, 44 frames, and a moment of the last pose.
start_app "$WORK/data-throw" "$EX/polish-finished.vat" --frame 0 "${VIEW[@]}"
clean_view
park; sleep 0.6
rec_start throw 590 70 360 465
sleep 0.4
play; sleep 2.6; play
rec_stop
gif throw "$DIR/throw.gif" 15 "$(motion_start throw)" 1.6  # from the first move: the 44 frames and a breath
app_stop

# blocking-to-spline.gif: the blocked throw pops from pose to pose; Convert Blocking to Spline; it moves.
start_app "$WORK/data-block" "$EX/polish-start.vat" --frame 0 "${VIEW[@]}"
clean_view
park; sleep 0.6
rec_start blocking 590 70 360 465
sleep 0.3
play; sleep 1.45; play; sleep 0.4
qclick 62 12 800 895; sleep 0.4; qclick 153 223 800 895; sleep 0.3                # Edit > Convert Blocking to Spline
first_frame; sleep 0.2; play; sleep 1.45; play; sleep 0.6
rec_stop
COLORS=32 gif blocking "$DIR/blocking-to-spline.gif" 12
app_stop

# dope-sheet.gif: every bone selected, the Summary row's diamond at 18 clicked and dragged to 15.
start_app "$WORK/data-dope" "$EX/polish-start.vat" --select-all --window dope-sheet
park; sleep 0.6
rec_start dope-sheet 175 598 640 105
sleep 0.6
qclick 771 631 771 631; sleep 0.6
DRAG_PRE=0.05 drag 771 631 692 631 30 0.03; park; sleep 1.4
rec_stop
FPS=12 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/dope-sheet.mkv" "$DIR/dope-sheet.gif"
