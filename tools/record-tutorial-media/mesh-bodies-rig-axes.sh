#!/usr/bin/env bash
# docs/wiki/images/mesh-bodies/rig-axes-gizmo.png and docs/wiki/images/ik/mech-leg-drag.gif: the CC0 test mech (tools/mech_rig.h, written
# by vats_make_test_body --mech) shown as the mesh body. The still: the Rotate tool's Local rings on its hind knee, on
# the knee's own rig axes. The GIF: the hind foot's dot dragged through the air with Auto IK, the leg bending about its
# own hinges. Recorded headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start
MAKE_BODY=${MAKE_BODY:-$(dirname "$VATS")/../vats_make_test_body}
"$MAKE_BODY" "$REPO/data/character" "$WORK" --mech >/dev/null || { echo "cannot write the mech with $MAKE_BODY" >&2; exit 1; }
IMG=$REPO/docs/wiki/images/mesh-bodies

# A little round from the side, to look along the left hind leg's hinge.
side() {
    xdotool mousemove 600 300 keydown alt mousedown 1
    for x in 604 608 612 616 620 624; do xdotool mousemove $x 300; sleep 0.05; done
    xdotool mouseup 1 keyup alt
    park; sleep 0.8
}

app_start "$WORK/data-rig-axes" --mesh-body "$WORK/mech.dae" --select-group "Hind Limbs" --select mHindLimb2Left --focus \
    --tool rotate --view left --distance 3.2
side
ffmpeg -loglevel error -y -f x11grab -video_size "$SIZE" -i ":$DISP+0,0" -frames:v 1 "$WORK/rig-axes-full.png"
python3 -c "from PIL import Image; Image.open('$WORK/rig-axes-full.png').crop((420, 45, 820, 345)).save('$IMG/rig-axes-gizmo.png')"
"$REPO/tools/optimize-wiki-images.sh" "$IMG/rig-axes-gizmo.png"
app_stop

app_start "$WORK/data-mech-leg" --mesh-body "$WORK/mech.dae" --select-group "Hind Limbs" --select mHindLimb2Left --focus \
    --select mHindLimb4Left --tool select --view left --distance 3.2
side
rec_start mesh-bodies-mech-leg 450 50 340 260
sleep 0.6
move_to 607 235; sleep 0.8  # the hind foot's dot
# A step through the air: the foot lifts, swings forward and comes down in front, eased.
xdotool mousedown 1
# shellcheck disable=SC2046
xdotool $(awk 'BEGIN { n = 80; for (i = 1; i <= n; i++) { t = i / n; t = t * t * (3 - 2 * t)
    printf "mousemove %d %d sleep 0.04 ", 607 - 75 * t, 235 - 40 * sin(3.14159265 * t) - 8 * t } }')
sleep 0.2
xdotool mouseup 1
park; sleep 1.0
rec_stop
"$REPO/tools/optimize-wiki-gif.sh" "$WORK/mesh-bodies-mech-leg.mkv" "${GIF_OUT:-$REPO/docs/wiki/images/ik/mech-leg-drag.gif}"
