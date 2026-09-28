# VATs Wiki style guide

The help that ships with VATs (the app and the viewer editor) is a small wiki in the manner of the
Arch Wiki: one topic per page, reference first, dense and exact, written for people who will skim to the
part they need. These rules are for writers; they are not a page of the wiki.

## Pages

- One file per topic in `docs/wiki/`, named in lowercase with hyphens: `graph-editor.md`. The page title
  is the first line, `# Graph editor` (sentence case). The filename is the link target.
- First paragraph: what the thing is and what it's for, in two or three sentences. No "In this article".
- Then a **Related articles** line, right under the intro:
  `> Related articles: [[Keys and timeline]], [[Graph editor]], [[IK]]`
- Standard sections, in this order, using only those that apply:
  `## Usage` (how to do the task, grouped by task in `###` subsections), `## Configuration`,
  `## Tips and tricks`, `## Troubleshooting` (each problem a `###` heading, cause then fix),
  `## See also`. Other `##` sections are fine when the topic needs them (for example `## Format` on a
  file-format page).
- Keep a page to one topic. When a section grows into its own subject, give it its own page and link it.

## Writing

- Plain, exact English. Present tense, active voice, second person only when giving an instruction
  ("Select the bone, then press **S**"). No marketing, no "simply", "just", "easily".
- Say what happens, with real values: "the preview holds the last frame", "SL refuses files of 250,000
  bytes or more", "priority 0–6".
- Every claim must match the code and the app as they are now. Check labels, menus and defaults in the
  source (`ui/`, `app/`, `core/`) before writing them. Never document planned features as present; a planned
  feature may be mentioned once as "not yet available".
- UI names exactly as the app shows them, in bold: **Tools → Loop Tools → Make Loop Seamless**,
  **Mark a Beat Here**. Keys in bold: **Ctrl+Z**. When presets differ, give a small table, or say
  "(Industry preset)" and link [[Keyboard shortcuts]].
- Code, paths, file names and values in backticks: `~/.config/viewport-avatar-toolset/settings.json`.

## Links

- Internal links use double brackets with the page title: `[[Graph editor]]`, or with a section:
  `[[Graph editor#Tangent modes]]`, or with other text: `[[Graph editor|the graph]]`. They resolve to
  the file whose `#` title matches, case-insensitively.
- Link a term the first time it appears on a page, not every time.
- External links only to stable, official pages (Second Life wiki, Linden Lab, tool vendors):
  `[Second Life Wiki: Animation](https://wiki.secondlife.com/...)`.

## Notes, tips and warnings

Use these blocks, one short paragraph each, sparingly:

```
> **Note:** The viewer can't draw extra copies of a worn mesh body, so ghosts there are bones only.
> **Tip:** Run **Remove Hip Travel** before **Make Loop Seamless** on walks.
> **Warning:** Uploading costs L$ and cannot be undone.
```

## Markdown subset (what the help browser renders)

Headings `#`–`###`, paragraphs, `**bold**`, `*italic*`, `` `code` ``, fenced code blocks, bullet and
numbered lists (one level of nesting), simple pipe tables, the three blockquote boxes above, `[[links]]`,
`[text](https://...)` links, images, and example and target links (below). No HTML, no footnotes.

## Images

A screenshot teaches what a paragraph can't: where a panel is, what a state looks like. Use one when it
beats words, not to decorate.

- **When:** show a panel, dialog or state once per task, where the reader first needs it. Don't repeat a
  picture of the same panel on one page, and don't picture what one sentence says ("click **Save**").
- **Crop** to the area the text talks about: the panel or window, not the whole app. A crop to a named
  window is one line in the manifest (below).
- **Theme:** Dusk, the default, unless the page is about themes. 100% interface size.
- **Syntax:** the image alone on its line, then an optional caption line in italics directly under it (no
  blank line between):

  ```
  ![The Graph panel with the elbow's rotation curves](images/graph-editor/graph-panel.png)
  *The elbow at frame 22: Rotate Z (blue) dips to 60° three times.*
  ```

- **Alt text is required:** it says what the picture shows, for search and for when the file is missing
  (the help browser then shows the alt text in a frame).
- **Files:** `docs/wiki/images/<page-file-stem>/<name>.png` (or `.gif`, below), lowercase with hyphens,
  a PNG at most 1200 px wide. The help browser scales a picture down to the text column and shows it at
  the interface size (a 100% screenshot looks like the app).
- **Budget:** at most 250 KB per PNG, 450 KB per GIF, and 24 MB for the whole help folder (pages,
  images and examples). `vats_tests` (`wiki_images`, `wiki_budget`) fails when an image is missing,
  unreferenced, too wide or too big, a GIF does not decode, or the folder is over budget.

### Animated GIFs

A GIF shows an action and what comes of it: a drag on a gizmo, a panel filling in, a pose settling. The
help plays it in place and loops it; a click pauses and resumes it, and it stays still on its first
frame while the help window (or the app) is not focused.

- **GIF or PNG:** a GIF when the reader has to see something *move* to follow the text (a drag, the
  order of clicks, a result that plays out over time). A PNG for anything that holds still: a panel, a
  dialog, a state, a result you can see in one frame. When in doubt, a PNG.
- **Syntax:** as for a PNG, alt text required, optional caption: `![Dragging the blue ring turns the
  head](images/posing/rotate-drag.gif)`. The alt text says what happens, not just what is there; the
  viewer shows it in place of the GIF until its host can draw GIFs.
- **Length and rate:** at most 8 s, 12–15 fps. Start just before the action, end once the result has
  held for about a second (the loop's pause). One action per GIF.
- **Size:** at most 640 px wide and 450 KB. Crop hard to the area that changes: a small crop at 100%
  reads better than a big one scaled down, and costs less.
- **Making one:** record headless, as the README media are (`tools/record-tutorial-media/README.md`,
  worked example `tools/record-tutorial-media/posing-rotate-drag.sh`), then

  ```
  tools/optimize-wiki-gif.sh recording.mkv docs/wiki/images/<page>/<name>.gif [width]
  ```

  It takes a video or a GIF, makes the palette in two passes (ffmpeg `palettegen` / `paletteuse`, the
  README media's `to_gif`), runs `gifsicle -O3` when it is installed, and steps down (fewer colours and a coarser dither,
  12 fps, then narrower but not under 320 px unless nothing else fits) until the file fits 450 KB; it fails when nothing fits. `FPS`, `SS` and `DUR`
  set the rate and trim the recording. Never capture your own desktop.

### Making screenshots

Never capture your own desktop: every picture comes from `docs/wiki/images/shots.json`, so a UI change
is redone with one command:

```
tools/wiki-shots.sh              # every shot; tools/wiki-shots.sh graph-editor for some
```

Each entry names the output, the app's command line (a project from `examples/`, `--frame`, `--select`,
`--window`, `--open-help`, ...; see [[Command line]]), and optionally the theme, the window size
(default 1200x1000) and a crop: a window's title (`"crop": "Graph"`) or a pixel rectangle
`[x, y, w, h]`. The script runs the app on its own Xvfb display at 100% scale with a throwaway
`--data-dir`, crops, optimises and writes the PNG. Needs `Xvfb`, `python3` and Pillow or ImageMagick;
set `VATS_BIN` when the app is not in `build/` or `build-release/`.

Every image goes through `tools/optimize-wiki-images.sh` (the shot script runs it; run it yourself on
anything added another way). It scales to 1200 px wide, reduces to a 256-colour palette (`pngquant`, or
Pillow's libimagequant quantizer, or ImageMagick) and recompresses losslessly (`oxipng`, or `optipng`).
None of these needs root: `tools/optimize-wiki-images.sh --install-oxipng` puts the pinned static
`oxipng` build in `tools/bin/`, which is not committed.

## Worked examples

A small project the reader opens and plays with, to learn a task hands-on:

```
[Open the example](example:graph-basics.vat)
```

- The help browser shows the link as a button. It opens the project as **File → Open** would (asking to
  save changes first), as an untitled copy: **Save** asks for a new name, so the shipped file never
  changes.
- Files live in `docs/wiki/examples/`, named after the task. One example teaches one thing; say in the
  text what to look at ("select **mElbowRight**").
- Build examples with code, not by hand: `tools/make_wiki_examples.cpp` (`vats_make_wiki_examples
  docs/wiki/examples`) writes them through the core's project saver. Add a function there for a new one.
- Examples count towards the 24 MB budget. `vats_tests` checks that every example link has its file and
  every example loads.

### Targets

A tutorial teaches by direct manipulation: dragging the gizmo, the keys on the timeline and the graph's handles,
not typing exact values. So pair each example with a target, the project as the step should leave it, and let
the reader match it by eye:

```
[Open the example](example:posing-head-turn.vat) [Show the target](target:target-head-turn.vat)
```

- The help browser shows a target link as a button labelled with the link text, like an example link. It loads
  that project from `examples/` as the [[Target ghost]]: a see-through green body over the avatar at the same
  frame, with the status bar saying how far the selected bone is from it. The open project is not touched.
- Give the step's end state as the target, and say in the text which bone to select and which ring or handle to
  drag, and what "matched" looks like ("drag the blue ring until the distance turns green"). Typed values are a
  cross-check afterwards, not the method.
- A target is an example like any other: built in `tools/make_wiki_examples.cpp`, named after the step
  (`<task>-<step>.vat`), counted in the budget, and checked by `vats_tests` (every `target:` link has its file,
  and it loads).

## Example animations

A tutorial's example is the reader's model of good animation: it must be something an animator would ship, not
just keys that make the page's point. Before a page, its example or its GIF goes in:

- **Look at every frame from three views.** Render strips with `vats_example_review <example>.vat <out>`
  (`--step 2`, views `front`, `side` and `q34`; `--follow 1` for a walk that travels, `--near 0.2` for a close-up
  of a grip, `--body male` for the other default body) and look at them, not only at the numbers. Check both
  default bodies, female and male, whenever a prop or a foot contact is involved.
- **Forward is +X.** A walk, a step or a lunge moves the body forward, and a planted foot moves backwards
  relative to the hips while it is on the ground (on the spot: the ground runs back under it). A foot that stays
  down while the leg swings forward, or lifts while the leg pushes back, is a moonwalk: the swinging leg bends at
  the knee and clears the ground, the planted one is nearly straight.
- **Contact.** A planted foot stays put in the world (the metrics file's ankle and toe columns: within about 1 cm
  over the stance); no foot sinks more than a centimetre below the ground or floats above it; a walk's heel
  strikes, the foot rolls flat, and it pushes off over the toes.
- **Weight and balance.** The hips sit over the supporting foot or between the feet (the centre-of-mass dot stays
  green, see [[Balance]]); they drop at a heel strike and rise over the planted foot; a strike moves the weight
  onto the front foot.
- **Natural ranges.** No joint goes past what a body can do (the **Animation Check**'s joint limits rule: none on
  the example); knees and elbows only bend forwards; forearms turn palm up or down, wrists bend, they do not
  spin.
- **Props in the hand.** The grip sits in the fist (a starter prop at its **props.json** offset, the hand in
  the starter pose its `hand_pose` names, **Grip (Cylinder)** for most), fingers wrapped round it, not floating
  beside it or through it. No prop passes into the body on any frame: `vats_example_review` reports the frames with a prop inside the body mesh, and the answer
  must be 0 on both bodies. A blade's follow-through carries it past and away from the body, not through a leg.
- **Motion.** Arcs, not straight lines, where a real limb swings (check with a motion path); the body leads and
  the hand follows (hips, then chest, then arm, then wrist); ease in and out except at a hit; no twinning (left
  and right do not mirror each other exactly); overlap on loose parts.
- **References.** Take angles and timing from real motion: published gait data for walks (hip, knee and ankle
  angles over the cycle), real technique for fighting and weapon handling (a one-handed cut, a pistol stance, a
  rifle's butt in the shoulder and the cheek on the stock). Free motion capture such as the CMU Graphics Lab
  database may be looked at for angles; never ship it.
- **Second Life's speeds.** A walk plays at SL's 3.20 m/s, far faster than a person walks: match stride and cycle
  to it (**View → Treadmill → SL Walk**: the planted feet keep pace with the lines, the cycle reads about 100%).
- **The first GIF shows it at its best:** a three-quarter view that shows what the page is about (the prop, the
  feet), the whole body in the picture, a walk on the treadmill so it reads as forward. Look at the finished GIF
  frame by frame (`tools/record-tutorial-media/README.md`).
- **The page stays true.** Every value the reader types, and every number the page quotes, is the example's;
  after changing an example, update the page, re-shoot its pictures and follow it once in the app.

## Tutorials teach by doing

A reader learns the software by using it the way an animator does: with the mouse, in the view, the picker, the
timeline, the dope sheet and the graph. A tutorial that has them type the example's numbers teaches typing. Tutorials
that walk through several key poses to show a mechanism (contact, passing and up; anticipation, strike and
follow-through; blocking, then spline) are good: the reader builds each of those poses by hand.

- **Posing:** select by clicking the bone in the view, its dot in the [[Picker]], or a group label (**R ARM**,
  **HEAD**); a box drag on empty view space selects several. Turn with the Rotate tool's rings and say which ring,
  which way and roughly how far: "drag the blue ring until the face turns towards the shoulder, about a third of the
  way". Move IK handles and pins by dragging them. A starter pose or an earlier example is a fine place to begin.
- **Timing:** drag the playhead to a frame (name the frame so the reader can find it, "to 18, where the arms' keys
  are"), drag keys on the timeline and in the dope sheet, box-select them, retime with the dope sheet's scale
  handles, and scrub to check.
- **Curves:** drag keys and tangent handles in the graph, and use its tangent buttons.
- **Targets are pictures.** Show the pose or motion to aim for (a PNG or a GIF), say it in plain words with a
  tolerance ("the hand reaches about chin height"), and where matching a pose matters, add a target link beside
  the example's (**Targets**, above): the status bar's **Target** chip then says `N° away` for the
  selected bone, green under 5°. It measures each bone against its parent, so have the reader match from the hips
  outward. Exact values go only in a short **Check** note for readers who want to verify, marked as
  approximate: "> **Check:** about 30° on Rotation Z; anything from 25° to 35° reads the same."
- **Typing is right where the software is about values:** a BPM, a frame rate, a priority, an export name, the
  dynamics sliders (their comparison GIFs are the lesson), the ease times in **Properties**.
- **Show the drag.** At least one GIF on each tutorial shows the actual hand work: a ring being dragged, a key
  moving along the timeline, a tangent handle in the graph.
- **Follow the page by dragging** in the app (headless, `tools/record-tutorial-media/`) before it goes in. The
  result has to look like the example, not match it to the decimal. Where a drag to the described target is hard
  (a ring seen edge-on, a handle hidden behind the body), change the view in the steps, or report the UI problem.
- Keep the craft: the **Why** notes, the menu and tool names, the keyboard shortcuts as the other way to do it, and
  the **Check your result** and **Troubleshooting** sections, as things to look at.

## App and viewer

VATs runs as the standalone app and inside an SL viewer (the VATs Editor). Where they differ,
say so in a short **Note** or a two-column table ("App" / "Viewer"). Viewer-only topics live on their
own pages.

## Categories

End each page with one line naming its category, used by the help browser's contents:
`Category: Getting started` — one of **Getting started**, **Interface**, **Animating**, **Motion**,
**Import and export**, **Second Life**, **Viewer**, **Reference**, **Troubleshooting**.

Within a category the contents list pages by title. A page that belongs at a set place adds an
`Order: <n>` line after its category (`Order: 3`); pages with one come first, lowest first. The
**Getting started** pages all have one, and the tutorials' numbers follow the list in **Tutorials**
(a test checks it): a new tutorial takes the next number there and the ones after it move up.
