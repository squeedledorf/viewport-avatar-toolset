#include <algorithm>
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/pose_ops.h"

using namespace vats;

namespace {

bool same_rot(const Quat& a, const Quat& b, double tol) { return std::fabs(std::fabs(a.dot(b)) - 1) < tol; }

bool has(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

FCurve keys(std::initializer_list<std::pair<double, double>> ks) {
    FCurve c;
    for (auto& [f, v] : ks) c.set_key(f, v);
    return c;
}

}  // namespace

TEST(pose_ops_body_parts) {
    const Skeleton& s = skel();
    BodyPart arm = body_part_of(s, s.find("mElbowLeft"));
    CHECK(arm.kind == PartKind::Arm);
    CHECK_EQ(arm.side, std::string("Left"));
    CHECK_EQ(arm.label, std::string("Left Arm"));
    CHECK_EQ(arm.bones.size(), size_t(3));
    CHECK(!has(arm.bones, s.find("mWristLeft")));
    CHECK(std::find(arm.ik_tracks.begin(), arm.ik_tracks.end(), "ik.ArmLeft") != arm.ik_tracks.end());
    CHECK_EQ(arm.ik_tracks.size(), size_t(6));

    BodyPart hand = body_part_of(s, s.find("mHandIndex2Right"));
    CHECK(hand.kind == PartKind::Hand);
    CHECK_EQ(hand.bones.size(), size_t(16));  // wrist + 15 finger joints
    CHECK(has(hand.bones, s.find("mWristRight")));
    CHECK(body_part_of(s, s.find("mWristRight")).kind == PartKind::Hand);
    // Select/Copy arm adds the wrist and the hand (AM-90, AM-97); other parts are unchanged.
    BodyPart whole = with_hand(s, arm);
    CHECK_EQ(whole.bones.size(), size_t(3 + 16));
    CHECK(has(whole.bones, s.find("mWristLeft")) && has(whole.bones, s.find("mHandThumb3Left")));
    CHECK(!has(whole.bones, s.find("mWristRight")));
    CHECK_EQ(with_hand(s, hand).bones.size(), hand.bones.size());

    CHECK(body_part_of(s, s.find("mKneeLeft")).kind == PartKind::Leg);
    CHECK(body_part_of(s, s.find("mFaceJaw")).kind == PartKind::Head);
    CHECK(body_part_of(s, s.find("mSpine2")).kind == PartKind::Torso);
    CHECK_EQ(body_part_of(s, s.find("mSpine2")).bones.size(), size_t(7));
    BodyPart wing = body_part_of(s, s.find("mWing2Left"));
    CHECK(wing.kind == PartKind::Wing);
    CHECK_EQ(wing.bones.size(), size_t(5));
    CHECK_EQ(body_part_of(s, s.find("mTail3")).bones.size(), size_t(6));
    CHECK_EQ(body_part_of(s, s.find("mHindLimb2Right")).bones.size(), size_t(4));
    CHECK(body_part_of(s, s.find("mGroin")).kind == PartKind::Category);

    BodyPart point = body_part_of(s, s.find("L Upper Arm"));
    CHECK(point.kind == PartKind::Point);
    CHECK_EQ(point.side, std::string("Left"));
    CHECK_EQ(point.bones.size(), size_t(1));

    CHECK_EQ(pose_region(s, "arm", "Left").size(), size_t(4));
    CHECK_EQ(pose_region(s, "hand", "Left").size(), size_t(15));
    CHECK_EQ(side_of("R Forearm"), std::string("Right"));
    CHECK_EQ(side_of("mPelvis"), std::string(""));
}

TEST(pose_ops_mirror_rest_symmetry) {
    // E-5: mirror_rotation corrects for rest rotation, and for symmetric rests it is the plain quaternion mirror.
    const Skeleton& s = skel();
    Quat q = euler_to_quat({10, 20, 30});
    int l = s.find("mShoulderLeft"), r = s.find("mShoulderRight");
    CHECK(same_rot(mirror_rotation(s, l, r, q), Quat{q.w, -q.x, q.y, -q.z}, 1e-12));
    for (int i = 0; i < s.size(); ++i) {
        int m = s.mirror(i);
        Quat back = mirror_rotation(s, m, i, mirror_rotation(s, i, m, q));
        CHECK(same_rot(back, q, 1e-9));
    }
}

TEST(pose_ops_mirror_rest_is_about_z) {
    // Mirrored curves rely on this (E-5): rest(dst)^-1 * mirror(rest(src)) is a pure Z rotation for every node, so
    // it is a constant on rot_z. Chest, Spine and Skull are the ones where it is not the identity.
    const Skeleton& s = skel();
    int turned = 0;
    for (int i = 0; i < s.size(); ++i) {
        const Quat& r = s[i].rest;
        Quat c = s[s.mirror(i)].rest.conj() * Quat{r.w, -r.x, r.y, -r.z};
        CHECK_NEAR(c.x, 0, 1e-9);
        CHECK_NEAR(c.y, 0, 1e-9);
        turned += std::fabs(std::fabs(c.w) - 1) > 1e-9;
    }
    CHECK_EQ(turned, 3);
}

TEST(pose_ops_mirror_twice_returns_pose) {
    const Skeleton& s = skel();
    Clip clip;
    key_euler(clip, "mShoulderLeft", 0, {10, 25, -40});
    key_euler(clip, "mElbowRight", 0, {0, 0, 60});
    key_euler(clip, "mNeck", 0, {5, 10, 15});
    key_euler(clip, "L Upper Arm", 0, {20, 0, 0});
    key_offset(clip, "mPelvis", 0, {0.1, 0.2, 0.3});
    key_offset(clip, "mFaceJaw", 0, {0.01, 0.02, 0});
    clip.curves["ik.ArmLeft"]["pos_y"] = keys({{0, 0.4}});
    clip.curves["ik.ArmLeft"]["pole_y"] = keys({{0, 0.3}});
    const Pose p0 = evaluate_curves(s, clip, 0);

    mirror_pose(clip, s, 0, p0, MirrorMode::Flip);
    const Pose p1 = evaluate_curves(s, clip, 0);
    int sl = s.find("mShoulderLeft"), sr = s.find("mShoulderRight");
    CHECK(same_rot(p1.rot[sr], Quat{p0.rot[sl].w, -p0.rot[sl].x, p0.rot[sl].y, -p0.rot[sl].z}, 1e-9));
    CHECK_NEAR(p1.offset[0].y, -0.2, 1e-12);
    CHECK_NEAR(clip.curves["ik.ArmRight"]["pos_y"].evaluate(0), -0.4, 1e-12);
    CHECK_NEAR(clip.curves["ik.ArmRight"]["pole_y"].evaluate(0), -0.3, 1e-12);

    mirror_pose(clip, s, 0, p1, MirrorMode::Flip);
    const Pose p2 = evaluate_curves(s, clip, 0);
    for (int i = 0; i < s.size(); ++i) {
        CHECK(same_rot(p2.rot[i], p0.rot[i], 1e-9));
        CHECK_NEAR((p2.offset[i] - p0.offset[i]).length(), 0, 1e-12);
    }

    // Left to right keys the right side only, and reaches the "L "/"R " points (E-4).
    Clip lr;
    key_euler(lr, "mShoulderLeft", 0, {10, 25, -40});
    key_euler(lr, "mShoulderRight", 0, {1, 2, 3});
    key_euler(lr, "L Upper Arm", 0, {20, 0, 0});
    const Pose q0 = evaluate_curves(s, lr, 0);
    mirror_pose(lr, s, 0, q0, MirrorMode::LeftToRight);
    Vec3 e = curve_euler(lr, "mShoulderRight", 0);
    CHECK_NEAR(e.x, -10, 1e-9);
    CHECK_NEAR(e.y, 25, 1e-9);
    CHECK_NEAR(e.z, 40, 1e-9);
    CHECK(lr.curves.count("R Upper Arm"));
    CHECK_NEAR(curve_euler(lr, "mShoulderLeft", 0).x, 10, 0);

    Clip one;
    key_euler(one, "mElbowLeft", 0, {0, 0, 30});
    mirror_bones(one, s, 0, evaluate_curves(s, one, 0), {s.find("mElbowLeft")});
    CHECK_NEAR(curve_euler(one, "mElbowRight", 0).z, -30, 1e-9);
}

TEST(pose_ops_mirrored_clip) {
    const Skeleton& s = skel();
    Clip clip;
    key_euler(clip, "mShoulderLeft", 0, {10, 20, 30});
    key_euler(clip, "mShoulderRight", 0, {-10, 20, -30});
    key_euler(clip, "mShoulderLeft", 10, {15, 0, 5});
    key_euler(clip, "mShoulderRight", 10, {-15, 0, -5});
    key_euler(clip, "mPelvis", 0, {0, 5, 0});
    key_offset(clip, "mPelvis", 0, {0.1, 0, 0.2});
    key_offset(clip, "L Upper Arm", 0, {0, 0.1, 0});
    key_offset(clip, "R Upper Arm", 0, {0, -0.1, 0});
    key_euler(clip, "L Upper Arm", 0, {});
    key_euler(clip, "R Upper Arm", 0, {});
    CHECK(mirrored_clip(s, clip) == clip);

    clip.pins.push_back({"Left Hand", "Left Hand", "mChest", 0, -1, {0.1, 0.2, 0.3}, {0.9, 0.1, 0.2, 0.3}, -1, -1});
    clip.joint_priority["mShoulderLeft"] = 5;
    clip.curves["pin:Left Hand"]["pos_y"] = keys({{0, 0.5}});
    Clip m = mirrored_clip(s, clip);
    CHECK_EQ(m.pins[0].joint, std::string("Right Hand"));
    CHECK_EQ(m.pins[0].target, std::string("mChest"));
    CHECK_NEAR(m.pins[0].pos.y, -0.2, 0);
    CHECK_NEAR(m.pins[0].rot.x, -0.1, 0);
    CHECK_NEAR(m.pins[0].rot.y, 0.2, 0);
    CHECK_NEAR(m.pins[0].rot.z, -0.3, 0);
    CHECK_EQ(m.joint_priority["mShoulderRight"], 5);
    CHECK_NEAR(m.curves["pin:Right Hand"]["pos_y"].keys[0].value, -0.5, 0);
    CHECK(mirrored_clip(s, m) == clip);

    // E-5: the Chest point's rest rotation is not symmetric, so its keys are mirrored as whole rotations.
    Clip chest;
    key_euler(chest, "Chest", 0, {10, 20, 30});
    Clip mc = mirrored_clip(s, chest);
    int c = s.find("Chest");
    Quat want = mirror_rotation(s, c, c, euler_to_quat({10, 20, 30}));
    CHECK(same_rot(euler_to_quat(curve_euler(mc, "Chest", 0)), want, 1e-9));
    CHECK(same_rot(euler_to_quat(curve_euler(mirrored_clip(s, mc), "Chest", 0)), euler_to_quat({10, 20, 30}), 1e-9));

    // Between keys too: the curves keep their shape, only rot_z moves (no re-keying).
    key_euler(chest, "Chest", 12, {-40, 5, 70});
    key_euler(chest, "Spine", 3, {0, 30, 0});
    chest.curves["Spine"].erase("rot_z");
    mc = mirrored_clip(s, chest);
    CHECK_EQ(mc.curves["Chest"]["rot_x"].keys.size(), size_t(2));
    int sp = s.find("Spine");
    for (double t = 0; t <= 12; t += 0.5) {
        CHECK(same_rot(euler_to_quat(curve_euler(mc, "Chest", t)),
                       mirror_rotation(s, c, c, euler_to_quat(curve_euler(chest, "Chest", t))), 1e-9));
        CHECK(same_rot(euler_to_quat(curve_euler(mc, "Spine", t)),
                       mirror_rotation(s, sp, sp, euler_to_quat(curve_euler(chest, "Spine", t))), 1e-9));
    }
}

TEST(pose_ops_library_round_trip) {
    const Skeleton& s = skel();
    Clip clip;
    key_euler(clip, "mShoulderLeft", 0, {10, 20, 30});
    key_offset(clip, "mPelvis", 0, {0.1, 0.2, 0.3});
    Pose p = evaluate_curves(s, clip, 0);
    LibraryItem pose = make_pose(s, p, pose_region(s, "arm", "Left"), "arm", "Left", true);
    pose.name = "Wave";
    CHECK_EQ(pose.bones.size(), size_t(4));
    CHECK_NEAR(pose.bones["mShoulderLeft"].z, 30, 1e-9);
    CHECK(pose.hip.has_value());

    clip.curves["mShoulderLeft"]["rot_x"].keys[0].interp = Interp::Constant;
    key_euler(clip, "mShoulderLeft", 10, {0, 0, 0});
    LibraryItem c = make_clip(clip, {"mShoulderLeft"}, 0, 10, "arm", "Left");
    Library lib{{pose, c}};
    std::string text = save_library(lib);
    CHECK(text.find("\"vats-pose-library\"") != std::string::npos);

    Library back;
    std::string err;
    CHECK(load_library(text, back, err));
    CHECK_EQ(back.items.size(), size_t(2));
    CHECK_EQ(back.items[0].id, pose.id);
    CHECK_EQ(back.items[0].name, std::string("Wave"));
    CHECK(back.items[0].bones == pose.bones);
    CHECK(back.items[0].hip && back.items[0].hip->x == pose.hip->x);
    CHECK(back.items[1].clip);
    CHECK(back.items[1].curves == c.curves);
    CHECK_EQ(save_library(back), text);

    std::string sit = R"({"format": "vats-pose-library", "items": [{"id": "1700000000_42", "name": "Sit",
        "kind": "pose", "side": "", "bones": {"mHipLeft": [0, -90, 0]}, "hip": null}]})";
    CHECK(load_library(sit, back, err));
    CHECK(!back.items[0].hip);
    CHECK(!load_library(R"({"format": "vats-pose-library", "items": [{"bones": {"mHipLeft": [0]}}]})", back, err));
    CHECK(!load_library(R"({"format": "vats-project"})", back, err));
}

TEST(pose_ops_apply_pose_mirrored) {
    const Skeleton& s = skel();
    LibraryItem pose;
    pose.kind = "arm";
    pose.side = "Left";
    pose.bones["mShoulderLeft"] = {10, 20, 30};
    pose.bones["mNotABone"] = {1, 2, 3};
    pose.hip = Vec3{0.1, 0.2, 0.3};
    Clip clip;
    apply_pose(clip, s, pose, 5, true);
    Vec3 e = curve_euler(clip, "mShoulderRight", 5);
    CHECK_NEAR(e.x, -10, 1e-9);
    CHECK_NEAR(e.y, 20, 1e-9);
    CHECK_NEAR(e.z, -30, 1e-9);
    CHECK(!clip.curves.count("mShoulderLeft"));
    CHECK_NEAR(curve_offset(clip, "mPelvis", 5).y, -0.2, 0);
    LibraryItem chest;
    chest.bones["Chest"] = {10, 20, 30};
    apply_pose(clip, s, chest, 5, true);  // E-5: whole-rotation mirror for asymmetric rests
    int c = s.find("Chest");
    CHECK(same_rot(euler_to_quat(curve_euler(clip, "Chest", 5)), mirror_rotation(s, c, c, euler_to_quat({10, 20, 30})),
                   1e-9));
    apply_pose(clip, s, pose, 5, false);
    CHECK_NEAR(curve_euler(clip, "mShoulderLeft", 5).z, 30, 1e-9);
}

TEST(pose_ops_make_and_paste_clip) {
    const Skeleton& s = skel();
    Clip src;
    src.curves["mShoulderLeft"]["rot_x"] = keys({{0, 0}, {10, 30}, {20, -10}});
    src.curves["mShoulderLeft"]["rot_z"] = keys({{8, 5}});
    src.curves["ik.ArmLeft"]["pos_y"] = keys({{0, 1}, {20, 2}});
    src.curves["ik.ArmLeft"]["blend"] = keys({{0, 1}});
    src.curves["ik.IndexLeft"]["pos_x"] = keys({{0, 0.5}, {20, 0.7}});
    LibraryItem item = make_clip(src, {"mShoulderLeft", "ik.ArmLeft", "ik.IndexLeft"}, 5, 15, "arm", "Left");
    CHECK_NEAR(item.length, 10, 0);
    CHECK(item.relative == std::vector<std::string>{"ik.ArmLeft"});  // fingers are not relative
    CHECK(!item.curves["ik.ArmLeft"].count("blend"));
    CHECK_NEAR(item.curves["ik.ArmLeft"]["pos_y"].keys.front().value, 0, 1e-12);
    const FCurve& rx = item.curves["mShoulderLeft"]["rot_x"];
    CHECK_NEAR(rx.keys.front().frame, 0, 0);
    CHECK_NEAR(rx.keys.back().frame, 10, 0);
    for (auto& k : rx.keys) CHECK(k.left == Handle::Aligned && k.right == Handle::Aligned);
    const FCurve& rz = item.curves["mShoulderLeft"]["rot_z"];  // cut outside its keyed range: holds its value
    CHECK_EQ(rz.keys.size(), size_t(3));
    for (double t = 0; t <= 10; t += 0.5) CHECK_NEAR(rz.evaluate(t), 5, 1e-12);

    Clip dst;
    dst.curves["ik.ArmLeft"]["pos_y"] = keys({{0, 4}});
    std::vector<std::string> warnings;
    paste_clip(dst, s, item, 30, false, &warnings);
    const FCurve& p = dst.curves["mShoulderLeft"]["rot_x"];
    CHECK_NEAR(p.keys.front().frame, 29, 0);  // hold key before the paste
    CHECK_NEAR(p.keys.front().value, 0, 0);
    const FCurve& sx = src.curves["mShoulderLeft"]["rot_x"];
    for (double t = 0; t <= 10; t += 0.25) CHECK_NEAR(p.evaluate(30 + t), sx.evaluate(5 + t), 1e-4);
    const FCurve& ik = dst.curves["ik.ArmLeft"]["pos_y"];
    CHECK_NEAR(ik.evaluate(30), 4, 1e-12);  // relative: added to the destination's value at the paste frame
    const FCurve& sy = src.curves["ik.ArmLeft"]["pos_y"];
    CHECK_NEAR(ik.evaluate(40), 4 + sy.evaluate(15) - sy.evaluate(5), 1e-9);
    CHECK_NEAR(ik.keys.front().frame, 0, 0);  // an earlier key exists: no hold key
    CHECK_EQ(warnings.size(), size_t(1));    // ik.ArmLeft is FK at 30

    Clip mir;
    mir.curves["ik.ArmRight"]["blend"] = keys({{0, 1}});
    warnings.clear();
    paste_clip(mir, s, item, 0, true, &warnings);
    CHECK(!mir.curves.count("mShoulderLeft"));
    CHECK_NEAR(mir.curves["mShoulderRight"]["rot_x"].evaluate(5), -30, 1e-4);
    CHECK_NEAR(mir.curves["ik.ArmRight"]["pos_y"].evaluate(10), -(sy.evaluate(15) - sy.evaluate(5)), 1e-9);
    CHECK(warnings.empty());
    CHECK_EQ(mir.curves["mShoulderRight"]["rot_x"].keys.front().frame, 0.0);  // at frame 0: no hold key

    // E-5: a clip on Chest pasted mirrored gives, at every frame, what mirror_rotation gives; Skull too.
    Clip pt;
    key_euler(pt, "Chest", 0, {10, 20, 30});
    key_euler(pt, "Chest", 6, {-25, 40, -60});
    key_euler(pt, "Chest", 14, {5, -10, 170});
    key_euler(pt, "Skull", 4, {0, 0, 45});
    pt.curves["Skull"].erase("rot_y");
    LibraryItem ci = make_clip(pt, {"Chest", "Skull"}, 0, 14, "selection", "");
    Clip pd;
    paste_clip(pd, s, ci, 20, true, nullptr);
    for (const char* name : {"Chest", "Skull"}) {
        int n = s.find(name);
        for (double t = 0; t <= 14; t += 0.5)
            CHECK(same_rot(euler_to_quat(curve_euler(pd, name, 20 + t)),
                           mirror_rotation(s, n, n, euler_to_quat(curve_euler(pt, name, t))), 1e-9));
    }
    paste_clip(pd, s, ci, 20, false, nullptr);  // unmirrored is untouched
    for (double t = 0; t <= 14; t += 0.5)
        CHECK_NEAR((curve_euler(pd, "Chest", 20 + t) - curve_euler(pt, "Chest", t)).length(), 0, 1e-9);
}

TEST(pose_ops_pose_and_key_clipboards) {
    Clip clip;
    key_euler(clip, "mShoulderLeft", 0, {10, 20, 30});
    key_offset(clip, "mPelvis", 0, {0.1, 0.2, 0.3});
    clip.curves["ik.ArmLeft"]["pos_x"] = keys({{0, 0.5}});
    PoseClipboard cb = copy_pose(clip, 0, {});
    CHECK_EQ(cb.entries.size(), size_t(3));

    Clip dst;
    paste_pose(dst, cb, 10, {});
    CHECK_NEAR(curve_euler(dst, "mShoulderLeft", 10).z, 30, 0);
    CHECK_NEAR(curve_offset(dst, "mPelvis", 10).y, 0.2, 0);
    CHECK_NEAR(dst.curves["ik.ArmLeft"]["pos_x"].evaluate(10), 0.5, 0);

    PoseClipboard one = copy_pose(clip, 0, {"mShoulderLeft"});
    paste_pose(dst, one, 20, {"mElbowLeft", "mNeck", "ik.LegLeft"});
    CHECK_NEAR(curve_euler(dst, "mNeck", 20).y, 20, 0);
    CHECK(!dst.curves.count("ik.LegLeft"));

    paste_pose_part(dst, one, 30, {"mShoulderRight"});  // copy left, paste on right: mirrors
    Vec3 e = curve_euler(dst, "mShoulderRight", 30);
    CHECK_NEAR(e.x, -10, 0);
    CHECK_NEAR(e.z, -30, 0);

    KeyClipboard kc = copy_keys(clip, {{"mShoulderLeft", "rot_z", 0}, {"mPelvis", "pos_y", 0}});
    CHECK_EQ(kc.keys.size(), size_t(2));
    std::vector<KeyRef> sel = paste_keys(dst, kc, 50);
    CHECK_EQ(sel.size(), size_t(2));
    CHECK_NEAR(dst.curves["mShoulderLeft"]["rot_z"].keys[sel[0].index].frame, 50, 0);
    CHECK_NEAR(dst.curves["mPelvis"]["pos_y"].evaluate(50), 0.2, 0);
}
