#!/usr/bin/env bash
# The sword swing tutorial's GIF (docs/wiki/tutorial-sword-swing.md): swing.gif, the finished swing played twice,
# seen from the front right. Uses the README recorder's helpers (private Xvfb, throwaway data folder, own PIDs only).
#   VATS=build/app/vats tools/record-tutorial-media/tutorial-sword-swing.sh
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
export WORK=${WORK:-$HOME/Projects/.vats-tutorial-media}
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start

app_start "$WORK/data-sword" "$REPO/docs/wiki/examples/sword-swing.vat" --tool select --view front
xdotool keydown alt; drag 600 250 520 250 12 0.02 1; xdotool keyup alt  # orbit to the front right
qclick 174 12 640 712; sleep 0.4; qclick 240 356 640 712; sleep 0.3      # View > Show Body Bones off
park; sleep 0.5

rec_start swing 400 45 400 380
sleep 0.3
play; sleep 3.4; play                                                                   # 48 frames, twice
sleep 0.3
rec_stop
FPS=12 SS=0.3 DUR=3.6 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/swing.mkv" "$REPO/docs/wiki/images/tutorial-sword-swing/swing.gif" 360
