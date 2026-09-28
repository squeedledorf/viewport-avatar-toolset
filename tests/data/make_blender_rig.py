# Writes tests/data/blender_rig.fbx: a tiny synthetic rigged mesh the way Blender makes one, for the loader
# tests (Blender convention: the avatar faces -Y, bones point along their own Y, default FBX export).
# Run: blender -b --factory-startup --python tests/data/make_blender_rig.py -- tests/data/blender_rig.fbx
import sys
import bpy

out = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)

# SL rest joints (SL axes: +X forward, +Y left) turned into Blender's (the avatar faces -Y, left is +X).
def b(sl):
    x, y, z = sl
    return (y, -x, z)

joints = {  # name: (head, tail, parent) in SL axes
    "mPelvis": ((0, 0, 1.067), (0, 0, 1.151), None),
    "mTorso": ((0, 0, 1.151), (-0.015, 0, 1.365), "mPelvis"),
    "mChest": ((-0.015, 0, 1.365), (-0.015, 0, 1.586), "mTorso"),
    "mShoulderLeft": ((-0.036, 0.164, 1.521), (-0.036, 0.412, 1.521), "mChest"),
    "mElbowLeft": ((-0.036, 0.412, 1.521), (-0.036, 0.617, 1.521), "mShoulderLeft"),
    "mShoulderRight": ((-0.036, -0.164, 1.521), (-0.036, -0.412, 1.521), "mChest"),
    "mHead": ((-0.025, 0, 1.683), (-0.025, 0, 1.83), "mChest"),
}
arm_data = bpy.data.armatures.new("Armature")
arm = bpy.data.objects.new("Armature", arm_data)
bpy.context.scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="EDIT")
for name, (h, t, p) in joints.items():
    e = arm_data.edit_bones.new(name)
    e.head, e.tail = b(h), b(t)
    if p:
        e.parent = arm_data.edit_bones[p]
bpy.ops.object.mode_set(mode="OBJECT")

# One small triangle per joint, 5 cm along its bone, weighted fully to it.
verts, faces, groups = [], [], []
for name, (h, t, _) in joints.items():
    d = [(t[i] - h[i]) for i in range(3)]
    n = sum(x * x for x in d) ** 0.5
    c = [h[i] + d[i] / n * 0.05 for i in range(3)]
    base = len(verts)
    for off in ((0.01, 0, 0), (0, 0.01, 0), (0, 0, 0.01)):
        verts.append(b([c[i] + off[i] for i in range(3)]))
    faces.append((base, base + 1, base + 2))
    groups.append((name, [base, base + 1, base + 2]))
mesh = bpy.data.meshes.new("Body")
mesh.from_pydata(verts, [], faces)
body = bpy.data.objects.new("Body", mesh)
bpy.context.scene.collection.objects.link(body)
for name, idx in groups:
    body.vertex_groups.new(name=name).add(idx, 1.0, "REPLACE")
body.parent = arm
body.modifiers.new("Armature", "ARMATURE").object = arm

for o in (arm, body):
    o.select_set(True)
bpy.ops.export_scene.fbx(filepath=out, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                         bake_anim=False, apply_scale_options="FBX_SCALE_ALL")
print("wrote", out)
