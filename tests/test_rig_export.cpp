// The SL rig export (spec 08 RG-4/RG-5). Every rig here is built in the test: tetrahedra around SL joints, weighted to
// them, CC0 like everything VATs makes.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "check.h"
#include "fixtures.h"
#include "vats/dae.h"
#include "vats/rig_export.h"
#include "vats/xml.h"

using namespace vats;

namespace {

std::vector<Xform> rest_globals() {
    const Skeleton& s = skel();
    std::vector<Xform> rest = s.global_pose(Pose(s.size()));
    rest.push_back({});
    for (auto& v : s.volumes()) rest.push_back(rest[v.joint] * Xform{v.rot, v.pos});
    return rest;
}

int sk40(const std::string& name) {
    const Skeleton& s = skel();
    if (int v = s.find_volume(name); v >= 0) return dae_volume(s, v);
    return s.find(name);
}

struct Bind {
    std::string name;
    Xform x;  // global bind transform in SL space
};

// A rigged model: a tetrahedron (4 vertices, 4 triangles) around each bind, weighted 100 % to it.
DaeModel make_model(const std::vector<Bind>& binds, double size = 0.05) {
    const Skeleton& s = skel();
    DaeModel m;
    m.rigged = true;
    m.binds = rest_globals();
    m.bound.assign(m.binds.size(), false);
    m.materials.push_back({"Skin", {0.8f, 0.7f, 0.6f, 1.f}, "", false, false});
    const int root = dae_root(s);
    for (const Bind& b : binds) {
        const int j = sk40(b.name);
        if (j < 0) continue;
        m.binds[j] = b.x;
        m.bound[j] = true;
        const std::uint32_t base = std::uint32_t(m.vertex_count());
        const Vec3 corners[4] = {{0, 0, 0}, {size, 0, 0}, {0, size, 0}, {0, 0, size}};
        for (const Vec3& c : corners) {
            const Vec3 p = b.x.apply(c);
            m.positions.insert(m.positions.end(), {float(p.x), float(p.y), float(p.z)});
            m.normals.insert(m.normals.end(), {0, 0, 1});
            m.uvs.insert(m.uvs.end(), {0.25f, 0.75f});
            m.joints.insert(m.joints.end(), {j, root, root, root});
            m.weights.insert(m.weights.end(), {1, 0, 0, 0});
        }
        for (std::uint32_t t : {0u, 1u, 2u, 0u, 3u, 1u, 0u, 2u, 3u, 1u, 3u, 2u}) m.indices.push_back(base + t);
    }
    m.groups.push_back({0, 0, std::uint32_t(m.vertex_count()), 0, std::uint32_t(m.indices.size())});
    return m;
}

RigPart part_of(const DaeModel& m, const std::string& name = "test") {
    RigPart p;
    p.model = &m;
    p.name = name;
    return p;
}

bool has(const std::vector<RigFinding>& f, const char* rule, RigSeverity* sev = nullptr) {
    for (const RigFinding& x : f)
        if (x.rule == rule) {
            if (sev) *sev = x.severity;
            return true;
        }
    return false;
}

const RigFinding* find(const std::vector<RigFinding>& f, const char* rule) {
    for (const RigFinding& x : f)
        if (x.rule == rule) return &x;
    return nullptr;
}

// The harness half of RG-5: where SL puts each joint from the translations the uploader reads (parent-relative, in
// the pelvis-rooted hierarchy, rest rotations identity), so a bound joint's global can be checked to 0.1 mm.
std::map<std::string, Vec3> uploaded_globals(const std::string& dae) {
    const Skeleton& s = skel();
    std::map<std::string, Vec3> local;
    for (auto& [name, t] : uploader_joint_translations(dae)) local[name] = t;
    std::map<std::string, Vec3> g;
    for (int j = 0; j < s.joint_count(); ++j) {
        const Node& n = s[j];
        auto it = local.find(n.name);
        const Vec3 l = it == local.end() ? n.pos : it->second;
        g[n.name] = n.parent >= 0 ? g[s[n.parent].name] + l : l;
    }
    for (auto& v : s.volumes()) {
        auto it = local.find(v.name);
        g[v.name] = g[s[v.joint].name] + (it == local.end() ? v.pos : it->second);
    }
    return g;
}

std::string export_dae(const DaeModel& m, const RigExportOptions& opt = {}) {
    std::string out, err;
    if (!write_rig_dae(skel(), {part_of(m)}, opt, out, err)) std::fprintf(stderr, "  write_rig_dae: %s\n", err.c_str());
    return out;
}

bool reimport(const std::string& dae, DaeModel& m, DaeReport& r) {
    std::string err;
    const bool ok = load_dae(dae, "", skel(), m, r, err);
    if (!ok) std::fprintf(stderr, "  load_dae: %s\n", err.c_str());
    return ok;
}

double dist(const Vec3& a, const Vec3& b) { return (a - b).length(); }

const std::vector<std::string> kArmAndLeg = {"mPelvis", "mTorso", "mChest", "mShoulderLeft", "mElbowLeft", "mWristLeft",
                                             "mHipLeft", "mKneeLeft", "mAnkleLeft"};

}  // namespace

// ---- export (stage 2) and the loader-rules harness (stage 5) ----

TEST(rig_export_rest_rig_writes_defaults_and_round_trips) {
    const std::vector<Xform> rest = rest_globals();
    std::vector<Bind> binds;
    for (const std::string& n : kArmAndLeg) binds.push_back({n, rest[skel().find(n)]});
    const DaeModel m = make_model(binds);
    const std::string dae = export_dae(m);
    CHECK(!dae.empty());
    CHECK(dae.find("<up_axis>Z_UP</up_axis>") != std::string::npos);
    CHECK(dae.find("meter=\"1\"") != std::string::npos);
    CHECK(dae.find("mRoot") == std::string::npos);
    CHECK(dae.find("INV_BIND_MATRIX") != std::string::npos);
    // Every joint node carries SL's default translation: no joint positions.
    const std::map<std::string, Vec3> g = uploaded_globals(dae);
    for (int j = 0; j < skel().joint_count(); ++j) CHECK_NEAR(dist(g.at(skel()[j].name), rest[j].pos), 0, 1e-4);
    RigExportOptions opt;
    for (const RigJoint& r : rig_joints(skel(), m, opt)) CHECK(!r.uploads);
    // Re-import: the same binds, weights, one material.
    DaeModel back;
    DaeReport rep;
    CHECK(reimport(dae, back, rep));
    CHECK(back.rigged);
    CHECK_NEAR(rep.scale, 1.0, 1e-9);
    CHECK(rep.unmapped_joints.empty());
    CHECK_EQ(back.vertex_count(), m.vertex_count());
    CHECK_EQ(back.triangle_count(), m.triangle_count());
    CHECK_EQ(back.materials.size(), size_t(1));
    for (const std::string& n : kArmAndLeg) {
        const int j = skel().find(n);
        CHECK(back.bound[j]);
        CHECK_NEAR(dist(back.binds[j].pos, m.binds[j].pos), 0, 1e-4);
    }
    for (size_t k = 0; k < m.joints.size(); ++k) {
        CHECK_EQ(back.joints[k], m.joints[k]);
        CHECK_NEAR(back.weights[k], m.weights[k], 1e-5);
    }
}

TEST(rig_export_a_pose_keeps_bone_lengths) {
    const Skeleton& s = skel();
    // Arms 45 degrees down: an A-pose. Bone lengths are SL's, so SL must read no joint positions at all.
    Pose pose(s.size());
    pose.rot[s.find("mShoulderLeft")] = Quat::axis_angle({1, 0, 0}, 45 * kDegToRad);
    pose.rot[s.find("mShoulderRight")] = Quat::axis_angle({1, 0, 0}, -45 * kDegToRad);
    const std::vector<Xform> posed = s.global_pose(pose);
    std::vector<Bind> binds;
    for (const char* n : {"mPelvis", "mTorso", "mChest", "mShoulderLeft", "mElbowLeft", "mWristLeft", "mShoulderRight", "mElbowRight", "mWristRight"})
        binds.push_back({n, posed[s.find(n)]});
    const DaeModel m = make_model(binds);
    const std::string dae = export_dae(m);
    CHECK(!dae.empty());
    const std::vector<Xform> rest = rest_globals();
    const std::map<std::string, Vec3> g = uploaded_globals(dae);
    for (const Bind& b : binds) CHECK_NEAR(dist(g.at(b.name), rest[s.find(b.name)].pos), 0, 1e-4);
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    CHECK(has(f, "bind_pose"));
    CHECK(!rig_export_refused(f));
    // The inverse binds carry the pose: re-imported, the binds are the posed globals, rotation included.
    DaeModel back;
    DaeReport rep;
    CHECK(reimport(dae, back, rep));
    const int e = s.find("mElbowLeft");
    CHECK_NEAR(dist(back.binds[e].pos, posed[e].pos), 0, 1e-4);
    CHECK((back.binds[e].rot.conj() * posed[e].rot).angle() < 1e-3);
}

TEST(rig_export_a_pose_longer_forearm_offsets_along_rest_axis) {
    const Skeleton& s = skel();
    Pose pose(s.size());
    pose.rot[s.find("mShoulderLeft")] = Quat::axis_angle({1, 0, 0}, 45 * kDegToRad);
    std::vector<Xform> posed = s.global_pose(pose);
    const int e = s.find("mElbowLeft"), w = s.find("mWristLeft");
    // 3 cm more forearm, along the posed bone.
    posed[w].pos = posed[w].pos + (posed[w].pos - posed[e].pos).normalized() * 0.03;
    std::vector<Bind> binds;
    for (const char* n : {"mPelvis", "mTorso", "mChest", "mShoulderLeft", "mElbowLeft", "mWristLeft"}) binds.push_back({n, posed[s.find(n)]});
    const DaeModel m = make_model(binds);
    const std::string dae = export_dae(m);
    std::map<std::string, Vec3> local;
    for (auto& [name, t] : uploader_joint_translations(dae)) local[name] = t;
    // The wrist's translation is SL's rest offset plus 3 cm along the rest direction; nothing else moves.
    const Vec3 want = s[w].pos + s[w].pos.normalized() * 0.03;
    CHECK_NEAR(dist(local.at("mWristLeft"), want), 0, 1e-4);
    CHECK_NEAR(dist(local.at("mElbowLeft"), s[e].pos), 0, 1e-4);
    RigExportOptions opt;
    int uploads = 0;
    for (const RigJoint& r : rig_joints(s, m, opt)) uploads += r.uploads;
    CHECK_EQ(uploads, 1);
    // Bind pose only: no joint position at all, the inverse binds still posed.
    opt.bind_pose_only = true;
    const std::string plain = export_dae(m, opt);
    local.clear();
    for (auto& [name, t] : uploader_joint_translations(plain)) local[name] = t;
    CHECK_NEAR(dist(local.at("mWristLeft"), s[w].pos), 0, 1e-6);
    for (const RigJoint& r : rig_joints(s, m, opt)) CHECK(!r.uploads);
    DaeModel back;
    DaeReport rep;
    CHECK(reimport(plain, back, rep));
    CHECK_NEAR(dist(back.binds[w].pos, posed[w].pos), 0, 1e-4);
}

TEST(rig_export_creature_positions_within_tenth_mm) {
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    const std::vector<std::string> names = {"mPelvis", "mTorso", "mChest", "mNeck", "mHead", "mHipLeft", "mKneeLeft", "mAnkleLeft",
                                            "mHipRight", "mKneeRight", "mAnkleRight", "mTail1", "mTail2", "mTail3", "mWingsRoot", "mWing1Left"};
    // Long legs, a low tail, a rolled wing root, the head forward: a creature.
    g[s.find("mKneeLeft")].pos.z -= 0.07, g[s.find("mKneeRight")].pos.z -= 0.07;
    g[s.find("mAnkleLeft")].pos.z -= 0.2, g[s.find("mAnkleRight")].pos.z -= 0.2;
    g[s.find("mTail1")].pos.z -= 0.1, g[s.find("mTail2")].pos = g[s.find("mTail2")].pos + Vec3{-0.1, 0, -0.15};
    g[s.find("mTail3")].pos = g[s.find("mTail3")].pos + Vec3{-0.2, 0, -0.2};
    g[s.find("mHead")].pos.x += 0.12345;
    // A roll turns a bone about its own axis (towards its child), so the child stays where it is.
    const Vec3 wing = (g[s.find("mWing1Left")].pos - g[s.find("mWingsRoot")].pos).normalized();
    g[s.find("mWingsRoot")].rot = Quat::axis_angle(wing, 20 * kDegToRad);
    std::vector<Bind> binds;
    for (const std::string& n : names) binds.push_back({n, g[s.find(n)]});
    const DaeModel m = make_model(binds);
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    CHECK(!rig_export_refused(f));
    const std::string dae = export_dae(m);
    const std::map<std::string, Vec3> up = uploaded_globals(dae);
    for (const Bind& b : binds) CHECK_NEAR(dist(up.at(b.name), b.x.pos), 0, 1e-4);  // 0.1 mm
    // Re-import and compare the binds too; the body's proportions come back through shape_from_binds.
    DaeModel back;
    DaeReport rep;
    CHECK(reimport(dae, back, rep));
    CHECK_NEAR(rep.scale, 1.0, 1e-9);
    for (const Bind& b : binds) CHECK_NEAR(dist(back.binds[s.find(b.name)].pos, b.x.pos), 0, 1e-4);
    Shape a, b2;
    CHECK(shape_from_binds(s, {&m}, nullptr, a));
    CHECK(shape_from_binds(s, {&back}, nullptr, b2));
    for (int j = 0; j < s.joint_count(); ++j) CHECK_NEAR(dist(a.offset[j], b2.offset[j]), 0, 1e-4);
    // The joint list holds exactly the weighted joints (all moved ones are weighted here), none over 110.
    const size_t at = dae.find("<Name_array");
    const std::string names_text = dae.substr(dae.find('>', at) + 1, dae.find("</Name_array>") - dae.find('>', at) - 1);
    for (const std::string& n : names) CHECK(names_text.find(n + " ") != std::string::npos);
    CHECK(names_text.find("mRoot") == std::string::npos);
}

TEST(rig_export_moved_unweighted_joint_is_listed) {
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    g[s.find("mKneeLeft")].pos.z -= 0.05;
    DaeModel m = make_model({{"mPelvis", g[0]}, {"mHipLeft", g[s.find("mHipLeft")]}, {"mKneeLeft", g[s.find("mKneeLeft")]}});
    // Take the knee's weights away: it is moved but unweighted.
    for (size_t v = 0; v < m.joints.size(); v += 4)
        if (m.joints[v] == s.find("mKneeLeft")) m.joints[v] = s.find("mHipLeft");
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    CHECK(has(f, "unweighted_moved"));
    const std::string dae = export_dae(m);
    const size_t at = dae.find("<Name_array");
    const std::string names_text = dae.substr(dae.find('>', at) + 1, dae.find("</Name_array>") - dae.find('>', at) - 1);
    CHECK(names_text.find("mKneeLeft ") != std::string::npos);
    CHECK_NEAR(dist(uploaded_globals(dae).at("mKneeLeft"), g[s.find("mKneeLeft")].pos), 0, 1e-4);
    // Without joint positions it is not listed, and sits at default.
    RigExportOptions opt;
    opt.joint_positions = false;
    const std::string plain = export_dae(m, opt);
    const size_t at2 = plain.find("<Name_array");
    CHECK(plain.substr(at2, plain.find("</Name_array>") - at2).find("mKneeLeft") == std::string::npos);
    CHECK_NEAR(dist(uploaded_globals(plain).at("mKneeLeft"), rest_globals()[s.find("mKneeLeft")].pos), 0, 1e-6);
}

TEST(rig_export_collision_volumes_carry_rotation_and_scale) {
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    const int belly = sk40("BELLY"), pec = sk40("LEFT_PEC"), torso = s.find("mTorso"), chest = s.find("mChest");
    g[belly].pos.x += 0.02;  // the belly 2 cm forward: a fitted body with its own belly
    const DaeModel m = make_model({{"mPelvis", g[0]}, {"mTorso", g[torso]}, {"mChest", g[chest]}, {"BELLY", g[belly]}, {"LEFT_PEC", g[pec]}});
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    CHECK(has(f, "volumes"));
    CHECK(!rig_export_refused(f));
    const std::string dae = export_dae(m);
    // The volume nodes hang off their joints, and BELLY's translation carries the move.
    CHECK(dae.find("name=\"BELLY\" sid=\"BELLY\" type=\"JOINT\"") != std::string::npos);
    const std::map<std::string, Vec3> up = uploaded_globals(dae);
    CHECK_NEAR(dist(up.at("BELLY"), g[belly].pos), 0, 1e-4);
    CHECK_NEAR(dist(up.at("LEFT_PEC"), g[pec].pos), 0, 1e-4);
    // BELLY's inverse bind includes SL's volume scale: its rows are 1 / scale long.
    const CollisionVolume& v = s.volumes()[belly - dae_root(s) - 1];
    const size_t at = dae.find("<Name_array");
    const std::string names_text = dae.substr(dae.find('>', at) + 1, dae.find("</Name_array>") - dae.find('>', at) - 1);
    int slot = 0;
    for (size_t i = 0, n = 0; i < names_text.size(); ++i)
        if (names_text[i] == ' ') {
            if (names_text.substr(0, i).rfind("BELLY") == i - 5) slot = int(n);
            ++n;
        }
    const size_t bp = dae.find("bind_poses-array\" count");
    const std::string arr = dae.substr(dae.find('>', bp) + 1, dae.find("</float_array>", bp) - dae.find('>', bp) - 1);
    std::vector<double> f16;
    for (size_t i = 0; i < arr.size();) {
        while (i < arr.size() && arr[i] == ' ') ++i;
        size_t b = i;
        while (i < arr.size() && arr[i] != ' ') ++i;
        if (i > b) f16.push_back(std::atof(arr.substr(b, i - b).c_str()));
    }
    CHECK(f16.size() >= size_t(slot + 1) * 16);
    const double* ibm = f16.data() + slot * 16;
    for (int row = 0; row < 3; ++row)
        CHECK_NEAR(std::hypot(ibm[row * 4], ibm[row * 4 + 1], ibm[row * 4 + 2]), 1.0 / v.scale[row], 1e-3);
    // Re-imported, the volume's bind is where it was (the importer drops the scale again).
    DaeModel back;
    DaeReport rep;
    CHECK(reimport(dae, back, rep));
    CHECK(back.bound[belly]);
    CHECK_NEAR(dist(back.binds[belly].pos, g[belly].pos), 0, 1e-4);
    CHECK((back.binds[belly].rot.conj() * g[belly].rot).angle() < 1e-3);
    CHECK_EQ(back.joints[12 * 4], belly);  // the belly's first vertex (the fourth tetrahedron) stays weighted to BELLY
}

TEST(rig_export_pelvis_offset_only_when_asked) {
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    for (Xform& x : g) x.pos.z += 0.03;  // the whole rig 3 cm up: a pelvis offset
    const DaeModel m = make_model({{"mPelvis", g[0]}, {"mTorso", g[s.find("mTorso")]}});
    RigExportOptions opt;
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, opt);
    RigSeverity sev;
    CHECK(has(f, "pelvis", &sev));
    CHECK(sev == RigSeverity::Info);
    const std::string dae = export_dae(m, opt);
    std::map<std::string, Vec3> up = uploaded_globals(dae);
    CHECK_NEAR(dist(up.at("mPelvis"), rest_globals()[0].pos), 0, 1e-6);
    CHECK_NEAR(dist(up.at("mTorso"), rest_globals()[s.find("mTorso")].pos), 0, 1e-4);  // keeps its offset from the pelvis
    opt.pelvis_offset = true;
    f = check_rig_export(s, {part_of(m)}, opt);
    CHECK(has(f, "pelvis", &sev));
    CHECK(sev == RigSeverity::Warning);
    up = uploaded_globals(export_dae(m, opt));
    CHECK_NEAR(dist(up.at("mPelvis"), g[0].pos), 0, 1e-4);
    CHECK_NEAR(dist(up.at("mTorso"), g[s.find("mTorso")].pos), 0, 1e-4);
}

TEST(rig_export_110_joint_limit) {
    const Skeleton& s = skel();
    const std::vector<Xform> rest = rest_globals();
    std::vector<Bind> binds;
    for (int j = 0; j < s.joint_count() && binds.size() < 110; ++j) binds.push_back({s[j].name, rest[j]});
    DaeModel m = make_model(binds);
    CHECK(!rig_export_refused(check_rig_export(s, {part_of(m)}, {})));
    CHECK(!export_dae(m).empty());
    // One more joint, moved but unweighted, is the 111th.
    m.binds[110].pos.z += 0.01;
    m.bound[110] = true;
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    RigSeverity sev;
    CHECK(has(f, "too_many_joints", &sev));
    CHECK(sev == RigSeverity::Error);
    CHECK(find(f, "too_many_joints")->message.find("111") != std::string::npos);
    std::string out, err;
    CHECK(!write_rig_dae(s, {part_of(m)}, {}, out, err));
    CHECK(err.find("111 joints") != std::string::npos);
    // Without joint positions the moved joint is not listed, and the mesh fits again.
    RigExportOptions opt;
    opt.joint_positions = false;
    CHECK(!has(check_rig_export(s, {part_of(m)}, opt), "too_many_joints"));
}

// ---- validation (stage 3): every rule fires ----

TEST(rig_check_not_rigged_and_unknown_joints) {
    DaeModel m;
    m.positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    m.indices = {0, 1, 2};
    std::vector<RigFinding> f = check_rig_export(skel(), {part_of(m)}, {});
    RigSeverity sev;
    CHECK(has(f, "not_rigged", &sev));
    CHECK(sev == RigSeverity::Error);
    const DaeModel rigged = make_model({{"mPelvis", rest_globals()[0]}});
    RigPart p = part_of(rigged);
    p.unmapped_joints = {"Bone", "Bone.001"};
    f = check_rig_export(skel(), {p}, {});
    CHECK(has(f, "unknown_joints", &sev));
    CHECK(sev == RigSeverity::Warning);
    CHECK(find(f, "unknown_joints")->joints.size() == 2);
}

TEST(rig_check_zero_weight_vertices_fix_weights_to_nearest_bone) {
    const Skeleton& s = skel();
    const std::vector<Xform> rest = rest_globals();
    DaeModel m = make_model({{"mPelvis", rest[0]}, {"mHead", rest[s.find("mHead")]}});
    // The head's 4 vertices lose their weights.
    for (int v = 4; v < 8; ++v) m.joints[v * 4] = dae_root(s), m.weights[v * 4] = 0;
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    const RigFinding* z = find(f, "zero_weight_vertices");
    CHECK(z && z->severity == RigSeverity::Error && z->fix);
    CHECK(z->message.find("4 vertices") != std::string::npos);
    std::string out, err;
    CHECK(!write_rig_dae(s, {part_of(m)}, {}, out, err));
    z->fix(m);
    for (int v = 4; v < 8; ++v) CHECK_EQ(m.joints[v * 4], s.find("mHead"));  // nearest weighted bone: the head, not the pelvis
    CHECK(!has(check_rig_export(s, {part_of(m)}, {}), "zero_weight_vertices"));
}

TEST(rig_check_false_offsets_snap_to_default) {
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    const int k = s.find("mKneeLeft"), a = s.find("mAnkleLeft");
    g[k].pos.z += 0.0004;   // 0.4 mm: noise
    g[a].pos.z -= 0.00005;  // 0.05 mm: under SL's threshold, ignored
    DaeModel m = make_model({{"mPelvis", g[0]}, {"mHipLeft", g[s.find("mHipLeft")]}, {"mKneeLeft", g[k]}, {"mAnkleLeft", g[a]}});
    RigExportOptions opt;
    std::vector<RigJoint> rows = rig_joints(s, m, opt);
    const RigJoint *knee = nullptr, *ankle = nullptr;
    for (const RigJoint& r : rows) {
        if (r.node == k) knee = &r;
        if (r.node == a) ankle = &r;
    }
    CHECK(knee && knee->uploads && knee->noise && knee->listed && knee->weighted);
    CHECK_NEAR(knee->offset_mm, 0.4, 0.01);
    // The uploader compares parent-relative translations: the ankle sits 0.05 mm low under a knee 0.4 mm high, so its
    // own translation is 0.45 mm off, noise too.
    CHECK(ankle && ankle->noise);
    CHECK_NEAR(ankle->offset_mm, 0.45, 0.01);
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, opt);
    const RigFinding* fo = find(f, "false_offsets");
    CHECK(fo && fo->severity == RigSeverity::Warning && fo->fix);
    CHECK((fo->joints == std::vector<std::string>{"mKneeLeft", "mAnkleLeft"}));
    fo->fix(m);
    rows = rig_joints(s, m, opt);
    for (const RigJoint& r : rows)
        if (r.node == k || r.node == a) CHECK(r.offset_mm < 1e-6 && !r.uploads);
    CHECK(!has(check_rig_export(s, {part_of(m)}, opt), "false_offsets"));
    // snap_joint_to_default itself: a bound joint snaps (again) to exactly default; an unbound joint refuses.
    CHECK(snap_joint_to_default(s, m, a));
    CHECK(!snap_joint_to_default(s, m, s.find("mHead")));
}

TEST(rig_check_scale_faces_and_triangles) {
    const Skeleton& s = skel();
    const std::vector<Xform> rest = rest_globals();
    DaeModel m = make_model({{"mPelvis", rest[0]}, {"mTorso", rest[s.find("mTorso")]}});
    RigPart p = part_of(m);
    p.scale = 1, p.measured_scale = 1.08;
    std::vector<RigFinding> f = check_rig_export(s, {p}, {});
    CHECK(has(f, "scale_mismatch"));
    CHECK(has(f, "triangles"));
    p.measured_scale = 1.005;
    CHECK(!has(check_rig_export(s, {p}, {}), "scale_mismatch"));
    // Nine materials: the uploader splits the mesh. 65,535 vertices on one: it refuses it.
    m.groups.clear();
    for (int g = 0; g < 9; ++g) m.groups.push_back({g, 0, 1, 0, 0}), m.materials.push_back({"m" + std::to_string(g)});
    f = check_rig_export(s, {part_of(m)}, {});
    RigSeverity sev;
    CHECK(has(f, "faces", &sev));
    CHECK(sev == RigSeverity::Warning);
    CHECK(!rig_export_refused(f));
    // 65,535 corners that differ in position: the uploader cannot share any of them.
    m.groups[0].first_vertex = std::uint32_t(m.vertex_count());
    m.groups[0].vertex_count = 65535;
    for (int v = 0; v < 65535; ++v) {
        m.positions.insert(m.positions.end(), {float(v), 0.f, 0.f});
        m.normals.insert(m.normals.end(), {0.f, 0.f, 1.f});
        m.uvs.insert(m.uvs.end(), {0.f, 0.f});
        m.joints.insert(m.joints.end(), {0, dae_root(s), dae_root(s), dae_root(s)});
        m.weights.insert(m.weights.end(), {1.f, 0.f, 0.f, 0.f});
    }
    f = check_rig_export(s, {part_of(m)}, {});
    CHECK(has(f, "face_vertices", &sev));
    CHECK(sev == RigSeverity::Error);
    CHECK(find(f, "face_vertices")->message.find("Skin") != std::string::npos);
}

TEST(rig_export_two_parts_share_one_skeleton) {
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    g[s.find("mKneeLeft")].pos.z -= 0.05;
    const DaeModel upper = make_model({{"mPelvis", g[0]}, {"mChest", g[s.find("mChest")]}});
    const DaeModel lower = make_model({{"mPelvis", g[0]}, {"mHipLeft", g[s.find("mHipLeft")]}, {"mKneeLeft", g[s.find("mKneeLeft")]}});
    std::string out, err;
    std::vector<RigPart> parts = {part_of(upper, "upper"), part_of(lower, "lower")};
    CHECK(write_rig_dae(s, parts, {}, out, err));
    CHECK(out.find("<geometry id=\"mesh0\" name=\"upper\"") != std::string::npos);
    CHECK(out.find("<geometry id=\"mesh1\" name=\"lower\"") != std::string::npos);
    CHECK(out.find("<controller id=\"mesh1-skin\"") != std::string::npos);
    // One skeleton: the knee's position, bound only by the lower part, is written.
    CHECK_NEAR(dist(uploaded_globals(out).at("mKneeLeft"), g[s.find("mKneeLeft")].pos), 0, 1e-4);
    DaeModel back;
    DaeReport rep;
    CHECK(reimport(out, back, rep));
    CHECK_EQ(back.triangle_count(), upper.triangle_count() + lower.triangle_count());
}

TEST(uploader_joint_translations_reads_as_lldaeloader) {
    const std::string dae = R"(<?xml version="1.0"?><COLLADA><library_visual_scenes><visual_scene id="S">
<node id="a" name="mPelvis" type="JOINT"><translate sid="location">1 2 3</translate><translate>9 9 9</translate>
  <node id="b" name="mTorso" type="JOINT"><matrix sid="transform">1 0 0 4 0 1 0 5 0 0 1 6 0 0 0 1</matrix></node>
  <node id="c" name="mChest" type="JOINT"><translate>7 8 9</translate></node>
  <node id="d" name="mNeck" type="JOINT"><translate>0 0 0</translate><translate sid="translate">1 1 1</translate></node>
  <node id="e" name="Armature" type="NODE"><translate>5 5 5</translate></node>
</node></visual_scene></library_visual_scenes></COLLADA>)";
    const auto t = uploader_joint_translations(dae);
    CHECK_EQ(t.size(), size_t(4));
    std::map<std::string, Vec3> m(t.begin(), t.end());
    CHECK((m.at("mPelvis") == Vec3{1, 2, 3}));
    CHECK((m.at("mTorso") == Vec3{4, 5, 6}));
    CHECK((m.at("mChest") == Vec3{7, 8, 9}));
    CHECK((m.at("mNeck") == Vec3{1, 1, 1}));
}

// ---- where SL stands it (spec 08 RG-8) ----

namespace {

// LLAvatarAppearance::computeBodySize for the default shape (every scale 1), from the translations the uploader reads
// and SL's defaults for the joints the file leaves out.
SlBodySize viewer_body_size(const std::string& dae) {
    const Skeleton& s = skel();
    std::map<std::string, Vec3> t;
    for (auto& [name, v] : uploader_joint_translations(dae)) t[name] = v;
    auto z = [&](const char* n) { return t.count(n) ? t.at(n).z : s[s.find(n)].pos.z; };
    SlBodySize b;
    b.pelvis_to_foot = z("mHipLeft") - z("mKneeLeft") - z("mAnkleLeft") - z("mFootLeft");
    b.height = b.pelvis_to_foot + std::sqrt(2.0) * z("mSkull") + z("mHead") + z("mNeck") + z("mChest") + z("mTorso");
    return b;
}

}  // namespace

TEST(rig_height_neck_stretch_sinks_half_and_mskull_evens_it) {
    // A head 10 cm higher on a longer neck: the deformer case checked in-world (2026-09-29), which sinks the
    // wearer by half the extra height, 5 cm.
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    const int head = s.find("mHead"), skull = s.find("mSkull");
    g[head].pos.z += 0.10;
    DaeModel m = make_model({{"mPelvis", g[0]}, {"mNeck", g[s.find("mNeck")]}, {"mHead", g[head]}});
    RigHeight h = rig_in_world_height(s, {part_of(m)}, {});
    CHECK(h.valid);
    CHECK_NEAR(h.sole, -0.05, 1e-6);
    CHECK_NEAR(h.body - h.default_body, 0.10, 1e-6);
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    const RigFinding* w = find(f, "in_world_height");
    CHECK(w && w->severity == RigSeverity::Warning && w->fix && w->part == 0);
    CHECK(w->message.find("Sinks 5.0 cm") != std::string::npos);
    CHECK(w->message.find("\"Z offset (raise or lower avatar)\" to 0.050") != std::string::npos);
    // The fix: mSkull 10/sqrt(2) cm lower, unweighted but listed, so its position uploads; the head stays.
    w->fix(m);
    h = rig_in_world_height(s, {part_of(m)}, {});
    CHECK_NEAR(h.sole, 0, 1e-6);
    CHECK(!has(check_rig_export(s, {part_of(m)}, {}), "in_world_height"));
    const std::string dae = export_dae(m);
    std::map<std::string, Vec3> t;
    for (auto& [name, v] : uploader_joint_translations(dae)) t[name] = v;
    CHECK_NEAR(t.at("mSkull").z, s[skull].pos.z - 0.10 / std::sqrt(2.0), 1e-6);
    CHECK_NEAR(dist(uploaded_globals(dae).at("mHead"), g[head].pos), 0, 1e-4);
    const size_t at = dae.find("<Name_array");
    CHECK(dae.substr(at, dae.find("</Name_array>") - at).find("mSkull") != std::string::npos);
    for (size_t k = 0; k < m.joints.size(); ++k) CHECK(m.joints[k] != skull || m.weights[k] == 0);
    // The viewer's own sum over the uploaded file gives the default height back.
    CHECK_NEAR(viewer_body_size(dae).height, h.default_body, 1e-5);
}

TEST(rig_height_level_spine_creature_floats) {
    // A creature: its body along the ground at 60 cm, the hind (left) leg short, the head forward.
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    auto at = [&](const char* n, Vec3 p) { g[s.find(n)].pos = p; };
    at("mPelvis", {0, 0, 0.60});
    at("mTorso", {0.20, 0, 0.62});
    at("mChest", {0.40, 0, 0.64});
    at("mNeck", {0.50, 0, 0.75});
    at("mHead", {0.60, 0, 0.85});
    at("mHipLeft", {0.0, 0.10, 0.55});
    at("mKneeLeft", {0.05, 0.10, 0.32});
    at("mAnkleLeft", {-0.02, 0.10, 0.10});
    at("mFootLeft", {0.0, 0.10, 0.02});
    std::vector<Bind> binds;
    for (const char* n : {"mPelvis", "mTorso", "mChest", "mNeck", "mHead", "mHipLeft", "mKneeLeft", "mAnkleLeft", "mFootLeft"})
        binds.push_back({n, g[s.find(n)]});
    DaeModel m = make_model(binds);
    const std::string dae = export_dae(m);
    const SlBodySize live = viewer_body_size(dae), def = sl_body_size(s);
    // Its ground, 60 cm under the pelvis as bound, where the viewer puts the pelvis: SL's default pelvis height,
    // lowered by half the height the region keeps over this one, raised by the change in pelvis-to-foot.
    const double want = (s[0].pos.z - 0.60) + 0.5 * (def.height - live.height) + (live.pelvis_to_foot - def.pelvis_to_foot);
    CHECK(want > 0.3);  // it floats, a lot
    const RigHeight h = rig_in_world_height(s, {part_of(m)}, {});
    CHECK_NEAR(h.sole, want, 1e-5);
    CHECK_NEAR(h.body, live.height, 1e-5);
    const std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    const RigFinding* w = find(f, "in_world_height");
    CHECK(w && w->message.rfind("Floats ", 0) == 0);
    if (w) w->fix(m);
    CHECK_NEAR(rig_in_world_height(s, {part_of(m)}, {}).sole, 0, 1e-6);
    // mSkull weighted in a part: no fix offered, only the uploader's Z offset.
    const DaeModel body = make_model(binds);
    const DaeModel hat = make_model({{"mPelvis", g[0]}, {"mSkull", g[s.find("mHead")] * Xform{{}, s[s.find("mSkull")].pos}}});
    const std::vector<RigFinding> f2 = check_rig_export(s, {part_of(body), part_of(hat)}, {});
    const RigFinding* w2 = find(f2, "in_world_height");
    CHECK(w2 && !w2->fix && w2->message.find("Z offset") != std::string::npos);
    // An SL-proportioned rig stands where it should: nothing to report.
    std::vector<Bind> rest;
    for (const std::string& n : kArmAndLeg) rest.push_back({n, rest_globals()[s.find(n)]});
    CHECK(!has(check_rig_export(s, {part_of(make_model(rest))}, {}), "in_world_height"));
}

// ---- stand it in SL's rest pose (spec 08 RG-9) ----

namespace {

Quat arc_for_test(const Vec3& u, const Vec3& v) {
    const Vec3 c = u.cross(v);
    return Quat{1 + u.dot(v), c.x, c.y, c.z}.normalized();
}

// How far off straight down the left forearm's end points, in degrees, once the exported file is read back and posed by
// an SL animation that lowers SL's rest arm (out level) straight down.
double arm_down_error(const std::string& dae) {
    const Skeleton& s = skel();
    DaeModel back;
    DaeReport rep;
    if (!reimport(dae, back, rep)) return 180;
    Shape shape;
    const Shape* sh = shape_from_binds(s, {&back}, nullptr, shape) ? &shape : nullptr;
    const int sho = s.find("mShoulderLeft"), wri = s.find("mWristLeft");
    const std::vector<Xform> rest = s.global_pose(Pose(s.size()));
    Pose pose(s.size());
    pose.rot[sho] = arc_for_test((rest[wri].pos - rest[sho].pos).normalized(), {0, 0, -1});
    const std::vector<Xform> g = s.global_pose(pose, sh);
    std::vector<float> pos, nrm;
    skin_prop(back, s, g, sh, pos, nrm);
    for (size_t v = 0; v < back.joints.size() / 4; ++v)
        if (back.joints[v * 4] == wri) {  // the wrist tetrahedron's first corner: the wrist itself
            const Vec3 d = (Vec3{pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]} - g[sho].pos).normalized();
            return std::acos(std::clamp(d.dot({0, 0, -1}), -1.0, 1.0)) * kRadToDeg;
        }
    return 180;
}

}  // namespace

TEST(rig_export_rest_pose_stands_a_mapped_a_pose_in_t_pose) {
    // A mapped rig modelled in an A-pose: arms 45 degrees down, bound on SL's unturned joints (settle_rig's foreign
    // rigs), forearms 3 cm longer than SL's.
    const Skeleton& s = skel();
    Pose a(s.size());
    a.rot[s.find("mShoulderLeft")] = Quat::axis_angle({1, 0, 0}, 45 * kDegToRad);
    a.rot[s.find("mShoulderRight")] = Quat::axis_angle({1, 0, 0}, -45 * kDegToRad);
    std::vector<Xform> posed = s.global_pose(a);
    const std::vector<Xform> rest = rest_globals();
    const int e = s.find("mElbowLeft"), w = s.find("mWristLeft");
    posed[w].pos = posed[w].pos + (posed[w].pos - posed[e].pos).normalized() * 0.03;
    std::vector<Bind> binds;
    for (const char* n : {"mPelvis", "mTorso", "mChest", "mNeck", "mHead", "mCollarLeft", "mShoulderLeft", "mElbowLeft", "mWristLeft",
                          "mHandMiddle1Left", "mCollarRight", "mShoulderRight", "mElbowRight", "mWristRight"})
        binds.push_back({n, Xform{rest[s.find(n)].rot, posed[s.find(n)].pos}});
    const DaeModel m = make_model(binds);
    // As it is: the A-pose is written as joint positions, and an SL animation turns the arm 45 degrees too far.
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    const RigFinding* hint = find(f, "rest_pose");
    CHECK(hint && hint->message.find("45 degrees") != std::string::npos);
    const double plain = arm_down_error(export_dae(m));
    CHECK(plain > 40);
    // Stood in SL's rest pose: the joints are written out level along SL's rest directions at the file's lengths
    // (the forearm still 3 cm long), the binds carry the A-pose, and the same animation puts the arm straight down.
    RigExportOptions opt;
    opt.rest_pose = true;
    CHECK(!has(check_rig_export(s, {part_of(m)}, opt), "rest_pose"));
    const std::string dae = export_dae(m, opt);
    const std::map<std::string, Vec3> up = uploaded_globals(dae);
    CHECK_NEAR(dist(up.at("mElbowLeft"), rest[e].pos), 0, 1e-4);
    const Vec3 forearm = up.at("mWristLeft") - up.at("mElbowLeft");
    CHECK_NEAR(forearm.length(), (rest[w].pos - rest[e].pos).length() + 0.03, 1e-4);
    CHECK(forearm.normalized().dot((rest[w].pos - rest[e].pos).normalized()) > std::cos(0.5 * kDegToRad));
    CHECK(arm_down_error(dae) < 2);
    // The hand turns with the forearm: the finger stays in line, as modelled.
    const int mid = s.find("mHandMiddle1Left");
    const Vec3 hand = up.at("mHandMiddle1Left") - up.at("mWristLeft");
    CHECK(hand.normalized().dot((posed[mid].pos - posed[w].pos).normalized()) < 0.9);  // no longer hanging
    CHECK(hand.normalized().dot((rest[mid].pos - rest[w].pos).normalized()) > 0.99);
    // The body is untouched.
    CHECK_NEAR(dist(up.at("mHead"), rest[s.find("mHead")].pos), 0, 1e-4);
}

// ---- shape-proof (spec 08 RG-10) ----

TEST(rig_export_shape_proof_lists_every_ancestor_over_the_threshold) {
    // A creature's longer shin: only the knee's position uploads, so SL locks only its scale, and the wearer's shape
    // sliders still scale the hip and the pelvis above it (LLVOAvatar::addAttachmentOverridesForObject locks a scale
    // only for a joint listed and moved over 0.1 mm).
    const Skeleton& s = skel();
    std::vector<Xform> g = rest_globals();
    const int pelvis = 0, hip = s.find("mHipLeft"), knee = s.find("mKneeLeft"), ankle = s.find("mAnkleLeft");
    g[ankle].pos.z -= 0.05;
    g[s.find("mFootLeft")].pos.z -= 0.05;
    const DaeModel m = make_model({{"mPelvis", g[pelvis]}, {"mHipLeft", g[hip]}, {"mKneeLeft", g[knee]}, {"mAnkleLeft", g[ankle]}});
    auto uploads = [&](const RigExportOptions& opt) {
        std::set<int> out;
        for (const RigJoint& r : rig_joints(s, m, opt))
            if (r.uploads) out.insert(r.node);
        return out;
    };
    RigExportOptions opt;
    std::set<int> plain = uploads(opt);
    CHECK(plain.count(ankle) && !plain.count(knee) && !plain.count(hip) && !plain.count(pelvis));
    opt.shape_proof = true;
    std::set<int> proof = uploads(opt);
    for (int j : {pelvis, hip, knee, ankle}) CHECK(proof.count(j));
    // Every weighted joint's ancestors too: the torso is weighted nowhere and sits above nothing listed, so it is left.
    CHECK(!proof.count(s.find("mTorso")));
    // The nudged ones sit 0.11 mm off SL's default, as the uploader reads the file, and are not reported as noise.
    const std::string dae = export_dae(m, opt);
    std::map<std::string, Vec3> t;
    for (auto& [name, v] : uploader_joint_translations(dae)) t[name] = v;
    for (int j : {pelvis, hip, knee}) {
        const double off = (t.at(s[j].name) - s[j].pos).length();
        CHECK(off > 0.0001 && off < 0.00012);
    }
    for (const RigJoint& r : rig_joints(s, m, opt))
        if (r.node == hip) CHECK(r.nudged && !r.noise);
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, opt);
    CHECK(!has(f, "false_offsets"));
    const RigFinding* sp = find(f, "shape_proof");
    CHECK(sp && sp->message.find("Lock scale if joint position defined") != std::string::npos);
    // The mesh stays where it was bound, to the nudge.
    for (const char* n : {"mHipLeft", "mKneeLeft", "mAnkleLeft"}) CHECK_NEAR(dist(uploaded_globals(dae).at(n), g[s.find(n)].pos), 0, 3e-4);
}

// ---- vertices that share a position (spec 08 RG-11) ----

TEST(rig_check_shared_positions_and_nudge_apart) {
    const Skeleton& s = skel();
    const std::vector<Xform> rest = rest_globals();
    DaeModel m = make_model({{"mPelvis", rest[0]}, {"mHipLeft", rest[s.find("mHipLeft")]}, {"mKneeLeft", rest[s.find("mKneeLeft")]}});
    const int root = dae_root(s), hip = s.find("mHipLeft"), knee = s.find("mKneeLeft");
    auto vertex = [&](Vec3 p, int j, Vec3 n) {
        m.positions.insert(m.positions.end(), {float(p.x), float(p.y), float(p.z)});
        m.normals.insert(m.normals.end(), {float(n.x), float(n.y), float(n.z)});
        m.uvs.insert(m.uvs.end(), {0.f, 0.f});
        m.joints.insert(m.joints.end(), {j, root, root, root});
        m.weights.insert(m.weights.end(), {1.f, 0.f, 0.f, 0.f});
        return m.vertex_count() - 1;
    };
    // Two plates touching at the knee: one corner on the thigh's plate, one on the shin's, at the same spot.
    const Vec3 at = rest[knee].pos + Vec3{0.08, 0, 0};
    const int a = vertex(at, hip, {0, 0, 1}), b = vertex(at, knee, {0, 0, -1});
    // A UV seam: same position, same weights. The uploader shares those happily.
    vertex(rest[hip].pos + Vec3{0.08, 0, 0}, hip, {1, 0, 0}), vertex(rest[hip].pos + Vec3{0.08, 0, 0}, hip, {1, 0, 0});
    // Close, not equal: 0.0005 mm apart is inside the uploader's 1e-5 of the model's size (about 0.15 m across here).
    const int c = vertex(at + Vec3{0, 0.1, 0}, hip, {0, 1, 0}), d = vertex(at + Vec3{0, 0.1 + 0.0000005, 0}, knee, {0, -1, 0});
    std::vector<RigFinding> f = check_rig_export(s, {part_of(m)}, {});
    const RigFinding* sp = find(f, "shared_positions");
    CHECK(sp && sp->severity == RigSeverity::Warning && sp->fix);
    CHECK(sp && sp->message.find("4 vertices") == sp->message.find(": ") + 2);
    const std::vector<float> before = m.positions;
    sp->fix(m);
    CHECK(!has(check_rig_export(s, {part_of(m)}, {}), "shared_positions"));
    // Only the second of each pair moved, along its normal, by a few hundredths of a millimetre.
    auto moved = [&](int v) { return dist({before[v * 3], before[v * 3 + 1], before[v * 3 + 2]}, {m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]}); };
    CHECK(moved(a) == 0 && moved(c) == 0);
    CHECK(moved(b) > 0 && moved(b) < 0.0001 && moved(d) > 0 && moved(d) < 0.0001);
    CHECK(m.positions[b * 3 + 2] < before[b * 3 + 2]);  // down, along the shin plate's normal
}
