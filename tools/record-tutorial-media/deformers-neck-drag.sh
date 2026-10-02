#!/usr/bin/env bash
# docs/wiki/images/deformers/neck-drag.gif: the neck dragged up with the Move tool's blue arrow at frame 30, to the
# target ghost's head, which keys its position. Recorded headless (tools/record-readme-media/lib.sh); see README.md here.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SIZE=1600x900
source "$REPO/tools/record-readme-media/lib.sh"
# No bus and a private runtime folder: nothing the app starts can reach the desktop's session (or its portals).
mkdir -p "$WORK/xdg" && chmod 700 "$WORK/xdg"
vats_env() {
    exec env -u WAYLAND_DISPLAY -u GDK_SCALE -u QT_SCALE_FACTOR DISPLAY=:$DISP GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 \
        DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent XDG_RUNTIME_DIR="$WORK/xdg" SDL_FILE_DIALOG_DRIVER=zenity "$@"
}
trap cleanup EXIT
[ -n "${DISPLAY:-}" ] || xvfb_start

ex="$REPO/docs/wiki/examples"
app_start "$WORK/data-deformers" "$ex/deformer-start.vat" --frame 30 --select mNeck --tool move --view right \
    --target "$ex/deformer-long-neck.vat" --focus --distance 2.4
park; sleep 0.8

rec_start deformers-neck-drag 552 110 400 330
sleep 0.8
drag 752 255 752 187 40 0.035; park; sleep 1.4                    # the blue arrow, up to the green head
rec_stop
"$REPO/tools/optimize-wiki-gif.sh" "$WORK/deformers-neck-drag.mkv" "${GIF_OUT:-$REPO/docs/wiki/images/deformers/neck-drag.gif}"
