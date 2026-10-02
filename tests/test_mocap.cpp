// Motion capture tests, spec 08 section 3: OSC parsing, VMC axes and mapping, recording.
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/dae.h"
#include "vats/edit.h"
#include "vats/face_anim.h"
#include "vats/mocap.h"
#include "vats/rig.h"

using namespace vats;

namespace {

// --- A tiny OSC writer for building packets by hand ---

struct Osc {
    std::vector<std::uint8_t> b;
    void pad() { while (b.size() % 4) b.push_back(0); }
    void str(const std::string& s) {
        b.insert(b.end(), s.begin(), s.end());
        b.push_back(0);
        pad();
    }
    void u32(std::uint32_t v) {
        for (int s = 24; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
    }
    void f32(float f) {
        std::uint32_t u;
        std::memcpy(&u, &f, 4);
        u32(u);
    }
};

std::vector<std::uint8_t> message(const std::string& address, const std::string& tags, const std::vector<float>& fs,
                                  const std::vector<std::string>& ss = {}, const std::vector<int>& is = {}) {
    Osc o;
    o.str(address);
    o.str("," + tags);
    size_t f = 0, s = 0, i = 0;
    for (char t : tags) {
        if (t == 'f') o.f32(fs[f++]);
        if (t == 's') o.str(ss[s++]);
        if (t == 'i') o.u32(std::uint32_t(is[i++]));
    }
    return o.b;
}

std::vector<std::uint8_t> bundle(const std::vector<std::vector<std::uint8_t>>& parts) {
    Osc o;
    o.str("#bundle");
    o.u32(0), o.u32(1);  // time tag "immediately"
    for (auto& p : parts) {
        o.u32(std::uint32_t(p.size()));
        o.b.insert(o.b.end(), p.begin(), p.end());
    }
    return o.b;
}

// SL axes back to Unity's, the inverse of unity_to_sl.
Vec3 sl_to_unity(const Vec3& v) { return {-v.y, v.z, v.x}; }
Quat sl_to_unity(const Quat& q) { return Quat{q.w, q.y, -q.z, -q.x}; }

std::vector<std::uint8_t> bone_msg(const std::string& name, const Vec3& sl_pos, const Quat& sl_rot) {
    Vec3 p = sl_to_unity(sl_pos);
    Quat q = sl_to_unity(sl_rot);
    return message("/VMC/Ext/Bone/Pos", "sfffffff",
                   {float(p.x), float(p.y), float(p.z), float(q.x), float(q.y), float(q.z), float(q.w)}, {name});
}

RigTable vrm_table() {
    std::ifstream f(std::string(VATS_DATA_DIR) + "/../retarget/vrm-humanoid.json");
    std::stringstream ss;
    ss << f.rdbuf();
    RigTable t;
    std::string err;
    CHECK(parse_rig_table(ss.str(), t, err));
    return t;
}

// A VMC sender whose avatar has SL's rest proportions: Unity humanoid bones at the rest positions
// of the SL joints they map to, identity rotations (a VRM-normalised T-pose).
const std::pair<const char*, const char*> kBones[] = {
    {"Hips", "mPelvis"},         {"Spine", "mTorso"},          {"Chest", "mChest"},
    {"Neck", "mNeck"},           {"Head", "mHead"},            {"LeftShoulder", "mCollarLeft"},
    {"LeftUpperArm", "mShoulderLeft"}, {"LeftLowerArm", "mElbowLeft"}, {"LeftHand", "mWristLeft"},
    {"RightShoulder", "mCollarRight"}, {"RightUpperArm", "mShoulderRight"}, {"RightLowerArm", "mElbowRight"},
    {"RightHand", "mWristRight"}, {"LeftUpperLeg", "mHipLeft"},   {"LeftLowerLeg", "mKneeLeft"},
    {"LeftFoot", "mAnkleLeft"},  {"LeftToes", "mFootLeft"},    {"RightUpperLeg", "mHipRight"},
    {"RightLowerLeg", "mKneeRight"}, {"RightFoot", "mAnkleRight"}, {"RightToes", "mFootRight"},
};
const char* parent_of(const std::string& n) {
    static const std::map<std::string, std::string> p = {
        {"Spine", "Hips"}, {"Chest", "Spine"}, {"Neck", "Chest"}, {"Head", "Neck"},
        {"LeftShoulder", "Chest"}, {"LeftUpperArm", "LeftShoulder"}, {"LeftLowerArm", "LeftUpperArm"},
        {"LeftHand", "LeftLowerArm"}, {"RightShoulder", "Chest"}, {"RightUpperArm", "RightShoulder"},
        {"RightLowerArm", "RightUpperArm"}, {"RightHand", "RightLowerArm"}, {"LeftUpperLeg", "Hips"},
        {"LeftLowerLeg", "LeftUpperLeg"}, {"LeftFoot", "LeftLowerLeg"}, {"LeftToes", "LeftFoot"},
        {"RightUpperLeg", "Hips"}, {"RightLowerLeg", "RightUpperLeg"}, {"RightFoot", "RightLowerLeg"},
        {"RightToes", "RightFoot"}};
    auto it = p.find(n);
    return it == p.end() ? nullptr : it->second.c_str();
}

// The sender's state with one bone turned by sl_turn (a local rotation, SL axes).
VmcState sender(const std::string& turned = "", const Quat& sl_turn = {}) {
    const Skeleton& s = skel();
    auto G = s.global_pose(Pose(s.size()));
    std::map<std::string, Vec3> world;
    for (auto& [u, sl] : kBones) world[u] = G[s.find(sl)].pos;
    std::vector<std::vector<std::uint8_t>> msgs;
    for (auto& [u, sl] : kBones) {
        const char* p = parent_of(u);
        Vec3 local = p ? world[u] - world[p] : world[u];
        msgs.push_back(bone_msg(u, local, u == turned ? sl_turn : Quat{}));
    }
    msgs.push_back(message("/VMC/Ext/Root/Pos", "sfffffff", {0, 0, 0, 0, 0, 0, 1}, {"root"}));
    msgs.push_back(message("/VMC/Ext/OK", "i", {}, {}, {1}));
    std::vector<std::uint8_t> packet = bundle(msgs);
    std::vector<OscMessage> out;
    CHECK(parse_osc(packet.data(), packet.size(), out));
    VmcState st;
    for (auto& m : out) CHECK(apply_vmc(m, st));
    return st;
}

double angle_deg(const Quat& a, const Quat& b) { return (a.conj() * b).normalized().angle() * kRadToDeg; }

}  // namespace

TEST(mocap_osc_message_and_alignment) {
    // "/ab" + NUL fits 4 bytes exactly; "/abc" + NUL needs padding to 8.
    for (const char* addr : {"/ab", "/abc", "/abcdefg"}) {
        auto p = message(addr, "isf", {2.5f}, {"hello"}, {-7});
        CHECK(p.size() % 4 == 0);
        std::vector<OscMessage> out;
        CHECK(parse_osc(p.data(), p.size(), out));
        CHECK_EQ(out.size(), size_t(1));
        CHECK_EQ(out[0].address, std::string(addr));
        CHECK_EQ(out[0].args.size(), size_t(3));
        CHECK_EQ(out[0].args[0].num, -7.0);
        CHECK_EQ(out[0].args[1].str, std::string("hello"));
        CHECK_EQ(out[0].args[2].num, 2.5);
    }
    // Truncated packets and junk are refused.
    auto p = message("/x", "f", {1});
    std::vector<OscMessage> out;
    CHECK(!parse_osc(p.data(), p.size() - 4, out));
    std::uint8_t junk[8] = {'a', 'b', 0, 0, 0, 0, 0, 0};
    CHECK(!parse_osc(junk, 8, out));
}

TEST(mocap_osc_bundles_nest) {
    auto inner = bundle({message("/a", "i", {}, {}, {1}), message("/b", "i", {}, {}, {2})});
    auto outer = bundle({inner, message("/c", "s", {}, {"x"})});
    std::vector<OscMessage> out;
    CHECK(parse_osc(outer.data(), outer.size(), out));
    CHECK_EQ(out.size(), size_t(3));
    CHECK_EQ(out[0].address, std::string("/a"));
    CHECK_EQ(out[1].args[0].num, 2.0);
    CHECK_EQ(out[2].args[0].str, std::string("x"));
    // An element length running past the end is an error.
    outer[19] = 0xff;
    out.clear();
    CHECK(!parse_osc(outer.data(), outer.size(), out));
}

TEST(mocap_unity_to_sl_axes) {
    // Unity forward (+Z), right (+X), up (+Y) are SL forward (+X), right (-Y), up (+Z).
    CHECK(unity_to_sl(Vec3{0, 0, 1}) == (Vec3{1, 0, 0}));
    CHECK(unity_to_sl(Vec3{1, 0, 0}) == (Vec3{0, -1, 0}));
    CHECK(unity_to_sl(Vec3{0, 1, 0}) == (Vec3{0, 0, 1}));
    // Rotations commute with the axis change: M (q v) == q' (M v), for arbitrary q and v.
    for (int i = 0; i < 20; ++i) {
        Quat q = Quat{std::cos(i * 0.7), std::sin(i * 1.3), std::cos(i * 2.1), std::sin(i * 0.4)}.normalized();
        Vec3 v{std::sin(i * 0.9), 0.3 * i - 2, std::cos(i * 1.7)};
        Vec3 a = unity_to_sl(q.rotate(v)), b = unity_to_sl(q).rotate(unity_to_sl(v));
        CHECK_NEAR((a - b).length(), 0.0, 1e-12);
    }
    // Unity's positive turn about Y turns forward to the right, which in SL is forward to -Y.
    Quat turn = Quat::axis_angle({0, 1, 0}, kPi / 2);
    Vec3 f = unity_to_sl(turn).rotate({1, 0, 0});
    CHECK_NEAR(f.y, -1.0, 1e-12);
}

TEST(mocap_vmc_known_pose_maps_to_sl) {
    const Skeleton& s = skel();
    RigTable table = vrm_table();
    VmcState rest = vmc_t_pose(sender());
    // The left upper arm lowered 60 degrees (about SL +X, which drops a +Y arm), and a hip shift.
    Quat down = Quat::axis_angle({1, 0, 0}, -60 * kDegToRad);
    VmcState now = sender("LeftUpperArm", down);
    now.root.pos = Vec3{0.1, 0, 0};
    Clip live = live_pose(s, table, rest, now);
    Rig rig(s);
    Evaluation e = evaluate(rig, live, 0, nullptr);
    auto G = s.global_pose(Pose(s.size()));
    const int sh = s.find("mShoulderLeft"), el = s.find("mElbowLeft"), pel = s.find("mPelvis");
    CHECK(angle_deg(e.globals[sh].rot, down * G[sh].rot) < 0.5);
    // The elbow swings round the shoulder, below it.
    Vec3 want = e.globals[sh].pos + down.rotate(G[el].pos - G[sh].pos);
    CHECK_NEAR((e.globals[el].pos - want).length(), 0.0, 2e-3);
    CHECK(e.globals[el].pos.z < e.globals[sh].pos.z - 0.1);
    // Hips moved forward (SL +X); the retarget scales travel by leg length, here ~1.
    CHECK_NEAR(e.globals[pel].pos.x - G[pel].pos.x, 0.1, 0.01);
    // Untouched joints keep their rest.
    CHECK(angle_deg(e.globals[s.find("mElbowRight")].rot, G[s.find("mElbowRight")].rot) < 0.5);
}

TEST(mocap_recorder_countdown_and_punch) {
    MocapRecorder r;
    VmcState st;
    r.begin(0, 3, 10, 5, 9);
    CHECK(r.feed(1.0, st));
    CHECK(r.counting_down(1.0));
    CHECK(r.frames.empty());
    CHECK(r.feed(3.0, st));
    CHECK_EQ(r.frames.size(), size_t(1));
    CHECK(r.feed(3.25, st));
    CHECK_EQ(r.frames.size(), size_t(3));
    CHECK(!r.feed(10.0, st));  // a long gap fills in, capped at the range
    CHECK_EQ(r.frames.size(), size_t(5));
    CHECK(!r.active());
    // Open-ended.
    r.begin(0, 0, 30, 0, -1);
    CHECK(r.feed(1.0, st));
    CHECK_EQ(r.frames.size(), size_t(31));
}

TEST(mocap_merge_writes_range_and_parts_only) {
    const Skeleton& s = skel();
    RigTable table = vrm_table();
    VmcState rest = vmc_t_pose(sender());
    std::vector<VmcState> frames;
    for (int f = 0; f < 5; ++f)
        frames.push_back(sender("LeftUpperArm", Quat::axis_angle({1, 0, 0}, -15.0 * f * kDegToRad)));

    Clip clip;
    clip.fps = 30;
    clip.end_frame = 20;
    clip.curves["mShoulderLeft"]["rot_x"].set_key(0, 7);
    clip.curves["mShoulderLeft"]["rot_x"].set_key(20, 7);
    clip.curves["mHead"]["rot_z"].set_key(10, 33);
    const Clip before = clip;
    MocapCleanup clean;
    clean.reduce = false;
    clean.blend = 0;  // exact take values; the edge blend has its own test
    auto report = merge_recording(clip, s, table, rest, frames, 5, {"mShoulderLeft"}, clean);
    CHECK(!report.empty());
    // Other tracks untouched; no pelvis track appeared.
    CHECK(clip.curves["mHead"] == before.curves.at("mHead"));
    CHECK(!clip.curves.count("mPelvis"));
    // Keys outside 5..9 survive; the range holds the recording.
    const FCurve& x = clip.curves["mShoulderLeft"]["rot_x"];
    CHECK(x.find(0) >= 0 && x.find(20) >= 0);
    for (int f = 5; f <= 9; ++f) CHECK(x.find(f) >= 0);
    Rig rig(s);
    auto G = s.global_pose(Pose(s.size()));
    const int sh = s.find("mShoulderLeft");
    Evaluation e = evaluate(rig, clip, 8, nullptr);
    CHECK(angle_deg(e.globals[sh].rot, Quat::axis_angle({1, 0, 0}, -45 * kDegToRad) * G[sh].rot) < 0.5);

    // All parts: the pelvis is written too, and reduction with smoothing still lands in range.
    Clip all = before;
    clean.reduce = true;
    clean.smooth = 1;
    merge_recording(all, s, table, rest, frames, 5, {}, clean);
    CHECK(all.curves.count("mPelvis"));
    for (auto& [ch, c] : all.curves["mShoulderLeft"])
        for (auto& k : c.keys) CHECK(k.frame <= 10 || k.frame == 20);  // 4 and 10 hold the old animation
}

TEST(mocap_punch_edges_blend) {
    const Skeleton& s = skel();
    RigTable table = vrm_table();
    VmcState rest = vmc_t_pose(sender());
    // A take holding the arm 60 degrees down, punched into 20..40 of an arm held level.
    std::vector<VmcState> frames(21, sender("LeftUpperArm", Quat::axis_angle({1, 0, 0}, -60 * kDegToRad)));
    Clip base;
    base.fps = 30;
    base.end_frame = 60;
    base.curves["mShoulderLeft"]["rot_x"].set_key(0, 0);
    base.curves["mShoulderLeft"]["rot_x"].set_key(60, 0);
    Rig rig(s);
    const int sh = s.find("mShoulderLeft");
    auto max_step = [&](const Clip& c) {
        double worst = 0;
        for (int f = 1; f <= 60; ++f)
            worst = std::max(worst, angle_deg(evaluate(rig, c, f - 1, nullptr).globals[sh].rot,
                                              evaluate(rig, c, f, nullptr).globals[sh].rot));
        return worst;
    };
    MocapCleanup clean;
    clean.reduce = false;
    clean.blend = 0;
    Clip hard = base;
    merge_recording(hard, s, table, rest, frames, 20, {"mShoulderLeft"}, clean);
    CHECK(max_step(hard) > 50);  // without blending the edges jump the full 60 degrees
    clean.blend = 4;
    Clip soft = base;
    merge_recording(soft, s, table, rest, frames, 20, {"mShoulderLeft"}, clean);
    CHECK(max_step(soft) < 60.0 / 5 + 1);  // eased over 4 frames at each edge
    // The middle of the take is the recording itself.
    auto G = s.global_pose(Pose(s.size()));
    CHECK(angle_deg(evaluate(rig, soft, 30, nullptr).globals[sh].rot,
                    Quat::axis_angle({1, 0, 0}, -60 * kDegToRad) * G[sh].rot) < 0.5);
}

// --- Hip travel (07 RT-8 through MC-3) ---------------------------------------------------------

namespace {

// A VMC performer smaller than SL's avatar (hips 0.9 m above the floor), whose avatar root stands on the floor at
// root_pos turned by yaw about the vertical, as a tracking app places it in its scene.
struct Performer {
    double k = 0.9 / skel().global_pose(Pose(skel().size()))[0].pos.z;  // performer size / SL size
    Vec3 root_pos{2.5, -1.5, 0};
    double yaw = 30 * kDegToRad;
    bool send_root = true;  // false: a packet with the bones only (the root comes in a later one)
};

// One frame: the hips `step` metres along the performer's forward, the legs bent `bend` radians into a crouch
// with the feet flat on the floor (the hips lowered by `drop`, which is returned).
std::vector<std::uint8_t> performer_packet(const Performer& p, double step, double bend, double* drop = nullptr) {
    const Skeleton& s = skel();
    auto G = s.global_pose(Pose(s.size()));
    std::map<std::string, Vec3> world, local;
    std::map<std::string, Quat> rot;
    for (auto& [u, sl] : kBones) world[u] = G[s.find(sl)].pos * p.k;
    for (auto& [u, sl] : kBones) local[u] = parent_of(u) ? world[u] - world[parent_of(u)] : world[u];
    for (const char* side : {"Left", "Right"}) {  // knees forward, shins back, feet level
        rot[std::string(side) + "UpperLeg"] = Quat::axis_angle({0, 1, 0}, -bend);
        rot[std::string(side) + "LowerLeg"] = Quat::axis_angle({0, 1, 0}, 2 * bend);
        rot[std::string(side) + "Foot"] = Quat::axis_angle({0, 1, 0}, -bend);
    }
    // Forward kinematics from the hips: how far the bend lifts the lowest foot joint.
    auto lowest = [&](bool bent) {
        std::map<std::string, Xform> w;
        double low = 1e9;
        for (auto& [u, sl] : kBones) {
            const Xform x{bent && rot.count(u) ? rot[u] : Quat{}, local[u]};
            w[u] = parent_of(u) ? w[parent_of(u)] * x : x;
            if (std::string(u).find("Foot") != std::string::npos || std::string(u).find("Toes") != std::string::npos)
                low = std::min(low, w[u].pos.z);
        }
        return low;
    };
    const double lift = lowest(true) - lowest(false);
    if (drop) *drop = lift;
    std::vector<std::vector<std::uint8_t>> msgs;
    for (auto& [u, sl] : kBones) {
        Vec3 x = local[u];
        if (!parent_of(u)) x += Vec3{step, 0, -lift};  // the hips, in the root's axes
        msgs.push_back(bone_msg(u, x, rot.count(u) ? rot[u] : Quat{}));
    }
    if (p.send_root) {
        const Vec3 up = sl_to_unity(p.root_pos);
        const Quat uq = sl_to_unity(Quat::axis_angle({0, 0, 1}, p.yaw));
        msgs.push_back(message("/VMC/Ext/Root/Pos", "sfffffff",
                               {float(up.x), float(up.y), float(up.z), float(uq.x), float(uq.y), float(uq.z), float(uq.w)},
                               {"root"}));
    }
    return bundle(msgs);
}

// The app's receive path (MocapUi): each packet applied to the sender's state; the rest pose is the T-pose of the
// first state with bones in it, until one is captured.
struct Receiver {
    VmcState state, rest;
    bool have_data = false;
    void receive(const std::vector<std::uint8_t>& packet) {
        std::vector<OscMessage> msgs;
        CHECK(parse_osc(packet.data(), packet.size(), msgs));
        for (auto& m : msgs) apply_vmc(m, state);
        if (!have_data && !state.bones.empty()) have_data = true, rest = vmc_t_pose(state);
    }
    void capture_rest() {  // Capture Rest Pose Now
        rest = state;
        rest.captured = true;
    }
};

// Records the packets as a take at 30 fps (one packet per frame) through MocapRecorder and merge_recording.
Clip record(Receiver& rx, const std::vector<std::vector<std::uint8_t>>& packets) {
    MocapRecorder rec;
    rec.begin(0, 0, 30, 0, -1);
    for (size_t i = 0; i < packets.size(); ++i) {
        rx.receive(packets[i]);
        rec.feed((i + 0.5) / 30, rx.state);
    }
    CHECK_EQ(rec.frames.size(), packets.size());
    Clip clip;
    clip.fps = 30;
    MocapCleanup clean;
    clean.reduce = false;
    clean.blend = 0;
    merge_recording(clip, skel(), vrm_table(), rx.rest, rec.frames, 0, {}, clean);
    return clip;
}

// SL playing an exported clip: mPelvis's position at frame fr, lerped between the decoded keys as the viewer does.
// It is an offset from where the avatar stands, so zero plays at standing height.
Vec3 played_pelvis(const AnimFile& f, int fr, int fps) {
    const AnimJoint* pelvis = nullptr;
    for (auto& j : f.joints)
        if (j.name == "mPelvis") pelvis = &j;
    if (!pelvis || pelvis->pos.empty()) return {};
    const double t = double(fr) / fps;
    auto time = [&](size_t i) { return double(u16_to_f32(pelvis->pos[i][0], 0.f, f.duration)); };
    size_t i = 0;
    while (i + 1 < pelvis->pos.size() && time(i + 1) <= t) ++i;
    Vec3 p = decode_position(pelvis->pos[i]);
    if (i + 1 < pelvis->pos.size() && time(i) < t)
        p = p + (decode_position(pelvis->pos[i + 1]) - p) * ((t - time(i)) / (time(i + 1) - time(i)));
    return p;
}

}  // namespace

TEST(mocap_vmc_hip_travel_is_from_standing) {
    // Reported 2026-09-27: a VMC body take from TDPT floated in SL, mPelvis 0.53 to 0.78 m up all through. The
    // hips' travel was measured from the first packet after Listen, not from where the performer stood.
    const Performer p;
    const Quat facing = Quat::axis_angle({0, 0, 1}, p.yaw);
    const double scale = 1 / p.k;  // RT-8: SL leg length over the performer's
    double drop = 0;
    performer_packet(p, 0, 0.6, &drop);
    CHECK(drop > 0.1);
    auto check_take = [&](const Clip& c, const std::vector<std::pair<int, Vec3>>& want) {
        for (auto& [fr, w] : want) {
            const Vec3 got = curve_offset(c, "mPelvis", fr);
            CHECK((got - w).length() < 2e-3);
            if ((got - w).length() >= 2e-3)
                std::fprintf(stderr, "    frame %d: <%.4f %.4f %.4f>, want <%.4f %.4f %.4f>\n", fr, got.x, got.y,
                             got.z, w.x, w.y, w.z);
        }
    };
    const Vec3 forward = facing.rotate({0.3 * scale, 0, 0}), down{0, 0, -drop * scale};

    // Standing, a step of 0.3 m forward, then a crouch there. The first packet arrives before the root does.
    Performer no_root = p;
    no_root.send_root = false;
    Receiver rx;
    rx.receive(performer_packet(no_root, 0, 0));
    std::vector<std::vector<std::uint8_t>> take;
    for (int f = 0; f < 30; ++f) take.push_back(performer_packet(p, f < 10 ? 0 : 0.3, f < 20 ? 0 : 0.6));
    const Clip c = record(rx, take);
    check_take(c, {{0, {}}, {5, {}}, {15, forward}, {25, forward + down}});
    // Exported and played in SL: at standing height at rest, lower in the crouch.
    AnimExportResult r = export_anim(skel(), c, AnimExportOptions{});
    CHECK(r.errors.empty());
    const double tol = 10.0 / 65535 * 2 * std::sqrt(3.0) + AnimExportOptions{}.reduce_pos_m + 2e-3;
    CHECK(played_pelvis(r.file, 5, 30).length() < tol);
    CHECK((played_pelvis(r.file, 15, 30) - forward).length() < tol);
    CHECK(std::fabs(played_pelvis(r.file, 25, 30).z - down.z) < tol);

    // The first packet after Listen is a crouch, and so is the start of the take: the take starts low, never high.
    Receiver low;
    low.receive(performer_packet(p, 0, 0.6));
    take.clear();
    for (int f = 0; f < 30; ++f) take.push_back(performer_packet(p, f < 20 ? 0 : 0.3, f < 10 ? 0.6 : 0));
    check_take(record(low, take), {{0, down}, {5, down}, {15, {}}, {25, forward}});

    // A captured rest pose is the reference: a take starting a step in front of it starts a step forward.
    Receiver cap;
    cap.receive(performer_packet(p, 0, 0.6));
    cap.receive(performer_packet(p, 0, 0));
    cap.capture_rest();
    take.clear();
    for (int f = 0; f < 20; ++f) take.push_back(performer_packet(p, 0.3, f < 10 ? 0 : 0.6));
    check_take(record(cap, take), {{0, forward}, {5, forward}, {15, forward + down}});

    // A performer the same size as SL's avatar, root at the origin and unturned: travel comes through unscaled.
    Performer sl;
    sl.k = 1, sl.root_pos = {}, sl.yaw = 0;
    Receiver same;
    same.receive(performer_packet(sl, 0, 0));
    take.clear();
    for (int f = 0; f < 10; ++f) take.push_back(performer_packet(sl, f < 5 ? 0 : 0.3, 0));
    check_take(record(same, take), {{2, {}}, {7, {0.3, 0, 0}}});
}

// --- Rokoko Studio Live -------------------------------------------------------------------------

namespace {

// An LZ4 frame header (version 01, independent blocks, no checksums or size) followed by blocks.
std::vector<std::uint8_t> lz4_frame(const std::vector<std::vector<std::uint8_t>>& blocks,
                                    const std::vector<bool>& raw) {
    std::vector<std::uint8_t> f = {0x04, 0x22, 0x4D, 0x18, 0x60, 0x40, 0x82};
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) f.push_back(std::uint8_t(v >> (8 * i)));
    };
    for (size_t i = 0; i < blocks.size(); ++i) {
        u32(std::uint32_t(blocks[i].size()) | (raw[i] ? 0x80000000u : 0));
        f.insert(f.end(), blocks[i].begin(), blocks[i].end());
    }
    u32(0);
    return f;
}

// A Rokoko JSON v3 packet: SL rest positions moved by shift, each joint's world rotation = turn (if under `from`)
// times a fixed per-joint frame, as Studio's joints have their own axes.
std::string rokoko_packet(const char* from_sl = nullptr, const Quat& turn = {}, const Vec3& shift = {}) {
    const Skeleton& s = skel();
    auto G = s.global_pose(Pose(s.size()));
    const std::pair<const char*, const char*> joints[] = {
        {"hip", "mPelvis"}, {"spine", "mTorso"}, {"chest", "mChest"}, {"neck", "mNeck"}, {"head", "mHead"},
        {"leftShoulder", "mCollarLeft"}, {"leftUpperArm", "mShoulderLeft"}, {"leftLowerArm", "mElbowLeft"},
        {"leftHand", "mWristLeft"}, {"rightShoulder", "mCollarRight"}, {"rightUpperArm", "mShoulderRight"},
        {"rightLowerArm", "mElbowRight"}, {"rightHand", "mWristRight"}, {"leftUpLeg", "mHipLeft"},
        {"leftLeg", "mKneeLeft"}, {"leftFoot", "mAnkleLeft"}, {"rightUpLeg", "mHipRight"},
        {"rightLeg", "mKneeRight"}, {"rightFoot", "mAnkleRight"}};
    const int pivot = from_sl ? s.find(from_sl) : -1;
    auto under = [&](int n) {
        for (int i = n; i >= 0; i = s[i].parent)
            if (i == pivot) return true;
        return false;
    };
    std::string body;
    int k = 0;
    for (auto& [r, sl] : joints) {
        const int n = s.find(sl);
        ++k;
        Quat frame = Quat::axis_angle(Vec3{0.3, 1, 0.2 * k}.normalized(), 0.4 * k);  // a joint's own axes
        Vec3 p = G[n].pos;
        Quat q = frame;
        if (under(n)) p = G[pivot].pos + turn.rotate(p - G[pivot].pos), q = turn * q;
        Vec3 up = sl_to_unity(p + shift);
        Quat uq = sl_to_unity(q);
        char buf[400];
        std::snprintf(buf, sizeof buf,
                      "%s\"%s\":{\"position\":{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g},"
                      "\"rotation\":{\"x\":%.9g,\"y\":%.9g,\"z\":%.9g,\"w\":%.9g}}",
                      body.empty() ? "" : ",", r, up.x, up.y, up.z, uq.x, uq.y, uq.z, uq.w);
        body += buf;
    }
    return "{\"version\":3,\"fps\":60,\"scene\":{\"timestamp\":1.5,\"actors\":[{\"name\":\"Glove only\",\"meta\":"
           "{\"hasBody\":false}},{\"name\":\"Newton\",\"meta\":{\"hasBody\":true},\"body\":{" +
           body + "}}],\"props\":[]}}";
}

VmcState rokoko_state(const std::string& json, bool compress) {
    std::vector<std::uint8_t> bytes(json.begin(), json.end());
    if (compress) bytes = lz4_frame({bytes}, {true});
    VmcState st;
    std::string used, err;
    CHECK(apply_rokoko(bytes.data(), bytes.size(), st, "", used, err));
    CHECK_EQ(used, std::string("Newton"));  // the first actor with a body
    return st;
}

}  // namespace

TEST(rokoko_lz4_frame_decoder) {
    // "abc", then a match of 9 at offset 3 (overlapping), then 5 literals to end the block.
    std::vector<std::uint8_t> block = {0x35, 'a', 'b', 'c', 0x03, 0x00, 0x50, 'X', 'Y', 'Z', 'W', 'V'};
    std::vector<std::uint8_t> raw = {'!', '?'};
    auto f = lz4_frame({block, raw}, {false, true});
    std::string out;
    CHECK(lz4_frame_decompress(f.data(), f.size(), out));
    CHECK_EQ(out, std::string("abcabcabcabcXYZWV!?"));
    // Long literal and match lengths (15 + extension bytes).
    std::vector<std::uint8_t> big = {0xFF, 3};  // 18 literals, then a long match
    for (int i = 0; i < 18; ++i) big.push_back(std::uint8_t('a' + i));
    big.insert(big.end(), {0x12, 0x00, 1, 0x10, 'z'});  // match 4+15+1 = 20 at offset 18, then "z"
    f = lz4_frame({big}, {false});
    CHECK(lz4_frame_decompress(f.data(), f.size(), out));
    std::string want = "abcdefghijklmnopqr";
    CHECK_EQ(out, want + "abcdefghijklmnopqrab" + "z");
    // Corrupt input fails instead of reading out of bounds.
    std::vector<std::uint8_t> bad = {0x00, 0x05, 0x00};  // offset 5 into an empty output
    f = lz4_frame({bad}, {false});
    CHECK(!lz4_frame_decompress(f.data(), f.size(), out));
    f.resize(9);
    CHECK(!lz4_frame_decompress(f.data(), f.size(), out));
    CHECK(!lz4_frame_decompress(reinterpret_cast<const std::uint8_t*>("{}"), 2, out));
}

TEST(rokoko_packet_maps_to_sl) {
    const Skeleton& s = skel();
    RigTable table = vrm_table();
    VmcState rest = rokoko_state(rokoko_packet(), false);  // Studio actor in its rest pose
    CHECK(rest.bones.count("Hips") && rest.bones.count("LeftHand") && rest.bones.count("RightFoot"));
    CHECK(!rest.bones.count("UpperChest"));  // Rokoko sends none; Neck and Shoulders hang off Chest
    Quat down = Quat::axis_angle({1, 0, 0}, -60 * kDegToRad);
    VmcState now = rokoko_state(rokoko_packet("mShoulderLeft", down), true);  // LZ4 path
    Clip live = live_pose(s, table, rest, now);
    Rig rig(s);
    Evaluation e = evaluate(rig, live, 0, nullptr);
    auto G = s.global_pose(Pose(s.size()));
    const int sh = s.find("mShoulderLeft"), el = s.find("mElbowLeft");
    CHECK(angle_deg(e.globals[sh].rot, down * G[sh].rot) < 0.5);
    CHECK(e.globals[el].pos.z < e.globals[sh].pos.z - 0.1);
    CHECK(angle_deg(e.globals[s.find("mElbowRight")].rot, G[s.find("mElbowRight")].rot) < 0.5);
    // A packet that is not JSON v3 is refused with a reason.
    std::string bad = "{\"version\":2,\"actors\":[]}", used, err;
    VmcState st;
    CHECK(!apply_rokoko(reinterpret_cast<const std::uint8_t*>(bad.data()), bad.size(), st, "", used, err));
    CHECK(err.find("JSON v3") != std::string::npos);
}

TEST(rokoko_hip_travel_is_from_where_the_take_starts) {
    // Rokoko sends world positions: the actor stood elsewhere in Studio's space when VATs started listening (the
    // first frame, the rest), and the take starts 1.2 m away from there. The take still starts at no travel.
    const Skeleton& s = skel();
    const VmcState rest = rokoko_state(rokoko_packet(nullptr, {}, {1.0, 0.8, 0}), false);
    MocapRecorder rec;
    rec.begin(0, 0, 30, 0, -1);
    for (int f = 0; f < 10; ++f)
        rec.feed((f + 0.5) / 30, rokoko_state(rokoko_packet(nullptr, {}, {f < 5 ? 0.0 : 0.3, 0, 0}), false));
    Clip clip;
    clip.fps = 30;
    MocapCleanup clean;
    clean.reduce = false;
    clean.blend = 0;
    merge_recording(clip, s, vrm_table(), rest, rec.frames, 0, {}, clean);
    CHECK(curve_offset(clip, "mPelvis", 2).length() < 1e-3);
    CHECK((curve_offset(clip, "mPelvis", 7) - Vec3{0.3, 0, 0}).length() < 1e-3);  // SL proportions: scale 1
}

// --- Face tracking (MC-5) -----------------------------------------------------------------------

namespace {

const FaceTable& face_table() {
    static FaceTable t = [] {
        std::ifstream f(std::string(VATS_DATA_DIR) + "/../retarget/face-arkit.json");
        std::stringstream ss;
        ss << f.rdbuf();
        FaceTable out;
        std::string err;
        if (!parse_face_table(ss.str(), out, err)) std::fprintf(stderr, "face table: %s\n", err.c_str());
        return out;
    }();
    return t;
}

Vec3 face_rot(const VmcState& s, const std::string& bone, const FaceSettings& fs = {}) {
    Clip c;
    key_face(c, face_table(), s, fs, 0);
    return curve_euler(c, bone, 0);
}

}  // namespace

TEST(face_vmc_blendshapes_latch_on_apply) {
    std::vector<std::uint8_t> pkt = bundle({message("/VMC/Ext/Blend/Val", "sf", {1.f}, {"jawOpen"}),
                                            message("/VMC/Ext/Blend/Val", "sf", {0.5f}, {"Blink_L"})});
    std::vector<OscMessage> msgs;
    CHECK(parse_osc(pkt.data(), pkt.size(), msgs));
    VmcState s;
    for (auto& m : msgs) CHECK(apply_vmc(m, s));
    CHECK(s.blend.empty());  // not applied yet
    std::vector<std::uint8_t> apply = message("/VMC/Ext/Blend/Apply", "", {});
    msgs.clear();
    CHECK(parse_osc(apply.data(), apply.size(), msgs));
    CHECK(apply_vmc(msgs[0], s));
    CHECK_NEAR(s.blend["jawOpen"], 1.0, 1e-6);
    CHECK_NEAR(s.blend["Blink_L"], 0.5, 1e-6);
}

TEST(face_ifacialmocap_packets) {
    VmcState s;
    CHECK(apply_ifacialmocap("mouthSmile_R-50|eyeBlink_L-100|jawOpen-0|=head#0,30,0,0.1,0,0|rightEye#1,2,3|leftEye#1,2,3|", s));
    CHECK_NEAR(s.blend["mouthSmile_R"], 0.5, 1e-6);
    CHECK_NEAR(s.blend["eyeBlink_L"], 1.0, 1e-6);
    CHECK(s.has_face_head && std::fabs(s.face_head.w) < 0.999);  // a 30-degree turn
    CHECK(apply_ifacialmocap("jawOpen&75|=head#0,0,0,0,0,0|", s));  // version 2
    CHECK_NEAR(s.blend["jawOpen"], 0.75, 1e-6);
    CHECK(s.blend.count("eyeBlink_L") == 0);  // a packet is a whole frame
    CHECK(!apply_ifacialmocap("not a face packet", s));
    CHECK(arkit_name("eyeBlink_L") == "eyeBlinkLeft" && arkit_name("EyeBlinkRight") == "eyeBlinkRight");
}

namespace {

// A VTube Studio frame as the public docs describe it.
std::string vts_frame(const char* shapes = "{\"k\":\"EyeBlinkLeft\",\"v\":0.8},{\"k\":\"JawOpen\",\"v\":0.5},"
                                           "{\"k\":\"MouthSmile_R\",\"v\":1.7}") {
    return std::string("{\"Timestamp\":1695000000.5,\"Hotkey\":-1,\"FaceFound\":true,\"NewField\":[1,2],"
                       "\"Rotation\":{\"x\":0,\"y\":30,\"z\":0},\"Position\":{\"x\":0.1,\"y\":0,\"z\":0},"
                       "\"EyeLeft\":{\"x\":10,\"y\":0,\"z\":0},\"EyeRight\":{\"x\":10,\"y\":0,\"z\":0},\"BlendShapes\":[") +
           shapes + "]}";
}

// A Live Link Face packet laid out as PyLiveLinkFace (MIT) writes it: version (LE), a 37-byte device id, the name
// length (BE) and name, frame number, sub-frame, rate and denominator (BE), then 61 and 61 BE floats.
std::vector<std::uint8_t> llf_packet(const std::vector<float>& values, const std::string& name = "iPhone") {
    std::vector<std::uint8_t> p = {6, 0, 0, 0};
    const std::string id = "$01234567-89AB-CDEF-0123-456789ABCDEF";
    p.insert(p.end(), id.begin(), id.end());
    auto be = [&](std::uint32_t u) {
        for (int s = 24; s >= 0; s -= 8) p.push_back(std::uint8_t(u >> s));
    };
    auto bef = [&](float f) {
        std::uint32_t u;
        std::memcpy(&u, &f, 4);
        be(u);
    };
    be(std::uint32_t(name.size()));
    p.insert(p.end(), name.begin(), name.end());
    be(1234), bef(0.5f), be(60), be(1);
    p.push_back(61);
    for (int i = 0; i < 61; ++i) bef(i < int(values.size()) ? values[i] : 0.f);
    return p;
}

double yaw_deg(const Quat& q) { return quat_to_euler(q).z; }

}  // namespace

TEST(face_vts_frames) {
    VmcState s;
    CHECK(apply_vts(vts_frame(), s));
    CHECK_NEAR(s.blend["EyeBlinkLeft"], 0.8, 1e-6);
    CHECK_NEAR(s.blend["MouthSmile_R"], 1.0, 1e-6);  // clamped to 0..1
    CHECK(s.has_face_head && std::fabs(std::fabs(yaw_deg(s.face_head)) - 30) < 1e-6);  // Unity Euler, as iFacialMocap
    CHECK(s.has_eyes);
    // Into the face pipeline under ARKit names.
    const auto w = face_weights(face_table(), s.blend, FaceSettings{});
    CHECK(w.count("eyeBlinkLeft") && w.count("jawOpen") && w.count("mouthSmileRight"));
    // A frame is whole: a shape it leaves out is gone. A face with no angles keeps the last ones.
    CHECK(apply_vts("{\"BlendShapes\":[{\"k\":\"JawOpen\",\"v\":0.25},{\"k\":7,\"v\":1},{\"k\":\"x\"}]}", s));
    CHECK(s.blend.size() == 1 && std::fabs(s.blend["JawOpen"] - 0.25) < 1e-6);
    // Truncated, garbage, other JSON: refused, and nothing changes.
    const std::string whole = vts_frame();
    CHECK(!apply_vts(std::string_view(whole).substr(0, whole.size() / 2), s));
    CHECK(!apply_vts("\x01\x02garbage\xff", s));
    CHECK(!apply_vts("{\"Rotation\":{\"x\":1,\"y\":2,\"z\":3}}", s));
    CHECK(!apply_vts("[1,2,3]", s));
    CHECK(s.blend.size() == 1);
    // A hostile frame cannot grow the state without bound.
    std::string many;
    for (int i = 0; i < 1000; ++i) many += (i ? "," : "") + std::string("{\"k\":\"s") + std::to_string(i) + "\",\"v\":1}";
    CHECK(apply_vts(vts_frame(many.c_str()), s));
    CHECK(s.blend.size() == 128);
    // The request names the port the phone replies to.
    const std::string req = vts_request(21413);
    CHECK(req.find("\"iOSTrackingDataRequest\"") != std::string::npos && req.find("[21413]") != std::string::npos);
}

TEST(face_live_link_face_packets) {
    std::vector<float> v(61, 0.f);
    v[0] = 0.9f;    // eyeBlinkLeft
    v[17] = 0.6f;   // jawOpen
    v[24] = 1.2f;   // mouthSmileRight, clamped
    v[51] = 0.3f;   // tongueOut
    v[52] = 0.5f;   // head yaw
    v[56] = -0.1f;  // left eye pitch
    VmcState s;
    CHECK(apply_live_link_face(llf_packet(v).data(), llf_packet(v).size(), s));
    CHECK(s.blend.size() == 52);
    CHECK_NEAR(s.blend["eyeBlinkLeft"], 0.9, 1e-6);
    CHECK_NEAR(s.blend["jawOpen"], 0.6, 1e-6);
    CHECK_NEAR(s.blend["mouthSmileRight"], 1.0, 1e-6);
    CHECK_NEAR(s.blend["tongueOut"], 0.3, 1e-6);
    CHECK(s.has_face_head && std::fabs(std::fabs(yaw_deg(s.face_head)) - 0.5 * kLiveLinkFaceDegreesPerUnit) < 1e-3);
    CHECK(s.has_eyes && std::fabs(std::fabs(quat_to_euler(s.eye_left).y) - 0.1 * kLiveLinkFaceDegreesPerUnit) < 1e-3);
    // Every table name exists in the face table, so all 52 reach the pipeline.
    for (auto& [name, w] : s.blend) CHECK(face_table().shapes.count(name));
    // Read from the tail: another name length (a header that differs) still reads.
    VmcState t;
    const auto renamed = llf_packet(v, "A much longer device name");
    CHECK(apply_live_link_face(renamed.data(), renamed.size(), t) && std::fabs(t.blend["jawOpen"] - 0.6) < 1e-6);
    // Truncated, garbage, a wrong count, a NaN: refused, and the state is left alone.
    auto p = llf_packet(v);
    VmcState u;
    CHECK(!apply_live_link_face(p.data(), 200, u));
    CHECK(!apply_live_link_face(p.data(), p.size() - 1, u));
    CHECK(!apply_live_link_face(nullptr, 0, u));
    std::vector<std::uint8_t> junk(400);
    for (size_t i = 0; i < junk.size(); ++i) junk[i] = std::uint8_t(i * 97 + 13);
    junk[junk.size() - 245] = 61;  // even with the count byte in place, wild values are refused
    CHECK(!apply_live_link_face(junk.data(), junk.size(), u));
    auto wrong = p;
    wrong[wrong.size() - 245] = 60;
    CHECK(!apply_live_link_face(wrong.data(), wrong.size(), u));
    auto nan = p;
    for (int i = 0; i < 4; ++i) nan[nan.size() - 4 + i] = 0xFF;
    CHECK(!apply_live_link_face(nan.data(), nan.size(), u));
    CHECK(u.blend.empty() && !u.has_face_head);
}

TEST(face_rokoko_face_weights) {
    // meta.hasFace with actors[i].face: ARKit weights 0..100, next to the body. faceId is not a weight.
    std::string json = rokoko_packet();
    const std::string face = "\"meta\":{\"hasBody\":true,\"hasFace\":true},\"face\":{\"faceId\":\"f1\","
                             "\"jawOpen\":50,\"eyeBlinkLeft\":100,\"mouthSmileRight\":250},";
    const size_t at = json.find("\"meta\":{\"hasBody\":true}");
    json.replace(at, std::string("\"meta\":{\"hasBody\":true},").size(), face);
    VmcState s = rokoko_state(json, false);
    CHECK(s.bones.count("Hips"));
    CHECK(s.blend.size() == 3);
    CHECK_NEAR(s.blend["jawOpen"], 0.5, 1e-6);
    CHECK_NEAR(s.blend["mouthSmileRight"], 1.0, 1e-6);
    CHECK(face_weights(face_table(), s.blend, FaceSettings{}).count("eyeBlinkLeft"));
    // Without meta.hasFace the face is ignored.
    VmcState plain = rokoko_state(rokoko_packet(), false);
    CHECK(plain.blend.empty());
    // A face-only actor (Face Capture, no suit) is used when no actor has a body.
    const std::string only = "{\"version\":3,\"scene\":{\"actors\":[{\"name\":\"Face\",\"meta\":{\"hasFace\":true},"
                             "\"face\":{\"jawOpen\":40}}]}}";
    VmcState f;
    std::string used, err;
    CHECK(apply_rokoko(reinterpret_cast<const std::uint8_t*>(only.data()), only.size(), f, "", used, err));
    CHECK(used == "Face" && f.bones.empty() && std::fabs(f.blend["jawOpen"] - 0.4) < 1e-6);
    // Truncated: refused.
    CHECK(!apply_rokoko(reinterpret_cast<const std::uint8_t*>(only.data()), only.size() / 2, f, "", used, err));
}

TEST(face_shapes_move_the_mapped_bones) {
    CHECK(face_table().shapes.size() == 52);
    VmcState s;
    s.blend["jawOpen"] = 1;
    const Vec3 jaw = face_rot(s, "mFaceJaw");
    CHECK_NEAR(jaw.y, face_table().shapes.at("jawOpen")[0].rot.y, 1e-6);
    CHECK(jaw.y > 10);  // opens: the jaw tips down
    s.blend = {{"eyeBlinkLeft", 1.f}};
    CHECK(face_rot(s, "mFaceEyeLidUpperLeft").y > 20);            // the left lid closes
    CHECK_NEAR(face_rot(s, "mFaceEyeLidUpperRight").y, 0.0, 1e-9);  // the right one stays open
    CHECK_NEAR(face_rot(s, "mFaceJaw").y, 0.0, 1e-9);                // and the jaw returns to rest
    s.blend = {{"A", 1.f}, {"Blink_R", 1.f}};                          // VRM presets through aliases
    CHECK_NEAR(face_rot(s, "mFaceJaw").y, 0.7 * jaw.y, 1e-6);
    CHECK(face_rot(s, "mFaceEyeLidUpperRight").y > 20);
}

TEST(face_eyes_from_shapes_drive_both_eye_bones_and_lids) {
    VmcState s;
    s.blend = {{"eyeLookOutLeft", 1.f}};  // the left eye looks out: to the avatar's left, positive yaw
    CHECK_NEAR(face_rot(s, "mEyeLeft").z, 20.0, 1e-6);
    CHECK_NEAR(face_rot(s, "mFaceEyeAltLeft").z, 20.0, 1e-6);  // the Bento eye follows too
    CHECK_NEAR(face_rot(s, "mEyeRight").z, 0.0, 1e-9);
    s.blend = {{"eyeLookDownLeft", 1.f}};  // positive pitch looks down
    CHECK_NEAR(face_rot(s, "mEyeLeft").y, 18.0, 1e-6);
    CHECK_NEAR(face_rot(s, "mFaceEyeLidUpperLeft").y, 0.4 * 18.0, 1e-6);  // the upper lid lowers with it
    CHECK_NEAR(face_rot(s, "mFaceEyeLidLowerLeft").y, 0.15 * 18.0, 1e-6);
    FaceSettings fs;
    fs.eye_gain = 0;
    CHECK_NEAR(face_rot(s, "mEyeLeft", fs).y, 0.0, 1e-9);
    CHECK_NEAR(face_rot(s, "mFaceEyeLidUpperLeft", fs).y, 0.0, 1e-9);
}

TEST(face_eyes_from_sender_rotations_win_and_are_clamped) {
    VmcState s;
    s.blend = {{"eyeLookOutLeft", 1.f}};
    // VMC eye bones (already in SL axes): 10 degrees down wins over the eyeLook shape.
    s.bones["LeftEye"] = Xform{euler_to_quat({0, 10, 0}), {}};
    CHECK_NEAR(face_rot(s, "mEyeLeft").y, 10.0, 1e-4);
    CHECK_NEAR(face_rot(s, "mEyeLeft").z, 0.0, 1e-4);
    CHECK_NEAR(face_rot(s, "mFaceEyeLidUpperLeft").y, 4.0, 1e-4);
    // iFacialMocap eye fields: Unity yaw +20 turns right, which is negative yaw in SL; 40 is clamped.
    VmcState f;
    CHECK(apply_ifacialmocap("jawOpen-0|=head#0,0,0,0,0,0|rightEye#0,40,0|leftEye#0,20,0|", f));
    CHECK(f.has_eyes);
    CHECK_NEAR(face_rot(f, "mEyeLeft").z, -20.0, 1e-4);
    CHECK_NEAR(face_rot(f, "mFaceEyeAltLeft").z, -20.0, 1e-4);
    CHECK_NEAR(face_rot(f, "mEyeRight").z, -25.0, 1e-4);  // the default limit
    FaceSettings fs;
    fs.eye_yaw_max = 30;
    CHECK_NEAR(face_rot(f, "mEyeRight", fs).z, -30.0, 1e-4);
    // Unity pitch +10 looks down: positive in SL too.
    CHECK(apply_ifacialmocap("jawOpen-0|=head#0,0,0,0,0,0|rightEye#10,0,0|leftEye#10,0,0|", f));
    CHECK_NEAR(face_rot(f, "mEyeLeft").y, 10.0, 1e-4);
}

TEST(face_lip_corners_go_by_position) {
    // SL's skeleton has the corner bones swapped by name: mFaceLipCornerLeft sits on the avatar's right.
    // A left smile must move the corner on the avatar's left (+Y), whatever it is called.
    VmcState s;
    s.blend = {{"mouthSmileLeft", 1.f}};
    Clip c;
    FaceSettings fs;
    fs.positions = true;
    key_face(c, face_table(), s, fs, 0);
    const auto g = skel().global_pose(Pose(skel().size()));
    std::string moved;
    for (const char* bone : {"mFaceLipCornerLeft", "mFaceLipCornerRight"})
        if (curve_offset(c, bone, 0).length() > 1e-4) moved = bone;
    CHECK(!moved.empty());
    CHECK(g[skel().find(moved)].pos.y > 0);
}

TEST(face_positions_off_keys_rotations_only) {
    // A mesh head with its own face joint positions: no offset keys, so the head keeps its shape.
    VmcState s;
    s.blend = {{"mouthSmileLeft", 1.f}, {"jawOpen", 1.f}};
    FaceSettings fs;
    fs.positions = false;
    Clip c;
    key_face(c, face_table(), s, fs, 0);
    for (auto& [track, channels] : c.curves) CHECK(!c.has_channels(track, kPosChannels));
    CHECK(curve_euler(c, "mFaceJaw", 0).length() > 1e-3);
}

TEST(live_face_positions_off_moves_no_joint) {
    // The live drive (the viewer's preview) takes the same setting: with Move face bones off, the evaluated pose
    // has no offset on any bone, so a host sets no joint position and a mesh head keeps its own (reported on build 13).
    VmcState now;
    now.blend = {{"mouthSmileLeft", 1.f}, {"jawOpen", 1.f}, {"browInnerUp", 1.f}};
    FaceSettings fs;
    fs.positions = false;
    RigTable none;
    Clip live = live_pose(skel(), none, VmcState{}, now, nullptr, &face_table(), fs);
    CHECK(!live.curves.empty());
    const Pose pose = evaluate_curves(skel(), live, 0);
    for (int i = 0; i < skel().size(); ++i) CHECK(pose.offset[i].length() < 1e-9);
    fs.positions = true;  // and with it on, the table does move some
    live = live_pose(skel(), none, VmcState{}, now, nullptr, &face_table(), fs);
    const Pose moved = evaluate_curves(skel(), live, 0);
    bool any = false;
    for (int i = 0; i < skel().size(); ++i) any = any || moved.offset[i].length() > 1e-6;
    CHECK(any);
}

TEST(face_calibration_and_gain) {
    FaceSettings fs;
    fs.neutral["jawOpen"] = 0.2;  // this performer rests with the mouth slightly open
    VmcState s;
    s.blend["jawOpen"] = 0.2;
    CHECK_NEAR(face_rot(s, "mFaceJaw", fs).y, 0.0, 1e-4);
    s.blend["jawOpen"] = 0.6;
    const double full = face_table().shapes.at("jawOpen")[0].rot.y;
    CHECK_NEAR(face_rot(s, "mFaceJaw", fs).y, 0.5 * full, 1e-4);  // (0.6 - 0.2) / (1 - 0.2)
    fs.gains["jawOpen"] = 2;
    CHECK_NEAR(face_rot(s, "mFaceJaw", fs).y, 1.0 * full, 1e-4);
}

TEST(face_only_take_records_face_bones) {
    std::vector<VmcState> frames(3);
    frames[1].blend["jawOpen"] = 1;
    Clip clip;
    clip.fps = 30;
    MocapCleanup clean;
    clean.reduce = false;
    clean.blend = 0;
    RigTable none;
    auto report = merge_recording(clip, skel(), none, VmcState{}, frames, 10, {}, clean, nullptr, &face_table());
    CHECK(!report.empty() && report[0].find("recorded 3 frames") != std::string::npos);
    CHECK(clip.curves.count("mFaceJaw") == 1);
    CHECK_NEAR(curve_euler(clip, "mFaceJaw", 10).y, 0.0, 1e-6);
    CHECK(curve_euler(clip, "mFaceJaw", 11).y > 10);
    CHECK(clip.curves.count("mPelvis") == 0);  // no body in a face-only take
}

TEST(face_take_creates_no_position_channel_a_shape_did_not_move) {
    // A worn mesh head (2026-09-27): a take keyed rest offsets on every face bone, and the upload pinned them all
    // to the default face. Now only bones a shape moved get position channels; the rest keep rotations only.
    CHECK(!FaceSettings{}.positions);  // Move face bones is off by default
    std::vector<VmcState> frames(3);
    frames[1].blend["mouthSmileLeft"] = 1;
    MocapCleanup clean;
    clean.reduce = false;
    clean.blend = 0;
    FaceSettings fs;
    fs.positions = true;
    RigTable none;
    Clip clip;
    clip.fps = 30;
    clip.curves["mFaceChin"]["pos_z"].set_key(0, 0.01);  // positioned before the take
    merge_recording(clip, skel(), none, VmcState{}, frames, 10, {}, clean, nullptr, &face_table(), fs);
    std::string corner;
    for (const char* bone : {"mFaceLipCornerLeft", "mFaceLipCornerRight"})
        if (curve_offset(clip, bone, 11).length() > 1e-4) corner = bone;
    CHECK(!corner.empty());                               // the smile moved a corner, exactly as before
    CHECK_NEAR(curve_offset(clip, corner, 10).length(), 0.0, 1e-9);  // from rest
    int unmoved = 0;
    for (const std::string& bone : face_table().bones()) {
        if (bone == corner || bone == "mFaceChin") continue;
        bool moved = false;
        for (auto& m : face_table().shapes.at("mouthSmileLeft")) moved = moved || (m.bone == bone && m.has_pos);
        if (moved) continue;
        CHECK(!clip.has_channels(bone, kPosChannels));
        unmoved += clip.curves.count(bone) && clip.has_channels(bone, kRotChannels);
    }
    CHECK(unmoved > 10);  // they are still keyed, with rotations
    // A channel the clip already had is overwritten with rest over the take, as before.
    CHECK_NEAR(curve_offset(clip, "mFaceChin", 11).z, 0.0, 1e-9);
}

// --- Acceptance (2026-09-27): "it has to ensure the end result doesn't deform a furry head" ----------

namespace {

// A furry muzzle: every face joint 2-6 cm forward and to the side of SL's default, each by a different amount,
// and a bigger head. As the viewer host reports it: offsets over the skeleton's positions, and scales.
Shape furry_head() {
    const Skeleton& s = skel();
    Shape h;
    h.scale.assign(s.size(), Vec3{1, 1, 1});
    h.offset.assign(s.size(), Vec3{});
    int k = 0;
    for (int i = 0; i < s.size(); ++i) {
        if (s[i].category != Category::Face) continue;
        h.offset[i] = {0.02 + 0.04 * (k % 7) / 6.0, (k % 2 ? 1 : -1) * (0.005 + 0.003 * (k % 5)), 0.002 * (k % 3)};
        ++k;
    }
    h.scale[s.find("mHead")] = {1.15, 1.2, 1.1};
    return h;
}

std::vector<std::string> overridden(const Shape& worn) {
    std::vector<std::string> names;
    for (int i = 0; i < skel().size(); ++i)
        if (worn.offset[i].length() > 0) names.push_back(skel()[i].name);
    return names;
}

// A recorded take through the real path (merge_recording, key_face): rest, a smile with brows and jaw, rest, a
// sneer and pucker, rest. Frames 0, 4 and 7 are neutral.
Clip face_take(bool move_face_bones) {
    std::vector<VmcState> frames(8);
    for (auto& f : frames) f.blend["jawOpen"] = 0;  // neutral frames still send a face
    for (int i : {1, 2, 3})
        frames[i].blend = {{"mouthSmileLeft", 0.3f * i}, {"browInnerUp", 0.25f * i}, {"jawOpen", 0.2f * i}};
    for (int i : {5, 6}) frames[i].blend = {{"noseSneerRight", 0.8f}, {"mouthPucker", 0.5f * (i - 4)}, {"cheekPuff", 0.6f}};
    MocapCleanup clean;
    clean.reduce = false, clean.blend = 0, clean.smooth = 0;
    FaceSettings fs;
    fs.positions = move_face_bones;
    fs.head = false;
    RigTable none;
    Clip take;
    take.fps = 30;
    take.ease_in = take.ease_out = 0;  // a quarter-second take: no ease warning
    merge_recording(take, skel(), none, VmcState{}, frames, 0, {}, clean, nullptr, &face_table(), fs);
    return take;
}

struct Played {
    double worst_local = 0;     // |played - (worn + intended offset)|, local, every face joint and frame
    double worst_world = 0;     // the same in world space, on the worn head
    double worst_rest = 0;      // |played - worn| on the neutral frames
    int unmoved_with_keys = 0;  // face joints the take never moved in position that still carry position keys
};

// Plays the .anim bytes as SL does on an avatar wearing `worn`: a position key replaces the joint's position, a
// joint without position keys keeps the worn one; rotations (which include the rest) replace the rest rotation.
Played play_on(const std::vector<std::uint8_t>& bytes, const Clip& take, const Shape& worn) {
    const Skeleton& s = skel();
    AnimFile f;
    std::string err;
    CHECK(parse_anim(bytes, f, err));
    std::vector<const AnimJoint*> by_node(s.size(), nullptr);
    for (const AnimJoint& j : f.joints)
        if (int i = s.find_viewer(j.name); i >= 0) by_node[i] = &j;
    CHECK(!by_node[0]);  // a face take leaves the pelvis alone
    auto at = [&](const auto& keys, double t, auto decode, auto mix) {
        auto time = [&](size_t k) { return double(u16_to_f32(keys[k][0], 0.f, f.duration)); };
        size_t k = 0;
        while (k + 1 < keys.size() && time(k + 1) <= t) ++k;
        if (k + 1 >= keys.size() || time(k) >= t) return decode(keys[k]);
        return mix(decode(keys[k]), decode(keys[k + 1]), (t - time(k)) / (time(k + 1) - time(k)));
    };
    Played out;
    for (int i = 0; i < s.size(); ++i)
        if (s[i].category == Category::Face && by_node[i] && !by_node[i]->pos.empty()) {
            bool moved = false;
            for (int fr = 0; fr <= take.end_frame; ++fr) moved = moved || curve_offset(take, s[i].name, fr).length() > 0;
            out.unmoved_with_keys += !moved;
        }
    for (int fr = 0; fr <= take.end_frame; ++fr) {
        const double t = double(fr) / take.fps;
        const Pose intended = evaluate_curves(s, take, fr);
        const std::vector<Xform> want = s.global_pose(intended, &worn);
        std::vector<Xform> got(s.size());
        const bool neutral = fr == 0 || fr == 4 || fr == 7;
        for (int i = 0; i < s.size(); ++i) {
            const Node& n = s[i];
            Vec3 local = n.pos + worn.offset[i];
            Quat rot = n.rest;
            if (const AnimJoint* j = by_node[i]) {
                if (!j->pos.empty())
                    local = at(j->pos, t, decode_position, [](Vec3 a, Vec3 b, double w) { return a + (b - a) * w; });
                if (!j->rot.empty())
                    rot = at(j->rot, t, decode_rotation, [](Quat a, Quat b, double w) { return nlerp(a, b, w); });
            }
            const Vec3 scaled = n.parent >= 0 ? local.mul(worn.scale[n.parent]) : local;
            got[i] = n.parent >= 0 ? got[n.parent] * Xform{rot, scaled} : Xform{rot, scaled};
            if (n.category != Category::Face) continue;
            const Vec3 worn_pos = n.pos + worn.offset[i];
            out.worst_local = std::max(out.worst_local, (local - (worn_pos + intended.offset[i])).length());
            out.worst_world = std::max(out.worst_world, (got[i].pos - want[i].pos).length());
            if (neutral) out.worst_rest = std::max(out.worst_rest, (local - worn_pos).length());
        }
    }
    return out;
}

}  // namespace

TEST(face_take_on_a_furry_head_plays_as_recorded) {
    const Shape worn = furry_head();
    const double quantum = 10.0 / 65535 * 2 * std::sqrt(3.0);  // two position codes per axis (double-pass quantiser)
    for (bool move : {true, false}) {
        const Clip take = face_take(move);
        int positioned = 0;
        for (auto& [track, ch] : take.curves) positioned += take.has_channels(track, kPosChannels);
        CHECK(move ? positioned > 3 : positioned == 0);
        for (double reduce : {0.0, AnimExportOptions{}.reduce_pos_m}) {
            AnimExportOptions opt;  // "Your avatar": the worn head's positions
            opt.reduce_pos_m = reduce;
            opt.positions = &worn;
            opt.worn_overrides = overridden(worn);
            AnimExportResult r = export_anim(skel(), take, opt);
            CHECK(r.errors.empty());
            CHECK(r.warnings.empty());
            const Played p = play_on(write_anim(r.file), take, worn);
            CHECK(p.worst_local <= quantum + reduce);  // (a) worn + the take's offset, every frame
            CHECK(p.worst_rest <= quantum);            // (b) neutral frames sit on the worn positions
            CHECK(p.unmoved_with_keys == 0);           // (c) unmoved joints carry no position keys
            CHECK(p.worst_world <= 1e-3);              // and the face lands where the preview shows it
            if (p.worst_local > quantum + reduce || p.worst_world > 1e-3 || p.worst_rest > quantum)
                std::fprintf(stderr, "    move %d reduce %g: local %.6f world %.6f rest %.6f\n", move, reduce,
                             p.worst_local, p.worst_world, p.worst_rest);
        }
    }
}

TEST(face_take_baked_on_sl_default_deforms_a_furry_head_and_warns) {
    // The same check catches the reported bug: SL Default positions on a worn furry head.
    const Shape worn = furry_head();
    AnimExportOptions opt;
    opt.worn_overrides = overridden(worn);
    AnimExportResult r = export_anim(skel(), face_take(true), opt);
    const Played p = play_on(write_anim(r.file), face_take(true), worn);
    CHECK(p.worst_local > 0.015 && p.worst_rest > 0.015);  // moved joints jump to the default face
    CHECK(r.warnings.size() == 1 && r.warnings[0].find("mesh head is worn") != std::string::npos);
    // With Move face bones off, SL Default is safe too: no face position keys, no warning.
    AnimExportResult off = export_anim(skel(), face_take(false), opt);
    CHECK(off.warnings.empty());
    const Played q = play_on(write_anim(off.file), face_take(false), worn);
    CHECK(q.worst_local <= 1e-9 && q.unmoved_with_keys == 0);
}

TEST(mocap_filter_replaces_box_and_reports_shake) {
    // MC-4a: a take of the arm swinging 1 Hz with +-1.5 degrees of tracker jitter. The Butterworth clean-up
    // cuts the shoulder's shake at least four-fold and reports it; the box filter stays for old settings.
    const Skeleton& s = skel();
    RigTable table = vrm_table();
    VmcState rest = vmc_t_pose(sender());
    std::vector<VmcState> frames;
    unsigned st = 7;
    for (int i = 0; i < 60; ++i) {
        st = st * 1664525u + 1013904223u;
        const double deg = 30 * std::sin(2 * kPi * i / 30) + ((st >> 8) / double(1u << 24) * 3 - 1.5);
        frames.push_back(sender("LeftUpperArm", Quat::axis_angle({1, 0, 0}, deg * kDegToRad)));
    }
    MocapCleanup clean;
    clean.reduce = false, clean.blend = 0;
    auto shake = [&](const Clip& c) { return shake_scores(c, {"mShoulderLeft"}, 0, 59).at(0).rot; };
    Clip raw, box, filtered;
    raw.fps = box.fps = filtered.fps = 30;
    merge_recording(raw, s, table, rest, frames, 0, {}, clean);
    clean.smooth = 2;
    merge_recording(box, s, table, rest, frames, 0, {}, clean);
    clean.use_filter = true;
    clean.filter.kind = FilterKind::Butterworth;
    const auto report = merge_recording(filtered, s, table, rest, frames, 0, {}, clean);
    std::printf("    shoulder shake: raw %.0f, box %.0f, Butterworth %.0f\n", shake(raw), shake(box), shake(filtered));
    CHECK(shake(box) < shake(raw) && shake(box) != shake(filtered));
    CHECK(shake(filtered) * 4 < shake(raw));
    bool reported = false;
    for (const std::string& line : report) reported |= line.rfind("Butterworth filter: shake ", 0) == 0;
    CHECK(reported);
}

TEST(face_table_round_trip) {
    const FaceTable& original = face_table();
    std::string json_str = write_face_table(original);
    FaceTable roundtrip;
    std::string err;
    CHECK(parse_face_table(json_str, roundtrip, err));
    CHECK(err.empty());
    CHECK(original == roundtrip);
    CHECK_EQ(original.shapes.size(), roundtrip.shapes.size());
    CHECK_EQ(original.bones().size(), roundtrip.bones().size());
}

TEST(face_offsets_scale_on_mesh_head) {
    const Skeleton& s = skel();
    const FaceTable& table = face_table();
    // A mesh head whose eyes and mouth corners are 1.5x as far apart as SL's
    Shape mesh_head;
    mesh_head.scale.assign(s.size(), Vec3{1, 1, 1});
    mesh_head.offset.assign(s.size(), Vec3{});
    const auto rest_globals = s.global_pose(Pose(s.size()));
    auto widen = [&](const char* a, const char* b, double k) {
        const int i = s.find(a), j = s.find(b);
        const double d0 = (rest_globals[i].pos - rest_globals[j].pos).length();
        mesh_head.offset[i] = Vec3{0, d0 * (k - 1) / 2, 0};
        mesh_head.offset[j] = Vec3{0, -d0 * (k - 1) / 2, 0};
    };
    widen("mEyeLeft", "mEyeRight", 1.5);
    widen("mFaceEyeAltLeft", "mFaceEyeAltRight", 1.5);
    widen("mFaceLipCornerRight", "mFaceLipCornerLeft", 1.5);  // SL's corners are swapped by name
    const int lc = s.find("mFaceLipCornerRight");             // avatar's left (+Y)
    const double expected_scale = 1.5;
    const double measured_scale = face_scale(s, &mesh_head);
    CHECK_NEAR(measured_scale, expected_scale, 1e-3);

    // A mouth 1.5x as wide on SL's eyes, and the same with the eyes 0.2 mm wider: about the same scale. (The first
    // measure that differed at all used to win: 1.5, then 1.003.)
    Shape mouth;
    mouth.scale.assign(s.size(), Vec3{1, 1, 1});
    mouth.offset.assign(s.size(), Vec3{});
    for (const char* n : {"mFaceLipCornerRight", "mFaceLipCornerLeft"}) mouth.offset[s.find(n)] = mesh_head.offset[s.find(n)];
    Shape eyes_nudged = mouth;
    eyes_nudged.offset[s.find("mEyeLeft")].y = 0.0001, eyes_nudged.offset[s.find("mEyeRight")].y = -0.0001;
    CHECK(std::fabs(face_scale(s, &mouth) - face_scale(s, &eyes_nudged)) < 0.01);

    // Key a smile on this mesh head
    VmcState st;
    st.blend["mouthSmileLeft"] = 1.0f;
    FaceSettings fs;
    fs.positions = true;
    fs.head = false;
    fs.scale = measured_scale;
    Clip clip;
    key_face(clip, table, st, fs, 0);

    // The keyed offset in clip must be table_offset * measured_scale
    Vec3 raw_offset;
    for (auto& m : table.shapes.at("mouthSmileLeft")) {
        if (m.bone == "mFaceLipCornerRight" && m.has_pos) raw_offset = m.pos;
    }
    const Vec3 keyed_offset = curve_offset(clip, "mFaceLipCornerRight", 0);
    CHECK_NEAR(keyed_offset.y, raw_offset.y * measured_scale, 1e-6);
    CHECK_NEAR(keyed_offset.z, raw_offset.z * measured_scale, 1e-6);

    // When exported to .anim with opt.positions = &mesh_head:
    // in Second Life, positions replace the joint rest. The exported position must equal
    // (mesh head joint position) + (scaled offset).
    AnimExportOptions opt;
    opt.positions = &mesh_head;
    opt.worn_overrides = {"mFaceLipCornerRight", "mFaceLipCornerLeft"};
    AnimExportResult r = export_anim(s, clip, opt);
    CHECK(r.errors.empty());
    AnimFile af;
    std::string err;
    CHECK(parse_anim(write_anim(r.file), af, err));
    const AnimJoint* j = nullptr;
    for (auto& joint : af.joints) {
        if (joint.name == "mFaceLipCornerRight") j = &joint;
    }
    CHECK(j != nullptr && !j->pos.empty());
    const Vec3 exported_pos = decode_position(j->pos[0]);
    const Vec3 mesh_head_joint = s[lc].pos + mesh_head.offset[lc];
    const Vec3 expected_pos = mesh_head_joint + keyed_offset;
    CHECK_NEAR(exported_pos.x, expected_pos.x, 1e-3);
    CHECK_NEAR(exported_pos.y, expected_pos.y, 1e-3);
    CHECK_NEAR(exported_pos.z, expected_pos.z, 1e-3);
}

namespace {

// A constructed head (CC0): the SL face k times its size (every face joint and the eyes spread k times as far from
// their parents), and one vertex per (joint, point) rigid on that joint.
struct SkinnedHead {
    Shape shape;
    DaeModel model;
};

Shape face_sized(double k) {
    const Skeleton& s = skel();
    Shape h;
    h.scale.assign(s.size(), Vec3{1, 1, 1});
    h.offset.assign(s.size(), Vec3{});
    for (int i = 0; i < s.size(); ++i)
        if (s[i].name.rfind("mFace", 0) == 0 || s[i].name == "mEyeLeft" || s[i].name == "mEyeRight")
            h.offset[i] = s[i].pos * (k - 1);
    return h;
}

SkinnedHead skinned_head(const Shape& shape, const std::vector<std::pair<int, Vec3>>& verts) {
    const Skeleton& s = skel();
    SkinnedHead h{shape, {}};
    DaeModel& m = h.model;
    m.rigged = true;
    m.binds = s.global_pose(Pose(s.size()), &h.shape);
    m.binds.resize(dae_index_count(s));  // mRoot and the volumes: identity, nothing is weighted to them
    m.bound.assign(dae_index_count(s), true);
    for (auto& [j, p] : verts) {
        m.positions.insert(m.positions.end(), {float(p.x), float(p.y), float(p.z)});
        m.normals.insert(m.normals.end(), {0.f, 0.f, 1.f});
        m.joints.insert(m.joints.end(), {j, dae_root(s), dae_root(s), dae_root(s)});
        m.weights.insert(m.weights.end(), {1.f, 0.f, 0.f, 0.f});
    }
    return h;
}

// Where each vertex goes when the shapes are keyed at these weights, as the face shows them: the table's keys
// posed on the head's shape and skinned.
std::vector<Vec3> skin_face(const SkinnedHead& h, const std::map<std::string, float>& weights, bool positions,
                            double scale) {
    const Skeleton& s = skel();
    VmcState st;
    st.blend = weights;
    FaceSettings fs;
    fs.positions = positions, fs.head = false, fs.scale = scale;
    Clip c;
    key_face(c, face_table(), st, fs, 0);
    Pose p(s.size());
    for (const std::string& b : face_table().bones())
        if (const int j = s.find(b); j >= 0) {
            p.rot[j] = euler_to_quat(curve_euler(c, b, 0));
            if (positions) p.offset[j] = curve_offset(c, b, 0);
        }
    std::vector<float> pos, nrm;
    skin_prop(h.model, s, s.global_pose(p, &h.shape), &h.shape, pos, nrm);
    std::vector<Vec3> out;
    for (size_t v = 0; v + 2 < pos.size(); v += 3) out.push_back({pos[v], pos[v + 1], pos[v + 2]});
    return out;
}

}  // namespace

TEST(face_moves_scale_with_the_head_they_play_on) {
    // The same face 1.3 times SL's size, its skin 1.3 times as far from each joint: every shape at full weight must
    // move that skin 1.3 times as far as on SL's face, and never more than 5 cm on it (the open jaw's lips go
    // farthest). This holds only when face_scale measures the head and the keys use it.
    const Skeleton& s = skel();
    auto head = [&](double k) {
        const Shape shape = face_sized(k);
        const std::vector<Xform> g = s.global_pose(Pose(s.size()), &shape);
        std::vector<std::pair<int, Vec3>> verts;
        for (const std::string& b : face_table().bones())
            if (const int j = s.find(b); j >= 0)
                for (const Vec3& d : {Vec3{0.02, 0, 0}, Vec3{0.015, 0.01, -0.005}}) verts.push_back({j, g[j].pos + d * k});
        return skinned_head(shape, verts);
    };
    const SkinnedHead sl = head(1), big = head(1.3);
    const double k = face_scale(s, &big.shape);
    CHECK_NEAR(k, 1.3, 1e-6);
    CHECK_NEAR(face_scale(s, &sl.shape), 1.0, 1e-9);
    const std::vector<Vec3> sl0 = skin_face(sl, {}, true, 1), big0 = skin_face(big, {}, true, k);
    for (auto& [name, motions] : face_table().shapes) {
        const std::vector<Vec3> a = skin_face(sl, {{name, 1.f}}, true, 1), b = skin_face(big, {{name, 1.f}}, true, k);
        for (size_t v = 0; v < a.size(); ++v) {
            const Vec3 da = a[v] - sl0[v], db = b[v] - big0[v];
            CHECK((db - da * 1.3).length() < 1e-5);
            CHECK(db.length() < 0.05 * 1.3);
        }
    }
}

TEST(face_shapes_move_the_skin_the_right_way_wherever_it_is_weighted) {
    // A mesh head may weight a joint's skin far from the joint: the OCOL devkit's mFaceLipCornerRight skin sits on
    // the other side of the mouth, 5 cm from the joint. Turning such a joint swings its skin about a far pivot, so
    // a smile's corner roll pulled that skin down. Each check skins a vertex at the joint, one in front of it, and
    // one mirrored across the mouth and in front, as OCOL has it.
    const Skeleton& s = skel();
    const Shape shape = face_sized(1);
    const std::vector<Xform> g = s.global_pose(Pose(s.size()), &shape);
    auto skin_of = [&](const char* bone) {
        const int j = s.find(bone);
        const Vec3 at = g[j].pos;
        return std::vector<std::pair<int, Vec3>>{
            {j, at}, {j, at + Vec3{0.025, 0, 0.005}}, {j, Vec3{at.x + 0.03, -at.y, at.z + 0.004}}};
    };
    // How far each vertex rises, in metres, for one shape.
    auto rise = [&](const char* bone, const char* shape_name, bool positions) {
        const SkinnedHead h = skinned_head(shape, skin_of(bone));
        const std::vector<Vec3> a = skin_face(h, {}, positions, 1), b = skin_face(h, {{shape_name, 1.f}}, positions, 1);
        std::vector<double> dz;
        for (size_t v = 0; v < a.size(); ++v) dz.push_back(b[v].z - a[v].z);
        return dz;
    };
    // SL's corners are swapped by name: mouthSmileLeft moves mFaceLipCornerRight, on the avatar's left.
    for (bool positions : {false, true}) {
        for (auto [shape_name, bone] : {std::pair{"mouthSmileLeft", "mFaceLipCornerRight"},
                                        std::pair{"mouthSmileRight", "mFaceLipCornerLeft"}})
            for (double dz : rise(bone, shape_name, positions)) CHECK(positions ? dz > 0.002 : dz > -1e-6);
        for (auto [shape_name, bone] : {std::pair{"mouthFrownLeft", "mFaceLipCornerRight"},
                                        std::pair{"mouthFrownRight", "mFaceLipCornerLeft"}})
            for (double dz : rise(bone, shape_name, positions)) CHECK(positions ? dz < -0.002 : dz < 1e-6);
    }
    // Brows turn (pitch) as well as move, so they rise and fall with Move face bones off too. Their skin is in front
    // of the joint on any head; the vertex at the joint itself only moves with positions.
    for (bool positions : {false, true}) {
        auto check = [&](const char* bone, const char* shape_name, double sign) {
            const std::vector<double> dz = rise(bone, shape_name, positions);
            CHECK(sign * dz[1] > 0.001);
            CHECK(sign * dz[0] > (positions ? 0.001 : -1e-6));
        };
        check("mFaceEyebrowInnerLeft", "browInnerUp", 1);
        check("mFaceEyebrowInnerRight", "browInnerUp", 1);
        check("mFaceEyebrowOuterLeft", "browOuterUpLeft", 1);
        check("mFaceEyebrowOuterRight", "browOuterUpRight", 1);
        check("mFaceEyebrowInnerLeft", "browDownLeft", -1);
        check("mFaceEyebrowInnerRight", "browDownRight", -1);
    }
    // A pucker pushes the lips forward rather than curling them.
    const SkinnedHead lips = skinned_head(shape, skin_of("mFaceLipUpperCenter"));
    const std::vector<Vec3> a = skin_face(lips, {}, true, 1), b = skin_face(lips, {{"mouthPucker", 1.f}}, true, 1);
    for (size_t v = 0; v < a.size(); ++v) CHECK(b[v].x - a[v].x > 0.002);
}
