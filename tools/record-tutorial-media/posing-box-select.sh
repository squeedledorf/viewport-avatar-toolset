#!/usr/bin/env bash
# docs/wiki/images/posing/box-select.gif: a drag on empty space with the Select tool boxes the hips and legs; the
# bones inside light up while dragging and are selected on the release.
# Recorded headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start

app_start "$WORK/data-posing-box-select" "$REPO/docs/wiki/examples/first-wave.vat" --tool select
park; sleep 0.8

rec_start posing-box-select 430 60 380 360
sleep 0.8
drag 505 205 690 408 40 0.04; park; sleep 1.4                         # the box, then the selection holds
rec_stop
"$REPO/tools/optimize-wiki-gif.sh" "$WORK/posing-box-select.mkv" "${GIF_OUT:-$REPO/docs/wiki/images/posing/box-select.gif}"
