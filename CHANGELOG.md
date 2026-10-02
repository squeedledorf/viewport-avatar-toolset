# Changelog

All notable changes to Viewport Avatar Toolset are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-10-02

### Added

- Auto IK: drag any joint by the dot that appears on it, or with the Move tool, and the bones above it turn to follow, keyed as plain rotations with no IK target left behind. The mouse wheel or **[** and **]** take one bone more or fewer up the chain; elbows, knees, hind legs, wing and finger joints bend only about their hinge. On by default (**Tools → Auto IK**).
- Posing by dragging the body: press any part of the avatar or a mesh body (the bone picked from its skin weights) and the point pressed follows the pointer. Dragging the pelvis, torso or chest moves the hips with the planted feet held by Auto IK; **Alt** during the drag lets the feet go with the body. Tails, wings, ears and soft parts lag and settle while you drag (**Tools → Follow-Through While Posing**); only the dragged bones are keyed.
- Joint limits: hinge, and cone-with-twist limits per joint, saved in the project per body, that stop the Rotate gizmo, Auto IK, limb IK and body drags at the limit (**Tools → Respect Joint Limits**, on by default). Edit them with handles in the view (**L**) or in **Properties → Joint Limits**, mirrored to the other side; **Rig → Suggest Joint Limits...** proposes limits from templates, the body's bind pose and a collision sweep, to review joint by joint with a test sweep before applying. Exports bake IK within the limits, as the view shows it. Limits never change the `.anim` format.
- Map Rig to Second Life (**Rig** menu): a model rigged to bones of its own (`.fbx`, `.gltf`, `.glb` or `.dae`) is mapped onto SL's skeleton and becomes a mesh body, keeping its own joint positions. Rig tables for Mixamo, Rigify, the Unreal mannequin, VRM, CMU and Rokoko, Daz Genesis 8 and 9, Character Creator, 3ds Max Biped, MMD, MPFB and Auto-Rig Pro, and a guess from names, mirrored pairs and chain shapes for the rest; quadrupeds (front legs on the arms or on Bento hind limbs), birds and fish; a check that shows bones out of place in red; mappings saved beside the model. A CC0 example mech is included.
- Spare chains: a scarf, ponytail, skirt or cape rides an SL chain the model leaves free (wings, tail, hind limbs, groin, spine, tongue, ears), with presets for the next model and a one-click baked swing from the body's motion in **Tools → Dynamics**.
- Soft body: collision volumes drawn as shells at SL's size, a weight glow showing which skin the hovered or selected joint carries (**View → Bones → Show Weights of Selected**), a share slider moving flesh between a volume and the joint beside it, and **Tools → Avatar Physics Preview**, which bounces BELLY, BUTT and the breasts as SL's avatar physics does, with SL's settings and presets in **Tools → Dynamics** and an optional bake.
- Rig a Model from Scratch (**Rig** menu): drag markers onto the joints of a model with no skeleton; VATs fits SL's skeleton to them, including tails, wings, hind limbs, ears and the Bento face, and weights the mesh by bone heat. **Paint Weights** touches up the weights of any rigged mesh body with an add, subtract and smooth brush: a click on a bone picks it, a drag on the body paints it, and with no bone picked a click on the body picks the bone carrying that spot. Paint Weights, Rig from Scratch and Map Rig open in the **Rig** workspace, and leaving it turns the brush off.
- Export Rigged Mesh for SL (**File** menu): writes the mesh body shown as a rigged COLLADA file the SL uploader takes, joint positions included, after checking it against the uploader's rules, with one-click fixes, where SL will stand it, and what to set in the uploader. Options for a bind pose only, standing an A-posed humanoid in SL's rest pose, shape-proof joint positions and the pelvis offset. `vats_rig_export`, built from source, does the same from the command line.
- Joint Offset Inspector (**Rig** menu): each joint's offset from SL's default as the uploader reads it, which positions will upload, and a snap for float noise.
- Rigged glTF and GLB import for mesh bodies and props, and rig axes: a body's own bone axes for the Local gizmo, the rotation readout and Auto IK's hinges.
- Mesh parts and shape keys: each object of a body's files is a part you can hide, and each shape key (glTF morph targets, FBX blend shapes, COLLADA morphs) a slider, in **Properties → Body** (every workspace) and **Inventory → Bodies**, and the parts in **View → Body**; kept in the project and the mapping, and baked into the rigged export as shown.
- Face poses from shape keys: right-click a face key's slider and choose **Make Face Pose from This Key** to fit the Bento face bones to it, saved as a face pose.
- Deformers: **End at rest**, **Hold without sinking** (`mSkull` counter-keys that keep the avatar's height) and **Also export an undeformer** in the export settings, for animations whose position keys reshape the avatar.
- **Reset joint positions** in the export settings: rest position keys for the joints this clip rotates, for those other clips move, or for joints you pick, so a stand does not inherit a walk's moved joints.
- Animation Check rules: position keys that make the avatar taller (and sink it in-world), joint positions an animation leaves moved when it ends, and joint positions inherited from other clips, each with a fix.
- Workspaces (a trial): **Pose**, **Animate**, **Face**, **Rig** and **Export** tabs in the menu bar, each with only the panels that job needs and its own remembered layout, and **All** for the full layout (**View → Workspaces**, **Preferences → Look → Layout**).
- The Tab pie: hold **Tab** over the viewport for a ring of eight tools around the pointer (Set Key, IK / FK, Rotate, Limits, Auto IK, Move, Select and a second ring), on the bone under the pointer.
- **Find a Tool** (**F3**) and a search box at the top of every menu, finding any command or tool window by name, submenu items included, with the menu each is in.
- **Edit → Flip Animation**: left and right swapped on every key of the animation.
- **Edit → Undo History**: a window listing the undo steps by name; click one to go back or forward to it.
- SoapStorm's VATs Editor: **Viewer → Inventory** opens the viewer's own Inventory window inside the editor, to wear and take off objects (no editing).
- **Sit on This** (**Properties → Prop**, **Tools → Sit on Seat**): the Sitting pose onto a seat prop in one step, the hips on the seat and the feet held on the floor.
- **Tools → Bind to...**: pick the point, then click the bone it rides in the view.
- **View → Maximise Panel** (**Ctrl+Space**): the panel under the pointer fills the window, and again to put it back.
- **View → Bones → Hide Unused Bones** (on by default): with a mesh body, only the bones it is weighted to; the groups a body uses switch on by themselves.
- Plain bone names beside SL's (**mHipLeft** Left Thigh) in the bone list and the status bar, and found by the bone filter (**View → Bones → Plain Names**).
- Hold **Right Alt** to see each toolbar button's key on a badge; **Previous Key** and **Next Key** arrows beside **Set Key**.
- **Preferences → Camera → Reduce motion**: camera moves jump instead of gliding.
- The Hand Poser shows each finger's curl as a ring and in the status bar, and with **Respect Joint Limits** on stops fingers at their joint limits.
- Loop Assist says when the loop already joins cleanly.
- Hovering a red bone or a red timeline mark says why it is red: the body parts that pass through each other on that frame, with a pointer to the Animation Check.
- The Welcome window has **First Steps** and **Tutorials** buttons.
- Help pages: Deformers, Joint limits, Joint offset inspector, Rig any model, Rig a model from scratch, and Rigging for SL without add-ons.
- Command-line options for screenshots and tests of the new tools: `--workspace`, `--pie`, `--mesh-body`, `--import-body`, `--map-rig`, `--rig-scratch`, `--export-rig`, `--physics`, `--sit`, `--bone-filter` and others.

### Changed

- The workspace tabs are on for everyone. An installation from before this release gets them on **All**, so its layout stays as it was.
- New installations start with the Second Life control preset. An existing settings file keeps its preset.
- The beginner tutorials (First steps, Your first pose, Timing and spacing, A breathing idle, A sit pose for furniture) are rewritten to better reflect user experience. Some images and examples continue to use the old bone examples and will be replaced in a future revision.
- Bones are drawn as sticks and dots over the body that follow the shown body's own joints, the only bone style. **View → Bones → Bones in Front (X-ray)** became **Collision Volumes in Front (X-ray)**, since bones are always in front.
- Face moves are sized for the head the export plays on, the same in face tracking, the Face sliders, expression packs and lip sync. The face table no longer turns the lip corners, the brows pitch, and a pucker pushes the lips forward. **Move face bones** is set in the Face window; Motion Capture shows it.
- The interface: one shared look for sliders, headers, buttons and rows; rounded panels; a viewport with no tab that nothing docks over; Properties sections per workspace with **Loop in** and **Loop out** beside **Loop**; short one-line window intros; Preferences in **Look** and **Behaviour** groups; Motion Capture opening with **Listen** and folding its setup checklist once data arrives. The **Find** menu is gone (**F3** and the menu search replace it).
- The export dialog and panel keep the name, number, side, pattern, folder and bake shape at the top, fold the rest into **Also Write** and **Clean Up**, and start with the clip's **Priority**. The button is **Export .anim**.
- **Flip Pose** is **Ctrl+Shift+V** in every preset; **Esc** is **Select None** in every preset, and in the Second Life preset resets the camera when nothing is selected. **Esc** closes Help.
- In text fields **Ctrl+A**, **Ctrl+C**, **Ctrl+V** and **Ctrl+X** always edit the text, and a double-click selects the whole number.
- Arrow keys and **Alt** no longer move a keyboard focus box around the editor; keyboard navigation is kept for menus and popups.
- Typing or dragging a bone value in **Properties** says in the status bar what it keyed.
- Onion-skin ghosts wrap round a loop, and **Every** shows its number.
- The graph's tangent buttons show their names when the toolbar has room, and the **Animate** workspace gives the graph and dope sheet at least half the window's height.
- Rig a Model from Scratch ticks **Tail**, **Wings**, **Hind limbs** and **Ears** when the model's shape clearly has them, and keeps marker labels inside the view.
- **Bind to...** on an arm or a leg binds its hand or foot.
- The Contrapposto starter pose no longer trips the Animation Check's self-contact rule, and the priority advice tells an AO's stand or walk to set its AO state instead.
- **Treadmill → Stretch Time** names the new length before the click.
- Motion Quality's **Shake** is called **Jitter**.
- Joint names are read as SL's viewer reads them: SL's names and LL's aliases exactly, then collision volumes, so a game rig named `Head` or `Chest` goes to Map Rig instead of loading on fitted-mesh volumes.
- Balance takes its floor, soles and support outline from the shown body's mesh, and every planted foot counts, a creature's included.
- **Hip and leg position keys** in the Animation Check no longer flag the spine bones, which Second Life's height never reads.

### Fixed

- Large mesh bodies no longer slow the editor down: checks that scanned every vertex each frame now run once per body, the floor contact reuses the frame's skinning, and skinning a big body uses several cores.
- Windows: settings, the pose library and the prop library saved only once; every later save failed silently. A user folder with non-ASCII characters works too.
- A mesh body whose face bones were bound in Blender's bone axes, and its body bones in SL's, imported with a deformed head.
- A straight leg switched to IK bent its knee inward when the feet were stepped apart.
- In the Hand Poser a long drag down a finger bent it backwards instead of holding a full curl.
- The Paint Weights brush stayed armed while its window was behind another Rig tab.
- After a file dialog closed, the editor window did not get the keyboard back until clicked.
- `--theme` was saved into the settings, so later runs kept it.
- Long status-bar messages covered the hints and the Check badge, and message dialogs ran off the window at large interface sizes.
- Bone groups switched on for one mesh body stayed on for the next.
- SoapStorm's VATs Editor: the editor's menus and popups draw over the viewer's own windows, such as chat.
- SoapStorm's VATs Editor: in **As It Plays In-World** the editor's ground sit no longer lowers the hips (the avatar stands up meanwhile), and a swapped body stands on the ground on its own feet whatever shape you wear.

## [0.1.1] - 2026-09-29

### Added

- SoapStorm's VATs Editor: **View > Body** can show a rigged mesh body (a devkit, or any rigged .dae or .fbx, a creature too) in your avatar's place, on your screen only. Your avatar, its attachments and its name tag are hidden on your screen; nothing is sent. The body poses on its own joint positions, and **Bake shape > Your avatar** exports against it, position keys included.
- **View > Body > Keep in Real-Avatar Modes** (on by default): As It Plays In-World, Test as My Walk / Run and Place on Furniture Point keep the swapped body, moving as your avatar really does (the region's and your AO's animations, the walk, the sit) on its own proportions. Turn it off to see your real avatar in those modes.

### Fixed

- Rigged meshes with their own proportions (joints far from SL's, as on a creature) imported smaller than Second Life shows them: when no unit fitted, the rig was scaled to the median ratio of its joint distances. They now keep the unit the file declares, as Second Life does. DAE and FBX.

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

[0.2.0]: ../../releases/tag/v0.2.0
[0.1.1]: ../../releases/tag/v0.1.1
[0.1.0]: ../../releases/tag/v0.1.0
