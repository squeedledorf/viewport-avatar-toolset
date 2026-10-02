# Features

Everything Viewport Avatar Toolset does in this release, grouped by area. Each line links to the help page that explains it; the same pages are in the app under **Help → Help Contents** (**F1**).

## Posing and IK

- Rotate, move and scale gizmos with local, world and gimbal axes; posing keys the bone at the current frame. [Posing](wiki/posing.md)
- **Auto IK**: drag a joint by its dot, or with the Move tool, and the bones above it turn to follow, keyed as plain rotations; the wheel or **[** and **]** set how many bones follow, and hinges bend only about their axis. [Posing](wiki/posing.md#dragging-a-joint-in-the-air-auto-ik), [IK](wiki/ik.md#auto-ik)
- Pose by dragging the body: the point pressed follows the pointer, the bone picked from the skin's weights; dragging the hips or chest keeps planted feet in place (**Alt** lets them go), and loose parts lag and settle while you drag (**Tools → Follow-Through While Posing**). [Posing](wiki/posing.md#pose-by-dragging-the-body)
- Joint limits: hinge and cone-with-twist limits per joint, saved per body in the project, that stop the gizmo, Auto IK, limb IK and body drags (**Tools → Respect Joint Limits**); edit them with handles in the view (**L**) or in **Properties → Joint Limits**, mirrored. [Joint limits](wiki/joint-limits.md)
- **Rig → Suggest Joint Limits...** proposes limits from templates, the bind pose and a collision sweep, reviewed joint by joint with a test sweep. [Joint limits](wiki/joint-limits.md#reviewing-with-suggest-limits)
- Exact rotation in degrees and position in metres in the Properties panel, with **Animate Position** for bones that normally only rotate. [Posing](wiki/posing.md#typing-exact-values)
- Rotation snapping to a step of 1° to 90° (5° by default). [Posing](wiki/posing.md), [Preferences](wiki/preferences.md)
- Select by click, Shift-click, parent, child and siblings, keyed on frame, all keyed, and by name in the filtered bone list. [Posing](wiki/posing.md#selecting-bones)
- The Picker tab: click a body part on a front, back, hand or face outline to select its bones, coloured by what they hold at the frame. [Picker](wiki/picker.md#picking-bones)
- Selection sets: named groups of bones saved with the project, optionally in the library, recalled with a click. [Picker](wiki/picker.md#selection-sets)
- Body-part right-click menus: key, reset, mirror, copy, paste and save a whole arm, leg, hand or head at once. [Posing](wiki/posing.md#the-right-click-menu)
- Reset the selected bone, the hip position or the whole pose; copy and paste poses. [Posing](wiki/posing.md#resetting-and-copying)
- Live mirror: every gizmo, hand-poser and IK drag also keys the partner on the other side; centre bones optionally kept symmetric. [Posing](wiki/posing.md#mirror-while-posing)
- Scratch pose: edit the pose you see without keying until **Set Key**; leaving the frame asks whether to keep it. [Posing](wiki/posing.md#scratch-pose)
- Propagate Pose writes the current pose onto the next key, the keys in a range, or every later key. [Posing](wiki/posing.md#propagating-a-pose)
- IK for both arms, both legs, every finger, the spine, both hind legs and both wings, with pole targets where the limb has a joint to aim. [IK](wiki/ik.md)
- Switch any limb between IK and FK at any frame, matched so the limb does not jump; the switch is a stepped **IK / FK Blend** curve in the graph. [IK](wiki/ik.md#switching-a-limb-to-ik)
- Full-body reach: an arm or leg target with a **Pull**, dragged out of reach, leans the spine through its IK and moves the hips after it. [IK](wiki/ik.md#full-body-reach)
- IK is baked into ordinary rotation keys on export. [IK](wiki/ik.md#tips-and-tricks)
- **Hold in World**: keep a point still in the world from a frame on. [Hold and bind](wiki/hold-and-bind.md#holding-a-point-in-the-world)
- **Bind**: make a point follow another bone, keeping its distance and angle; **Tools → Bind to...** then a click on the bone in the view. [Hold and bind](wiki/hold-and-bind.md#binding-a-point-to-another-bone)
- Pins on limb ends drive the limb through IK; pins on attachment points move whatever is worn there. [Hold and bind](wiki/hold-and-bind.md#pinning-hands-and-feet)
- Release or delete a pin; edit its range as a band in the graph; offset a pinned point without breaking the pin. [Hold and bind](wiki/hold-and-bind.md#seeing-and-editing-pins)
- **Follow Target (Bake)**: write a key on every frame so one bone follows another over a range. [Hold and bind](wiki/hold-and-bind.md#follow-target-bake)
- **Clean Up Foot Sliding** plants the feet with leg IK: heel and toe contacts held separately, the ground height measured, **Put Feet on the Ground**, and the hips lowered where a leg cannot reach. [Retargeting](wiki/retargeting.md#clean-up-foot-sliding)
- Hand poser: curl and spread each finger, or all four, by dragging a dot, with a ring and the status bar showing the curl, and fingers stopped at their joint limits. [Hand poser](wiki/hand-poser.md)
- Mirror left to right or right to left, mirror selected bones to their partners, flip the whole pose (**Ctrl+Shift+V**), flip the whole animation (**Edit → Flip Animation**), and reverse it. [Mirror, flip and reverse](wiki/mirror-flip-and-reverse.md)
- Every Bento bone group, attachment point and collision volume, shown or hidden per group; bones drawn as sticks and dots over the body that follow the shown body's joints, so a bone inside it can be clicked. [Skeleton](wiki/skeleton.md)
- **Hide Unused Bones** (on by default) shows only the bones a mesh body is weighted to; plain names beside SL's (**mHipLeft** Left Thigh), found by the bone filter. [Skeleton](wiki/skeleton.md#showing-and-hiding-bone-groups)
- Attachment points are bones: key them to wave a worn sword or pass a worn glass. [Skeleton](wiki/skeleton.md#attachment-points)

## Balance

- **View → Centre of Mass** draws the body's centre of mass, dropped onto the ground, against the support polygon of the planted feet: green inside, red when the pose would topple. [Balance](wiki/balance.md#the-centre-of-mass)
- The floor, the soles and the support outline come from the shown body's mesh, and every planted foot counts, a creature's included. [Balance](wiki/balance.md)
- **Auto-Balance** moves the hips over the feet on every frame of a range, the feet held by leg IK, smoothed, with an optional torso counter-lean. [Balance](wiki/balance.md#auto-balance)
- **Jump Arc** keys the hips on a free-fall parabola between a takeoff and a landing frame, with gravity, forward travel and lateral motion settings. [Balance](wiki/balance.md#jump-arc)

## Timeline and graph

- Keys on any frame, for the selection or every visible bone; delete keys per bone or per frame. [Keys and timeline](wiki/keys-and-timeline.md#setting-keys)
- Tween: a slider and a **Shift+E** drag key the selected bones part of the way between their previous and next keys, through IK controls where a limb uses IK; **Relax** pulls existing keys toward the curve around them. [Keys and timeline](wiki/keys-and-timeline.md#tweening-between-keys)
- Blocking: while it is on, every new key is stepped; **Convert Blocking to Spline** smooths them and turns Hold pairs into moving holds. [Keys and timeline](wiki/keys-and-timeline.md#blocking-and-key-tags)
- Key tags Extreme, Breakdown and Hold, drawn on the timeline and in the graph; a Breakdown keeps its share of the time when a neighbouring key moves. [Keys and timeline](wiki/keys-and-timeline.md#blocking-and-key-tags)
- Play, pause, step frames, jump between keys, go to start and end. [Keys and timeline](wiki/keys-and-timeline.md#moving-through-time)
- Frame rate 1–120 fps with **Keep Frame Numbers** or **Keep Timing** when it changes; length up to 3600 frames with a warning past 60 seconds. [Keys and timeline](wiki/keys-and-timeline.md#length-and-frame-rate)
- Loop in and loop out flags on the timeline; ease in and ease out handles. [Keys and timeline](wiki/keys-and-timeline.md#looping)
- Shift-drag frame ranges for time edits, clips and graph selection. [Keys and timeline](wiki/keys-and-timeline.md#picking-a-frame-range)
- Graph editor with rotation, translation, IK blend, pole and pin-offset curves for the selected or all animated bones. [Graph editor](wiki/graph-editor.md)
- Tangent types Auto, Spline, Plateau, Linear, Flat and Stepped; Break and Unify handles; frozen and automatic handles. [Graph editor](wiki/graph-editor.md#shaping-curves-tangents)
- Easing presets between selected keys: Ease In, Out and In-Out as Quad, Cubic or Sine handles, or Back, Elastic and Bounce baked to per-frame keys. [Graph editor](wiki/graph-editor.md#easing-presets)
- Click, Shift-click, Ctrl-click and box selection; drag keys in time and value; scale a selection with an eight-handle box; snap to whole frames. [Graph editor](wiki/graph-editor.md#selecting-and-moving-keys)
- Insert keys by double-click, copy and paste keys, Euler filter, Flip Time and Flip Values. [Graph editor](wiki/graph-editor.md#fixing-and-flipping-curves)
- Filter Curves: One-Euro, Savitzky-Golay or Butterworth over a frame range, previewed live with a ghost of the pose before and each bone's shake before and after. [Graph editor](wiki/graph-editor.md#filtering-curves)
- Simplify Curves: dense baked, captured or filtered keys refitted to a few keys at the turns and inflections, within a rotation and position tolerance, optionally keeping the frames where feet plant and lift. [Graph editor](wiki/graph-editor.md#simplifying-curves)
- Buffer curves: snapshot every curve in grey under the live ones and swap between the two versions. [Graph editor](wiki/graph-editor.md#buffer-curves)
- Posing writes the Euler angles closest to the existing curve so rotations stay continuous past 180°. [Graph editor](wiki/graph-editor.md#posing-writes-keys-to-the-curves)
- Dope sheet: a summary row and one row of key diamonds per body part, opening to one per bone, sharing the graph's selection, clipboard, snap and time range. [Dope sheet](wiki/dope-sheet.md#reading-the-rows)
- In the dope sheet, drag keys in time, stretch a selection with side handles, set tangents, delete, copy and paste across many bones at once. [Dope sheet](wiki/dope-sheet.md#moving-and-stretching-keys)
- Motion paths: the line a selected bone's tip travels through the frames around the current one, or the whole clip, with keyed frames marked and numbered. [Motion paths](wiki/motion-paths.md#showing-a-path)
- Drag a keyed dot on a motion path to move that limb's IK target at that frame. [Motion paths](wiki/motion-paths.md#moving-an-ik-target-from-its-path)
- Onion skin: up to five ghosts before and after, on every Nth frame or keyed frames only, as the body or bones only, wrapping round a loop, saved with the project. [Onion skin](wiki/onion-skin.md)
- Pinned ghosts: keep a frame, another actor's frame or a library pose in view as a violet ghost until you remove it. [Onion skin](wiki/onion-skin.md#pinned-ghosts)
- Undo and redo for every edit to the animation and its props, and **Edit → Undo History** to go back to any step by name. [First steps](wiki/first-steps.md#tips-and-tricks)

## Loop and time tools

- Seam check: a red tick at loop out lists the channels that jump where the loop wraps. [Loop tools](wiki/loop-tools.md#finding-a-seam)
- **Make Loop Seamless** matches value and slope at both ends, with a blend of 0–15 frames. [Loop tools](wiki/loop-tools.md#making-a-loop-seamless)
- **Remove Hip Travel (In Place)** reports the speed it removed; **Add Travel Forward** puts travel back at a chosen speed. [Loop tools](wiki/loop-tools.md#walking-in-place)
- **Start Cycle at Frame N** turns a loop in time so it begins on a chosen pose. [Loop tools](wiki/loop-tools.md#starting-the-cycle-on-another-pose)
- **Find Best Loop Points** lists the frame pairs whose poses and speeds match best, and sets the loop from one of them. [Loop tools](wiki/loop-tools.md#finding-the-best-loop-points)
- **Fit Loop to Beats** stretches the loop to a number of beats of the audio's BPM, reporting the residual per loop and a frame rate that puts beats on whole frames. [Loop tools](wiki/loop-tools.md#fitting-a-loop-to-the-beat)
- Loop-aware tangents: the keys at loop in and loop out take their slope across the seam, and the graph draws the loop repeated faintly beyond it. [Loop tools](wiki/loop-tools.md#loop-aware-tangents)
- Treadmill: ground lines scroll under an in-place cycle at Second Life's walk, run, crouch-walk or fly speed or a custom one, with the stride, cycle and implied speed measured from the foot contacts, and **Match Cycle to Speed**. [Loop tools](wiki/loop-tools.md#walking-on-a-treadmill)
- Insert empty frames, remove a range, stretch or squash a range to a new length. [Time editing](wiki/time-editing.md)
- Copy a range and paste it over, paste inserting, or paste mirrored; on the selected bones or the whole animation, with pins, loop points and IK controls following. [Time editing](wiki/time-editing.md#copying-and-pasting-a-range)
- Retime markers: double-click the ruler to drop a marker and drag it to stretch or squash the keys since the marker before, snapping to beats. [Time editing](wiki/time-editing.md#retiming-with-markers)
- **Split Dance at Beats** cuts a long animation into parts of at most 60 seconds on the beat, saved as projects or exported as numbered `.anim` files. [Time editing](wiki/time-editing.md#split-a-dance)
- **Make Transition** keys the frames between a frame or a library pose and a frame, eased. [Pose library](wiki/pose-library.md#making-a-transition)

## Motion capture

- Live capture over UDP from the VMC protocol (webcam and VR-tracker apps), Rokoko Studio Live (JSON v3) and iFacialMocap. [Motion capture](wiki/motion-capture.md#sources)
- A setup checklist with your address, listening state, other-device access and firewall state; on Linux it can open the port for the local subnet. [Motion capture](wiki/motion-capture.md#the-setup-checklist)
- **Drive the Avatar** shows the incoming motion live without changing the clip. [Motion capture](wiki/motion-capture.md#watching-the-motion)
- Record takes with a countdown, a start frame, punch-in to a frame range, selected body parts only or face only; each take is one undo step. [Motion capture](wiki/motion-capture.md#recording-a-take)
- Rest-pose capture for senders whose T-pose differs, and hip movement scaled to the avatar's leg length. [Motion capture](wiki/motion-capture.md#hip-movement)
- Clean-up: box, One-Euro, Savitzky-Golay or Butterworth smoothing, key reduction, edge blending for punched-in takes, and foot-sliding clean-up with heel and toe contacts; the take's shake reported before and after. [Motion capture](wiki/motion-capture.md#cleaning-up-a-take)
- **Motion Quality** puts numbers on the animation: keys, bytes, jitter, foot slide, hip drift and seam jump, and shows them before and after each clean-up step. [Motion quality](wiki/motion-quality.md#reading-the-numbers)

## Face tracking

- Blendshapes from iFacialMocap or VMC apps become motion on the Bento face bones: jaw, lips, eyelids, brows, cheeks, tongue and eyes. [Face tracking](wiki/face-tracking.md)
- Neutral-face capture so a resting expression records as rest. [Face tracking](wiki/face-tracking.md#setting-the-neutral-face)
- Natural, Subtle and Expressive presets, overall and per-shape strength, eye strength and eye limits. [Face tracking](wiki/face-tracking.md#configuration)
- **Move face bones** off by default so mesh heads keep their own joint positions; on, shapes that move bones are recorded too. [Face tracking](wiki/face-tracking.md#moving-face-bones)
- Head from the iPhone's head tracking, optional. [Face tracking](wiki/face-tracking.md#connecting-ifacialmocap)
- The 52-shape ARKit table with VRM aliases is a data file you can edit. [Face tracking](wiki/face-tracking.md#the-face-table)

## Face animation and lip sync

- Sliders for the 52 ARKit shapes and the VRM presets key the face bones at the frame; the sliders read back from the keys. [Face animation](wiki/face-animation.md#setting-an-expression)
- Save a face pose to the library and apply it like any pose, mirrored too. [Face animation](wiki/face-animation.md#saving-a-face-pose)
- A layer of blinks and eye darts, with a look-at target (a point, a prop, the camera or another actor), baked to keys on the eyes, lids and head, loop-safe. [Face animation](wiki/face-animation.md#blinks-eye-darts-and-a-look-at-target)
- **Look At** turns the selected bones towards a target on every frame; **Look at Partner** in the Actors window aims the head and eyes at another actor. [Face animation](wiki/face-animation.md#looking-at-something)
- Your own heads as editable face tables, chosen for the sliders, the layer and face tracking alike. [Face animation](wiki/face-animation.md#head-and-move-face-bones)
- Export Expression Pack: one short face-only `.anim` per starter expression or face pose, named `<prefix>_<name>`, with its own priority, length, eases and loop, to a folder and optionally the Animations library. [Face animation](wiki/face-animation.md#exporting-an-expression-pack)
- Face poses from a mesh body's shape keys: **Make Face Pose from This Key** fits the Bento face bones to a viseme or expression. [Face animation](wiki/face-animation.md#face-poses-from-shape-keys)
- Lip sync from the loaded audio: loudness opens the jaw and the vowel picks an open, rounded or wide mouth. [Lip sync](wiki/lip-sync.md#from-the-audio)
- Lip sync from a Rhubarb Lip Sync JSON or TSV file, its shapes A–H and X mapped through an editable mouth-shape table. [Lip sync](wiki/lip-sync.md#from-rhubarb-lip-sync)
- The mouth shapes on the timeline, dragged to nudge the timing; the lip sync is added to the expression underneath and can be removed again. [Lip sync](wiki/lip-sync.md#nudging-the-shapes-on-the-timeline)

## Retargeting

- Import `.bvh`, `.gltf`, `.glb` and `.fbx` animations made for other skeletons. [Retargeting](wiki/retargeting.md)
- Rig tables for Mixamo, CMU and Rokoko BVH, the Unreal mannequin, VRM and Unity Humanoid, and Blender Rigify, picked automatically by bone names; add your own as a JSON file. [Retargeting](wiki/retargeting.md#rigs)
- Map bones by hand in the dialog, and save the mapping as a rig table for the next file. [Retargeting](wiki/retargeting.md#save-a-mapping)
- Rest-pose correction, Y-up to Z-up, and hip travel scaled to the SL leg length; **Rest Pose from Frame 0** when the bind pose is missing. [Retargeting](wiki/retargeting.md#rest-pose)
- Fit SL's limits automatically: raise key reduction, lower the frame rate, drop the face, finger tips and toes, in order, until the file is under 250,000 bytes. [Retargeting](wiki/retargeting.md#fit-sls-limits)
- Split a long clip into parts under 60 seconds, or trim it to a range. [Retargeting](wiki/retargeting.md#split-or-trim-a-long-clip)
- **Batch Retarget Folder** converts every motion file in a folder to projects or `.anim` files, with a report per file. [Retargeting](wiki/retargeting.md#retarget-a-whole-folder)
- Plain BVH import for files that already use SL joint names, and BVH export of the animated bones or every Bento bone. [BVH](wiki/bvh.md)

## Couples, groups and clips

- Several actors in one project on one shared timeline with synced playback; add a mirrored partner, a duplicate, a blank actor or one from a file. [Couples and groups](wiki/couples-and-groups.md#add-actors)
- Each actor has a name, colour, body (none, the default body or a mesh body) and a placement relative to the shared sit target, with a move and turn gizmo. [Couples and groups](wiki/couples-and-groups.md#set-up-the-active-actor)
- Load a `.anim`, BVH or project into an actor; the animation is retimed to the scene. [Couples and groups](wiki/couples-and-groups.md#load-an-animation-into-an-actor)
- Bind one actor's hand to another actor's bone; it exports baked. [Couples and groups](wiki/couples-and-groups.md#touch-another-actor)
- Hide, lock, save or export a single actor. [Couples and groups](wiki/couples-and-groups.md#choose-the-actor-to-edit)
- Export one `.anim` per actor plus a `_placement.txt` with a ready-made `llSitTarget` line for each. [Couples and groups](wiki/couples-and-groups.md#export)
- AVsitter2 and nPose V4 lines for every actor from a furniture root offset, to copy or save as text. [Couples and groups](wiki/couples-and-groups.md#sit-systems-avsitter-and-npose)
- Several named clips per project, as takes every actor switches with, each with its own length, loop, priority, eases and export settings. [Clips](wiki/clips.md#add-switch-and-arrange-clips)
- **Export All Clips** and **Upload All Clips** write every clip in one go, with `[CLIP]` in the naming pattern. [Clips](wiki/clips.md#export-every-clip)
- Firestorm AO and ZHAO-II notecards written from the clips' AO states, to copy or save as text. [Clips](wiki/clips.md#write-an-ao-notecard)

## Props and mesh bodies

- Import COLLADA, FBX and glTF meshes as static or rigged props; a starter set of furniture, drinks and hand-held objects ships with VATs. [Props](wiki/props.md)
- Attach a prop to a bone or attachment point, or place it in the world; hand props snap to their grip. [Props](wiki/props.md#add-a-prop-from-the-inventory)
- Position, rotation, scale and visibility per prop; copy SL vectors to and from the clipboard to match the build tools. [Props](wiki/props.md#copy-values-to-and-from-second-life)
- A prop library with thumbnails, variants and renaming. [Props](wiki/props.md#add-a-prop-from-the-inventory)
- **Sit on This**: the Sitting pose onto a seat prop in one step, the hips on the seat and the feet held on the floor. [Props](wiki/props.md#sit-on-a-seat)
- Import a devkit body in several parts (`.dae`, `.fbx`, `.gltf`, `.glb`); VATs remembers where the files are and never copies or uploads them. [Mesh bodies](wiki/mesh-bodies.md#import-a-body)
- Pose on the body's own joint positions so IK and pins reach where the body really is. [Mesh bodies](wiki/mesh-bodies.md#pose-on-the-bodys-proportions)
- Rig axes: the Local gizmo, the rotation readout and Auto IK's hinges follow the body's own bone axes; keys stay in SL's frames. [Mesh bodies](wiki/mesh-bodies.md#rig-axes)
- Bake the export against a mesh body's proportions. [Export to Second Life](wiki/export-to-second-life.md#choose-the-bake-shape)
- View the Linden default (female or male), the classic Female and Male bodies, or the skeleton alone. [Skeleton](wiki/skeleton.md#choosing-a-body)

## Rigging your own models

- **Map Rig to Second Life** maps a model rigged to bones of its own (`.fbx`, `.gltf`, `.glb`, `.dae`) onto SL's skeleton as a mesh body, keeping its joint positions, with a check that shows bones out of place in red. [Rig any model](wiki/rig-any-model.md)
- Rig tables for Mixamo, Rigify, the Unreal mannequin, VRM, CMU and Rokoko, Daz Genesis 8 and 9, Character Creator, 3ds Max Biped, MMD, MPFB and Auto-Rig Pro, and a guess from names, mirrored pairs and chain shapes for any other rig. [Rig any model](wiki/rig-any-model.md#how-the-suggestion-works)
- Quadrupeds with the front legs on the arms or on Bento hind limbs; birds and fish along their spine. [Rig any model](wiki/rig-any-model.md#map-a-four-legged-animal)
- Spare chains: a scarf, ponytail, skirt or cape rides an SL chain the model leaves free, with presets for the next model. [Rig any model](wiki/rig-any-model.md#put-a-scarf-on-a-spare-chain)
- Soft body: collision volumes as shells at SL's size, a weight glow for the hovered or selected joint, a share slider between a volume and its neighbouring joint, and a preview of SL's avatar physics bounce. [Rig any model](wiki/rig-any-model.md#soft-body)
- Parts and shape keys: hide a model's objects and set its shape keys in **Properties → Body**; kept in the project and baked into the rigged export. [Rig any model](wiki/rig-any-model.md#parts-and-shape-keys)
- **Rig a Model from Scratch**: drag markers onto the joints of a model with no skeleton; SL's skeleton is fitted to them, tails, wings, hind limbs, ears and face included, and the mesh weighted by bone heat. [Rig a model from scratch](wiki/rig-from-scratch.md)
- **Paint Weights**: an add, subtract and smooth brush on any rigged mesh body. [Rig a model from scratch](wiki/rig-from-scratch.md#6-touch-up-the-weights)
- **Export Rigged Mesh for SL** writes a rigged COLLADA file the uploader takes, joint positions included, checked against the uploader's rules with one-click fixes, where SL will stand it, and what to set in the uploader. [Rigging for SL without add-ons](wiki/rigging-for-sl-without-add-ons.md#check-and-export)
- The **Joint Offset Inspector** lists each joint's offset from SL's default as the uploader reads it, which positions upload, and snaps float noise. [Joint offset inspector](wiki/joint-offset-inspector.md)

## Dynamics, ragdoll and layers

- Spring chains on tails, ears, hair and wings, and jiggle on collision volumes, with Tail, Ears, Jiggle and Overlap presets. [Dynamics](wiki/dynamics.md)
- Stiffness, damping, drag, gravity and a collision radius per chain; 120 simulation steps per second; loops pre-rolled so the end matches the start. [Dynamics](wiki/dynamics.md#configuration)
- Preview while playing, then bake, re-bake, unbake or bake all chains as undo steps. [Dynamics](wiki/dynamics.md#baking)
- A part on a spare chain (a scarf, hair, a cape) animated from the body's motion in one click. [Dynamics](wiki/dynamics.md#parts-on-spare-chains)
- Idle layer: a slow breath or a faint sway on chosen bones, made to loop cleanly, previewed while playing and baked to keys. [Idle layer](wiki/idle-layer.md#adding-a-layer)
- Overlap: each bone down a keyed chain plays its keys a little later than the one before, with a delay and falloff, loop-safe. [Overlap](wiki/overlap.md#applying-overlap)
- Ragdoll for the whole body or selected bones from a chosen frame, colliding with the ground and props. [Ragdoll](wiki/ragdoll.md)
- Gravity, stiffness, friction, fall direction, and blend in and out; simulate, scrub, then bake. [Ragdoll](wiki/ragdoll.md#configuration)

## Audio

- One audio track per project: WAV, MP3, FLAC or Ogg Vorbis, with its waveform on the timeline and playback in sync. [Audio track](wiki/audio-track.md)
- Scrubbing plays short snippets; slide the audio in time; set the volume. [Audio track](wiki/audio-track.md#sliding-the-audio)
- A BPM beat grid, tapped beat markers, and snapping of the playhead and ranges to beats. [Audio track](wiki/audio-track.md#beats)

## Export and SL limits

- Export `.anim` files, the viewer's own binary format, with values quantised as the viewer quantises them. [Anim format](wiki/anim-format.md#what-vats-writes)
- Import `.anim` files (versions 1.0 and 0.1); an unedited import exports back byte for byte. [Anim format](wiki/anim-format.md#import)
- Clip priority 0–6 and per-bone priority overrides; loop, ease in and out, hand pose and expression in the file header. [Animation priority](wiki/animation-priority.md)
- Naming patterns with name, number, side, actor and clip tokens; count the number up after each export; also save to the Animations library. [Export to Second Life](wiki/export-to-second-life.md#name-the-files)
- Export mirrored, or export both sides at once. [Export to Second Life](wiki/export-to-second-life.md#name-the-files)
- Export for other heights: the animation baked again on SL Default bodies of chosen heights, IK and pins solved on each, with `_H175`-style names. [Export to Second Life](wiki/export-to-second-life.md#export-for-other-heights)
- Key reduction per bone, with rotation and position tolerances, or by one distance anywhere on the body, measured where a left-out key moves the farthest point below the bone; the first, last and keyed frames are always kept. [Export to Second Life](wiki/export-to-second-life.md#reduce-keys)
- Upload meter: the file's bytes against 250,000 and its length against 60 seconds as you edit, split by body part; **Fit to 250 KB** raises the tolerances until it fits, and **Split into Parts** saves consecutive projects when it cannot. [Export to Second Life](wiki/export-to-second-life.md#check-the-upload-size)
- Position keys left out where they would move nothing, so mesh heads and bodies keep their joint positions; optionally leave out bones that do not move. [Export to Second Life](wiki/export-to-second-life.md#positions-that-do-not-move)
- Files the viewer would reject (over 60 seconds, 250,000 bytes or more, nothing keyed) are refused with the reason; other problems export with warnings. [Export to Second Life](wiki/export-to-second-life.md#troubleshooting)
- Deformers: **End at rest**, **Hold without sinking** and **Also export an undeformer**, for animations whose position keys reshape the avatar. [Deformers](wiki/deformers.md)
- **Reset joint positions**: rest position keys for the joints this clip rotates, those other clips move, or joints you pick. [Export to Second Life](wiki/export-to-second-life.md#animations-that-leave-a-pose-behind)
- BVH export lists what BVH cannot carry before it writes. [BVH](wiki/bvh.md#what-bvh-loses)

## Checking the animation

- **Preview as SL Plays It** exports the animation in memory and plays the bytes back on the body, keys on whole frames, rounded and blended as the viewer blends them, with the original as a green ghost. [Preview as SL plays it](wiki/sl-preview.md#turn-the-preview-on)
- A table of the largest difference per bone, in millimetres and degrees, sorted worst first; click a bone to go to its worst frame. [Preview as SL plays it](wiki/sl-preview.md#read-the-table)
- Animation Check: rules for loop seams, hip travel, loop ranges and eases, keys between frames, turns over 90°, priorities, hip and face position keys, bones that never move, eye keys, expression and hand-pose clashes, joint limits, feet off the ground, position keys that make the avatar taller, joint positions left moved or inherited from other clips, upload size and duration, each with a one-click fix where there is one. [Animation check](wiki/animation-check.md#the-rules)
- Body parts passing through each other, found with the ragdoll's capsules on every frame, marked on the timeline and tinted in the view, with **Push Out** through the arm's IK. [Animation check](wiki/animation-check.md#see-where-body-parts-pass-through-each-other)
- The check runs by itself after each edit, with a status-bar badge, and rules can be switched off. [Animation check](wiki/animation-check.md)
- Priority Planner: which animation wins each bone when the clip plays with other `.anim` files or projects, by Second Life's per-bone rule, with the bones tinted by their winner. [Priority planner](wiki/priority-planner.md#read-who-wins)
- The bones the clip loses, grouped by the clip that takes them, and warnings for AO makers about stands at 4 and faces or hands at 5 or 6. [Priority planner](wiki/priority-planner.md#bones-your-clip-loses)

## Reference and listing media

- A PNG reference image behind the avatar as a backdrop or on a plane in the scene, with opacity, scale, offset, flips and a lock to one view, saved with the project. [Reference images](wiki/reference-images.md#placement)
- Numbered picture sequences that follow the timeline at their own frame rate, for tracing a video turned into frames. [Reference images](wiki/reference-images.md#picture-sequences)
- **Export Listing Media** films the animation to an animated GIF or numbered PNG pictures, at a chosen size and frame rate, with an optional turntable. [Listing media](wiki/listing-media.md#settings)

## Projects and files

- `.vat` projects in plain JSON, documented for other tools, with unknown fields preserved. [Project file format](wiki/project-file-format.md)
- Safe saves with a `.bak` of the previous version; autosave every two minutes and recovery after a crash. [Projects and files](wiki/projects-and-files.md#autosave)
- Drop files on the window: projects open, `.anim` and `.bvh` import, meshes become props, glTF opens the retarget dialog, audio loads. [Projects and files](wiki/projects-and-files.md#opening)
- Project and animation libraries in the Inventory, with thumbnails, folders you add, insert-at-frame, rename, duplicate and delete. [Project library](wiki/project-library.md)
- **Insert, Matching Poses** joins an animation onto the end of the clip where the two poses match best, hips aligned and the join blended. [Project library](wiki/project-library.md#inserting-with-matched-poses)
- **Add Community Folder** lists a cloned folder of shared CC0 poses, clips, animations and projects in the Inventory. [Community content](wiki/community-content.md#add-it-to-the-inventory)
- File associations for `.vat` on Linux and Windows, registered from Preferences. [Installation](wiki/installation.md#opening-project-files-by-double-click)
- Command-line options for files, presets, views, lights, a reference image, listing media, tool windows, screenshots and benchmarks. [Command line](wiki/command-line.md)

## Help and presets

- Built-in help with contents and search (**F1**), the same pages as `docs/wiki/`; the Welcome window opens **First Steps** and **Tutorials**. [Interface](wiki/interface.md#help-windows)
- Workspaces (a trial): **Pose**, **Animate**, **Face**, **Rig** and **Export** tabs, each with the panels its job needs and its own layout. [Interface](wiki/interface.md#workspaces)
- The Tab pie: hold **Tab** over the viewport for a ring of tools at the pointer. [Interface](wiki/interface.md#the-tab-pie)
- **Find a Tool** (**F3**) and a search box in every menu, finding any command by name, submenu items included. [Interface](wiki/interface.md#menus)
- **View → Maximise Panel** (**Ctrl+Space**): the panel under the pointer fills the window. [Interface](wiki/interface.md#workspaces)
- **Help → Controls** lists every key and mouse control of the active preset. [Keyboard shortcuts](wiki/keyboard-shortcuts.md)
- Control presets: Second Life (the default), Industry (Maya-style), Blender (with modal G and R and 3-button emulation) and QAvimator. [Control presets](wiki/control-presets.md)
- Pose and clip library with starter hand and body poses, applied plain or mirrored, with a Blend slider afterwards and **Show as Ghost**. [Pose library](wiki/pose-library.md)
- Orthographic view, on **Num 5**, sharing its projection with picking, the gizmo and the ghosts. [Interface](wiki/interface.md#orthographic-view)
- Light menu presets Flat Noon, Three-Quarter Key, Rim / Back, Dusk and Night, and a plain grey backdrop behind the actor. [Interface](wiki/interface.md#menus)
- Dusk and Studio Grey themes, interface size 75–250 %, gizmo size, **Reduce motion** for the camera, panel layout remembered between sessions. [Preferences](wiki/preferences.md)
- Camera views stored per project and in settings; a view cube; framing of the selection or the whole avatar. [Interface](wiki/interface.md)
- Status bar hints for every command, and greyed menu items that say why they are unavailable. [First steps](wiki/first-steps.md#tips-and-tricks)
