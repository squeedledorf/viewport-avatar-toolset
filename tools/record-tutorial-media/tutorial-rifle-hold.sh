#!/usr/bin/env bash
# The rifle hold tutorial's GIF (docs/wiki/tutorial-rifle-hold.md): over-walk.gif, the aim hold over a walk as
# Second Life combines them per bone (examples/rifle-over-walk.vat). Same recording as the pistol's.
#   VATS=build/app/vats WORK=~/Projects/.vats-media/tutorial tools/record-tutorial-media/tutorial-rifle-hold.sh
PAGE=tutorial-rifle-hold EXAMPLE=rifle-over-walk REC="350 45 430 380" exec bash "$(dirname "$0")/tutorial-pistol-hold.sh"
