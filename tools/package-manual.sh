#!/usr/bin/env bash
# Packs the manual for offline reading: every page of docs/wiki but the tutorials (which ship with the app), with
# their pictures, as the app's Help reads them. Attach the result to each GitHub release as vats-manual.tar.gz:
# Help > Download the Manual fetches it from the newest release.
#
# Usage: tools/package-manual.sh [docs/wiki of the public copy]   (default: the staged copy, tools/stage-public.sh)
# Output: dist/vats-manual.tar.gz
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="${1:-$HOME/Projects/vats-public-staging/docs/wiki}"
out="$here/dist/vats-manual.tar.gz"
mkdir -p "$here/dist"
files=()
cd "$src"
for page in *.md; do
    stem="${page%.md}"
    case "$stem" in tutorial*|STYLE|README|hex*-import) continue ;; esac
    files+=("$page")
    [ -d "images/$stem" ] && files+=("images/$stem")
done
tar -czf "$out" --owner=0 --group=0 --numeric-owner "${files[@]}"
echo "$out: $(tar -tzf "$out" | grep -c '\.md$') pages, $(du -h "$out" | cut -f1)"
