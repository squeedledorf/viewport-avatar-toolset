// Retargeting tests, spec 07 RT-T1..RT-T4.
#include <cmath>
#include <fstream>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/bvh.h"
#include "vats/edit.h"
#include "vats/json.h"
#include "vats/retarget.h"

using namespace vats;

namespace {

const char* const kBody[] = {"mPelvis",       "mTorso",       "mChest",      "mNeck",        "mHead",
                             "mCollarLeft",   "mShoulderLeft", "mElbowLeft",  "mWristLeft",   "mCollarRight",
                             "mShoulderRight", "mElbowRight",  "mWristRight", "mHipLeft",     "mKneeLeft",
                             "mAnkleLeft",    "mFootLeft",    "mHipRight",   "mKneeRight",   "mAnkleRight",
                             "mFootRight"};

// A known SL pose per frame: key rotations as Euler degrees.
Vec3 known_euler(int joint, int frame) {
    return {12 * std::sin(0.3 * frame + joint), 18 * std::cos(0.2 * frame + 2 * joint), 9 * std::sin(0.1 * frame * joint)};
}

double angle_deg(const Quat& a, const Quat& b) { return (a.conj() * b).normalized().angle() * kRadToDeg; }

// RT-T1 source: the SL body joints (same names), but in an A-pose: both arm chains turned 45 degrees
// down about the shoulder, identity rest rotations like BVH. Frames are built so that the retarget
// must reproduce the known SL pose.
SourceAnim a_pose_source(int frames, BoneMap& map) {
    const Skeleton& s = skel();
    Pose zero(s.size());
    auto G = s.global_pose(zero);
    SourceAnim src;
    std::vector<int> node;
    for (const char* n : kBody) node.push_back(s.find(n));
    auto index_of = [&](int n) {
        for (size_t i = 0; i < node.size(); ++i)
            if (node[i] == n) return int(i);
        return -1;
    };
    // Rest world positions, arms turned down.
    std::vector<Vec3> rest(node.size());
    std::vector<Quat> arm_turn(node.size());
    for (size_t i = 0; i < node.size(); ++i) {
        rest[i] = G[node[i]].pos;
        const std::string& name = s[node[i]].name;
        bool left = name.find("Left") != std::string::npos;
        bool arm = name.find("Shoulder") != std::string::npos || name.find("Elbow") != std::string::npos ||
                   name.find("Wrist") != std::string::npos;
        if (!arm) continue;
        Vec3 pivot = G[s.find(left ? "mShoulderLeft" : "mShoulderRight")].pos;
        arm_turn[i] = Quat::axis_angle({1, 0, 0}, (left ? -45 : 45) * kDegToRad);
        rest[i] = pivot + arm_turn[i].rotate(rest[i] - pivot);
    }
    for (size_t i = 0; i < node.size(); ++i) {
        int p = s[node[i]].parent;
        while (p >= 0 && index_of(p) < 0) p = s[p].parent;
        int pi = p >= 0 ? index_of(p) : -1;
        src.joints.push_back({s[node[i]].name, pi, pi >= 0 ? rest[i] - rest[pi] : rest[i], {}, 1});
        map[s[node[i]].name] = int(i);
    }
    // The alignment the retarget will find: the arm turn for shoulders and elbows (they aim along
    // the turned chain), none for the wrists (no finger to aim at) and everything else.
    for (int f = 0; f < frames; ++f) {
        Pose pose(s.size());
        for (size_t i = 0; i < node.size(); ++i) pose.rot[node[i]] = euler_to_quat(known_euler(int(i), f));
        pose.offset[node[0]] = {0.01 * f, -0.02 * f, 0.005 * f};
        auto W = s.global_pose(pose);
        std::vector<Quat> world(node.size()), local(node.size());
        std::vector<Vec3> pos(node.size());
        for (size_t i = 0; i < node.size(); ++i) {
            const std::string& name = s[node[i]].name;
            bool aimed = name.find("Shoulder") != std::string::npos || name.find("Elbow") != std::string::npos;
            Quat A = aimed ? arm_turn[i] : Quat{};
            world[i] = W[node[i]].rot * G[node[i]].rot.conj() * A.conj();  // D, with identity source rest
            int pi = src.joints[i].parent;
            local[i] = pi >= 0 ? world[pi].conj() * world[i] : world[i];
            pos[i] = src.joints[i].offset;
        }
        pos[0] = rest[0] + pose.offset[node[0]];
        src.rot.push_back(local);
        src.pos.push_back(pos);
    }
    src.fps = 30;
    return src;
}

// The same source seen from a Y-up, +Z-forward tool (glTF / BVH axes).
SourceAnim to_y_up(SourceAnim s) {
    // SL (x forward, y left, z up) -> (x' = y, y' = z, z' = x): a proper rotation.
    Quat q = Quat{0.5, 0.5, 0.5, 0.5}.conj();
    for (auto& j : s.joints) j.offset = q.rotate(j.offset), j.rot = q * j.rot * q.conj();
    for (auto& f : s.rot)
        for (auto& r : f) r = q * r * q.conj();
    for (auto& f : s.pos)
        for (auto& p : f) p = q.rotate(p);
    return s;
}

}  // namespace

TEST(retarget_a_pose_source_rt_t1) {
    BoneMap map;
    SourceAnim src = a_pose_source(12, map);
    RetargetResult r = retarget(skel(), src, map);
    CHECK_EQ(r.clip.fps, 30);
    double worst = 0;
    for (int f = 0; f < 12; ++f)
        for (size_t i = 0; i < std::size(kBody); ++i) {
            Quat got = euler_to_quat(curve_euler(r.clip, kBody[i], f));
            worst = std::max(worst, angle_deg(got, euler_to_quat(known_euler(int(i), f))));
        }
    CHECK(worst < 0.5);
    Vec3 hip = curve_offset(r.clip, "mPelvis", 10);
    CHECK_NEAR(hip.x, 0.1, 1e-6);
    CHECK_NEAR(hip.y, -0.2, 1e-6);
}

TEST(retarget_y_up_matches_z_up_rt_t2) {
    BoneMap map;
    SourceAnim z = a_pose_source(6, map);
    RetargetResult a = retarget(skel(), z, map), b = retarget(skel(), to_y_up(z), map);
    double worst = 0;
    for (auto& [name, tr] : a.clip.curves)
        for (auto& [ch, c] : tr)
            for (size_t k = 0; k < c.keys.size(); ++k)
                worst = std::max(worst, std::fabs(c.keys[k].value - b.clip.curves[name][ch].keys[k].value));
    CHECK(worst < 1e-6);
}

TEST(retarget_fit_to_limits_rt_t3) {
    const Skeleton& s = skel();
    Clip clip;
    clip.fps = 30;
    clip.end_frame = clip.loop_out = 1800;  // 60 s
    // Every joint moves smoothly with a little capture jitter, keyed on every frame.
    unsigned seed = 7;
    auto jitter = [&] { seed = seed * 1103515245 + 12345; return ((seed >> 8) % 1000) / 1000.0 - 0.5; };
    for (int n = 0; n < s.joint_count(); ++n)
        for (int c = 0; c < 3; ++c) {
            FCurve& fc = clip.curves[s[n].name][kRotChannels[c]];
            for (int f = 0; f <= 1800; f += 1) {
                Key k;
                k.frame = f;
                k.value = 20 * std::sin(0.05 * f + n + c) + 0.6 * jitter();
                k.interp = Interp::Linear;
                fc.keys.push_back(k);
            }
        }
    const Clip full = clip;
    FitReport rep = fit_to_limits(s, clip);
    CHECK(rep.bytes_before >= kAnimMaxUploadBytes);
    CHECK(rep.bytes_after < kAnimMaxUploadBytes);
    CHECK(rep.fits && !rep.too_long);
    // No body joint is dropped before face and fingers go.
    bool face = false, fingers = false;
    for (auto& step : rep.steps) {
        if (step.find("face") != std::string::npos) face = true;
        if (step.find("finger") != std::string::npos) fingers = true;
        if (step.find("toes") != std::string::npos) CHECK(face && fingers);
    }
    const Json* reduce = clip.export_settings.find("reduce");
    CHECK(reduce && reduce->arr.size() == 2 && reduce->arr[0].num == rep.rot_tol_deg);

    // With tolerance and frame-rate trades forbidden (RT-11), joints go: face, fingers, then toes.
    Clip only_drops = full;
    FitOptions no_trades;
    no_trades.allow_tolerance = no_trades.allow_fps = false;
    FitReport d = fit_to_limits(s, only_drops, no_trades);
    CHECK(d.steps.size() >= 3);
    CHECK(d.steps[0].find("face") != std::string::npos);
    CHECK(d.steps[1].find("finger") != std::string::npos);
    CHECK(d.steps[2].find("toes") != std::string::npos);
    for (auto& name : d.dropped) {
        const Node& n = s[s.find(name)];
        CHECK(n.category == Category::Face || n.name.rfind("mHand", 0) == 0 || n.name.rfind("mToe", 0) == 0);
    }
}

TEST(retarget_bvh_round_trip_rt_t4) {
    const Skeleton& s = skel();
    Clip clip;
    clip.fps = 30;
    clip.end_frame = clip.loop_out = 10;
    for (size_t i = 0; i < std::size(kBody); ++i)
        for (int f = 0; f <= 10; ++f) key_euler(clip, kBody[i], f, known_euler(int(i), f));
    for (int f = 0; f <= 10; ++f) key_offset(clip, "mPelvis", f, {0.01 * f, 0.02 * f, -0.01 * f});
    std::string text = export_bvh(s, clip).text;
    SourceAnim src;
    std::string err;
    CHECK(read_bvh_source(text, src, err));
    BoneMap map;
    for (int j = 0; j < int(src.joints.size()); ++j) map[src.joints[j].name] = j;  // identity mapping
    RetargetResult r = retarget(s, src, map);
    // BVH frame 0 is the rest reference, so clip frame f is retargeted frame f + 1.
    double worst = 0;
    for (int f = 0; f <= 10; ++f)
        for (auto* name : kBody)
            worst = std::max(worst, angle_deg(euler_to_quat(curve_euler(r.clip, name, f + 1)),
                                              euler_to_quat(curve_euler(clip, name, f))));
    CHECK(worst < 0.01);
    Vec3 hip = curve_offset(r.clip, "mPelvis", 11);
    CHECK_NEAR(hip.x, 0.1, 1e-5);
    CHECK_NEAR(hip.z, -0.1, 1e-5);
}

TEST(retarget_tables_pick_the_rig) {
    std::vector<RigTable> tables;
    for (const char* f : {"cmu-rokoko", "unreal", "vrm-humanoid", "rigify", "mixamo"}) {  // CMU first: the hint must win the tie
        std::ifstream in(std::string(VATS_DATA_DIR) + "/../retarget/" + f + ".json");
        std::stringstream ss;
        ss << in.rdbuf();
        RigTable t;
        std::string err;
        CHECK(parse_rig_table(ss.str(), t, err));
        tables.push_back(t);
    }
    SourceAnim src;
    for (const char* n : {"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2", "mixamorig:Head",
                          "mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:RightArm", "mixamorig:RightForeArm",
                          "mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:RightUpLeg", "mixamorig:RightLeg"})
        src.joints.push_back({n, -1, {}, {}, 1});
    BoneMap map;
    CHECK_EQ(tables[best_rig_table(tables, src, map)].name, std::string("Mixamo"));
    CHECK_EQ(map["mChest"], 3);  // Spine2 before Spine1
    CHECK_EQ(map["mElbowRight"], 8);
    CHECK(map_is_usable(map));
    // Unreal's right side comes from the mirror pairs.
    CHECK_EQ(tables[1].bones["mKneeRight"][0], std::string("calf_r"));
    CHECK_EQ(tables[3].bones["mWristRight"][0], std::string("DEF-hand.R"));
    CHECK_EQ(tables[4].hint, std::string("mixamorig"));
}

TEST(retarget_gltf_reads_embedded_animation) {
    // Two joints, a rotation animation on the second, base64 buffer: times 0,1; quats id, 90 deg Z.
    float data[2 + 8] = {0, 1, 0, 0, 0, 1, 0, 0, 0.70710678f, 0.70710678f};
    std::string raw(reinterpret_cast<const char*>(data), sizeof data), b64;
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < raw.size(); i += 3) {
        unsigned v = (unsigned char)raw[i] << 16 | (i + 1 < raw.size() ? (unsigned char)raw[i + 1] << 8 : 0) |
                     (i + 2 < raw.size() ? (unsigned char)raw[i + 2] : 0);
        b64 += tbl[v >> 18 & 63];
        b64 += tbl[v >> 12 & 63];
        b64 += i + 1 < raw.size() ? tbl[v >> 6 & 63] : '=';
        b64 += i + 2 < raw.size() ? tbl[v & 63] : '=';
    }
    std::string gltf = R"({"asset":{"version":"2.0"},
      "nodes":[{"name":"Hips","children":[1],"translation":[0,1,0]},{"name":"Spine","translation":[0,0.2,0]}],
      "skins":[{"joints":[0,1]}],
      "buffers":[{"byteLength":40,"uri":"data:application/octet-stream;base64,)" + b64 + R"("}],
      "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":8},{"buffer":0,"byteOffset":8,"byteLength":32}],
      "accessors":[{"bufferView":0,"componentType":5126,"count":2,"type":"SCALAR"},
                   {"bufferView":1,"componentType":5126,"count":2,"type":"VEC4"}],
      "animations":[{"channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}],
                     "samplers":[{"input":0,"output":1,"interpolation":"LINEAR"}]}]})";
    SourceAnim src;
    std::string err;
    CHECK(read_gltf_source(std::vector<std::uint8_t>(gltf.begin(), gltf.end()), ".", src, err));
    CHECK_EQ(src.frames(), 31);
    CHECK_EQ(src.joints[1].parent, 0);
    CHECK_NEAR(src.joints[0].offset.y, 1.0, 1e-9);
    CHECK_NEAR(src.rot[30][1].angle() * kRadToDeg, 90.0, 1e-3);
    CHECK_NEAR(src.rot[15][1].angle() * kRadToDeg, 45.0, 1e-3);
}
