// Viewport Avatar Toolset - a small rigged test body with its own bone axes: a boxy mech on SL joint names, with
// rolled bones and a pair of digitigrade hind legs whose knees hinge off SL's axes. Written as COLLADA the way Blender
// writes a rig (each joint's Y along its bone), for the rig axes and Auto IK tests and the help's pictures.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE. The mesh it writes is CC0.
//
// Header-only: tests/test_rig_axes.cpp, tests/test_rig_map.cpp (renamed, as a foreign rig) and tools/make_test_body.cpp
// (--mech) share it.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "vats/skeleton.h"

namespace vats::mech {

struct Bone {
    int node = -1;
    Vec3 head, tail;  // SL space, Z up, facing +X
    Quat axes;        // the authored bone axes in SL space: Y along the bone, X across it (a hinge's axis)
    double width = 0.05;
    int colour = 0;   // 0 steel, 1 hazard orange (the hind legs), 2 teal (the head)
};

struct Body {
    std::vector<Bone> bones;
    const Bone* find(int node) const {
        for (const Bone& b : bones)
            if (b.node == node) return &b;
        return nullptr;
    }
};

inline Quat from_axes(const Vec3& x, const Vec3& y, const Vec3& z) {
    const double m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
    const double tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0) {
        const double s = std::sqrt(tr + 1) * 2;
        q = {0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1 + m00 - m11 - m22) * 2;
        q = {(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const double s = std::sqrt(1 + m11 - m00 - m22) * 2;
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s};
    } else {
        const double s = std::sqrt(1 + m22 - m00 - m11) * 2;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s};
    }
    return q.normalized();
}

// Y along head -> tail; X the given hinge made square to Y.
inline Quat axes_with_x(const Vec3& head, const Vec3& tail, const Vec3& x_hint) {
    const Vec3 y = (tail - head).normalized();
    const Vec3 x = (x_hint - y * x_hint.dot(y)).normalized();
    return from_axes(x, y, x.cross(y));
}

// Y along head -> tail, rolled by roll_deg about it from the axis square to Y nearest world up (or forward).
inline Quat axes_rolled(const Vec3& head, const Vec3& tail, double roll_deg) {
    const Vec3 y = (tail - head).normalized();
    Vec3 ref = Vec3{0, 0, 1}.cross(y);
    if (ref.length() < 0.2) ref = Vec3{1, 0, 0}.cross(y);
    const Vec3 x = Quat::axis_angle(y, roll_deg * kDegToRad).rotate(ref.normalized());
    return from_axes(x, y, x.cross(y));
}

// The world hinge of the mech's left or right hind leg: splayed 20 degrees out and cambered 10 degrees, so it lines
// up with none of SL's axes.
inline Vec3 hind_forward(bool left) { return Quat::axis_angle({0, 0, 1}, (left ? 20 : -20) * kDegToRad).rotate({1, 0, 0}); }
inline Vec3 hind_down(bool left) {
    return Quat::axis_angle(hind_forward(left), (left ? 10 : -10) * kDegToRad).rotate({0, 0, -1});
}
inline Vec3 hind_hinge(bool left) { return hind_forward(left).cross(hind_down(left)).normalized(); }

// extras (spec 08 FP): a six-bone tail on mTail1..6 and a belly pod weighted to the BELLY collision volume, for the
// tests and pictures of dragging the body and of follow-through. hooves: the hind feet stand on the floor as hooves
// pointing down from mHindLimb4, whose joint stays 17 cm up (planting a creature by its skin, spec 08 FP-3).
inline Body build(const Skeleton& skel, bool extras = false, bool hooves = false) {
    const std::vector<Xform> rest = skel.global_pose(Pose(skel.size()));
    auto at = [&](const std::string& n) { return rest[skel.find(n)].pos; };
    Body b;
    auto bone = [&](const std::string& name, const Vec3& head, const Vec3& tail, const Quat& axes, double w, int colour) {
        b.bones.push_back({skel.find(name), head, tail, axes, w, colour});
    };
    auto to = [&](const std::string& name, const std::string& child, double roll, double w) {
        bone(name, at(name), at(child), axes_rolled(at(name), at(child), roll), w, 0);
    };
    auto leaf = [&](const std::string& name, const Vec3& dir, double roll, double w, int colour = 0) {
        bone(name, at(name), at(name) + dir, axes_rolled(at(name), at(name) + dir, roll), w, colour);
    };
    to("mPelvis", "mTorso", 0, 0.09);
    to("mTorso", "mChest", 15, 0.10);
    to("mChest", "mNeck", -15, 0.11);
    to("mNeck", "mHead", 0, 0.035);
    leaf("mHead", {0.02, 0, 0.19}, 30, 0.07, 2);
    for (bool left : {true, false}) {
        const double k = left ? 1 : -1;
        auto n = [&](const char* stem) { return std::string(stem) + (left ? "Left" : "Right"); };
        to(n("mCollar"), n("mShoulder"), 50 * k, 0.04);
        to(n("mShoulder"), n("mElbow"), 35 * k, 0.045);
        to(n("mElbow"), n("mWrist"), 35 * k, 0.04);
        leaf(n("mWrist"), (at(n("mWrist")) - at(n("mElbow"))).normalized() * 0.12, 35 * k, 0.035);
        to(n("mHip"), n("mKnee"), 20 * k, 0.06);
        to(n("mKnee"), n("mAnkle"), 20 * k, 0.05);
        to(n("mAnkle"), n("mFoot"), -20 * k, 0.045);
        leaf(n("mFoot"), {0.09, 0, -0.02}, 0, 0.04);
        // The hind leg: thigh forward and down, shin back and down, a long metatarsal nearly upright, the foot flat.
        const Vec3 f = hind_forward(left), d = hind_down(left), h = hind_hinge(left);
        auto dir = [&](double below_deg, bool back) {
            const double a = below_deg * kDegToRad;
            return f * (std::cos(a) * (back ? -1 : 1)) + d * std::sin(a);
        };
        const Vec3 h1 = at(n("mHindLimb1")), h2 = h1 + dir(50, false) * 0.40, h3 = h2 + dir(70, true) * 0.42,
                   h4 = h3 + dir(80, false) * 0.22;
        // A hoof's tip level with the front feet's soles (4 cm under their foot joints).
        const Vec3 toe = hooves ? h4 + d * ((h4.z - at(n("mFoot")).z + 0.04) / -d.z) : h4 + f * 0.16;
        bone(n("mHindLimb1"), h1, h2, axes_with_x(h1, h2, h), 0.06, 1);
        bone(n("mHindLimb2"), h2, h3, axes_with_x(h2, h3, h), 0.05, 1);
        bone(n("mHindLimb3"), h3, h4, axes_with_x(h3, h4, h), 0.04, 1);
        bone(n("mHindLimb4"), h4, toe, axes_with_x(h4, toe, h), 0.045, 1);
    }
    leaf("mHindLimbsRoot", {-0.12, 0, 0}, 0, 0.07, 1);
    if (extras) {
        for (int k = 1; k < 6; ++k) to("mTail" + std::to_string(k), "mTail" + std::to_string(k + 1), 0, 0.04 - 0.004 * k);
        leaf("mTail6", (at("mTail6") - at("mTail5")).normalized() * 0.1, 0, 0.016, 2);
        leaf("BELLY", {0.11, 0, 0}, 0, 0.06, 2);
    }
    return b;
}

// A rig with no SL names, for rig mapping (spec 08 RM): the bones renamed, the file's own joint tree, helper bones
// that carry no weights, and every length units times longer (the file declares the unit, as a centimetre rig does).
struct Foreign {
    std::function<std::string(int node)> name;  // the file's name for each SL joint the body rigs
    double units = 1;                            // file units per metre
    // Helper JOINT nodes with no weights: name, parent (the file's name; "" at the root), where it sits (SL space).
    struct Helper {
        std::string name, parent;
        Vec3 at;
    };
    std::vector<Helper> helpers;
};

inline std::string fmt(const char* f, double a, double b, double c) {
    char buf[96];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

// A foreign rig's JOINT nodes, nested as SL nests the joints, each at its bind against its parent's; then the helpers.
inline std::string joint_tree(const Skeleton& skel, const Body& body, const Quat& file, const Foreign& f) {
    auto bind = [&](const Bone& b) { return Xform{file, {}} * Xform{b.axes, b.head * f.units}; };
    auto parent_of = [&](const Bone& b) -> const Bone* {
        for (int p = skel[b.node].parent; p >= 0; p = skel[p].parent)
            if (const Bone* pb = body.find(p)) return pb;
        return nullptr;
    };
    auto matrix = [](const Xform& x) {
        const Vec3 c[3] = {x.rot.rotate({1, 0, 0}), x.rot.rotate({0, 1, 0}), x.rot.rotate({0, 0, 1})};
        return fmt("%.9g %.9g %.9g ", c[0].x, c[1].x, c[2].x) + fmt("%.9g %.9g %.9g ", x.pos.x, c[0].y, c[1].y) +
               fmt("%.9g %.9g %.9g ", c[2].y, x.pos.y, c[0].z) + fmt("%.9g %.9g %.9g 0 0 0 1", c[1].z, c[2].z, x.pos.z);
    };
    std::function<std::string(const std::string&, const Xform&)> helpers_under = [&](const std::string& parent, const Xform& at) {
        std::string out;
        for (const Foreign::Helper& h : f.helpers)
            if (h.parent == parent)
                out += "<node id=\"" + h.name + "\" sid=\"" + h.name + "\" name=\"" + h.name + "\" type=\"JOINT\"><matrix>" +
                       matrix(at.inverse() * Xform{file, file.rotate(h.at * f.units)}) + "</matrix>" +
                       helpers_under(h.name, Xform{file, file.rotate(h.at * f.units)}) + "</node>";
        return out;
    };
    std::function<std::string(const Bone&)> node = [&](const Bone& b) {
        const Bone* p = parent_of(b);
        const std::string n = f.name(b.node);
        std::string out = "<node id=\"" + n + "\" sid=\"" + n + "\" name=\"" + n + "\" type=\"JOINT\"><matrix>" +
                          matrix(p ? bind(*p).inverse() * bind(b) : bind(b)) + "</matrix>";
        for (const Bone& c : body.bones)
            if (parent_of(c) == &b) out += node(c);
        return out + helpers_under(n, bind(b)) + "</node>";
    };
    std::string out;
    for (const Bone& b : body.bones)
        if (!parent_of(b)) out += node(b);
    return out + helpers_under("", Xform{});
}

// The body as COLLADA: every bone a box weighted to its joint, in the given frame. turn: quarter turns about Z the
// whole file is written turned by (-1 faces -Y, as Blender rigs do); y_up writes it Y up. foreign: renamed, with a
// joint tree. Returns the document.
inline std::string dae(const Skeleton& skel, const Body& body, int turn = 0, bool y_up = false, const Foreign* foreign = nullptr) {
    Quat file = Quat::axis_angle({0, 0, 1}, turn * kPi / 2);
    if (y_up) file = Quat::axis_angle({1, 0, 0}, -kPi / 2) * file;  // SL (x, y, z) -> file (x, z, -y)
    const double u = foreign ? foreign->units : 1;
    auto name = [&](int node) { return foreign ? foreign->name(node) : skel[node].name; };
    std::string pos, nrm, vcount, v, weights, jn, ibm;
    std::string tris[3];
    int count[3] = {}, nv = 0;
    for (size_t bi = 0; bi < body.bones.size(); ++bi) {
        const Bone& b = body.bones[bi];
        const Vec3 x = b.axes.rotate({1, 0, 0}), y = b.axes.rotate({0, 1, 0}), z = b.axes.rotate({0, 0, 1});
        const double len = (b.tail - b.head).length(), w = b.width;
        auto corner = [&](int i) {
            return b.head + x * ((i & 1) ? w : -w) + y * ((i & 2) ? len : 0) + z * ((i & 4) ? w : -w);
        };
        // Faces as corner quads, counter-clockwise from outside, with their normals.
        const int faces[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
        const Vec3 normals[6] = {-x, x, -y, y, -z, z};
        for (int f = 0; f < 6; ++f) {
            const Vec3 n = file.rotate(normals[f]);
            for (int k = 0; k < 4; ++k) {
                const Vec3 p = file.rotate(corner(faces[f][k])) * u;
                pos += fmt("%.6f %.6f %.6f ", p.x, p.y, p.z);
                nrm += fmt("%.5f %.5f %.5f ", n.x, n.y, n.z);
                vcount += "1 ";
                v += std::to_string(bi) + " " + std::to_string(bi) + " ";
            }
            for (int t : {0, 1, 2, 0, 2, 3}) tris[b.colour] += std::to_string(nv + t) + " ";
            count[b.colour] += 2;
            nv += 4;
        }
        weights += "1 ";
        jn += name(b.node) + " ";
        const Xform bind = Xform{file, {}} * Xform{b.axes, b.head * u};
        const Xform inv = bind.inverse();
        const Vec3 c[3] = {inv.rot.rotate({1, 0, 0}), inv.rot.rotate({0, 1, 0}), inv.rot.rotate({0, 0, 1})};
        char buf[400];
        std::snprintf(buf, sizeof buf, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g 0 0 0 1 ", c[0].x, c[1].x,
                      c[2].x, inv.pos.x, c[0].y, c[1].y, c[2].y, inv.pos.y, c[0].z, c[1].z, c[2].z, inv.pos.z);
        ibm += buf;
    }
    const int nj = int(body.bones.size());
    // Each vertex takes weight index = its bone index, so the weight list is one 1.0 per bone.
    const char* colours[3] = {"0.55 0.60 0.68", "0.95 0.55 0.15", "0.30 0.75 0.80"};
    std::string fx, mats, prims;
    for (int m = 0; m < 3; ++m) {
        const std::string id = std::to_string(m);
        fx += "<effect id=\"fx" + id + "\"><profile_COMMON><technique sid=\"c\"><lambert><diffuse><color>" +
              colours[m] + " 1</color></diffuse></lambert></technique></profile_COMMON></effect>";
        mats += "<material id=\"m" + id + "\"><instance_effect url=\"#fx" + id + "\"/></material>";
        if (count[m])
            prims += "<triangles count=\"" + std::to_string(count[m]) + "\" material=\"m" + id +
                     "\"><input semantic=\"VERTEX\" source=\"#g-v\" offset=\"0\"/><p>" + tris[m] + "</p></triangles>\n";
    }
    auto source = [](const std::string& id, const std::string& data, int n, int stride, const char* params) {
        return "<source id=\"" + id + "\"><float_array id=\"" + id + "-a\" count=\"" + std::to_string(n * stride) + "\">" +
               data + "</float_array><technique_common><accessor source=\"#" + id + "-a\" count=\"" + std::to_string(n) +
               "\" stride=\"" + std::to_string(stride) + "\">" + params + "</accessor></technique_common></source>\n";
    };
    const char* xyz = "<param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/>";
    return std::string("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                       "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
                       "<asset><unit name=\"meter\" meter=\"" + fmt("%.9g", 1 / u, 0, 0) + "\"/><up_axis>") +
           (y_up ? "Y_UP" : "Z_UP") + "</up_axis></asset>\n<library_effects>" + fx +
           "</library_effects>\n<library_materials>" + mats + "</library_materials>\n" +
           "<library_geometries><geometry id=\"g\"><mesh>\n" + source("g-p", pos, nv, 3, xyz) +
           source("g-n", nrm, nv, 3, xyz) +
           "<vertices id=\"g-v\"><input semantic=\"POSITION\" source=\"#g-p\"/><input semantic=\"NORMAL\" "
           "source=\"#g-n\"/></vertices>\n" +
           prims + "</mesh></geometry></library_geometries>\n" +
           "<library_controllers><controller id=\"ctl\"><skin source=\"#g\"><bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 "
           "0 1</bind_shape_matrix>\n" +
           "<source id=\"ctl-j\"><Name_array id=\"ctl-ja\" count=\"" + std::to_string(nj) + "\">" + jn +
           "</Name_array><technique_common><accessor source=\"#ctl-ja\" count=\"" + std::to_string(nj) +
           "\" stride=\"1\"><param name=\"JOINT\" type=\"name\"/></accessor></technique_common></source>\n" +
           source("ctl-ibm", ibm, nj, 16, "<param name=\"TRANSFORM\" type=\"float4x4\"/>") +
           source("ctl-w", weights, nj, 1, "<param name=\"WEIGHT\" type=\"float\"/>") +
           "<joints><input semantic=\"JOINT\" source=\"#ctl-j\"/><input semantic=\"INV_BIND_MATRIX\" "
           "source=\"#ctl-ibm\"/></joints>\n<vertex_weights count=\"" + std::to_string(nv) +
           "\"><input semantic=\"JOINT\" source=\"#ctl-j\" offset=\"0\"/><input semantic=\"WEIGHT\" source=\"#ctl-w\" "
           "offset=\"1\"/><vcount>" + vcount + "</vcount><v>" + v + "</v></vertex_weights></skin></controller>"
           "</library_controllers>\n<library_visual_scenes><visual_scene id=\"Scene\">" + (foreign ? joint_tree(skel, body, file, *foreign) : "") +
           "<node id=\"mech\">"
           "<instance_controller url=\"#ctl\"><bind_material><technique_common>"
           "<instance_material symbol=\"m0\" target=\"#m0\"/><instance_material symbol=\"m1\" target=\"#m1\"/>"
           "<instance_material symbol=\"m2\" target=\"#m2\"/></technique_common></bind_material></instance_controller>"
           "</node></visual_scene></library_visual_scenes>\n<scene><instance_visual_scene url=\"#Scene\"/></scene>\n"
           "</COLLADA>\n";
}

}  // namespace vats::mech
