// Rigging a model from scratch (spec 08 RG-13, RG-14): markers fit SL's skeleton, the guess finds the joints of an
// unrigged body, and the rig saves, loads and exports with its weights. On CC0 bodies made in code (tools/blob_body.h).
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

#include "../tools/blob_body.h"
#include "check.h"
#include "fixtures.h"
#include "vats/auto_rig.h"
#include "vats/fbx.h"
#include "vats/rig_export.h"
#include "vats/rig_map.h"
#include "vats/weight_paint.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

// The blob humanoid stood up as the rig would stand it (it is built facing +X on the ground already).
DaeModel body(bool a_pose = false, double cell = 0.025) {
    DaeModel m = blob::mesh(blob::humanoid(a_pose), cell);
    place_model(m, scratch_placement(m, 0, 0));
    return m;
}

// The body's own joints, moved as placement moved the mesh.
std::map<std::string, Vec3> truth(bool a_pose = false, double cell = 0.025) {
    DaeModel m = blob::mesh(blob::humanoid(a_pose), cell);
    const ScratchPlacement pl = scratch_placement(m, 0, 0);
    std::map<std::string, Vec3> out;
    for (const auto& [id, p] : blob::humanoid(a_pose).joints) out[id] = pl.apply(p);
    return out;
}

double weight_on(const DaeModel& m, int v, int node) {
    double w = 0;
    for (int k = 0; k < 4; ++k)
        if (m.joints[size_t(v) * 4 + size_t(k)] == node) w += m.weights[size_t(v) * 4 + size_t(k)];
    return w;
}

}  // namespace

TEST(scratch_markers_fit_sl_joints_where_expected) {
    const DaeModel m = body();
    const auto t = truth();
    ScratchRig rig;
    for (const auto& [id, p] : t)
        if (find_rig_marker(id)) rig.markers[id] = {p, 100, ""};
    const ScratchFit fit = fit_scratch_joints(skel(), m, rig);
    auto J = [&](const char* n) { return fit.joints.count(n) ? fit.joints.at(n) : Vec3{1e9, 1e9, 1e9}; };
    // The markers' own joints sit on them.
    for (const auto& [id, joint] : std::vector<std::pair<std::string, std::string>>{
             {"shoulder_l", "mShoulderLeft"}, {"elbow_r", "mElbowRight"}, {"wrist_l", "mWristLeft"}, {"hip_r", "mHipRight"},
             {"knee_l", "mKneeLeft"}, {"ankle_r", "mAnkleRight"}, {"toe_l", "mToeLeft"}, {"neck", "mNeck"}, {"groin", "mGroin"}})
        CHECK_NEAR((fit.joints.at(joint) - t.at(id)).length(), 0, 1e-9);
    // The pelvis above the hips, between them; the spine on the line from it to the neck, in SL's order.
    const Vec3 pelvis = J("mPelvis"), neck = J("mNeck");
    const double middle = (t.at("hip_l").y + t.at("hip_r").y) / 2;
    CHECK(pelvis.z > t.at("hip_l").z && std::fabs(pelvis.y - middle) < 0.002);  // SL's own hips sit a millimetre off its middle
    for (const char* n : {"mTorso", "mChest"}) CHECK_NEAR((closest_on_segment(J(n), pelvis, neck) - J(n)).length(), 0, 1e-6);
    CHECK(pelvis.z < J("mTorso").z && J("mTorso").z < J("mChest").z && J("mChest").z < neck.z);
    // Collars between the chest and the shoulders, at shoulder height; the foot between the ankle and the toes.
    CHECK(J("mCollarLeft").y > J("mChest").y && J("mCollarLeft").y < J("mShoulderLeft").y);
    CHECK_NEAR(J("mCollarLeft").z, J("mShoulderLeft").z, 1e-9);
    CHECK(J("mFootLeft").x > J("mAnkleLeft").x && J("mFootLeft").x < J("mToeLeft").x);
    // The head above the chin and the skull above the head.
    CHECK(J("mHead").z > t.at("chin").z && J("mSkull").z > J("mHead").z);
    // Fingers out along the hand (a T-pose: the left hand reaches +Y), within it, the thumb ahead of the little finger.
    for (const char* f : {"Thumb", "Index", "Middle", "Ring", "Pinky"})
        for (int seg = 1; seg <= 3; ++seg) {
            const Vec3 p = J((std::string("mHand") + f + std::to_string(seg) + "Left").c_str());
            CHECK(p.y > t.at("wrist_l").y && p.y < t.at("hand_tip_l").y + 0.01);
        }
    CHECK(J("mHandThumb1Left").x > J("mHandPinky1Left").x + 0.02);
    CHECK(J("mHandMiddle3Left").y > J("mHandMiddle1Left").y + 0.03);
    // A symmetric body with symmetric markers: every left joint the mirror image of its right one across its middle (to
    // within its surface's facets: the fingers follow the hand's shape).
    for (const auto& [name, p] : fit.joints)
        if (name.find("Left") != std::string::npos)
            CHECK_NEAR((Vec3{p.x, 2 * middle - p.y, p.z} - J(Skeleton::mirror_name(name).c_str())).length(), 0, 0.012);
    // Placing a marker with mirroring moves its partner to the mirror image; a middle marker stays on the middle.
    place_marker(rig, "wrist_l", {0.1, 0.7, 1.3}, true);
    CHECK(rig.markers.at("wrist_r").pos == (Vec3{0.1, -0.7, 1.3}));
    CHECK_EQ(rig.markers.at("wrist_l").confidence, 100);
    place_marker(rig, "neck", {0.0, 0.05, 1.45}, true);
    CHECK_NEAR(rig.markers.at("neck").pos.y, 0, 1e-12);
    place_marker(rig, "wrist_l", {0.1, 0.6, 1.3}, false);
    CHECK(rig.markers.at("wrist_r").pos == (Vec3{0.1, -0.7, 1.3}));
}

// A joint no marker places can be pinned: it goes where you put it, the joints below it follow (a finger with its
// knuckle), the carry stops at marker joints (a pinned pelvis leaves the hips on their markers), pins mirror, and the
// mapping file keeps them.
TEST(scratch_pins_place_unmarked_joints_and_carry_their_chain) {
    const DaeModel m = body();
    const auto t = truth();
    ScratchRig rig;
    for (const auto& [id, p] : t)
        if (find_rig_marker(id)) rig.markers[id] = {p, 100, ""};
    const ScratchFit before = fit_scratch_joints(skel(), m, rig);
    const Vec3 knuckle = before.joints.at("mHandIndex1Left"), tip = before.joints.at("mHandIndex3Left");
    const Vec3 shift{0.01, 0.02, -0.015};
    place_pin(rig, "mHandIndex1Left", knuckle + shift, true);
    CHECK(rig.pins.count("mHandIndex1Right"));
    CHECK_NEAR(rig.pins.at("mHandIndex1Right").y, -(knuckle + shift).y, 1e-12);
    const Vec3 pelvis = before.joints.at("mPelvis");
    place_pin(rig, "mPelvis", pelvis + Vec3{0, 0, 0.05}, false);
    const ScratchFit after = fit_scratch_joints(skel(), m, rig);
    CHECK_NEAR((after.joints.at("mHandIndex1Left") - (knuckle + shift)).length(), 0, 1e-9);
    CHECK_NEAR((after.joints.at("mHandIndex3Left") - (tip + shift)).length(), 0, 1e-9);  // carried along
    CHECK_NEAR(after.joints.at("mPelvis").z, pelvis.z + 0.05, 1e-9);
    CHECK_NEAR(after.joints.at("mTorso").z, before.joints.at("mTorso").z + 0.05, 1e-9);  // the spine follows
    CHECK_NEAR((after.joints.at("mHipLeft") - before.joints.at("mHipLeft")).length(), 0, 1e-9);  // the hips stay
    RigMap map;
    map.scratch = rig;
    RigMap back;
    std::string err;
    CHECK(parse_rig_map_json(write_rig_map_json(map), back, err));
    CHECK(back.scratch.pins.size() == rig.pins.size());
    CHECK_NEAR((back.scratch.pins.at("mPelvis") - rig.pins.at("mPelvis")).length(), 0, 1e-5);
}

TEST(scratch_guess_finds_wrists_ankles_and_neck_of_an_unrigged_body) {
    for (const bool a_pose : {false, true}) {
        const DaeModel m = body(a_pose);
        const auto t = truth(a_pose);
        const auto g = guess_markers(m, {"fingers", "groin"});
        auto err = [&](const char* id) { return g.count(id) ? (g.at(id).pos - t.at(id)).length() : 1e9; };
        for (const char* id : {"wrist_l", "wrist_r", "ankle_l", "ankle_r"}) CHECK(err(id) < 0.03);
        for (const char* id : {"shoulder_l", "shoulder_r", "elbow_l", "elbow_r", "hip_l", "hip_r", "knee_l", "knee_r"})
            CHECK(err(id) < 0.045);
        CHECK(err("neck") < 0.06);
        for (const auto& [id, mk] : g) CHECK(mk.confidence > 0 && mk.confidence < 100 && !mk.why.empty());
    }
    // Which way it faces: turned a quarter turn in the file, the guess turns it back (its toes lead).
    DaeModel turned = blob::mesh(blob::humanoid(), 0.025);
    for (size_t i = 0; i + 2 < turned.positions.size(); i += 3) {
        const float x = turned.positions[i], y = turned.positions[i + 1];
        turned.positions[i] = -y, turned.positions[i + 1] = x;  // facing +Y
    }
    std::string why;
    const int turn = guess_scratch_turn(turned, why);
    place_model(turned, scratch_placement(turned, turn, 0));
    const auto g = guess_markers(turned, {});
    CHECK(g.at("toe_l").pos.x > g.at("ankle_l").pos.x + 0.05);
    CHECK(g.at("wrist_l").pos.y > 0.4);  // its left hand on its left (+Y)
}

// The optional groups ticked from the shape: a tail behind the hips, cat ears on the head; a plain body gets neither,
// in a T-pose or an A-pose (arms out are not wings). The tester's blob had an obvious tail and Tail unticked.
TEST(scratch_groups_guessed_from_the_shape) {
    auto groups = [](bool a_pose, bool tail, bool ears) {
        DaeModel m = blob::mesh(blob::humanoid(a_pose, tail, ears), 0.025);
        place_model(m, scratch_placement(m, 0, 0));
        std::vector<std::string> why;
        const auto g = guess_rig_groups(m, &why);
        CHECK(why.size() + 2 == g.size());  // a line for each group found
        return g;
    };
    const std::set<std::string> plain{"fingers", "groin"};
    CHECK(groups(false, false, false) == plain);
    CHECK(groups(true, false, false) == plain);
    CHECK(groups(false, true, false) == (std::set<std::string>{"fingers", "groin", "tail"}));
    CHECK(groups(true, true, false).count("tail"));
    CHECK(groups(false, false, true) == (std::set<std::string>{"ears", "fingers", "groin"}));
    CHECK(groups(false, true, true) == (std::set<std::string>{"ears", "fingers", "groin", "tail"}));
}

TEST(scratch_rig_saves_loads_and_exports_with_its_weights) {
    // An unrigged body on disk, rigged from scratch and weighed by bone heat, its rig saved beside it.
    const fs::path dir = fs::temp_directory_path() / ("vats-scratch-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const std::string path = (dir / "blob.dae").string();
    std::ofstream(path) << blob::static_dae(blob::mesh(blob::humanoid(), 0.03));
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_mesh_file(path, skel(), m, r, err));
    CHECK(!m.rigged);
    RigMap map;
    std::string why;
    map.turn = guess_scratch_turn(m, why);
    place_model(m, scratch_placement(m, map.turn, map.height));
    map.scratch.markers = guess_markers(m, map.scratch.groups);
    rig_from_scratch(skel(), m, map.scratch);
    ScratchWeighReport report;
    CHECK(weigh_scratch_rig(skel(), m, map.scratch, {}, report));
    CHECK(!report.lines.empty() && !report.stats.empty());
    CHECK(m.rigged && map.scratch.weighted(m.vertex_count()));
    // Sane weights: every vertex sums to 1 with at most 4; the left hand's tip on the hand, the left shin on the knee.
    const int wrist = skel().find("mWristLeft"), knee = skel().find("mKneeLeft");
    int tip = 0, shin = -1;
    for (int v = 0; v < m.vertex_count(); ++v) {
        double sum = 0;
        for (int k = 0; k < 4; ++k) sum += m.weights[size_t(v) * 4 + size_t(k)];
        CHECK_NEAR(sum, 1, 1e-4);
        const Vec3 p{m.positions[size_t(v) * 3], m.positions[size_t(v) * 3 + 1], m.positions[size_t(v) * 3 + 2]};
        if (p.y > m.positions[size_t(tip) * 3 + 1]) tip = v;
        if (shin < 0 && std::fabs(p.z - 0.3) < 0.02 && p.y > 0.05) shin = v;
    }
    double hand = weight_on(m, tip, wrist);
    for (const char* f : {"Thumb", "Index", "Middle", "Ring", "Pinky"})
        for (int seg = 1; seg <= 3; ++seg) hand += weight_on(m, tip, skel().find(std::string("mHand") + f + std::to_string(seg) + "Left"));
    CHECK(hand > 0.95);
    CHECK(shin >= 0 && weight_on(m, shin, knee) > 0.9);
    // The mapping file keeps it; loading the model again rigs it the same.
    std::ofstream(rig_map_path(path)) << write_rig_map_json(map);
    DaeModel back;
    DaeReport rb;
    CHECK(load_mesh_file(path, skel(), back, rb, err));
    CHECK(back.rigged && rb.scratch && !rig_needs_mapping(skel(), rb));
    CHECK_EQ(back.vertex_count(), m.vertex_count());
    double worst = 0;
    for (size_t i = 0; i < m.positions.size(); ++i) worst = std::max(worst, double(std::fabs(m.positions[i] - back.positions[i])));
    CHECK(worst < 1e-5);
    for (int v = 0; v < m.vertex_count(); ++v)
        for (int k = 0; k < 4; ++k) {
            const int j = m.joints[size_t(v) * 4 + size_t(k)];
            if (m.weights[size_t(v) * 4 + size_t(k)] > 0) CHECK_NEAR(weight_on(back, v, j), weight_on(m, v, j), 2e-4);
        }
    for (const auto& [name, at] : map.scratch.joints) {
        const int n = skel().find(name);
        CHECK(back.bound[size_t(n)]);
        CHECK_NEAR((back.binds[size_t(n)].pos - at).length(), 0, 1e-5);
    }
    // Exported for SL and read back: rigged to SL's names, the same joint positions and, vertex for vertex by position,
    // the same weights.
    std::string dae;
    CHECK(write_rig_dae(skel(), {{&back, "blob", {}, 0, 1}}, {}, dae, err));
    DaeModel re;
    DaeReport rr;
    CHECK(load_dae(dae, "", skel(), re, rr, err));
    CHECK(re.rigged && rr.unmapped_joints.empty());
    for (const auto& [name, at] : map.scratch.joints) {
        const int n = skel().find(name);
        if (n == skel().find("mPelvis")) continue;  // written only with the pelvis offset; the rest hang from it
        CHECK_NEAR((re.binds[size_t(n)].pos - back.binds[size_t(n)].pos).length(), 0, 2e-4);
    }
    std::map<std::array<long long, 3>, int> at;
    for (int v = 0; v < back.vertex_count(); ++v)
        at[{std::llround(back.positions[size_t(v) * 3] * 1e5), std::llround(back.positions[size_t(v) * 3 + 1] * 1e5),
            std::llround(back.positions[size_t(v) * 3 + 2] * 1e5)}] = v;
    int matched = 0;
    for (int v = 0; v < re.vertex_count(); ++v) {
        const auto it = at.find({std::llround(re.positions[size_t(v) * 3] * 1e5), std::llround(re.positions[size_t(v) * 3 + 1] * 1e5),
                                 std::llround(re.positions[size_t(v) * 3 + 2] * 1e5)});
        if (it == at.end()) continue;
        ++matched;
        for (int k = 0; k < 4; ++k)
            if (back.weights[size_t(it->second) * 4 + size_t(k)] > 0) {
                const int j = back.joints[size_t(it->second) * 4 + size_t(k)];
                CHECK_NEAR(weight_on(re, v, j), weight_on(back, it->second, j), 1e-3);
            }
    }
    CHECK(matched > re.vertex_count() * 9 / 10);
    fs::remove_all(dir);
}

// Weights painted on a body VATs did not rig itself (one rigged to SL's own names here) live in the mapping file beside
// it and are laid over the file's own when it loads; weights for another version of the model are refused with a warning.
TEST(painted_weights_overlay_a_body_rigged_elsewhere) {
    const fs::path dir = fs::temp_directory_path() / ("vats-painted-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    DaeModel m = blob::mesh(blob::humanoid(), 0.04);
    RigMap rigged;
    std::string why, err;
    place_model(m, scratch_placement(m, 0, 0));
    rigged.scratch.markers = guess_markers(m, rigged.scratch.groups);
    rig_from_scratch(skel(), m, rigged.scratch);
    ScratchWeighReport report;
    CHECK(weigh_scratch_rig(skel(), m, rigged.scratch, {}, report));
    std::string dae;
    CHECK(write_rig_dae(skel(), {{&m, "blob", {}, 0, 1}}, {}, dae, err));
    const std::string path = (dir / "body.dae").string();
    std::ofstream(path) << dae;  // rigged to SL's names: no mapping needed
    DaeModel own;
    DaeReport ro;
    CHECK(load_mesh_file(path, skel(), own, ro, err));
    CHECK(own.rigged && !ro.painted);
    const int chest = skel().find("mChest"), root = dae_root(skel());
    RigMap paint;
    paint.scratch.wjoints.assign(size_t(own.vertex_count()) * 4, root);
    paint.scratch.weights.assign(size_t(own.vertex_count()) * 4, 0.f);
    for (int v = 0; v < own.vertex_count(); ++v) paint.scratch.wjoints[size_t(v) * 4] = chest, paint.scratch.weights[size_t(v) * 4] = 1;
    paint.scratch.painted = true;
    std::ofstream(rig_map_path(path)) << write_rig_map_json(paint);
    DaeModel back;
    DaeReport rb;
    CHECK(load_mesh_file(path, skel(), back, rb, err));
    CHECK(back.rigged && rb.painted && !rb.scratch);
    for (int v = 0; v < back.vertex_count(); ++v) CHECK_NEAR(weight_on(back, v, chest), 1, 1e-6);
    // Painted on another version (one vertex fewer): the file's own weights, and a warning.
    paint.scratch.wjoints.resize(paint.scratch.wjoints.size() - 4), paint.scratch.weights.resize(paint.scratch.weights.size() - 4);
    std::ofstream(rig_map_path(path)) << write_rig_map_json(paint);
    DaeModel stale;
    DaeReport rs;
    CHECK(load_mesh_file(path, skel(), stale, rs, err));
    CHECK(!rs.painted && !rs.warnings.empty());
    CHECK(stale.weights == own.weights);
    fs::remove_all(dir);
}

TEST(scratch_part_copying_from_nothing_is_weighed_itself_and_saved_weights_settle) {
    // A second part (a copy of the body moved aside) set to copy from a part with no triangles: it is weighed by heat
    // itself rather than left with no weights at all.
    DaeModel m = body(false, 0.04);
    const std::uint32_t nv = std::uint32_t(m.vertex_count()), ni = std::uint32_t(m.indices.size());
    const std::vector<float> pos = m.positions;
    m.positions.insert(m.positions.end(), pos.begin(), pos.end());
    for (std::uint32_t i = 0; i < ni; ++i) m.indices.push_back(m.indices[i] + nv);
    m.positions.insert(m.positions.end(), {0, 0, 0});  // "Dot": one vertex, no triangles
    m.parts = {{"Body", 0, nv, 0, ni}, {"Coat", nv, nv, ni, ni}, {"Dot", 2 * nv, 1, 2 * ni, 0}};
    ScratchRig rig;
    for (const auto& [id, p] : truth(false, 0.04))
        if (find_rig_marker(id)) rig.markers[id] = {p, 100, ""};
    rig.transfer["Coat"] = "Dot";
    ScratchWeighReport rep;
    CHECK(weigh_scratch_rig(skel(), m, rig, {}, rep));
    for (std::uint32_t v = nv; v < 2 * nv; ++v) {
        double sum = 0;
        for (int k = 0; k < 4; ++k) sum += m.weights[size_t(v) * 4 + size_t(k)];
        CHECK_NEAR(sum, 1, 1e-4);
    }
    // Saved weights with empty slots, an unknown joint and a total under 1 come back whole.
    ScratchRig saved;
    saved.wjoints = {skel().find("mChest"), -1, 99999, -1};
    saved.weights = {0.5f, 0.f, 0.3f, 0.f};
    CHECK(settle_scratch_weights(skel(), saved, 1));
    CHECK_EQ(saved.wjoints[1], dae_root(skel()));
    CHECK_EQ(saved.wjoints[2], dae_root(skel()));
    CHECK_NEAR(saved.weights[0], 1, 1e-6);
    CHECK(!settle_scratch_weights(skel(), saved, 2));
}

TEST(robust_laplacian_keeps_bone_heat_in_range_on_broken_geometry) {
    // The body with slivers (corners pulled onto the opposite edge), holes, flipped duplicate triangles and fins (a third
    // triangle on an edge): the plain cotangent Laplacian's heat leaves [0, 1] there, the robust one's does not.
    const blob::Body body = blob::humanoid();
    DaeModel m = blob::mesh(body, 0.02);
    const ScratchPlacement pl = scratch_placement(m, 0, 0);
    place_model(m, pl);
    std::uint32_t seed = 7;
    auto next = [&](size_t n) { return size_t((seed = seed * 1664525u + 1013904223u) >> 8) % n; };
    const size_t nt = m.indices.size() / 3;
    for (size_t i = 0; i < nt / 25; ++i) {
        const size_t t = next(nt);
        const std::uint32_t a = m.indices[t * 3], b = m.indices[t * 3 + 1], c = m.indices[t * 3 + 2];
        for (int k = 0; k < 3; ++k)
            m.positions[a * 3 + size_t(k)] = 0.5f * (m.positions[b * 3 + size_t(k)] + m.positions[c * 3 + size_t(k)]) + 1e-7f * float(k + 1);
    }
    std::vector<std::uint32_t> idx;
    for (size_t t = 0; t < nt; ++t)
        if (next(50) != 0) idx.insert(idx.end(), {m.indices[t * 3], m.indices[t * 3 + 1], m.indices[t * 3 + 2]});
    for (size_t i = 0; i < nt / 50; ++i) {
        const size_t t = next(idx.size() / 3);
        idx.insert(idx.end(), {idx[t * 3], idx[t * 3 + 2], idx[t * 3 + 1]});
        const std::uint32_t a = idx[t * 3], b = idx[t * 3 + 1], v = std::uint32_t(m.positions.size() / 3);
        for (int k = 0; k < 3; ++k)
            m.positions.push_back(0.5f * (m.positions[a * 3 + size_t(k)] + m.positions[b * 3 + size_t(k)]) + (k == 0 ? 0.02f : 0.f));
        m.normals.insert(m.normals.end(), {1, 0, 0});
        m.uvs.insert(m.uvs.end(), {0, 0});
        idx.insert(idx.end(), {a, b, v});
    }
    m.indices = idx;
    m.parts = {{"Body", 0, std::uint32_t(m.positions.size() / 3), 0, std::uint32_t(idx.size())}};
    ScratchRig rig;
    for (const auto& [id, p] : body.joints)
        if (find_rig_marker(id)) rig.markers[id] = {pl.apply(p), 100, ""};
    int out[2] = {0, 0};
    for (const LaplacianKind kind : {LaplacianKind::Robust, LaplacianKind::Cotangent}) {
        DaeModel x = m;
        ScratchRig r = rig;
        BoneHeatOptions o;
        o.laplacian = kind;
        ScratchWeighReport rep;
        CHECK(weigh_scratch_rig(skel(), x, r, o, rep));
        CHECK_EQ(rep.stats.size(), size_t(1));
        for (const BoneHeatStats& s : rep.stats) out[kind == LaplacianKind::Cotangent] += s.out_of_range + (s.failed ? 1000 : 0);
        if (kind == LaplacianKind::Robust && !rep.stats.empty()) CHECK_EQ(rep.stats[0].negative_edges, 0);
    }
    CHECK_EQ(out[0], 0);
    CHECK(out[1] > 0);
}
