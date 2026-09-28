#!/usr/bin/env bash
# The Dynamics tutorials' GIFs (docs/wiki/tutorial-dynamics*.md). Each shot opens a project, sets up the view, then
# steps through the frames one by one (Right) and grabs each, so the timing is exact however slowly the display draws;
# baked chains play their keys when stepped. Comparison GIFs put three baked copies side by side, each labelled with
# its value; their projects come from vats_make_wiki_examples --dynamics-variants and are not shipped. The slider
# drags, recorded live, are in tutorial-dynamics-sliders.sh.
#   VATS=build/app/vats tools/record-tutorial-media/tutorial-dynamics.sh [name-filter]
# Uses the README recorder's helpers (private Xvfb, throwaway data folder, own PIDs only). Recorded layout 1600 x 900:
# Viewport 289,45 to 1214,533; View menu at 174,12.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
export WORK=${WORK:-$HOME/Projects/.vats-tutorial-media/dynamics} SIZE=1600x900 CAP_KB=${CAP_KB:-170}  # 14 GIFs share the pages
MAKE_EXAMPLES=${MAKE_EXAMPLES:-$(dirname "${VATS:-$REPO/build/app/vats}")/../vats_make_wiki_examples}
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
for _ in $(seq 50); do xdotool getdisplaygeometry >/dev/null 2>&1 && break; sleep 0.1; done
EX=$REPO/docs/wiki/examples VAR=$WORK/variants FONT=$REPO/app/assets/fonts/Inter-SemiBold.ttf
FILTER=${1:-}
mkdir -p "$VAR"
"$MAKE_EXAMPLES" - --dynamics-variants "$VAR" >/dev/null || exit 1

# View menu rows.
BODY=356 HAND=377 FACE=398 WING=419 TAIL=440 VOLUMES=531 COM=552 XRAY=734
toggle() { for y in "$@"; do qclick 174 12 800 890; sleep 0.3; qclick 240 "$y" 800 890; sleep 0.3; done; }
# camera <orbit-dx> <orbit-dy> <zoom> <pan-dy>: Alt + left drag orbits, Alt + right drag up zooms in, Alt + middle drag
# up moves the view up.
camera() {
    xdotool mousemove 750 290 keydown alt
    drag 750 290 $((750 + $1)) $((290 + $2)) 20 0.02 1
    (($3)) && drag 750 290 750 $((290 - $3)) 20 0.02 3
    (($4)) && drag 750 300 750 $((300 - $4)) 20 0.02 2
    xdotool keyup alt
}

# open <project> <view> <toggles...>: the project, the camera turned to a View menu view, and View menu rows toggled.
open() {
    local f=$1 v=$2
    shift 2
    for _ in 1 2 3; do  # the app now and then fails to reach a fresh display; try again
        app_start "$WORK/data" "$f" --tool select --view "$v" ${BACKDROP---backdrop} ${SELECT:+--select $SELECT} ${EXTRA:-}
        [ -n "$WIN" ] && kill -0 "$APP_PID" 2>/dev/null && break
        app_stop
    done
    toggle "$@"
}
# frames <dir> <x> <y> <w> <h> <last> <step>: frames 0..last of the open project, every step-th, as PNGs.
frames() {
    local d=$1 x=$2 y=$3 w=$4 h=$5 last=$6 step=$7 first=${8:-0}
    rm -rf "$d"
    mkdir -p "$d"
    park
    first_frame
    sleep 0.3
    for ((i = 0; i < first; ++i)); do xdotool key Right; done
    for ((f = first; f <= last; f += step)); do
        sleep 0.15
        ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -video_size "${w}x${h}" -i ":$DISP+$x,$y" -frames:v 1 \
            "$d/$(printf %03d $(((f - first) / step))).png"
        for ((i = 0; i < step; ++i)); do xdotool key Right; done
    done
}
# gif_of <dir> <out.gif> <fps> <width> [label]: a frame folder as a help GIF, with an optional label top left.
gif_of() {
    local vf="null"
    [ -n "${5:-}" ] && vf="drawtext=fontfile=$FONT:text='$5':x=10:y=8:fontsize=17:fontcolor=white:box=1:boxcolor=0x1c1f26@0.7:boxborderw=5"
    ffmpeg -loglevel error -y -framerate "$3" -i "$1/%03d.png" -vf "$vf" -c:v libx264rgb -qp 0 "$WORK/tmp.mkv"
    FPS=$3 "$REPO/tools/optimize-wiki-gif.sh" "$WORK/tmp.mkv" "$2" "$4"
}
# grid <out.gif> <fps> <width> <dir1> <label1> <dir2> <label2> <dir3> <label3>: three frame folders side by side.
grid() {
    local out=$1 fps=$2 width=$3
    shift 3
    local inputs=() filters="" n=0
    while (($#)); do
        inputs+=(-framerate "$fps" -i "$1/%03d.png")
        filters+="[$n:v]drawtext=fontfile=$FONT:text='$2':x=(w-tw)/2:y=8:fontsize=17:fontcolor=white:box=1:boxcolor=0x1c1f26@0.7:boxborderw=5,pad=iw+4:ih:2:0:0x14161b[c$n];"
        n=$((n + 1))
        shift 2
    done
    local stack=""
    for ((i = 0; i < n; ++i)); do stack+="[c$i]"; done
    ffmpeg -loglevel error -y "${inputs[@]}" -filter_complex "${filters}${stack}hstack=inputs=$n" -c:v libx264rgb -qp 0 \
        "$WORK/tmp.mkv"
    FPS=$fps "$REPO/tools/optimize-wiki-gif.sh" "$WORK/tmp.mkv" "$out" "$width"
}
want() { [ -z "$FILTER" ] || [[ $1 == *"$FILTER"* ]]; }

# --- tutorial-dynamics.md: the tail ---------------------------------------------------------------------------------
# Behind, above and to the right, for the walk; straight down with the avatar facing up, for the side-to-side swing;
# from the side, for gravity.
TAILCAM() { camera 70 110 0 0; }
TOPCAM() { camera 450 0 90 100; }
SIDECAM() { camera 0 0 90 0; }
IMG=$REPO/docs/wiki/images/tutorial-dynamics
mkdir -p "$IMG"
if want walk-tail; then
    BACKDROP= EXTRA="--window treadmill" open "$EX/dynamics-walk-tail.vat" back $BODY $HAND $COM $TAIL  # on the treadmill: it reads as forward
    TAILCAM
    frames "$WORK/walk-tail" 520 80 480 450 28 2
    app_stop
    CAP_KB=280 gif_of "$WORK/walk-tail" "$IMG/walk-tail.gif" 15 360  # the page's first GIF: the grid costs bytes
fi
declare -A LOW=([stiffness]=0.02 [damping]=0.02 [drag]=0 [gravity]="0 g")
declare -A MID=([stiffness]=0.08 [damping]=0.12 [drag]=0.03 [gravity]="0.3 g")
declare -A HIGH=([stiffness]=0.3 [damping]=0.5 [drag]=0.2 [gravity]="2 g")
declare -A WMID=([stiffness]=0.08 [damping]=0.12 [drag]=0.05 [gravity]="0.3 g")
title() { echo "${1^}"; }
for s in stiffness damping drag gravity; do
    want "tail-$s" || continue
    for i in 0 1 2; do
        if [[ $s == gravity ]]; then
            open "$VAR/tail-$s-$i.vat" right $BODY $HAND $COM $TAIL
            SIDECAM
            frames "$WORK/tail-$s-$i" 469 105 440 370 58 2
        else
            open "$VAR/tail-$s-$i.vat" top $BODY $HAND $COM $TAIL
            TOPCAM
            frames "$WORK/tail-$s-$i" 519 55 470 400 58 2
        fi
        app_stop
    done
    grid "$IMG/tail-$s.gif" 15 640 "$WORK/tail-$s-0" "$(title $s) ${LOW[$s]}" "$WORK/tail-$s-1" "$(title $s) ${MID[$s]}" \
        "$WORK/tail-$s-2" "$(title $s) ${HIGH[$s]}"
done

# --- tutorial-dynamics-wings.md ---------------------------------------------------------------------------------------
IMG=$REPO/docs/wiki/images/tutorial-dynamics-wings
mkdir -p "$IMG"
WINGCAM() { camera 0 25 0 -70; }
if want wings-flap; then
    open "$EX/dynamics-wings.vat" back $BODY $HAND $COM $WING
    WINGCAM
    frames "$WORK/wings" 470 60 560 360 29 1
    app_stop
    gif_of "$WORK/wings" "$IMG/wings-flap.gif" 15 480 "Half speed"
fi
for s in stiffness damping drag gravity; do
    want "wing-$s" || continue
    for i in 0 1 2; do
        open "$VAR/wings-$s-$i.vat" back $BODY $HAND $COM $WING
        WINGCAM
        frames "$WORK/wing-$s-$i" 745 60 300 360 29 1  # one wing: they mirror
        app_stop
    done
    grid "$IMG/wing-$s.gif" 15 640 "$WORK/wing-$s-0" "$(title $s) ${LOW[$s]}" "$WORK/wing-$s-1" "$(title $s) ${WMID[$s]}" \
        "$WORK/wing-$s-2" "$(title $s) ${HIGH[$s]}"
done

# --- tutorial-dynamics-jiggle.md --------------------------------------------------------------------------------------
IMG=$REPO/docs/wiki/images/tutorial-dynamics-jiggle
mkdir -p "$IMG"
if want jiggle; then
    for v in subtle strong; do
        f=$EX/dynamics-jiggle.vat
        [ $v = strong ] && f=$VAR/jiggle-strong.vat
        SELECT=BELLY EXTRA="--body none" open "$f" right $BODY $HAND $COM $VOLUMES  # the volumes alone, BELLY highlighted
        camera 0 0 60 0
        frames "$WORK/jiggle-$v" 600 60 290 330 52 1 24
        app_stop
    done
    CAP_KB=220 grid "$IMG/jiggle.gif" 15 520 "$WORK/jiggle-subtle" "Jiggle preset, half speed" "$WORK/jiggle-strong" "Exaggerated"
fi
