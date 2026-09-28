#!/usr/bin/env bash
# Makes a help GIF (docs/wiki/STYLE.md, Images) from a screen recording or a GIF, within the cap: at most 450 KB and
# 640 px wide. The conversion is the README media's (tools/record-readme-media/lib.sh, to_gif: ffmpeg palettegen /
# paletteuse), then gifsicle -O3 when it is installed. When the result is over 450 KB it tries again with fewer
# colours and a coarser Bayer dither, then 12 fps, then narrower (not under 320 px unless nothing else fits), and
# fails if nothing fits.
#
#   tools/optimize-wiki-gif.sh in.mp4|in.mkv|in.gif out.gif [width]   width: default and most 640, never enlarged
#
# Environment: FPS (default 15), SS and DUR (trim: start and length in seconds), CAP_KB (default 450). Needs ffmpeg and ffprobe; gifsicle
# is optional (any user install on PATH, no root).
set -euo pipefail
in=${1:?usage: $0 in.mp4|in.gif out.gif [width]} out=${2:?usage: $0 in.mp4|in.gif out.gif [width]}
CAP=$((${CAP_KB:-450} * 1024))  # CAP_KB: a smaller budget for pages with many GIFs
[ -f "$in" ] || { echo "No such file: $in" >&2; exit 1; }

src_w=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$in" | head -1)
width=${3:-640}
((width > 640)) && width=640
((src_w > 0 && width > src_w)) && width=$src_w
dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$in" 2>/dev/null || true)
awk -v d="${DUR:-${dur:-0}}" 'BEGIN { exit !(d > 8.05) }' && echo "warning: longer than 8 s; trim it (DUR=8) or split it" >&2

tmp=$(mktemp -d "$(dirname "$out")/.optimize-gif.XXXXXX")  # beside the output, not /tmp
trap 'rm -rf "$tmp"' EXIT
WORK=$tmp OUT=$tmp
# shellcheck source=record-readme-media/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/record-readme-media/lib.sh"  # to_gif

try() {  # try <fps> <width> <colours> <bayer-scale>: true when it fits
    COLORS=$3 BAYER=$4 to_gif "$in" "$tmp/try.gif" "$1" "$2" "${SS:-0}" "${DUR:-}"
    command -v gifsicle >/dev/null && gifsicle -O3 "$tmp/try.gif" -o "$tmp/try.gif"
    local size
    size=$(stat -c %s "$tmp/try.gif")
    echo "  ${1} fps, ${2} px, ${3} colours, Bayer $4: $((size / 1024)) KB" >&2
    ((size <= CAP))
}

fps=${FPS:-15}
# Fewer colours and a coarser dither cost less than a narrower picture, so they come first; the width steps down
# last and stays at least 320 px (or the source's width, when narrower) unless nothing else fits.
min_w=$((width < 320 ? width : 320))
widths=() narrow=()
for p in 100 85 70 55; do
    w=$((width * p / 100))
    if ((w >= min_w)); then widths+=("$w"); else narrow+=("$w"); fi
done
((widths[-1] > min_w)) && widths+=("$min_w")
for w in "${widths[@]}" "${narrow[@]}"; do
    ((w < min_w)) && echo "  nothing fits at $min_w px or wider; going narrower" >&2
    for f in "$fps" 12; do
        ((f > fps)) && continue
        for cb in "256 4" "128 4" "64 4" "64 5" "48 5" "32 5"; do  # colours, Bayer scale (5: coarsest)
            if try "$f" "$w" ${cb% *} ${cb#* }; then
                mv "$tmp/try.gif" "$out"
                echo "$out: $(($(stat -c %s "$out") / 1024)) KB, ${w} px wide, $f fps" >&2
                exit 0
            fi
        done
        [ "$f" = 12 ] && break
    done
done
echo "Could not fit $in in 450 KB; shorten it (DUR), crop it closer, or keep less of the screen moving" >&2
exit 1
