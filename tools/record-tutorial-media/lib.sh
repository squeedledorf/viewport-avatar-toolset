# Viewport Avatar Toolset - shared helpers for recording the tutorials' GIFs. Source this file.
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#
# Builds on tools/record-readme-media/lib.sh (private Xvfb display, throwaway data folder, xdotool, ffmpeg): the app
# starts from the default settings, as a reader's first run does, with only the Welcome window turned off, in the
# README recordings' 1280 x 720 layout. Each tutorial's script drives the app through the page's steps as written,
# so running it is also a test of the page: `snap` saves a still of the whole window to $WORK for checking.
#
# Environment: VATS (default: build/app/vats of this tree), WORK (default: build/tutorial-media), DISP, OUT
# (default: docs/wiki/images/<page-stem>, set by each script).

TUT=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$TUT/../.." && pwd)
VATS=${VATS:-$ROOT/build/app/vats}
WORK=${WORK:-$ROOT/build/tutorial-media}
DISP=${DISP:-}
source "$ROOT/tools/record-readme-media/lib.sh"
SIZE=1280x720

# app_start <data-dir> [args...]: a fresh data folder, the README layout, the Welcome window off. Run from docs/wiki
# so example paths are as the pages give them.
app_start() {
    local try
    for try in 1 2 3; do
        _app_start "$@"
        # A window that came up blank (seen now and then right after another app quit) is started again.
        snap "_started" 0.2
        magick "$WORK/_started.png" -format '%[fx:mean>0.05]' info: | grep -q 1 && return 0
        echo "app_start: blank window, starting again" >&2
        app_stop
        sleep 2
    done
}
_app_start() {
    local data=$1
    shift
    rm -rf "$data"
    mkdir -p "$data"
    cp "$ROOT/tools/record-readme-media/layout-1280.ini" "$data/layout.ini"
    printf '{\n\t"show_welcome": false\n}\n' >"$data/settings.json"
    (cd "$ROOT/docs/wiki" && VATS_HELP_DIR="$ROOT/docs/wiki" vats_env "$VATS" --data-dir "$data" "$@" >"$WORK/app.log" 2>&1) &
    APP_PID=$!
    for _ in $(seq 100); do
        WIN=$(xdotool search --pid "$APP_PID" --onlyvisible 2>/dev/null | head -1)
        [ -n "$WIN" ] && break
        sleep 0.1
    done
    xdotool windowsize "$WIN" 1280 720
    sleep 2.5
    park
}

# snap <name>: the whole window as it is now, for checking a step ($WORK/<name>.png).
snap() { sleep "${2:-0.3}"; ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -video_size 1280x720 -i ":$DISP" -frames:v 1 "$WORK/$1.png"; }

# gif <name> <out.gif> <x> <y> <w> <h> [width]: crop a recording to a panel or the avatar, make a GIF of COLORS
# colours (default 64) at FPS (default 12), then tools/optimize-wiki-gif.sh keeps it within the help's cap.
# The tutorials share a media budget of about 2 MB, so they use fewer colours than the optimizer's first try.
gif() {
    local name=$1 out=$2 x=$3 y=$4 w=$5 h=$6
    ffmpeg -loglevel error -y -i "$WORK/$name.mkv" -vf "crop=$w:$h:$x:$y" -c:v libx264rgb -preset ultrafast -qp 0 \
        "$WORK/$name-crop.mkv"
    _small_gif "$name" "$out" "${7:-0}"
}
_small_gif() {
    COLORS=${COLORS:-64} to_gif "$WORK/$1-crop.mkv" "$WORK/$1-small.gif" "${FPS:-12}" "$3" "${SS:-0}" "${DUR:-}"
    SS=0 DUR= FPS=${FPS:-12} "$ROOT/tools/optimize-wiki-gif.sh" "$WORK/$1-small.gif" "$2"
}

# gif_row <name> <out.gif> <x> <y> <w> <h> <row-y> [width]: as gif, with the Properties row at row-y (the field a
# drag changes) under the crop, so the picture shows the value with the motion.
gif_row() {
    local name=$1 out=$2 x=$3 y=$4 w=$5 h=$6 row=$7
    ffmpeg -loglevel error -y -i "$WORK/$name.mkv" -filter_complex \
        "[0]crop=$w:$h:$x:$y[a];[0]crop=300:26:976:$((row - 13)),pad=$w:26:(ow-iw)/2:0:color=0x1f2126[b];[a][b]vstack" \
        -c:v libx264rgb -preset ultrafast -qp 0 "$WORK/$name-crop.mkv"
    _small_gif "$name" "$out" "${8:-0}"
}

# Shared steps of the Industry preset's UI in the recorded layout.
filter() { scroll 30 4; click 110 68; sleep 0.35; key ctrl+a; sleep 0.2; key BackSpace; sleep 0.2; xdotool type --delay 60 -- "$1"; sleep 0.5; }  # the Bones or Inventory filter
scroll() { xdotool mousemove 110 300 click --repeat "$1" "${2:-5}"; sleep 0.3; }  # the left panel: 5 down, 4 up
menu() { click "$1" 12; sleep 0.4; click $(($1 + 80)) "$2"; sleep 0.3; }          # menu <bar-x> <item-y>
tools() { menu 320 "$1"; }                                                        # a Tools menu item by its row
field() { click "$1" "$2"; xdotool click 1; sleep 0.3; key ctrl+a; xdotool type -- "$3"; key Return; sleep 0.4; }  # double-click, type
select_none() { menu 269 104; }

# listing <example.vat> <name> <out.gif> [args...]: the example filmed by --listing (File > Export Listing Media...,
# 512 x 512, turntable), headless, then slowed to 12 fps and scaled to LISTING_W (default 320) px.
listing() {
    local vat=$1 name=$2 out=$3
    shift 3
    rm -rf "$WORK/data-$name"
    (cd "$ROOT/docs/wiki" && vats_env "$VATS" --data-dir "$WORK/data-$name" --size 1200x1000 "examples/$vat" "$@" \
        --listing "$WORK/$name-listing.gif" --screenshot "$WORK/$name-shot.png" >"$WORK/app.log" 2>&1)
    local fps
    fps=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$WORK/$name-listing.gif")
    ffmpeg -loglevel error -y -i "$WORK/$name-listing.gif" -vf "setpts=PTS*($fps)/12,fps=12,scale=${LISTING_W:-320}:-1:flags=lanczos" \
        -c:v libx264rgb -preset ultrafast -qp 0 "$WORK/$name-crop.mkv"
    _small_gif "$name" "$out" 0
}

# The selected bone's name as Properties > Bone shows it, read with tesseract (empty when it is not installed).
bone_shown() {
    command -v tesseract >/dev/null || return 0
    ffmpeg -loglevel error -y -f x11grab -video_size 1280x720 -i ":$DISP" -frames:v 1 "$WORK/name.png"
    magick "$WORK/name.png" -crop 200x18+984+84 +repage -resize 300% "$WORK/name-big.png"
    tesseract "$WORK/name-big.png" - --psm 7 2>/dev/null | awk '{print $1}'
}

# pick <bone> <row>: select a bone in the Bones tab by filtering for its name and clicking its row (the filtered
# tree's rows start at 187, 21 px apart; past 400 the list is scrolled to the end first), then check that
# Properties shows it. Three tries, then the script stops: a step that picked the wrong bone would key it.
pick() {
    local name=$1 row=$2 got
    for _ in 1 2 3; do
        click 34 35; sleep 0.3; filter "${name#m}"
        if [ "$row" -gt 400 ]; then scroll 15; click 170 407; else click 150 "$row"; fi
        sleep 0.4
        got=$(bone_shown)
        # OCR mixes up l, I and 1, so those are left out of the comparison
        if [ -z "$got" ] || [ "$(tr -d 'lI1' <<<"$got")" = "$(tr -d 'lI1' <<<"$name")" ]; then return 0; fi
        echo "pick $name: Properties shows '$got', trying again" >&2
    done
    echo "pick $name failed" >&2
    exit 1
}

# verify <data-dir> <example.vat>: waits for the app's next autosave (up to 2.5 minutes; NO_VERIFY=1 skips it) and
# compares its keys with the tutorial's example (compare-keys.py): the page's steps, followed, must make the example.
verify() {
    [ -n "${NO_VERIFY:-}" ] && return 0
    local mark f=""
    mark=$(date +%s)
    for _ in $(seq 150); do
        f=$(ls -t "$1"/autosave/*.vat 2>/dev/null | head -1)
        [ -n "$f" ] && [ "$(stat -c %Y "$f")" -gt "$mark" ] && break
        f=""
        sleep 1
    done
    [ -n "$f" ] || { echo "verify: no autosave came" >&2; return 1; }
    python3 "$TUT/compare-keys.py" "$f" "$ROOT/docs/wiki/examples/$2" || echo "verify: the followed steps differ from $2" >&2
}
