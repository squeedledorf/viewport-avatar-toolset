# Recording tutorial GIFs

The help's GIFs (`docs/wiki/STYLE.md`, Animated GIFs) are recorded headless with the README media's helpers,
`tools/record-readme-media/lib.sh`: the app runs on a private Xvfb display with a throwaway data folder,
`xdotool` drives it, `ffmpeg` records a region, and `tools/optimize-wiki-gif.sh` turns the recording into a GIF
within the cap (450 KB, 640 px wide). Nothing reaches your desktop and no settings of yours are read or written.
One script per GIF, kept here, so a UI change is redone by running it again.

Needs `Xvfb`, `xdotool`, `ffmpeg`; `gifsicle` is used when installed.

## A script

`posing-rotate-drag.sh` is a worked example (the head turned with the Rotate tool, for `posing.md`):

```bash
#!/usr/bin/env bash
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
source "$REPO/tools/record-readme-media/lib.sh"
trap cleanup EXIT                        # stops the recorder, the app and Xvfb by their own PIDs
[ -n "${DISPLAY:-}" ] || xvfb_start

app_start "$WORK/data-<name>" "$REPO/docs/wiki/examples/<example>.vat" --frame 12 --select mHead --tool rotate
# ... set the view up (keys, wheel, pan) ...
park; sleep 0.8

rec_start <name> <x> <y> <w> <h>        # the region to record, window pixels
sleep 0.6
drag 545 258 652 256 45 0.03; sleep 0.9  # the action, then about a second holding the result
rec_stop
"$REPO/tools/optimize-wiki-gif.sh" "$WORK/<name>.mkv" "$REPO/docs/wiki/images/<page>/<name>.gif"
```

Run it with the app you are documenting and a scratch folder that is not `/tmp` (the recordings are large):

```
VATS=build/app/vats WORK=~/Projects/.vats-media/tutorial tools/record-tutorial-media/<name>.sh
```

## What lib.sh gives you

- `xvfb_start`: its own display (the first free from `:57`); `DISP` picks one.
- `app_start <data-dir> [args...]`: a fresh `--data-dir` seeded with the recorded window layout, the app under
  `env -u WAYLAND_DISPLAY GDK_BACKEND=x11` on that display, 1280x720 (`SIZE=1600x900` for the larger layout).
  Any option on the help's Command line page (`docs/wiki/command-line.md`) works after the data folder: a project from `docs/wiki/examples/`, `--frame`,
  `--select`, `--tool`, `--open-help`, `--open-menu`, ...
- `click`, `qclick` (click and leave before a tooltip opens), `drag x1 y1 x2 y2 [steps] [delay] [button]` (eased,
  steady timing), `move_to`, `key`, `park` (the pointer to the status bar, out of the picture).
- `rec_start <name> x y w h` / `rec_stop`: a lossless (`libx264rgb -qp 0`) `$WORK/<name>.mkv` of that region, 30 fps, no pointer
  drawn. Show where the pointer acts through what it does (a highlighted ring, a hover), not the cursor.
- `cleanup`: stops everything this script started. Never `pkill` or `killall`.

## Finding coordinates

Start the app the same way, grab one frame and read positions off it:

```
ffmpeg -f x11grab -video_size 1280x720 -i :$DISP+0,0 -frames:v 1 "$WORK/look.png"
```

Then check the finished GIF frame by frame before committing it, for example
`ffmpeg -i out.gif -vf "select='not(mod(n\,10))',tile=5x2" -frames:v 1 contact.png`.

## Rules

- At most 8 s, 12–15 fps (`optimize-wiki-gif.sh` defaults to 15; `FPS=12` for longer clips), 640 px wide,
  450 KB; `vats_tests` (`wiki_images`) enforces the width and size.
- Dusk theme, 100% interface size (the recorded layout's defaults).
- One action per GIF; crop to what changes.

## Tutorials that test themselves

`tutorial-sit-pose.sh`, `tutorial-sip-mug.sh` and `tutorial-walk-cycle.sh` source `lib.sh` here, which adds to the
README helpers: the app in its first-run state, `pick` (select a bone by filtering the Bones tab, then read the
selected name back from **Properties** with `tesseract` and retry on a miss), `field` (type into a box), `menu`,
and `verify`. Each script follows its page's steps literally from a new project, saves a still of each step to
`$WORK`, then waits for the app's autosave and compares its keys with the page's shipped example
(`compare-keys.py`), so a page whose steps no longer build the example fails loudly. Then it records the page's
GIFs from the examples. `ONLY_GIFS=1` skips the steps; `NO_VERIFY=1` skips the comparison. Needs `tesseract`
for `pick`'s check (without it, `pick` trusts the click).
