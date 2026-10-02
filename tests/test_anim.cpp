#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/bvh.h"
#include "vats/edit.h"
#include "vats/pose_ops.h"
#include "vats/position_reset.h"
#include "vats/project.h"
#include "vats/rig.h"
#include "vats/shape.h"
#include "viewer_ref/viewer_ref.h"

using namespace vats;

namespace {

// A small clip: a waving left arm, a moving hip, a turning head and a keyed Chest attachment point.
Clip sample_clip(int fps = 30, int end = 60) {
    Clip c;
    c.fps = fps;
    c.end_frame = end;
    c.loop = true;
    c.loop_in = 10;
    c.loop_out = 50;
    auto key = [&](const char* track, const char* ch, double f, double v) { c.curves[track][ch].set_key(f, v); };
    for (int f = 0; f <= end; f += 15) {
        key("mShoulderLeft", "rot_x", f, (f % 30 == 0) ? 0 : 60);
        key("mShoulderLeft", "rot_z", f, f * 1.5);
        key("mElbowLeft", "rot_z", f, (f % 30 == 0) ? 10 : 80);
        key("mPelvis", "pos_z", f, (f % 30 == 0) ? 0 : -0.1);
        key("mPelvis", "pos_x", f, f * 0.002);
        key("Chest", "rot_y", f, (f % 30 == 0) ? 0 : 45);
        key("Chest", "pos_x", f, 0.02);
    }
    key("mHead", "rot_z", 0, -30);
    key("mHead", "rot_z", end, 30);
    return c;
}

double rot_error_deg(const Quat& a, const Quat& b) {
    return 2.0 * std::acos(std::min(1.0, std::fabs(a.normalized().dot(b.normalized())))) * kRadToDeg;
}

// Worst difference between two clips' FK poses over every integer frame.
void compare_clips(const Clip& a, const Clip& b, double rot_tol_deg, double pos_tol_m) {
    const Skeleton& s = skel();
    double worst_rot = 0, worst_pos = 0;
    for (int f = 0; f <= a.end_frame; ++f) {
        Pose pa = evaluate_curves(s, a, f), pb = evaluate_curves(s, b, f);
        for (int i = 0; i < s.size(); ++i) {
            worst_rot = std::max(worst_rot, rot_error_deg(pa.rot[i], pb.rot[i]));
            worst_pos = std::max(worst_pos, (pa.offset[i] - pb.offset[i]).length());
        }
    }
    CHECK(worst_rot <= rot_tol_deg);
    CHECK(worst_pos <= pos_tol_m);
    if (worst_rot > rot_tol_deg || worst_pos > pos_tol_m)
        std::fprintf(stderr, "    worst rotation %.5f deg, position %.6f m\n", worst_rot, worst_pos);
}

std::vector<std::uint8_t> export_bytes(const Clip& c, const AnimExportOptions& opt = {}) {
    auto r = export_anim(skel(), c, opt);
    for (auto& e : r.errors) std::fprintf(stderr, "    export error: %s\n", e.c_str());
    CHECK(r.errors.empty());
    return write_anim(r.file);
}

Clip import_bytes(const std::vector<std::uint8_t>& bytes) {
    AnimFile f;
    std::string err;
    CHECK(parse_anim(bytes, f, err));
    CHECK(validate_anim(f, skel(), true).empty());
    return import_anim(skel(), f).clip;
}

}  // namespace

// T2: the quantiser matches the viewer's code for every input we try.
TEST(anim_quantiser_matches_viewer) {
    using namespace viewer_ref;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> unit(-1.2f, 1.2f), metres(-6.f, 6.f);
    auto viewer = [](float v, float lo, float hi) {
        LLVector3 x(v, v, v);
        x.quantize16(lo, hi, lo, hi);
        return F32_to_U16(x.mV[0], lo, hi);
    };
    CHECK_EQ(anim_code(0.f, -1.f, 1.f), std::uint16_t(32767));
    CHECK_EQ(anim_code(0.f, -5.f, 5.f), std::uint16_t(32767));
    int mismatches = 0;
    for (int i = 0; i < 200000; ++i) {
        float r = unit(rng), m = metres(rng);
        mismatches += anim_code(r, -1.f, 1.f) != viewer(r, -1.f, 1.f);
        mismatches += anim_code(m, -5.f, 5.f) != viewer(m, -5.f, 5.f);
    }
    // Every code boundary and its float neighbours.
    for (int u = 0; u <= 65535; ++u) {
        float v = u16_to_f32(static_cast<std::uint16_t>(u), -1.f, 1.f);
        for (float w : {std::nextafter(v, -2.f), v, std::nextafter(v, 2.f)}) mismatches += anim_code(w, -1.f, 1.f) != viewer(w, -1.f, 1.f);
    }
    for (float d : {0.5f, 1.f / 3, 2.f, 59.99f, 60.f})
        for (int k = 0; k <= 1000; ++k) {
            float t = d * k / 1000;
            mismatches += f32_to_u16(t, 0.f, d) != F32_to_U16(t, 0.f, d);
        }
    CHECK_EQ(mismatches, 0);
}

// T3: rotation keys pack exactly like LLKeyframeMotion::serialize.
TEST(anim_rotation_pack_matches_viewer) {
    using namespace viewer_ref;
    std::mt19937 rng(11);
    std::normal_distribution<double> g;
    int mismatches = 0;
    for (int i = 0; i < 100000; ++i) {
        Quat q = Quat{g(rng), g(rng), g(rng), g(rng)}.normalized();
        if (i % 7 == 0) q.w = 1e-4 * g(rng);  // w near 0
        if (i % 2) q = -q;
        auto mine = encode_rotation(q);
        U16 x, y, z;
        serialize_rotation(LLQuaternion((F32)q.x, (F32)q.y, (F32)q.z, (F32)q.w), x, y, z);
        mismatches += mine[0] != x || mine[1] != y || mine[2] != z;
    }
    CHECK_EQ(mismatches, 0);
}

// IO-22: keys off whole frames are resampled by export, but an unedited raw import writes the original bytes.
TEST(anim_raw_import_reexport) {
    AnimFile f;
    f.duration = 1;
    f.loop_out = 1;
    AnimJoint j;
    j.name = "mHead";
    j.priority = 3;
    j.rot = {{0, 32767, 32767, 32767}, {20000, 40000, 32767, 32767}, {65535, 32767, 32767, 32767}};
    f.joints.push_back(j);
    auto bytes = write_anim(f);
    RawAnim raw{f, import_anim(skel(), f).clip};
    CHECK(export_anim(skel(), raw.clip).file.joints.size() == 1);
    const AnimFile* same = raw_reexport(raw, raw.clip);
    CHECK(same != nullptr);
    CHECK(write_anim(*same) == bytes);
    Clip with_prop = raw.clip;
    with_prop.props.push_back(Prop{});
    CHECK(raw_reexport(raw, with_prop) != nullptr);  // props are not in the file
    Clip edited = raw.clip;
    edited.priority = 4;
    CHECK(raw_reexport(raw, edited) == nullptr);
}

// IO-5 / D9: an over-long ease warns and names the values the viewer's BVH upload would use.
TEST(anim_ease_rescale_warning) {
    Clip c;
    c.end_frame = 30;  // 1 s at 30 fps
    c.curves["mHead"]["rot_x"].set_key(0, 0);
    c.ease_in = 1.5, c.ease_out = 0.5;
    auto r = export_anim(skel(), c);
    bool found = false;
    for (auto& w : r.warnings) found = found || w.find("0.75 s and 0.25 s") != std::string::npos;
    CHECK(found);
    CHECK_EQ(r.file.ease_in, 1.5f);  // written as given
}

// T1: any parsed file writes back byte for byte, including constraints and trailing bytes.
TEST(anim_raw_round_trip) {
    AnimExportOptions none{0, 0, 60};
    Clip c = sample_clip();
    AnimFile f = export_anim(skel(), c, none).file;
    f.joints[1].priority = -1;
    f.emote = "express_smile";
    AnimConstraint con{};
    con[0] = 1;  // chain length
    std::memcpy(con.data() + 2, "L_HAND", 6);
    std::memcpy(con.data() + 30, "GROUND", 6);
    f.constraints.push_back(con);
    f.num_constraints = 1;
    f.trailing = {1, 2, 3};
    auto bytes = write_anim(f);
    AnimFile g;
    std::string err;
    CHECK(parse_anim(bytes, g, err));
    CHECK(write_anim(g) == bytes);

    // Legacy v0.1 files round-trip too.
    AnimFile l;
    l.version = 0;
    l.sub_version = 1;
    l.duration = 1;
    AnimJoint j;
    j.name = "mHead";
    j.rot_legacy = {{0.f, 0.f, 0.f, 10.f}, {1.f, 0.f, 20.f, 0.f}};
    j.pos_legacy = {{0.5f, 0.1f, 0.2f, 0.3f}};
    l.joints.push_back(j);
    bytes = write_anim(l);
    CHECK(parse_anim(bytes, g, err));
    CHECK(g.legacy());
    CHECK(write_anim(g) == bytes);
}

TEST(anim_parse_rejects_truncation) {
    auto bytes = export_bytes(sample_clip());
    AnimFile f;
    std::string err;
    for (size_t n : {size_t(0), size_t(3), size_t(20), bytes.size() / 2, bytes.size() - 1}) {
        std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<long>(n));
        CHECK(!parse_anim(cut, f, err));
    }
}

TEST(anim_header_and_joints) {
    Clip c = sample_clip();
    AnimFile f = export_anim(skel(), c).file;
    CHECK_EQ(f.version, std::uint16_t(1));
    CHECK_NEAR(f.duration, 2.0, 1e-6);
    CHECK_NEAR(f.loop_in, 10.0 / 30, 1e-6);
    CHECK_NEAR(f.loop_out, 50.0 / 30, 1e-6);
    CHECK_EQ(f.loop, 1);
    // Skeleton order: bones, then attachment points.
    std::vector<std::string> names;
    for (auto& j : f.joints) names.push_back(j.name);
    std::vector<std::string> want = {"mPelvis", "mHead", "mShoulderLeft", "mElbowLeft", "Chest"};
    CHECK(names == want);
    CHECK(f.joints[0].pos.size() > 1);   // pelvis moves
    CHECK(f.joints[1].pos.empty());      // head only rotates
    // The pelvis position is written as an offset; Chest's as its absolute local position.
    Vec3 hip = decode_position(f.joints[0].pos.front());
    CHECK_NEAR(hip.length(), 0, 2e-4);
    Vec3 chest = decode_position(f.joints[4].pos.front());
    CHECK_NEAR(chest.x, 0.15 + 0.02, 2e-4);
    CHECK_NEAR(chest.z, -0.1, 2e-4);
    // Chest's rotation at rest includes its default orientation.
    Quat q0 = decode_rotation(f.joints[4].rot.front());
    CHECK(rot_error_deg(q0, skel()[skel().find("Chest")].rest) < 0.01);
}

// T4: clip -> .anim -> clip keeps the motion.
TEST(anim_semantic_round_trip) {
    Clip c = sample_clip();
    Clip exact = import_bytes(export_bytes(c, {0, 0, 60}));
    CHECK_EQ(exact.fps, 30);
    CHECK_EQ(exact.end_frame, 60);
    CHECK(exact.loop);
    CHECK_EQ(exact.loop_in, 10);
    CHECK_EQ(exact.loop_out, 50);
    // The viewer's double-pass floor quantiser errs by up to two codes (0.3 mm; rotations
    // lose more near 180 degrees, where w is rebuilt from a small value).
    compare_clips(c, exact, 0.03, 0.00031);
    Clip reduced = import_bytes(export_bytes(c));
    compare_clips(c, reduced, 0.08, 0.0008);
}

TEST(anim_infers_frame_rate) {
    Clip c = sample_clip(24, 48);
    Clip back = import_bytes(export_bytes(c));
    CHECK_EQ(back.fps, 24);
    CHECK_EQ(back.end_frame, 48);
    for (auto& [name, track] : back.curves)
        for (auto& [ch, curve] : track)
            for (auto& k : curve.keys) CHECK_NEAR(k.frame, std::round(k.frame), 0);
}

// T5: exporting what we imported reaches a fixed point.
TEST(anim_export_is_idempotent) {
    auto a = export_bytes(sample_clip());
    auto b = export_bytes(import_bytes(a));
    CHECK(a == b);  // imported keys are anchors, and imported values re-encode to the same codes
    CHECK(export_bytes(import_bytes(b)) == b);
    AnimExportOptions none{0, 0, 60};
    auto a0 = export_bytes(sample_clip(), none);
    CHECK(export_bytes(import_bytes(a0), none) == a0);
}

TEST(anim_key_reduction) {
    // A straight line needs only its ends; a gap limit adds keys in between.
    std::vector<Vec3> line;
    for (int i = 0; i <= 200; ++i) line.push_back({i * 0.01, 0, 0});
    auto k = reduce_position_keys(line, 0.0005, 60);
    CHECK_EQ(k.front(), 0);
    CHECK_EQ(k.back(), 200);
    for (size_t i = 1; i < k.size(); ++i) CHECK(k[i] - k[i - 1] <= 60);
    CHECK(k.size() <= 5);
    CHECK_EQ(reduce_position_keys(line, 0, 60).size(), line.size());
    // A corner is kept.
    std::vector<Vec3> corner = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {2, 2, 0}};
    auto kc = reduce_position_keys(corner, 0.001, 60);
    CHECK((kc == std::vector<int>{0, 2, 4}));
    // Anchors are never dropped.
    std::vector<char> anchors(line.size(), 0);
    anchors[77] = 1;
    auto ka = reduce_position_keys(line, 0.0005, 60, anchors);
    CHECK(std::find(ka.begin(), ka.end(), 77) != ka.end());
}

// T6 (part): the validator refuses what the viewer refuses.
TEST(anim_validator) {
    const Skeleton& s = skel();
    AnimFile good = export_anim(s, sample_clip()).file;
    CHECK(validate_anim(good, s, true).empty());
    auto refused = [&](auto change) {
        AnimFile f = good;
        change(f);
        return !validate_anim(f, s, true).empty();
    };
    CHECK(refused([](AnimFile& f) { f.duration = 60.5f; }));
    CHECK(refused([](AnimFile& f) { f.base_priority = -2; }));
    CHECK(refused([](AnimFile& f) { f.hand_pose = 15; }));
    CHECK(refused([](AnimFile& f) { f.joints.clear(); }));
    CHECK(refused([](AnimFile& f) { f.joints[0].name = "mRoot"; }));
    CHECK(refused([](AnimFile& f) { f.joints[0].name = "notABone"; }));
    CHECK(refused([](AnimFile& f) { f.joints[0].priority = -3; }));
    CHECK(refused([](AnimFile& f) { f.ease_in = std::nanf(""); }));
    CHECK(refused([](AnimFile& f) {
        AnimConstraint c{};
        std::memcpy(c.data() + 2, "NOPE", 4);
        f.constraints.push_back(c);
    }));
    CHECK(refused([](AnimFile& f) {
        AnimConstraint c{};
        c[0] = 2;
        std::memcpy(c.data() + 2, "L_HAND", 6);  // needs mWristLeft, mElbowLeft, mShoulderLeft records
        std::memcpy(c.data() + 30, "GROUND", 6);
        f.constraints.push_back(c);
    }));
    // Unknown joints are fine for playback, refused only at upload.
    AnimFile f = good;
    f.joints[0].name = "notABone";
    CHECK(validate_anim(f, s, false).empty());
}

TEST(anim_export_refusals) {
    Clip c = sample_clip();
    c.end_frame = 1801;  // 60.03 s at 30 fps
    CHECK(!export_anim(skel(), c).errors.empty());
    Clip empty;
    CHECK(!export_anim(skel(), empty).errors.empty());
}

// README decision 12: a keyed collision volume is written under its own name, after the attachment points,
// and reads back onto the same node.
TEST(anim_collision_volume_export) {
    Clip c = sample_clip();
    c.curves["BELLY"]["rot_y"].set_key(0, 0);
    c.curves["BELLY"]["rot_y"].set_key(60, 20);
    AnimFile f = export_anim(skel(), c).file;
    CHECK_EQ(f.joints.back().name, std::string("BELLY"));
    // Like an attachment point, the rotation replaces the volume's default one.
    Quat q0 = decode_rotation(f.joints.back().rot.front());
    CHECK(rot_error_deg(q0, skel()[skel().find("BELLY")].rest) < 0.01);
    Clip back = import_anim(skel(), f).clip;
    CHECK(back.curves.count("BELLY") == 1);
    CHECK(back.orphans.empty());
    compare_clips(c, back, 0.08, 0.0008);
}

TEST(anim_unknown_joint_survives) {
    AnimFile f = export_anim(skel(), sample_clip()).file;
    f.joints[1].name = "mMadeUpBone";
    Clip c = import_anim(skel(), f).clip;
    CHECK_EQ(c.orphans.size(), size_t(1));
    AnimFile g = export_anim(skel(), c).file;
    bool found = false;
    for (auto& j : g.joints) found = found || j.name == "mMadeUpBone";
    CHECK(found);
}

TEST(anim_import_survives_damaged_header) {
    AnimFile f = export_anim(skel(), sample_clip()).file;
    for (float bad : {std::nanf(""), 1e30f, -5.f}) {
        AnimFile g = f;
        g.duration = bad;
        g.loop_in = bad;
        g.ease_in = bad;
        Clip c = import_anim(skel(), g).clip;
        CHECK(c.end_frame >= 1 && c.end_frame <= 3600);
        CHECK(c.loop_in >= 0 && c.loop_in <= c.end_frame);
        CHECK(std::isfinite(c.ease_in));
        for (auto& [n, tr] : c.curves)
            for (auto& [ch, cv] : tr)
                for (auto& k : cv.keys) CHECK(std::isfinite(k.frame) && std::isfinite(k.value));
    }
}

TEST(anim_export_refuses_oversize) {
    // Every bone keyed on every frame of a 20 s clip, with no key reduction, is far over 250 KB.
    Clip c;
    c.end_frame = 600;
    for (int i = 0; i < skel().size(); ++i) {
        FCurve& f = c.curves[skel()[i].name]["rot_x"];
        for (int fr = 0; fr <= 600; fr += 2) f.set_key(fr, (fr / 2 % 2) * 20.0 + i);
    }
    auto r = export_anim(skel(), c, {0, 0, 60});
    CHECK(!r.errors.empty());
    CHECK(write_anim(r.file).size() >= kAnimMaxUploadBytes);
}

// Golden bytes: a clip with Bezier, linear and constant keys, a hip offset and a world pin (which makes export
// sample the full evaluation) must export to exactly these bytes. Performance work on evaluation and curves
// must not change what is written; update the hash only for an intended format or quantiser change.
TEST(anim_export_golden_bytes) {
    const Skeleton& s = skel();
    Clip c;
    c.fps = 30;
    c.end_frame = 90;
    c.priority = 4;
    const char* bones[] = {"mPelvis", "mTorso", "mChest", "mNeck", "mHead", "mShoulderLeft", "mElbowLeft",
                           "mWristLeft", "mHipRight", "mKneeRight", "mHandIndex1Left", "mTail1"};
    int n = 0;
    for (const char* b : bones) {
        for (int f = 0; f <= 90; f += 15)
            key_euler(c, b, f, {((f * 37 + n * 11) % 29 - 14) * 0.5, ((f * 13 + n * 7) % 23 - 11) * 0.75,
                                ((f * 5 + n * 19) % 31 - 15) * 0.25});  // exact values: no libm differences
        ++n;
    }
    for (auto& [ch, fc] : c.curves["mNeck"])
        for (auto& k : fc.keys) k.interp = Interp::Linear;
    for (auto& [ch, fc] : c.curves["mTail1"])
        for (auto& k : fc.keys) k.interp = Interp::Constant;
    for (int f = 0; f <= 90; f += 30) key_offset(c, "mPelvis", f, {0.001 * (f % 7), 0.0, 0.02 * f / 90.0});
    Rig rig(s);
    std::string why;
    CHECK(pin_here(c, rig, 30, s.find("Right Hand"), -1, nullptr, why));
    auto r = export_anim(s, c, {});
    CHECK(r.errors.empty());
    auto bytes = write_anim(r.file);
    std::uint64_t h = 1469598103934665603ull;  // FNV-1a 64
    for (auto b : bytes) h = (h ^ b) * 1099511628211ull;
    CHECK_EQ(bytes.size(), size_t(6678));
    CHECK_EQ(h, std::uint64_t(0x991f5bb17baceee9ull));
}

TEST(export_leaves_out_positions_that_move_nothing) {
    // IO-11a: a face take keyed offset 0 on every face bone; as absolute positions those keys pinned a mesh head's
    // face bones to the default head (reported 2026-09-27). Static channels are left out; real motion keeps its keys.
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    auto key = [&](const char* track, const char* ch, double f, double v) { c.curves[track][ch].set_key(f, v); };
    for (const char* ch : kPosChannels) {
        key("mFaceJaw", ch, 0, 0), key("mFaceJaw", ch, 30, 0);            // static, with rotations
        key("mFaceNoseBase", ch, 0, 0), key("mFaceNoseBase", ch, 30, 0);  // static, nothing else keyed
        key("mPelvis", ch, 0, 0), key("mPelvis", ch, 30, 0);              // the pelvis keeps its own
    }
    key("mFaceJaw", "rot_y", 0, 0), key("mFaceJaw", "rot_y", 30, 20);
    key("mFaceTongueBase", "pos_x", 0, 0), key("mFaceTongueBase", "pos_x", 15, 0.0002);  // under 0.5 mm
    key("mFaceLipCornerLeft", "pos_y", 0, 0), key("mFaceLipCornerLeft", "pos_y", 15, 0.004);  // moves
    auto r = export_anim(skel(), c);
    CHECK(r.errors.empty());
    CHECK(r.static_positions == 3);  // jaw, nose, tongue
    auto joint = [&](const char* name) -> const AnimJoint* {
        for (auto& j : r.file.joints)
            if (j.name == name) return &j;
        return nullptr;
    };
    CHECK(joint("mFaceJaw") && joint("mFaceJaw")->pos.empty() && !joint("mFaceJaw")->rot.empty());
    CHECK(!joint("mFaceNoseBase") && !joint("mFaceTongueBase"));  // keyed only by positions that move nothing
    CHECK(joint("mPelvis") && !joint("mPelvis")->pos.empty());
    CHECK(joint("mFaceLipCornerLeft") && !joint("mFaceLipCornerLeft")->pos.empty());
    // No reduction (0 / 0): only exactly-zero offsets count as static, so the tongue keeps its keys.
    CHECK(export_anim(skel(), c, {0, 0, 60}).static_positions == 2);
    // BVH with bone positions: static joints keep rotation-only channels; a joint that moves gets 6.
    BvhExportOptions bo;
    bo.joint_positions = true;
    auto b = export_bvh(skel(), c, bo);
    CHECK(b.static_positions == 3);
    auto channels = [&](const char* name) {
        const size_t at = b.text.find(std::string("JOINT ") + name + "\n");
        return at == std::string::npos ? -1 : std::atoi(b.text.c_str() + b.text.find("CHANNELS ", at) + 9);
    };
    CHECK(channels("mFaceJaw") == 3);
    CHECK(channels("mFaceLipCornerLeft") == 6);
    for (auto& l : b.lost) CHECK(l.find("mFaceJaw") == std::string::npos);
}

TEST(export_leaves_out_bones_that_do_not_move_when_asked) {
    // IO-11b: with "Leave out bones that don't move" a face take's untouched bones are left to other animations (an
    // AO's blinks); off (the default) every keyed joint keeps its rotations, as before.
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    auto key = [&](const char* track, const char* ch, double f, double v) { c.curves[track][ch].set_key(f, v); };
    key("mEyeLeft", "rot_x", 0, 0), key("mEyeLeft", "rot_x", 30, 0);            // keyed at rest: left out
    key("mFaceJaw", "rot_y", 0, 0), key("mFaceJaw", "rot_y", 30, 20);           // turns: kept
    key("mFaceLipLowerLeft", "rot_z", 0, 0), key("mFaceLipLowerLeft", "rot_z", 15, 0.02);  // under 0.05 degrees
    key("mFaceLipCornerLeft", "rot_z", 0, 0), key("mFaceLipCornerLeft", "rot_z", 30, 0);  // at rest, but moves:
    key("mFaceLipCornerLeft", "pos_y", 0, 0), key("mFaceLipCornerLeft", "pos_y", 15, 0.004);  // position only
    key("mPelvis", "rot_x", 0, 0), key("mPelvis", "rot_x", 30, 0);              // the pelvis keeps its own
    auto joint = [](const AnimExportResult& r, const char* name) -> const AnimJoint* {
        for (auto& j : r.file.joints)
            if (j.name == name) return &j;
        return nullptr;
    };
    auto off = export_anim(skel(), c);
    CHECK(off.static_rotations == 0);
    CHECK(joint(off, "mEyeLeft") && joint(off, "mFaceLipLowerLeft"));
    AnimExportOptions on;
    on.leave_out_static_rotations = true;
    auto r = export_anim(skel(), c, on);
    CHECK(r.errors.empty());
    CHECK(r.static_rotations == 3);  // eye, lower lip, lip corner
    CHECK(!joint(r, "mEyeLeft") && !joint(r, "mFaceLipLowerLeft"));  // nothing left: no record
    CHECK(joint(r, "mFaceLipCornerLeft") && joint(r, "mFaceLipCornerLeft")->rot.empty() &&
          !joint(r, "mFaceLipCornerLeft")->pos.empty());
    CHECK(joint(r, "mFaceJaw") && !joint(r, "mFaceJaw")->rot.empty());
    CHECK(joint(r, "mPelvis") && !joint(r, "mPelvis")->rot.empty());
    CHECK(r.file.joints.size() + 2 == off.file.joints.size());
}

TEST(export_positions_from_the_worn_avatar) {
    // "Your avatar" (the viewer): a mesh head's own face joint positions plus the offsets, not the default head's.
    const Skeleton& s = skel();
    Clip c;
    c.fps = 30;
    c.end_frame = 10;
    c.curves["mFaceLipCornerLeft"]["pos_y"].set_key(0, 0);
    c.curves["mFaceLipCornerLeft"]["pos_y"].set_key(10, 0.004);
    c.curves["mPelvis"]["pos_z"].set_key(0, 0.1);
    Shape worn;
    worn.scale.assign(s.size(), Vec3{1, 1, 1});
    worn.offset.assign(s.size(), Vec3{});
    const int corner = s.find("mFaceLipCornerLeft");
    for (int i = 0; i < s.size(); ++i) worn.offset[i] = {0.0123, -0.0234, 0.0345};  // every joint overridden
    worn.scale[s.find("mHead")] = {1.3, 1.3, 1.3};
    worn.offset[corner] = {0.01, -0.02, 0.005};  // the mesh head's joint position override
    for (const char* ch : kPosChannels) c.curves["mFaceJaw"][ch].set_key(0, 0);  // keyed, moves nothing
    c.curves["mFaceJaw"]["rot_y"].set_key(10, 15);
    AnimExportOptions opt{0, 0, 60};
    opt.positions = &worn;
    AnimFile f = export_anim(s, c, opt).file;
    // Least exposure: only the joint that moves carries a position, so no other worn value enters the file.
    for (const AnimJoint& j : f.joints)
        CHECK(j.pos.empty() || j.name == "mFaceLipCornerLeft" || j.name == "mPelvis");

    AnimImportResult back = import_anim(s, f);
    // Import subtracts the default position, so what is left is the worn joint position's difference plus the offset.
    CHECK((curve_offset(back.clip, "mFaceLipCornerLeft", 0) - worn.offset[corner]).length() < 2e-4);
    CHECK((curve_offset(back.clip, "mFaceLipCornerLeft", 10) - worn.offset[corner] - Vec3{0, 0.004, 0}).length() < 2e-4);
    CHECK_NEAR(curve_offset(back.clip, "mPelvis", 0).z, 0.1, 2e-4);  // an offset from rest, whatever the worn pelvis
    // BVH with bone positions writes the same absolute position.
    BvhExportOptions bo;
    bo.joint_positions = true;
    bo.positions = &worn;
    BvhImportResult bb = import_bvh(s, export_bvh(s, c, bo).text);
    CHECK(bb.ok);
    CHECK((curve_offset(bb.clip, "mFaceLipCornerLeft", 10) - worn.offset[corner] - Vec3{0, 0.004, 0}).length() < 1e-4);
    // The project keeps only the choice, never the worn positions (they are read live at each export).
    Project pr;
    Clip& saved = actor_clip(pr, 0);
    saved = c;
    saved.export_settings = Json::object();
    saved.export_settings.set("shape", std::string("avatar"));
    const std::string text = save_project(pr);
    CHECK(text.find("\"avatar\"") != std::string::npos);
    for (const char* worn_value : {"0.0123", "0.0234", "0.0345", "1.3"}) CHECK(text.find(worn_value) == std::string::npos);
}

TEST(pelvis_plays_at_standing_height_on_every_bake_shape) {
    // Reported 2026-09-27: an upload from the viewer floated the avatar. In SL mPelvis's position key is an offset
    // from standing (03 IO-11), so no bake shape may put a height into it: a clip without hip motion writes no pelvis
    // position, and hip motion comes out as exactly that offset.
    const Skeleton& s = skel();
    static const AvatarParams lad = [] {
        std::ifstream f(std::string(VATS_DATA_DIR) + "/avatar_lad.xml", std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        AvatarParams out;
        std::string err;
        CHECK(parse_avatar_params(text, out, err));
        return out;
    }();
    const BodyShape female = sl_default_shape(s, lad, false), male = sl_default_shape(s, lad, true);
    Shape raised = female.shape, lowered = female.shape;  // "Your avatar": a worn pelvis 20 cm up, 15 cm down
    raised.offset[0].z += 0.2;
    lowered.offset[0].z -= 0.15;
    raised.offset[s.find("mHipLeft")].z += 0.05;
    struct Case {
        const char* name;
        const Shape *shape, *positions;
    };
    const Case cases[] = {{"no shape", nullptr, nullptr},          {"SL Default", &female.shape, nullptr},
                          {"SL Default (Male)", &male.shape, nullptr}, {"Your avatar, raised", &female.shape, &raised},
                          {"Your avatar, lowered", &female.shape, &lowered}};
    Clip still;  // arms and a leg on IK (the baked path, which evaluates against the shape), no hip keys
    still.fps = 30;
    still.end_frame = 20;
    still.ease_in = still.ease_out = 0;
    still.curves["mShoulderLeft"]["rot_x"].set_key(0, 0);
    still.curves["mShoulderLeft"]["rot_x"].set_key(20, 50);
    still.curves["ik.LegLeft"]["blend"].set_key(0, 1);
    Clip hips = still;
    const double hip_keys[3][3] = {{0, 0, 0}, {10, -0.1, 0.03}, {20, 0.05, 0}};  // frame, z, x
    for (auto& k : hip_keys) hips.curves["mPelvis"]["pos_z"].set_key(k[0], k[1]), hips.curves["mPelvis"]["pos_x"].set_key(k[0], k[2]);
    const double tol = 10.0 / 65535 * 2 * std::sqrt(3.0) + AnimExportOptions{}.reduce_pos_m;
    for (const Case& k : cases) {
        AnimExportOptions opt;
        opt.shape = k.shape;
        opt.positions = k.positions;
        for (const Clip* c : {&still, &hips}) {
            AnimExportResult r = export_anim(s, *c, opt);
            CHECK(r.errors.empty());
            const AnimJoint* pelvis = nullptr;
            for (auto& j : r.file.joints)
                if (j.name == "mPelvis") pelvis = &j;
            if (c == &still) {
                CHECK(!pelvis || pelvis->pos.empty());  // plays at the avatar's own standing height
                continue;
            }
            CHECK(pelvis && !pelvis->pos.empty());
            if (!pelvis) continue;
            double worst = 0;
            for (int fr = 0; fr <= c->end_frame; ++fr) {  // the viewer's lerp between decoded keys
                const double t = double(fr) / c->fps;
                auto time = [&](size_t i) { return double(u16_to_f32(pelvis->pos[i][0], 0.f, r.file.duration)); };
                size_t i = 0;
                while (i + 1 < pelvis->pos.size() && time(i + 1) <= t) ++i;
                Vec3 p = decode_position(pelvis->pos[i]);
                if (i + 1 < pelvis->pos.size() && time(i) < t)
                    p = p + (decode_position(pelvis->pos[i + 1]) - p) * ((t - time(i)) / (time(i + 1) - time(i)));
                worst = std::max(worst, (p - curve_offset(*c, "mPelvis", fr)).length());
            }
            CHECK(worst <= tol);
            if (worst > tol) std::fprintf(stderr, "    %s: pelvis off by %.6f m\n", k.name, worst);
        }
    }
}

TEST(export_reset_joint_positions) {
    const Skeleton& s = skel();
    Clip stand;
    stand.fps = 30;
    stand.end_frame = 30;
    key_euler(stand, "mKneeLeft", 0, {20, 0, 0});
    key_euler(stand, "mKneeLeft", 30, {20, 0, 0});

    // 1. Off, the joints list is ignored; on with nothing to reset, nothing is added: both byte-identical.
    const std::vector<std::uint8_t> default_bytes = export_bytes(stand, {});
    AnimExportOptions opt_off;
    opt_off.reset_positions = false;
    opt_off.reset_position_joints = {"mKneeLeft", "mAnkleLeft"};
    CHECK(default_bytes == export_bytes(stand, opt_off));
    AnimExportOptions opt_empty;
    opt_empty.reset_positions = true;
    CHECK(default_bytes == export_bytes(stand, opt_empty));

    // 2. With option on: SL default bake shape
    AnimExportOptions opt_on;
    opt_on.reset_positions = true;
    opt_on.reset_position_joints = {"mKneeLeft"};
    AnimExportResult r_default = export_anim(s, stand, opt_on);
    CHECK(r_default.errors.empty());

    const AnimJoint* knee_def = nullptr;
    for (const auto& j : r_default.file.joints) {
        if (j.name == "mKneeLeft") knee_def = &j;
    }
    CHECK(knee_def != nullptr);
    if (knee_def) {
        CHECK(knee_def->pos.size() == 2);
        CHECK(knee_def->pos[0][0] == 0);
        CHECK(knee_def->pos[1][0] == 65535);
        const Vec3 p0 = decode_position(knee_def->pos[0]);
        const Vec3 p1 = decode_position(knee_def->pos[1]);
        CHECK((p0 - p1).length() < 1e-6);
        const int knee_node = s.find("mKneeLeft");
        CHECK((p0 - s[knee_node].pos).length() < 5e-4);
    }

    // 3. With option on: mesh body with joint offsets
    Shape mesh_body;
    mesh_body.offset.resize(s.size());
    const int knee_node = s.find("mKneeLeft");
    mesh_body.offset[knee_node] = {0.03, -0.02, 0.05};
    opt_on.positions = &mesh_body;
    AnimExportResult r_mesh = export_anim(s, stand, opt_on);
    CHECK(r_mesh.errors.empty());

    const AnimJoint* knee_mesh = nullptr;
    for (const auto& j : r_mesh.file.joints) {
        if (j.name == "mKneeLeft") knee_mesh = &j;
    }
    CHECK(knee_mesh != nullptr);
    if (knee_mesh) {
        CHECK(knee_mesh->pos.size() == 2);
        CHECK(knee_mesh->pos[0][0] == 0);
        CHECK(knee_mesh->pos[1][0] == 65535);
        const Vec3 p0 = decode_position(knee_mesh->pos[0]);
        const Vec3 expected = s[knee_node].pos + mesh_body.offset[knee_node];
        CHECK((p0 - expected).length() < 5e-4);
    }

    // 4. Reset position on joint not rotated by this clip (e.g. from other clips or picked)
    opt_on.positions = nullptr;
    opt_on.reset_position_joints = {"mAnkleLeft"};
    AnimExportResult r_unrotated = export_anim(s, stand, opt_on);
    CHECK(r_unrotated.errors.empty());
    const AnimJoint* ankle = nullptr;
    for (const auto& j : r_unrotated.file.joints) {
        if (j.name == "mAnkleLeft") ankle = &j;
    }
    CHECK(ankle != nullptr);
    if (ankle) {
        CHECK(ankle->rot.empty());  // unrotated joint gets NO rotation keys
        CHECK(ankle->pos.size() == 2);
        const int ankle_node = s.find("mAnkleLeft");
        CHECK((decode_position(ankle->pos[0]) - s[ankle_node].pos).length() < 5e-4);
    }

    // 5. resolve_reset_position_joints modes
    Clip walk;
    walk.curves["mWristRight"]["pos_x"].set_key(0, 0.1);
    walk.curves["mWristRight"]["pos_x"].set_key(30, 0.1);
    std::vector<const Clip*> other = {&walk};

    // rotated mode
    Json ex_rot = Json::object();
    ex_rot.set("reset_positions", true);
    ex_rot.set("reset_positions_mode", "rotated");
    auto rot_set = resolve_reset_position_joints(s, stand, other, ex_rot);
    CHECK((rot_set == std::vector<std::string>{"mKneeLeft"}));

    // other_clips mode
    Json ex_other = Json::object();
    ex_other.set("reset_positions", true);
    ex_other.set("reset_positions_mode", "other_clips");
    auto other_set = resolve_reset_position_joints(s, stand, other, ex_other);
    CHECK((other_set == std::vector<std::string>{"mWristRight"}));

    // pick mode
    Json ex_pick = Json::object();
    ex_pick.set("reset_positions", true);
    ex_pick.set("reset_positions_mode", "pick");
    Json picked = Json::array();
    picked.push("mElbowLeft");
    picked.push("mElbowRight");
    ex_pick.set("reset_positions_joints", picked);
    auto pick_set = resolve_reset_position_joints(s, stand, other, ex_pick);
    CHECK((pick_set == std::vector<std::string>{"mElbowLeft", "mElbowRight"}));
    // Attachment points and collision volumes are not joints an animation moves.
    picked.push("Chest");   // an attachment point
    picked.push("L_HAND");  // a collision volume
    ex_pick.set("reset_positions_joints", picked);
    CHECK((resolve_reset_position_joints(s, stand, other, ex_pick) == std::vector<std::string>{"mElbowLeft", "mElbowRight"}));
}

// Bake shape "Mesh body": the app bakes on the body (shape) and writes positions from SL's defaults (positions
// null, IO-11), but the reset keys must hold the body's own rest, or they pull its longer legs back to SL's.
TEST(export_reset_joint_positions_on_a_mesh_body) {
    const Skeleton& s = skel();
    Clip stand;
    stand.end_frame = 30;
    key_euler(stand, "mKneeLeft", 0, {20, 0, 0});
    Shape long_legs;  // vats_make_test_body --long-legs: knees 5 cm and ankles 10 cm lower
    long_legs.offset.resize(s.size());
    long_legs.offset[s.find("mKneeLeft")] = long_legs.offset[s.find("mAnkleLeft")] = {0, 0, -0.05};
    AnimExportOptions opt;  // as App::anim_export_options builds them for "mesh:"
    opt.shape = opt.reset_shape = &long_legs;
    opt.reset_positions = true;
    opt.reset_position_joints = {"mKneeLeft"};
    const AnimFile f = export_anim(s, stand, opt).file;
    const int knee = s.find("mKneeLeft");
    bool found = false;
    for (const AnimJoint& j : f.joints)
        if (j.name == "mKneeLeft" && j.pos.size() == 2) {
            found = true;
            CHECK((decode_position(j.pos[0]) - (s[knee].pos + long_legs.offset[knee])).length() < 5e-4);
        }
    CHECK(found);
}

// A joint whose position channels are keyed at rest writes no position (IO-11a), so it is reset like one with none.
TEST(reset_joint_positions_counts_positions_keyed_at_rest_as_none) {
    const Skeleton& s = skel();
    Clip stand;
    stand.end_frame = 30;
    key_euler(stand, "mKneeLeft", 0, {20, 0, 0});
    key_offset(stand, "mKneeLeft", 0, {});
    key_euler(stand, "mKneeRight", 0, {20, 0, 0});
    key_offset(stand, "mKneeRight", 15, {0.02, 0, 0});  // moves: its own keys are written
    Json ex = Json::object();
    ex.set("reset_positions", true);
    const auto set = resolve_reset_position_joints(s, stand, {}, ex);
    CHECK((set == std::vector<std::string>{"mKneeLeft"}));
    AnimExportOptions opt;
    opt.reset_positions = true;
    opt.reset_position_joints = set;
    bool reset = false;
    for (const AnimJoint& j : export_anim(s, stand, opt).file.joints)
        if (j.name == "mKneeLeft") reset = j.pos.size() == 2;
    CHECK(reset);
}

// Export mirrored: the joints come from the mirrored clip, and the picked ones swap sides with it.
TEST(reset_joint_positions_follow_export_mirrored) {
    const Skeleton& s = skel();
    Clip left;
    left.end_frame = 30;
    key_euler(left, "mKneeLeft", 0, {0, 30, 0});
    left.export_settings.set("reset_positions", true);
    const Clip m = mirrored_clip(s, left);
    CHECK((resolve_reset_position_joints(s, m, {}, m.export_settings) == std::vector<std::string>{"mKneeRight"}));
    Json picked = Json::array();
    picked.push("mElbowLeft");
    left.export_settings.set("reset_positions_mode", "pick");
    left.export_settings.set("reset_positions_joints", picked);
    const Clip mp = mirrored_clip(s, left);
    CHECK((resolve_reset_position_joints(s, mp, {}, mp.export_settings) == std::vector<std::string>{"mElbowRight"}));
}
