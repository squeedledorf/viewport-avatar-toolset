# Viewport Avatar Toolset - helpers for the tutorial recordings on top of tools/record-readme-media/lib.sh. Source this
# file after lib.sh.
# Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.

# app_start under the name the tutorial scripts use (lib.sh's app_start tries again when SDL cannot open the display).
start_app() { app_start "$@"; }

# motion_start <recording> [lead]: seconds into $WORK/<recording>.mkv where the picture first changes, less lead
# (default 0.1 s), for a GIF that starts with the motion however long the app took to play (0 when it never stills).
motion_start() {
    local t
    t=$(ffmpeg -hide_banner -i "$WORK/$1.mkv" -vf freezedetect=d=0.2 -map 0:v -f null - 2>&1 |
        sed -n 's/.*freeze_end: \([0-9.]*\).*/\1/p' | head -1)
    awk -v t="${t:-0}" -v l="${2:-0.1}" 'BEGIN { s = t - l; printf "%.2f", (s > 0 ? s : 0) }'
}

# View menu (the 1600x900 layout): hide the body and hand bone overlays and the centre of mass, so only bodies show.
clean_view() {
    for y in 356 377 552; do qclick 174 12 800 895; sleep 0.4; qclick 238 "$y" 800 895; sleep 0.3; done
}

# gif <recording> <out.gif> [fps] [trim-start] [duration]: a shaded body changes nearly every pixel it covers on every
# frame, so full palettes blow the 450 KB cap; COLORS (default 48) with a coarse ordered dither keeps it small and
# smooth. Fails, as tools/optimize-wiki-gif.sh does, when the result is over 450 KB or wider than 640 px.
gif() {
    COLORS=${COLORS:-48} BAYER=5 to_gif "$WORK/$1.mkv" "$2" "${3:-12}" 0 "${4:-0}" "${5:-}"
    local size width
    size=$(stat -c %s "$2")
    width=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$2")
    echo "$2: $((size / 1024)) KB, $width px wide" >&2
    ((size <= 450 * 1024 && width <= 640)) || { echo "$2 is over 450 KB or 640 px" >&2; return 1; }
}
