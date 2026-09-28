#!/usr/bin/env bash
# The Dynamics tutorials' slider drags, recorded live while the preview plays: stiffness-slider.gif on
# tutorial-dynamics.md (the swish from above, the tail chain's Stiffness dragged from 0.08 to about 0.03) and on
# tutorial-dynamics-wings.md (the wing beat from behind, both wing chains added in the Picker, the left chain's Stiffness
# dragged down while the right one keeps the preset). The slider row is stacked under the view so the value shows.
#   VATS=build/app/vats WORK=~/Projects/.vats-media/dynamics tools/record-tutorial-media/tutorial-dynamics-sliders.sh
# Uses the README recorder's helpers (private Xvfb, throwaway data folder, own PIDs only). Layout 1600 x 900.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SIZE=1600x900 TL_Y=799
source "$REPO/tools/record-readme-media/lib.sh"
source "$REPO/tools/record-tutorial-media/tutorial-helpers.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
EX=$REPO/docs/wiki/examples

BODY=356 HAND=377 TAIL=440 COM=552  # View menu rows
toggle() { for y in "$@"; do qclick 174 12 800 890; sleep 0.3; qclick 240 "$y" 800 890; sleep 0.3; done; }
# The Dynamics window, opened by --window at the left of the view, dragged by its title over Properties.
dyn_aside() { drag 500 69 1300 69 25 0.02; sleep 0.3; }
# stacked <name> <out.gif> <x> <y> <w> <h> <width>: the view crop with the Stiffness row (1230,408) under it.
stacked() {
    ffmpeg -loglevel error -y -ss 0.8 -t 6.6 -i "$WORK/$1.mkv" -filter_complex \
        "[0]crop=$5:$6:$3:$4[a];[0]crop=350:28:1230:408,pad=$5:28:(ow-iw)/2:0:color=0x1f2126[b];[a][b]vstack" \
        -c:v libx264rgb -qp 0 "$WORK/$1-crop.mkv"
    COLORS=24 BAYER=5 to_gif "$WORK/$1-crop.mkv" "$2" 10 "$7"
    echo "$2: $(($(stat -c %s "$2") / 1024)) KB" >&2
}
# drag_stiffness <name>: play, drag the Stiffness knob (0.08 at x 1337; 1 px is about 0.004) to about 0.03, play on.
drag_stiffness() {
    rec_start "$1" 0 0 1600 900
    play; sleep 2.2
    DRAG_PRE=0.3 drag 1337 422 1324 422 20 0.05; park; sleep 4
    play
    rec_stop
}

# The tail: the swish from straight above, the tail shown, its chain selected in the list.
start_app "$WORK/data-swish" "$EX/dynamics-swish.vat" --window dynamics --view top --tool select --backdrop
toggle $BODY $HAND $COM $TAIL
dyn_aside
move_to 745 330; for _ in 1 2 3 4; do xdotool click 4; sleep 0.15; done  # a little closer
click 1300 226; sleep 0.3; park; sleep 0.5
drag_stiffness swish-stiffness
app_stop
stacked swish-stiffness "$REPO/docs/wiki/images/tutorial-dynamics/stiffness-slider.gif" 610 46 420 354 340

# The wings: from behind; Picker > Extras > Wings, Left Wing 2, Add Chain; Swap Sides, Add Chain; the left chain.
start_app "$WORK/data-wings" "$EX/dynamics-wings-start.vat" --window dynamics --view back --tool select --backdrop
dyn_aside
toggle $BODY $HAND $COM
click 172 35; sleep 0.4; click 243 68; sleep 0.4; click 38 412; sleep 0.4    # Picker, Extras, Wings
click 104 155; sleep 0.4; click 1336 168; sleep 0.4                          # Left Wing 2, Add Chain
click 258 412; sleep 0.4; click 1336 168; sleep 0.4                          # Swap Sides, Add Chain
xdotool mousemove 750 290 keydown alt; drag 750 290 750 315 20 0.02 1; drag 750 300 750 370 20 0.02 2; xdotool keyup alt
click 1290 226; sleep 0.3; park; sleep 0.5
drag_stiffness wings-stiffness
app_stop
stacked wings-stiffness "$REPO/docs/wiki/images/tutorial-dynamics-wings/stiffness-slider.gif" 430 60 640 360 380
