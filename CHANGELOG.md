# Changelog

All notable changes to Viewport Avatar Toolset are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2026-09-28

Initial public release, for Linux (x86_64) and 64-bit Windows.

### Added

- Posing of the full Second Life Bento skeleton: 133 joints, 47 attachment points and 26 collision volumes, with rotate, move and scale gizmos in local, world and gimbal axes, exact values in the Properties panel, and body-part menus.
- Posing assists: live mirroring to the other side, a scratch pose that keys nothing until Set Key, Propagate Pose onto later keys, and a Picker tab that selects bones on an outline of the body, with selection sets saved in the project or the library.
- IK for the arms, legs, fingers, spine, hind legs and wings, switchable to FK per frame without a jump, with pole targets; full-body reach, where an arm or leg target with a Pull leans the spine and moves the hips when dragged out of reach.
- Pins: hold a point in the world or bind it to another bone, driven through IK on limb ends; Follow Target (Bake).
- Foot-sliding clean-up with heel and toe contacts held separately, a measured ground height, Put Feet on the Ground, and the hips lowered where a leg cannot reach.
- Hand poser; mirror, flip and reverse; copy and paste poses; a Blend slider after applying a pose; Make Transition between a frame or a library pose and a frame.
- Timeline with keys, loop and ease markers and frame ranges; a Tween slider and Shift+E drag for breakdowns, with Relax; Blocking, which keys stepped, and Convert Blocking to Spline with moving holds; Extreme, Breakdown and Hold key tags, Breakdowns keeping their share of the time when a neighbouring key moves.
- Graph editor with Auto, Spline, Plateau, Linear, Flat and Stepped tangents, broken and unified handles, box select, scaling, an Euler filter, time and value flips, easing presets (Quad, Cubic, Sine, Back, Elastic, Bounce), Filter Curves (One-Euro, Savitzky-Golay, Butterworth), Simplify Curves and buffer curves.
- Dope sheet: one row of key diamonds per body part, opening to one per bone, sharing the graph's selection, clipboard and time range; move, stretch, set tangents on, copy and delete keys across many bones.
- Motion paths: the arc a bone's tip travels through the frames around the current one, with keyed frames marked and IK targets draggable from the path.
- Onion skinning with ghosts before and after the current frame, by step or keyed frames, as the body or bones; pinned ghosts of a frame, another actor's frame or a library pose.
- Loop tools: seam check, Make Loop Seamless with a blend, Remove Hip Travel, Add Travel Forward, Start Cycle at Frame, Find Best Loop Points, Fit Loop to Beats, loop-aware tangents, and a treadmill at Second Life's walk, run, crouch and fly speeds with the cycle measured from the foot contacts.
- Time editing: insert, remove, stretch, copy, paste, paste inserting and paste mirrored on frame ranges; retime markers dragged on the ruler; Split Dance at Beats into parts of at most 60 seconds, saved as projects or exported together.
- Audio track (WAV, MP3, FLAC, Ogg Vorbis) with waveform, offset, volume, BPM grid, tapped beats and beat snapping.
- Motion capture over UDP from the VMC protocol, Rokoko Studio Live and iFacialMocap, with live preview, countdown, punch-in, body-part and face-only takes, rest-pose capture, smoothing (box, One-Euro, Savitzky-Golay or Butterworth), key reduction, edge blending and foot-sliding clean-up.
- Motion Quality: keys, bytes, shake, foot slide, hip drift and seam jump, before and after each clean-up step.
- Face tracking from ARKit and VRM blendshapes onto the Bento face bones, with a neutral-face capture, presets, per-shape strengths, eye limits and an editable mapping table.
- Face animation by hand: sliders for the 52 ARKit shapes and the VRM presets, face poses in the library, a layer of blinks, eye darts and a look-at target baked to keys, a Look At tool, Look at Partner for couples, and heads as editable face tables.
- Lip sync from the loaded audio (loudness and vowel formants) or from Rhubarb Lip Sync's JSON or TSV, keyed additively through an editable mouth-shape table, with the shapes on the timeline to nudge.
- Expression packs: one short face-only `.anim` per starter expression or face pose, named by a prefix, with its own priority, length, eases and loop, to a folder and optionally the Animations library.
- Retargeting of BVH, glTF, GLB and FBX animations from Mixamo, CMU and Rokoko, the Unreal mannequin, VRM and Unity Humanoid and Rigify rigs, with rest-pose correction, manual mapping saved as a rig table, automatic fitting to SL's size limit, and splitting or trimming of long clips; Batch Retarget Folder writes a whole folder as projects or `.anim` files with a report.
- BVH import for files using SL joint names, and BVH export of the animated or all Bento bones.
- Dynamics: spring chains and collision-volume jiggle with Tail, Ears, Jiggle and Overlap presets, live preview and baking.
- Idle layer: loop-safe breathing and sway on chosen bones, previewed while playing and baked as keys; Overlap: follow-through down a keyed chain by a per-bone delay and falloff.
- Ragdoll for the whole body or selected bones, colliding with the ground and props, with blend in and out and baking.
- Balance: the centre of mass drawn over the planted feet's support polygon; Auto-Balance moves the hips over the feet across a range with the feet held by IK and an optional torso counter-lean; Jump Arc keys the hips on a gravity parabola between takeoff and landing.
- Couples and groups: several actors on one shared timeline, per-actor body, colour and sit-target placement, cross-actor binds, loading animations into actors, one `.anim` per actor with a placement note, and AVsitter2 and nPose V4 lines with a furniture root offset.
- Clips: several named clips per project as takes every actor switches with, each with its own settings; Export All Clips and Upload All Clips with `[CLIP]` in the naming pattern; Firestorm AO and ZHAO-II notecards written from the clips' AO states.
- Props from COLLADA and FBX, static or rigged, with a starter set of 30 props (furniture, cups, handheld items and a CC0 weapon pack: sword, greatsword, knife, spear, axe, staff, bow, shield, pistol, rifle, shotgun, magazine), each gripped by a matching hand pose, attachment to bones and points, and SL vector copy and paste.
- Picker: a clean 2D body chart (Body, Hands, Face, Extras) with joint dots and bone lines, whole-group selection from labels, fingertips, knuckle rows and face chips, fitted to tall and short shapes; an optional backdrop of the rendered avatar with live pose on or off.
- Box selection of bones in the viewport, with each control preset's modifiers; bone glyphs drawn as solids, with rings where Second Life's Bento spine bones fold back on themselves.
- Target ghost: a see-through copy of another animation over the avatar, following the timeline, with how far the selected bone is from it.
- Fitting stances: eleven symmetric starter poses for fitting clothes and checking rigs.
- Keyboard Shortcuts editor with search, conflict warnings and per-command reset, over any control preset; the Second Life preset's camera ported from the viewer's own Alt-camera.
- Tutorials in the help: 26 step-by-step lessons (beginner, routine and advanced, including weapon holds, a sword swing, walk and run cycles, turns, a jump, a hug, a dance and four on dynamics), each with pictures, GIFs, a finished example and a target to match. The help plays animated GIFs and docks as a tab.
- VTube Studio and Live Link Face iPhone receivers, and Rokoko face data; the help lists the VMC and phone apps that already work (SlimeVR, XR Animator, VirtualMotionCapture, VSeeFace, Waidayo, AndroidMoCap).
- Mesh bodies imported from devkits for preview and as the export bake shape.
- `.anim` export matching the viewer's quantisation, with per-bone priority, hand pose, expression, loop and ease, naming patterns, mirrored copies, key reduction per bone or by a world-space distance anywhere on the body, and refusal of files the viewer would reject; `.anim` import of versions 1.0 and 0.1.
- Upload meter: the file's bytes against 250,000 and seconds against 60 as you edit, split by body part, with Fit to 250 KB and Split into Parts.
- Export for other heights: every file baked again on SL Default bodies of chosen heights, with `_H175`-style names and a per-height section in the placement note.
- Preview as SL Plays It: the exported bytes played back on the body, the original as a green ghost, and a table of the largest difference per bone.
- Animation Check: rules for loop seams, hip travel, loop ranges and eases, sub-frame keys, turns over 90 degrees, priorities, hip and face position keys, frozen bones, eye keys, expression and hand-pose clashes, joint limits, feet off the ground, body parts passing through each other, upload size and duration, each with a one-click fix where there is one; a status-bar badge.
- Priority Planner: which animation wins each bone when the clip plays with other `.anim` files or projects, bones tinted by the winner, the bones the clip loses, and warnings for AO makers.
- Reference images: a PNG or a numbered PNG sequence that follows the timeline, as a backdrop or a plane in the scene, with opacity, scale, offset, flips and a view lock, saved with the project.
- Listing media: the animation filmed to an animated GIF or PNG pictures, at a chosen size and frame rate, with an optional turntable.
- Pose and clip library with starter hand and body poses, Show as Ghost, and matched-pose insertion that joins a clip where the poses match best; project and animation libraries in the Inventory; community folders of shared CC0 content added to the Inventory in one step.
- Orthographic view; Light menu presets (Flat Noon, Three-Quarter Key, Rim / Back, Dusk, Night) and a plain backdrop.
- Control presets: Industry (Maya-style), Blender, QAvimator and Second Life.
- Built-in help with contents and search, a controls window, two themes, interface scaling, autosave with crash recovery, file associations, and command-line options for files, presets, views, lights, reference and listing media, tool windows, screenshots and benchmarks.

[0.1.0]: ../../releases/tag/v0.1.0
