#!/usr/bin/env bash
# docs/wiki/images/tutorial-dance-to-the-beat/: dance.gif (the finished groove, one loop), scale-keys.gif (the
# Dope Sheet's keys scaled from 64 to 60 frames) and overlap.gif (the groove without and with follow-through, side by side).
# Recorded headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SIZE=1600x900 TL_Y=799
source "$REPO/tools/record-readme-media/lib.sh"
source "$REPO/tools/record-tutorial-media/tutorial-helpers.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
DIR=${GIF_DIR:-$REPO/docs/wiki/images/tutorial-dance-to-the-beat}
EX=$REPO/docs/wiki/examples

# dance.gif: the finished groove from the front, a little round, one 2-second loop.
start_app "$WORK/data-dance" "$EX/dance-finished.vat" --view front --select mChest --focus --distance 3.2 --tool select \
    --backdrop
clean_view
xdotool keydown alt; drag 750 300 790 300 10 0.02 1; xdotool keyup alt
park; sleep 0.6
rec_start dance 590 60 340 470
sleep 0.3
play; sleep 2.5; play
rec_stop
gif dance "$DIR/dance.gif" 12 0.4 2.0
app_stop

# scale-keys.gif: the loop fitted to four beats by hand. With a click track loaded (the beat lines on the timeline),
# every key of All animated bones box-selected in the Dope Sheet's Summary row, then the right scale handle dragged from
# 64 to 60: the keys close up and the beat keys land on the lines at 15, 30, 45 and 60.
ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=1000:duration=8" \
    -af "volume='if(lt(mod(t,0.5),0.04),1,0)':eval=frame" -ar 44100 "$WORK/click_track.wav"
start_app "$WORK/data-scale" "$EX/dance-grid.vat" "$WORK/click_track.wav" --select-all --window dope-sheet
qclick 133 580 800 895; sleep 0.4; qclick 70 630 800 895; sleep 0.4                  # All animated bones
park; sleep 0.6
rec_start scale-keys 920 598 640 282
sleep 0.6
drag 210 622 1540 642 25 0.03; park; sleep 0.8                                     # box-select the Summary row
DRAG_PRE=0.3 drag 1470 660 1392 660 40 0.04; park; sleep 1.4                       # the right handle, 64 to 60
rec_stop
# the Dope Sheet's rows over the timeline's ruler and keys, without the timeline bar between them
ffmpeg -loglevel error -y -i "$WORK/scale-keys.mkv" -filter_complex "[0]crop=640:146:0:0[a];[0]crop=640:48:0:234[b];[a][b]vstack" \
    -c:v libx264rgb -preset ultrafast -qp 0 "$WORK/scale-keys-stack.mkv"
FPS=12 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/scale-keys-stack.mkv" "$DIR/scale-keys.gif"
app_stop

# overlap.gif: dance-compare.vat from the front, Before (no follow-through, left) and After (overlap on the arms, dynamics
# on the head, right) dancing side by side, one 2-second loop.
start_app "$WORK/data-overlap" "$EX/dance-compare.vat" --frame 0 --view front --select mChest --focus --distance 3.4 \
    --tool select --backdrop
clean_view
xdotool mousemove 850 300 sleep 0.2 mousedown 2 sleep 0.1 mousemove 820 300 sleep 0.05 mousemove 790 300 sleep 0.05 \
    mousemove 750 300 sleep 0.1 mouseup 2                                           # pan: both in the middle
park; sleep 0.6
rec_start overlap 580 90 420 420
sleep 0.3
play; sleep 2.6; play
rec_stop
COLORS=24 gif overlap "$DIR/overlap.gif" 12 0.4 2.0
