#!/usr/bin/env bash
# docs/wiki/images/tutorial-dynamics-jiggle/scrub-landing.gif: the hop with a BELLY chain added and baked in the app, as
# the page does it (the same keys as the shipped dynamics-jiggle.vat), its Translate Z curve alone
# and stretched upwards; the playhead dragged across the landing (frames 25 to 45) in the Graph's ruler. jiggle.gif (the preset beside an exaggerated setting) is not made here.
# Recorded headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SIZE=1600x900 TL_Y=799
source "$REPO/tools/record-readme-media/lib.sh"
source "$REPO/tools/record-tutorial-media/tutorial-helpers.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
DIR=${GIF_DIR:-$REPO/docs/wiki/images/tutorial-dynamics-jiggle}
EX=$REPO/docs/wiki/examples

start_app "$WORK/data-scrub" "$EX/dynamics-jump-start.vat" --select BELLY --view right --frame 22 --window dynamics
qclick 536 168 800 895; sleep 0.5                                     # Add Chain from Selected Bone: the Jiggle preset
qclick 460 538 800 895; sleep 1.0                                     # Bake
qclick 772 69 800 895; sleep 0.4                                      # close the window
qclick 55 731 800 895; sleep 0.4                                      # Translate Z alone
qclick 181 580 800 895; sleep 0.4                                     # Frame All
move_to 900 680; xdotool keydown shift                                # stretch values: the dip in centimetres
for _ in $(seq 31); do xdotool click 4; sleep 0.05; done
xdotool keyup shift
xdotool keydown alt; drag 900 720 900 640 15 0.02; xdotool keyup alt   # and pan the curve back into view
park; sleep 0.6
rec_start scrub-landing 560 598 640 144
sleep 1
DRAG_PRE=0.2 drag 666 608 1044 608 90 0.04; park; sleep 1.2
rec_stop
FPS=12 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/scrub-landing.mkv" "$DIR/scrub-landing.gif"
