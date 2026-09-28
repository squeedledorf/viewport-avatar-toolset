#!/usr/bin/env bash
# Optimises the help wiki's PNG images in place (docs/wiki/STYLE.md, Images):
#   1. at most 1200 px wide (scaled down, aspect kept);
#   2. reduced to a 256-colour palette: pngquant, or Pillow's quantizer (built on libimagequant, pngquant's
#      library), or ImageMagick;
#   3. recompressed losslessly: oxipng, or optipng.
#
#   tools/optimize-wiki-images.sh [file.png ...]    default: every PNG under docs/wiki/images
#   tools/optimize-wiki-images.sh --install-oxipng  fetches the pinned static oxipng build into tools/bin (no root)
#
# Steps 1 and 2 need Pillow (python3 -c "import PIL") or ImageMagick (magick). Without oxipng or optipng the
# lossless pass is skipped with a warning; the size check (vats_tests, wiki_image_budget) still applies.
set -euo pipefail
VATS="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OXIPNG_VERSION=10.2.1
OXIPNG_SHA256=1813750ef592c5350ca79c88f98b2c0876d05c826dc7156245256d4255c1ad17  # x86_64-unknown-linux-musl tarball

if [[ "${1:-}" == --install-oxipng ]]; then
    name="oxipng-$OXIPNG_VERSION-x86_64-unknown-linux-musl"
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    curl -fsSL -o "$tmp/o.tgz" "https://github.com/oxipng/oxipng/releases/download/v$OXIPNG_VERSION/$name.tar.gz"
    echo "$OXIPNG_SHA256  $tmp/o.tgz" | sha256sum -c --quiet
    tar -xzf "$tmp/o.tgz" -C "$tmp"
    mkdir -p "$VATS/tools/bin"
    install -m 755 "$tmp/$name/oxipng" "$VATS/tools/bin/oxipng"
    echo "Installed $("$VATS/tools/bin/oxipng" --version) in tools/bin (git-ignored)"
    exit 0
fi

files=("$@")
if ((${#files[@]} == 0)); then
    mapfile -t files < <(find "$VATS/docs/wiki/images" -name '*.png' | sort)
fi
((${#files[@]})) || { echo "No images to optimise"; exit 0; }

have() { command -v "$1" >/dev/null 2>&1; }
OXIPNG="$(command -v oxipng || true)"
[[ -z "$OXIPNG" && -x "$VATS/tools/bin/oxipng" ]] && OXIPNG="$VATS/tools/bin/oxipng"
PIL=0
python3 -c "from PIL import features; assert features.check('libimagequant')" 2>/dev/null && PIL=1
if ((!PIL)) && ! have magick; then
    echo "Needs Pillow with libimagequant (python3 -m pip install pillow) or ImageMagick" >&2
    exit 1
fi
[[ -n "$OXIPNG" ]] || have optipng ||
    echo "warning: no oxipng or optipng; skipping lossless recompression (run $0 --install-oxipng)" >&2

for f in "${files[@]}"; do
    before=$(stat -c %s "$f")
    # 1 and 2: width and palette.
    if have pngquant; then
        if ((PIL)); then
            python3 -c "import sys; from PIL import Image
im = Image.open(sys.argv[1])
if im.width > 1200: im.convert('RGBA').resize((1200, round(im.height * 1200 / im.width)), Image.LANCZOS).save(sys.argv[1])" "$f"
        else
            magick "$f" -resize '1200x>' "$f"
        fi
        # 98: the result would be larger, 99: under the quality floor; both keep the file as it is.
        pngquant --quality 65-95 --skip-if-larger --strip --force --output "$f" -- "$f" || { rc=$?; ((rc == 98 || rc == 99)); }
    elif ((PIL)); then
        python3 -c "import sys; from PIL import Image
im = Image.open(sys.argv[1]).convert('RGBA')
if im.width > 1200: im = im.resize((1200, round(im.height * 1200 / im.width)), Image.LANCZOS)
im.quantize(256, method=Image.Quantize.LIBIMAGEQUANT).save(sys.argv[1], optimize=True)" "$f"
    else
        magick "$f" -resize '1200x>' -strip -colors 256 "PNG8:$f"
    fi
    # 3: lossless.
    if [[ -n "$OXIPNG" ]]; then
        "$OXIPNG" -q -o 4 --strip safe "$f"
    elif have optipng; then
        optipng -quiet -o5 -strip all "$f"
    fi
    echo "$(realpath --relative-to="$VATS" "$f"): $before -> $(stat -c %s "$f") bytes"
done
