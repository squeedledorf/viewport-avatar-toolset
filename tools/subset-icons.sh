#!/usr/bin/env bash
# Rebuilds app/assets/fonts/lucide-icons.ttf: the Lucide icon font cut down to the glyphs ui/icons.h names.
#
#   tools/subset-icons.sh [lucide-version]
#
# Needs curl and fontTools' pyftsubset (pacman -S python-fonttools, apt install fonttools, or pip install
# fonttools). Downloads lucide-static from npm through jsDelivr, keeps the U+ codepoints listed in
# ui/icons.h and refreshes the licence file. See app/assets/fonts/ICONS.md.
set -euo pipefail
VATS="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-1.48.0}"
FONTS="$VATS/app/assets/fonts"
command -v pyftsubset >/dev/null || { echo "pyftsubset not found: install fonttools" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
BASE="https://cdn.jsdelivr.net/npm/lucide-static@$VERSION"
curl -fsSL "$BASE/font/lucide.ttf" -o "$TMP/lucide.ttf"
curl -fsSL "$BASE/LICENSE" -o "$FONTS/Lucide-ISC.txt"

UNICODES="$(grep -o 'U+[0-9A-F]\{4,5\}' "$VATS/ui/icons.h" | sort -u | paste -sd, -)"
[[ -n "$UNICODES" ]] || { echo "no U+ codepoints in ui/icons.h" >&2; exit 1; }
# No hinting (ImGui's rasteriser ignores it), no names or layout tables: just the outlines.
pyftsubset "$TMP/lucide.ttf" --unicodes="$UNICODES" --output-file="$FONTS/lucide-icons.ttf" \
    --no-hinting --desubroutinize --layout-features='' --name-IDs='0,1,2,3,4,5,6'
echo "lucide-icons.ttf: Lucide $VERSION, $(tr ',' '\n' <<<"$UNICODES" | wc -l) glyphs, $(stat -c %s "$FONTS/lucide-icons.ttf") bytes"
