// glTF 2.0 rigged-mesh import (spec 08 RG-1: stock Blender writes glTF now). The files are built here: a rig in glTF's
// Y-up axes, a skinned tetrahedron per joint, a data: buffer.
#include <cmath>
#include <cstring>
#include <map>

#include "check.h"
#include "fixtures.h"
#include "vats/dae.h"
#include "vats/gltf_mesh.h"
#include "vats/json.h"

using namespace vats;

namespace {

std::vector<Xform> rest_globals() {
    const Skeleton& s = skel();
    std::vector<Xform> rest = s.global_pose(Pose(s.size()));
    rest.push_back({});
    for (auto& v : s.volumes()) rest.push_back(rest[v.joint] * Xform{v.rot, v.pos});
    return rest;
}

double dist(const Vec3& a, const Vec3& b) { return (a - b).length(); }

// ---- glTF fixtures: a rig in glTF's Y-up axes with a skinned mesh, written as a .gltf with a data: buffer ----

std::string base64(const std::vector<std::uint8_t>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < in.size(); i += 3) {
        unsigned v = unsigned(in[i]) << 16 | (i + 1 < in.size() ? unsigned(in[i + 1]) << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
        o += t[v >> 18], o += t[(v >> 12) & 63];
        o += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        o += i + 2 < in.size() ? t[v & 63] : '=';
    }
    return o;
}

// SL (Z up, +X forward) to glTF (Y up): (x, y, z) -> (x, z, -y), the inverse of the importer's turn.
Vec3 to_gltf(const Vec3& v) { return {v.x, v.z, -v.y}; }
Quat to_gltf(const Quat& q) { return {q.w, q.x, q.z, -q.y}; }

struct GltfJoint {
    std::string name;
    int parent;  // index into the list
    Xform global;  // in glTF axes
};

struct GltfBuilder {
    std::vector<std::uint8_t> bin;
    Json accessors = Json::array(), views = Json::array();
    // A node above the armature and the mesh (an exporter's "Z_UP"): the joints' globals and the vertices are then in
    // the space below it, the skin's space, and the scene puts them through it.
    const Xform* above = nullptr;

    int accessor(const std::vector<float>& f, const char* type, int width, bool as_u16 = false) {
        Json view = Json::object();
        view.set("buffer", 0);
        view.set("byteOffset", int(bin.size()));
        if (as_u16) {
            for (float x : f) {
                std::uint16_t u = std::uint16_t(x);
                bin.push_back(std::uint8_t(u & 0xff)), bin.push_back(std::uint8_t(u >> 8));
            }
            while (bin.size() % 4) bin.push_back(0);
        } else {
            for (float x : f) {
                std::uint8_t b[4];
                std::memcpy(b, &x, 4);
                bin.insert(bin.end(), b, b + 4);
            }
        }
        view.set("byteLength", int(as_u16 ? f.size() * 2 : f.size() * 4));
        views.push(std::move(view));
        Json a = Json::object();
        a.set("bufferView", int(views.arr.size()) - 1);
        a.set("componentType", as_u16 ? 5123 : 5126);
        a.set("count", int(f.size() / width));
        a.set("type", type);
        accessors.push(std::move(a));
        return int(accessors.arr.size()) - 1;
    }

    // Joints as nodes (globals given; locals derived), a skin with inverse binds, and one mesh: a tetrahedron per
    // joint weighted to it. skin = false writes the same mesh unskinned at a node with a translation.
    std::string build(const std::vector<GltfJoint>& joints, bool skin = true, double size = 0.05, bool ibms = true) {
        std::vector<float> pos, nrm, uv, jn, wt, idx;
        std::vector<float> ibm;
        for (size_t j = 0; j < joints.size(); ++j) {
            const Xform& g = joints[j].global;
            const Vec3 corners[4] = {{0, 0, 0}, {size, 0, 0}, {0, size, 0}, {0, 0, size}};
            const std::uint32_t base = std::uint32_t(pos.size() / 3);
            for (const Vec3& c : corners) {
                const Vec3 p = g.apply(c);
                pos.insert(pos.end(), {float(p.x), float(p.y), float(p.z)});
                nrm.insert(nrm.end(), {0, 1, 0});
                uv.insert(uv.end(), {0.25f, 0.25f});
                jn.insert(jn.end(), {float(j), 0, 0, 0});
                wt.insert(wt.end(), {1, 0, 0, 0});
            }
            for (std::uint32_t t : {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u}) idx.push_back(float(base + t));
            // Inverse bind: column-major inverse of the global.
            const Xform inv = g.inverse();
            const Vec3 c[3] = {inv.rot.rotate({1, 0, 0}), inv.rot.rotate({0, 1, 0}), inv.rot.rotate({0, 0, 1})};
            for (int col = 0; col < 3; ++col) ibm.insert(ibm.end(), {float(c[col].x), float(c[col].y), float(c[col].z), 0});
            ibm.insert(ibm.end(), {float(inv.pos.x), float(inv.pos.y), float(inv.pos.z), 1});
        }
        Json doc = Json::object();
        Json asset = Json::object();
        asset.set("version", "2.0");
        doc.set("asset", std::move(asset));
        Json nodes = Json::array();
        for (size_t j = 0; j < joints.size(); ++j) {
            const Xform local = joints[j].parent >= 0 ? joints[joints[j].parent].global.inverse() * joints[j].global : joints[j].global;
            Json n = Json::object();
            n.set("name", joints[j].name);
            Json t = Json::array(), r = Json::array();
            t.push(local.pos.x), t.push(local.pos.y), t.push(local.pos.z);
            r.push(local.rot.x), r.push(local.rot.y), r.push(local.rot.z), r.push(local.rot.w);
            n.set("translation", std::move(t));
            n.set("rotation", std::move(r));
            Json children = Json::array();
            for (size_t k = 0; k < joints.size(); ++k)
                if (joints[k].parent == int(j)) children.push(int(k));
            if (!children.arr.empty()) n.set("children", std::move(children));
            nodes.push(std::move(n));
        }
        Json mesh_node = Json::object();
        mesh_node.set("name", "Body");
        mesh_node.set("mesh", 0);
        if (skin) mesh_node.set("skin", 0);
        else {
            Json t = Json::array();
            t.push(0.0), t.push(1.0), t.push(0.0);
            mesh_node.set("translation", std::move(t));
        }
        nodes.push(std::move(mesh_node));
        doc.set("nodes", std::move(nodes));
        Json attrs = Json::object();
        attrs.set("POSITION", accessor(pos, "VEC3", 3));
        attrs.set("NORMAL", accessor(nrm, "VEC3", 3));
        attrs.set("TEXCOORD_0", accessor(uv, "VEC2", 2));
        if (skin) {
            attrs.set("JOINTS_0", accessor(jn, "VEC4", 4, true));
            attrs.set("WEIGHTS_0", accessor(wt, "VEC4", 4));
        }
        Json prim = Json::object();
        prim.set("attributes", std::move(attrs));
        prim.set("indices", accessor(idx, "SCALAR", 1, true));
        prim.set("material", 0);
        prim.set("mode", 4);
        Json prims = Json::array();
        prims.push(std::move(prim));
        Json mesh = Json::object();
        mesh.set("name", "Body");
        mesh.set("primitives", std::move(prims));
        Json meshes = Json::array();
        meshes.push(std::move(mesh));
        doc.set("meshes", std::move(meshes));
        if (skin) {
            Json sk = Json::object();
            Json jl = Json::array();
            for (size_t j = 0; j < joints.size(); ++j) jl.push(int(j));
            sk.set("joints", std::move(jl));
            if (ibms) sk.set("inverseBindMatrices", accessor(ibm, "MAT4", 16));
            Json skins = Json::array();
            skins.push(std::move(sk));
            doc.set("skins", std::move(skins));
        }
        Json mat = Json::object(), pbr = Json::object(), colour = Json::array();
        colour.push(0.2), colour.push(0.4), colour.push(0.8), colour.push(1.0);
        pbr.set("baseColorFactor", std::move(colour));
        mat.set("name", "Blue");
        mat.set("pbrMetallicRoughness", std::move(pbr));
        mat.set("doubleSided", true);
        Json mats = Json::array();
        mats.push(std::move(mat));
        doc.set("materials", std::move(mats));
        Json scene = Json::object(), roots = Json::array();
        for (size_t j = 0; j < joints.size(); ++j)
            if (joints[j].parent < 0) roots.push(int(j));
        roots.push(int(joints.size()));
        if (above) {
            Json n = Json::object(), t = Json::array(), r = Json::array();
            n.set("name", "Z_UP");
            t.push(above->pos.x), t.push(above->pos.y), t.push(above->pos.z);
            r.push(above->rot.x), r.push(above->rot.y), r.push(above->rot.z), r.push(above->rot.w);
            n.set("translation", std::move(t));
            n.set("rotation", std::move(r));
            n.set("children", std::move(roots));
            Json* nodes = doc.find("nodes");
            nodes->push(std::move(n));
            roots = Json::array();
            roots.push(int(nodes->arr.size()) - 1);
        }
        scene.set("nodes", std::move(roots));
        Json scenes = Json::array();
        scenes.push(std::move(scene));
        doc.set("scenes", std::move(scenes));
        doc.set("scene", 0);
        Json buffer = Json::object();
        buffer.set("byteLength", int(bin.size()));
        buffer.set("uri", "data:application/octet-stream;base64," + base64(bin));
        Json buffers = Json::array();
        buffers.push(std::move(buffer));
        doc.set("buffers", std::move(buffers));
        doc.set("bufferViews", views);
        doc.set("accessors", accessors);
        return write_json(doc);
    }
};

// SL joints as glTF joints: parents by the SL hierarchy among the names given, globals from `globals` (SL space).
std::vector<GltfJoint> gltf_joints(const std::vector<std::string>& names, const std::vector<Xform>& globals,
                                   const std::map<std::string, std::string>& rename = {}) {
    const Skeleton& s = skel();
    std::vector<GltfJoint> out;
    for (const std::string& name : names) {
        const int j = s.find(name);
        int parent = -1;
        for (int a = s[j].parent; a >= 0 && parent < 0; a = s[a].parent)
            for (size_t k = 0; k < out.size(); ++k)
                if (names[k] == s[a].name) parent = int(k);
        auto rn = rename.find(name);
        out.push_back({rn == rename.end() ? name : rn->second, parent, {to_gltf(globals[j].rot), to_gltf(globals[j].pos)}});
    }
    return out;
}

bool load_gltf(const std::string& text, DaeModel& m, DaeReport& r) {
    std::string err;
    const bool ok = load_gltf_mesh(std::vector<std::uint8_t>(text.begin(), text.end()), "", skel(), m, r, err);
    if (!ok) std::fprintf(stderr, "  load_gltf_mesh: %s\n", err.c_str());
    return ok;
}

const std::vector<std::string> kArmAndLeg = {"mPelvis", "mTorso", "mChest", "mShoulderLeft", "mElbowLeft", "mWristLeft",
                                             "mHipLeft", "mKneeLeft", "mAnkleLeft"};

}  // namespace

// ---- glTF import (stage 1) ----

TEST(gltf_rigged_mesh_at_rest) {
    const std::vector<Xform> rest = rest_globals();
    const std::string text = GltfBuilder().build(gltf_joints(kArmAndLeg, rest));
    DaeModel m;
    DaeReport r;
    CHECK(load_gltf(text, m, r));
    CHECK(m.rigged);
    CHECK_EQ(r.up_axis, std::string("Y_UP"));
    CHECK_NEAR(r.scale, 1.0, 1e-9);
    CHECK(r.unmapped_joints.empty());
    CHECK_EQ(m.triangle_count(), int(kArmAndLeg.size()) * 4);
    for (const std::string& n : kArmAndLeg) {
        const int j = skel().find(n);
        CHECK(m.bound[j]);
        CHECK_NEAR(dist(m.binds[j].pos, rest[j].pos), 0, 1e-5);
    }
    // Weights: every vertex 100 % to its joint; the material came through.
    CHECK_EQ(m.joints[0], skel().find("mPelvis"));
    CHECK_NEAR(m.weights[0], 1.0, 1e-6);
    CHECK_EQ(m.materials.size(), size_t(1));
    CHECK_EQ(m.materials[0].name, std::string("Blue"));
    CHECK_NEAR(m.materials[0].rgba[2], 0.8, 1e-6);
    CHECK(m.materials[0].double_sided);
    // Vertices came into SL space: the wrist's tetrahedron sits at the wrist.
    const int w = skel().find("mWristLeft");
    bool found = false;
    for (int v = 0; v < m.vertex_count(); ++v)
        if (m.joints[v * 4] == w && dist({m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]}, rest[w].pos) < 1e-4) found = true;
    CHECK(found);
}

TEST(gltf_creature_moved_joints_rolled_bones) {
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    // Longer legs and a rolled forearm: the file's bind positions move, and the elbow turns about its own bone.
    g[s.find("mKneeLeft")].pos.z -= 0.05;
    g[s.find("mAnkleLeft")].pos.z -= 0.15;
    const Vec3 bone = (g[s.find("mWristLeft")].pos - g[s.find("mElbowLeft")].pos).normalized();
    g[s.find("mElbowLeft")].rot = Quat::axis_angle(bone, 30 * kDegToRad) * g[s.find("mElbowLeft")].rot;
    const std::string text = GltfBuilder().build(gltf_joints(kArmAndLeg, g));
    DaeModel m;
    DaeReport r;
    CHECK(load_gltf(text, m, r));
    CHECK(m.rigged);
    CHECK_NEAR(r.scale, 1.0, 1e-9);
    for (const char* n : {"mKneeLeft", "mAnkleLeft", "mElbowLeft", "mWristLeft"})
        CHECK_NEAR(dist(m.binds[s.find(n)].pos, g[s.find(n)].pos), 0, 1e-5);
    // A moved joint is what shape_from_binds reads as the body's proportions.
    Shape shape;
    CHECK(shape_from_binds(s, {&m}, nullptr, shape));
    CHECK_NEAR(shape.offset[s.find("mKneeLeft")].z, -0.05, 1e-4);
}

TEST(gltf_unknown_bone_names_reported) {
    const std::vector<Xform> rest = rest_globals();
    const std::string text = GltfBuilder().build(gltf_joints(kArmAndLeg, rest, {{"mWristLeft", "Bone.007"}, {"mAnkleLeft", "foot_L"}}));
    DaeModel m;
    DaeReport r;
    CHECK(load_gltf(text, m, r));
    CHECK(m.rigged);
    CHECK_EQ(r.unmapped_joints.size(), size_t(2));
    CHECK_EQ(r.unmapped_joints[0], std::string("Bone.007"));
    // Its vertices have no SL weight (100 % root), and the joint is not bound.
    CHECK(!m.bound[skel().find("mWristLeft")]);
    const int w = skel().find("mWristLeft");
    for (int v = 0; v < m.vertex_count(); ++v) CHECK(m.joints[v * 4] != w);
    CHECK_EQ(m.joints[20 * 4], dae_root(skel()));  // the wrist's tetrahedron: 100 % root
}

TEST(gltf_centimetre_rig_measured) {
    std::vector<Xform> g = rest_globals();
    for (Xform& x : g) x.pos = x.pos * 100;  // modelled in centimetres, declared as metres
    const std::string text = GltfBuilder().build(gltf_joints(kArmAndLeg, g), true, 5.0);
    DaeModel m;
    DaeReport r;
    CHECK(load_gltf(text, m, r));
    CHECK_NEAR(r.scale, 0.01, 1e-9);
    const int k = skel().find("mKneeLeft");
    CHECK_NEAR(dist(m.binds[k].pos, rest_globals()[k].pos), 0, 1e-4);
    CHECK_NEAR(m.bounds_max.z - m.bounds_min.z, (m.bounds_max.z - m.bounds_min.z), 0);  // finite
    CHECK(m.bounds_max.z < 3);  // the mesh came down to metres with the rig
}

TEST(gltf_static_mesh_y_up) {
    const std::string text = GltfBuilder().build(gltf_joints({"mPelvis"}, rest_globals()), false);
    DaeModel m;
    DaeReport r;
    CHECK(load_gltf(text, m, r));
    CHECK(!m.rigged);
    CHECK(!r.skins_as_static);
    // The node's translation (0, 1, 0) in glTF is (0, 0, 1) in SL: the tetrahedron at the pelvis sits a metre higher.
    CHECK_NEAR(m.bounds_min.z, 1.0 + rest_globals()[0].pos.z, 1e-4);
}

TEST(gltf_without_inverse_binds_warns) {
    const std::string text = GltfBuilder().build(gltf_joints(kArmAndLeg, rest_globals()), true, 0.05, false);
    DaeModel m;
    DaeReport r;
    CHECK(load_gltf(text, m, r));
    bool warned = false;
    for (auto& w : r.warnings) warned = warned || w.find("no inverse bind matrices") != std::string::npos;
    CHECK(warned);
}

// A Z-up export with a "Z_UP" node turning it into glTF's Y up (Khronos' CesiumMan): the skin's inverse binds and
// vertices are in the Z-up space below that node. Read through it, the model stands upright, not on its back.
TEST(gltf_node_above_the_armature_sets_the_up_axis) {
    const std::vector<Xform> rest = rest_globals();
    std::vector<GltfJoint> joints = gltf_joints(kArmAndLeg, rest);
    for (size_t i = 0; i < joints.size(); ++i) joints[i].global = rest[size_t(skel().find(kArmAndLeg[i]))];  // Z up
    const Xform z_up{Quat::axis_angle({1, 0, 0}, -kPi / 2), {0, 0, 0}};  // (x, y, z) -> (x, z, -y): Z up to Y up
    GltfBuilder b;
    b.above = &z_up;
    const std::string text = b.build(joints);
    DaeModel m;
    DaeReport r;
    CHECK(load_gltf(text, m, r));
    CHECK(m.rigged);
    for (const std::string& n : kArmAndLeg) {
        const int j = skel().find(n);
        CHECK(dist(m.binds[j].pos, rest[j].pos) < 1e-4);
    }
    for (const SourceBone& sb : r.bones)
        if (const int j = skel().find(sb.name); j >= 0) CHECK(dist(sb.bind.pos, rest[j].pos) < 1e-4);
    CHECK(m.bounds_min.z > rest[skel().find("mAnkleLeft")].pos.z - 0.06);  // standing on its feet
}

// Spec 08 RM-1: whatever its names, the file's own armature comes out, and a remap loads the skin through it.
TEST(gltf_reports_its_armature_and_loads_through_a_remap) {
    const Skeleton& s = skel();
    const std::vector<Xform> rest = rest_globals();
    std::map<std::string, std::string> rename;
    for (const std::string& n : kArmAndLeg) rename[n] = "Bone." + n.substr(1);
    const std::string text = GltfBuilder().build(gltf_joints(kArmAndLeg, rest, rename));
    DaeModel plain, m;
    DaeReport rp, r;
    CHECK(load_gltf(text, plain, rp));
    CHECK(!plain.rigged && rp.bones.size() == kArmAndLeg.size());
    SkinRemap remap;
    for (size_t i = 0; i < rp.bones.size() && i < kArmAndLeg.size(); ++i) {
        const SourceBone& b = rp.bones[i];
        const int j = s.find(kArmAndLeg[i]);
        CHECK(b.name == rename[kArmAndLeg[i]] && b.skinned && b.weight > 0);
        CHECK(dist(b.bind.pos, rest[j].pos) < 1e-5);  // SL axes: the Y-up turn applied
        CHECK(b.parent < 0 ? kArmAndLeg[i] == "mPelvis" : s.find(kArmAndLeg[size_t(b.parent)]) >= 0);
        remap.joints[b.name] = j;
        remap.binds[j] = b.bind;
    }
    std::string err;
    CHECK(load_gltf_mesh(std::vector<std::uint8_t>(text.begin(), text.end()), "", s, m, r, err, &remap));
    CHECK(m.rigged && r.remapped && r.unmapped_joints.empty());
    const int wrist = s.find("mWristLeft");
    CHECK(m.bound[wrist] && dist(m.binds[wrist].pos, rest[wrist].pos) < 1e-5);
    for (int v = 0; v < m.vertex_count(); ++v) CHECK(m.joints[v * 4] != dae_root(s) && std::fabs(m.weights[v * 4] - 1) < 1e-6);
}
