// FBX import tests (spec 07 RT-2.3, 08 BD-1). The FBX files are written here as ASCII FBX 7.4, since
// ufbx only reads.
#include <cmath>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/bvh.h"
#include "vats/edit.h"
#include "vats/dae.h"
#include "vats/fbx.h"

using namespace vats;

namespace {

constexpr long long kSecond = 46186158000;  // FBX KTime ticks per second

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

std::string list(const std::vector<double>& v) {
    std::ostringstream o;
    o.precision(10);
    o << "*" << v.size() << " { a: ";
    for (size_t i = 0; i < v.size(); ++i) o << (i ? "," : "") << v[i];
    return o.str() + " }";
}

std::string list(const std::vector<long long>& v) {
    std::string o = "*" + std::to_string(v.size()) + " { a: ";
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + std::to_string(v[i]);
    return o + " }";
}

std::string header(int up_axis, double unit_cm) {
    std::ostringstream o;
    o << "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\n"
      << "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n"
      << "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\"," << up_axis << "\n"
      << "\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
      << "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\"," << (up_axis == 1 ? 2 : 1) << "\n"
      << "\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
      << "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n"
      << "\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
      << "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\"," << unit_cm << "\n"
      << "\t\tP: \"TimeMode\", \"enum\", \"\", \"\",6\n\t}\n}\n";
    return o.str();
}

std::string vec3(const Vec3& v) {
    std::ostringstream o;
    o.precision(10);
    o << v.x << "," << v.y << "," << v.z;
    return o.str();
}

// A SourceAnim as an FBX skeleton with one take: Lcl Rotation (Euler XYZ degrees) and Lcl Translation keyed
// on every frame at 30 fps.
std::string write_skeleton_fbx(const SourceAnim& src) {
    std::ostringstream obj, con;
    const int n = int(src.joints.size()), frames = src.frames();
    std::vector<long long> times;
    for (int f = 0; f < frames; ++f) times.push_back(kSecond / 30 * f);
    obj << "Objects:  {\n";
    obj << "\tAnimationStack: 1, \"AnimStack::Take 001\", \"\" {\n\t\tProperties70:  {\n\t\t\tP: \"LocalStop\", \"KTime\", "
           "\"Time\", \"\","
        << kSecond / 30 * (frames - 1) << "\n\t\t}\n\t}\n";
    obj << "\tAnimationLayer: 2, \"AnimLayer::BaseLayer\", \"\" {\n\t}\n";
    con << "Connections:  {\n\tC: \"OO\",2,1\n";
    for (int j = 0; j < n; ++j) {
        const SourceJoint& sj = src.joints[j];
        long long model = 1000 + j, attr = 3000 + j;
        obj << "\tNodeAttribute: " << attr << ", \"NodeAttribute::\", \"LimbNode\" {\n\t\tTypeFlags: \"Skeleton\"\n\t}\n";
        obj << "\tModel: " << model << ", \"Model::" << sj.name << "\", \"LimbNode\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n"
            << "\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\"," << vec3(sj.offset) << "\n"
            << "\t\t\tP: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\"," << vec3(quat_to_euler(sj.rot)) << "\n\t\t}\n\t}\n";
        con << "\tC: \"OO\"," << attr << "," << model << "\n";
        con << "\tC: \"OO\"," << model << "," << (sj.parent >= 0 ? 1000 + sj.parent : 0) << "\n";
        for (int kind = 0; kind < 2; ++kind) {  // 0 rotation, 1 translation
            long long node = 10000 + j * 2 + kind;
            obj << "\tAnimationCurveNode: " << node << ", \"AnimCurveNode::" << (kind ? "T" : "R") << "\", \"\" {\n\t}\n";
            con << "\tC: \"OO\"," << node << ",2\n\tC: \"OP\"," << node << "," << model << ", \""
                << (kind ? "Lcl Translation" : "Lcl Rotation") << "\"\n";
            for (int c = 0; c < 3; ++c) {
                std::vector<double> values;
                for (int f = 0; f < frames; ++f) {
                    Vec3 v = kind ? src.pos[f][j] : quat_to_euler(src.rot[f][j]);
                    values.push_back(v[c]);
                }
                long long curve = 20000 + (j * 2 + kind) * 3 + c;
                obj << "\tAnimationCurve: " << curve << ", \"AnimCurve::\", \"\" {\n\t\tDefault: 0\n\t\tKeyVer: 4008\n\t\tKeyTime: "
                    << list(times) << "\n\t\tKeyValueFloat: " << list(values)
                    << "\n\t\tKeyAttrFlags: *1 { a: 24836 }\n\t\tKeyAttrDataFloat: *4 { a: 0,0,255790911,0 }"
                    << "\n\t\tKeyAttrRefCount: *1 { a: " << frames << " }\n\t}\n";  // one linear attribute for every key
                con << "\tC: \"OP\"," << curve << "," << node << ", \"d|" << "XYZ"[c] << "\"\n";
            }
        }
    }
    obj << "}\n";
    con << "}\n";
    return header(1, 1) + obj.str() + con.str();
}

double angle_deg(const Quat& a, const Quat& b) { return (a.conj() * b).normalized().angle() * kRadToDeg; }

}  // namespace

TEST(fbx_skeleton_animation_reads_like_bvh) {
    const Skeleton& s = skel();
    const char* body[] = {"mPelvis",        "mTorso",      "mChest",      "mNeck",     "mHead",       "mCollarLeft",
                          "mShoulderLeft",  "mElbowLeft",  "mWristLeft",  "mCollarRight", "mShoulderRight", "mElbowRight",
                          "mWristRight",    "mHipLeft",    "mKneeLeft",   "mAnkleLeft", "mFootLeft",    "mHipRight",
                          "mKneeRight",     "mAnkleRight", "mFootRight"};
    Clip clip;
    clip.fps = 30;
    clip.end_frame = 8;
    for (int i = 0; i < int(std::size(body)); ++i)
        for (int f = 0; f <= 8; ++f) key_euler(clip, body[i], f, {10.0 * std::sin(f + i), 25.0 * std::cos(0.5 * f), 7.0 * i - f});
    for (int f = 0; f <= 8; ++f) key_offset(clip, "mPelvis", f, {0.02 * f, 0, -0.01 * f});
    SourceAnim bvh, fbx;
    std::string err;
    CHECK(read_bvh_source(export_bvh(s, clip).text, bvh, err));
    CHECK(read_fbx_source(bytes(write_skeleton_fbx(bvh)), fbx, err));
    CHECK_EQ(err, std::string());
    CHECK_EQ(fbx.joints.size(), bvh.joints.size());
    CHECK_EQ(fbx.frames(), bvh.frames());
    CHECK_NEAR(fbx.fps, 30.0, 1e-9);
    double worst_rot = 0, worst_pos = 0;
    for (size_t j = 0; j < bvh.joints.size() && j < fbx.joints.size(); ++j) {
        CHECK_EQ(fbx.joints[j].name, bvh.joints[j].name);
        CHECK_EQ(fbx.joints[j].parent, bvh.joints[j].parent);
        worst_pos = std::max(worst_pos, (fbx.joints[j].offset - bvh.joints[j].offset).length());
        for (int f = 0; f < bvh.frames() && f < fbx.frames(); ++f) {
            worst_rot = std::max(worst_rot, angle_deg(fbx.rot[f][j], bvh.rot[f][j]));
            worst_pos = std::max(worst_pos, (fbx.pos[f][j] - bvh.pos[f][j]).length());
        }
    }
    CHECK(worst_rot < 1e-3);
    CHECK(worst_pos < 1e-5);

    // And through the retarget, identity mapping, the clip comes back (frame 0 is the BVH rest frame).
    BoneMap map;
    for (int j = 0; j < int(fbx.joints.size()); ++j) map[fbx.joints[j].name] = j;
    RetargetResult r = retarget(s, fbx, map);
    double worst = 0;
    for (int f = 0; f <= 8; ++f)
        for (auto* name : body)
            worst = std::max(worst, angle_deg(euler_to_quat(curve_euler(r.clip, name, f + 1)),
                                              euler_to_quat(curve_euler(clip, name, f))));
    CHECK(worst < 0.01);
}

TEST(fbx_rejects_garbage_and_missing_animation) {
    SourceAnim src;
    std::string err;
    CHECK(!read_fbx_source(bytes("not an fbx file at all"), src, err));
    CHECK(!err.empty());
    err.clear();
    CHECK(read_fbx_source(bytes(header(1, 1) + "Objects:  {\n\tModel: 1, \"Model::Hips\", \"LimbNode\" {\n\t}\n}\n"
                                                 "Connections:  {\n\tC: \"OO\",1,0\n}\n"),
                          src, err) == false);
    CHECK(err.find("animation") != std::string::npos);
}

// A Y-up, centimetre FBX skinned to mPelvis and mChest (plus one joint SL does not have), bound at the SL
// rest pose the way Blender or Maya would write it: bone bind matrices carry the Z-up -> Y-up turn.
TEST(fbx_skinned_mesh_maps_weights_and_binds) {
    const Skeleton& s = skel();
    auto rest = s.global_pose(Pose(s.size()));
    const int pelvis = s.find("mPelvis"), chest = s.find("mChest");
    const Vec3 P = rest[pelvis].pos, C = rest[chest].pos;
    auto yup = [](const Vec3& v) { return Vec3{v.x, v.z, -v.y} * 100.0; };  // inverse of load_dae's Y_UP turn, in cm
    // Vertices (SL metres): at the pelvis, 10 cm in front of the chest, and between them.
    const Vec3 v[3] = {P, C + Vec3{0.1, 0, 0}, (P + C) * 0.5};
    std::vector<double> verts;
    for (auto& p : v)
        for (int i = 0; i < 3; ++i) verts.push_back(yup(p)[i]);
    auto link = [&](const Vec3& sl) {  // TransformLink: the turn back to Y up, then the bone's position
        Vec3 t = yup(sl);
        return list(std::vector<double>{1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, t.x, t.y, t.z, 1});
    };
    auto inverse_link = [&](const Vec3& sl) {  // Transform: mesh (at the origin) to bone, the inverse of the link
        Vec3 t = sl * -100.0;
        return list(std::vector<double>{1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, t.x, t.y, t.z, 1});
    };
    std::ostringstream o;
    o << header(1, 1) << "Objects:  {\n"
      << "\tGeometry: 100, \"Geometry::body\", \"Mesh\" {\n\t\tVertices: " << list(verts)
      << "\n\t\tPolygonVertexIndex: *3 { a: 0,1,-3 }\n\t}\n"
      << "\tModel: 101, \"Model::body\", \"Mesh\" {\n\t\tVersion: 232\n\t}\n"
      << "\tModel: 200, \"Model::mPelvis\", \"LimbNode\" {\n\t}\n"
      << "\tModel: 201, \"Model::mChest\", \"LimbNode\" {\n\t}\n"
      << "\tModel: 202, \"Model::mixamorig:Tail\", \"LimbNode\" {\n\t}\n"
      << "\tDeformer: 300, \"Deformer::Skin\", \"Skin\" {\n\t\tVersion: 101\n\t}\n"
      << "\tDeformer: 301, \"SubDeformer::pelvis\", \"Cluster\" {\n\t\tIndexes: *2 { a: 0,2 }\n\t\tWeights: *2 { a: 1,0.5 }\n"
      << "\t\tTransform: " << inverse_link(P) << "\n\t\tTransformLink: " << link(P) << "\n\t}\n"
      << "\tDeformer: 302, \"SubDeformer::chest\", \"Cluster\" {\n\t\tIndexes: *2 { a: 1,2 }\n\t\tWeights: *2 { a: 1,0.25 }\n"
      << "\t\tTransform: " << inverse_link(C) << "\n\t\tTransformLink: " << link(C) << "\n\t}\n"
      << "\tDeformer: 303, \"SubDeformer::tail\", \"Cluster\" {\n\t\tIndexes: *1 { a: 2 }\n\t\tWeights: *1 { a: 0.25 }\n"
      << "\t\tTransform: " << inverse_link(C) << "\n\t\tTransformLink: " << link(C) << "\n\t}\n"
      << "}\nConnections:  {\n"
      << "\tC: \"OO\",101,0\n\tC: \"OO\",100,101\n\tC: \"OO\",200,0\n\tC: \"OO\",201,200\n\tC: \"OO\",202,201\n"
      << "\tC: \"OO\",300,100\n\tC: \"OO\",301,300\n\tC: \"OO\",302,300\n\tC: \"OO\",303,300\n"
      << "\tC: \"OO\",200,301\n\tC: \"OO\",201,302\n\tC: \"OO\",202,303\n}\n";

    DaeModel m;
    DaeReport rep;
    std::string err;
    CHECK(load_fbx_mesh(bytes(o.str()), "", s, m, rep, err));
    CHECK_EQ(err, std::string());
    CHECK(m.rigged && rep.rigged);
    CHECK_EQ(rep.up_axis, std::string("Y_UP"));
    CHECK_NEAR(rep.scale, 0.01, 1e-12);
    CHECK_EQ(m.vertex_count(), 3);
    CHECK_EQ(m.triangle_count(), 1);
    CHECK(rep.unmapped_joints.size() == 1 && rep.unmapped_joints[0] == "mixamorig:Tail");
    // Weights: the unmapped joint is dropped and the rest renormalised.
    for (int i = 0; i < 3; ++i) {
        Vec3 p{m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]};
        int k = 0;
        while (k < 3 && (p - v[k]).length() > 1e-4) ++k;
        CHECK(k < 3);
        if (k == 2) {
            CHECK_EQ(m.joints[i * 4], pelvis);
            CHECK_EQ(m.joints[i * 4 + 1], chest);
            CHECK_NEAR(m.weights[i * 4], 2.0 / 3, 1e-6);
            CHECK_NEAR(m.weights[i * 4 + 1], 1.0 / 3, 1e-6);
        }
        if (k == 1) CHECK(m.joints[i * 4] == chest && std::fabs(m.weights[i * 4] - 1) < 1e-6);
    }
    CHECK((m.binds[chest].pos - C).length() < 1e-6);
    CHECK(angle_deg(m.binds[chest].rot, Quat{}) < 1e-6);

    // Skinned at rest: the vertices stay put. Turning the chest carries the chest vertex with it.
    std::vector<float> pos, nrm;
    skin_prop(m, s, rest, nullptr, pos, nrm);
    for (int i = 0; i < 9; ++i) CHECK_NEAR(pos[i], m.positions[i], 1e-5);
    Pose turned(s.size());
    turned.rot[chest] = Quat::axis_angle({0, 0, 1}, kPi / 2);
    auto g = s.global_pose(turned);
    skin_prop(m, s, g, nullptr, pos, nrm);
    for (int i = 0; i < 3; ++i) {
        Vec3 p{m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]};
        if ((p - v[1]).length() > 1e-4) continue;
        Vec3 want = g[chest].apply(rest[chest].inverse().apply(v[1]));
        CHECK((Vec3{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]} - want).length() < 1e-4);
    }
}

// A 2000-level chain of nodes used to overflow the stack in the recursive tree walk; ufbx now stops at
// its depth limit and the reader reports an error.
TEST(fbx_deep_hierarchy_is_refused) {
    std::ostringstream o;
    o << header(1, 1) << "Objects:  {\n";
    for (int i = 1; i <= 2000; ++i) o << "\tModel: " << i << ", \"Model::n" << i << "\", \"LimbNode\" {\n\t}\n";
    o << "}\nConnections:  {\n\tC: \"OO\",1,0\n";
    for (int i = 2; i <= 2000; ++i) o << "\tC: \"OO\"," << i << "," << i - 1 << "\n";
    o << "}\n";
    SourceAnim src;
    std::string err;
    CHECK(!read_fbx_source(bytes(o.str()), src, err));
    CHECK(err.find("depth") != std::string::npos);
    DaeModel m;
    DaeReport rep;
    err.clear();
    CHECK(!load_fbx_mesh(bytes(o.str()), "", skel(), m, rep, err));
    CHECK(err.find("depth") != std::string::npos);
}

// tests/data/blender_rig.fbx, made by tests/data/make_blender_rig.py: Blender's default FBX export of a small
// rig facing -Y with bones along their own Y. It must skin in SL axes: bound at the SL rest, turned to face
// +X, and following a pose exactly like the joints it is weighted to.
TEST(fbx_blender_convention_skins_in_sl_axes) {
    const Skeleton& s = skel();
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_mesh_file(std::string(VATS_TEST_FILES) + "/blender_rig.fbx", s, m, r, err));
    CHECK(m.rigged);
    const int shoulder = s.find("mShoulderLeft");
    CHECK((m.binds[shoulder].pos - s.global_pose(Pose(s.size()))[shoulder].pos).length() < 1e-3);
    Shape shape;  // the joints where this rig put them, as the app does for a mesh body
    std::vector<const DaeModel*> parts{&m};
    const Shape* sp = shape_from_binds(s, parts, nullptr, shape) ? &shape : nullptr;
    const auto rest = s.global_pose(Pose(s.size()), sp);
    CHECK(std::fabs(m.binds[shoulder].rot.w) > 0.9999);  // SL rest rotation, not Blender's bone axes

    std::vector<float> p, n;
    skin_prop(m, s, rest, sp, p, n);
    double worst = 0;
    int left = 0;
    for (size_t v = 0; v < p.size() / 3; ++v) {
        worst = std::max(worst, (Vec3{p[v * 3], p[v * 3 + 1], p[v * 3 + 2]} -
                                 Vec3{m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]}).length());
        left += m.joints[v * 4] == shoulder && p[v * 3 + 1] > 0.15;  // the left arm is on SL's +Y side
    }
    CHECK(worst < 1e-5);
    CHECK_EQ(left, 3);

    Pose pose(s.size());
    const Quat q = Quat::axis_angle({1, 0, 0}, kPi / 2);
    pose.rot[shoulder] = q;
    const auto g = s.global_pose(pose, sp);
    skin_prop(m, s, g, sp, p, n);
    for (size_t v = 0; v < p.size() / 3; ++v) {
        if (m.joints[v * 4] != shoulder) continue;
        Vec3 bind{m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]};
        Vec3 want = rest[shoulder].pos + q.rotate(bind - rest[shoulder].pos);
        CHECK((Vec3{p[v * 3], p[v * 3 + 1], p[v * 3 + 2]} - want).length() < 1e-4);
    }
}
