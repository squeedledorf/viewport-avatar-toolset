#include <algorithm>
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/pose_ops.h"
#include "vats/pose_tools.h"
#include "vats/footlock.h"
#include "vats/pose_presets.h"
#include "vats/suggest_limits.h"

using namespace vats;

namespace {

bool same_rot(const Quat& a, const Quat& b, double tol) { return std::fabs(std::fabs(a.dot(b)) - 1) < tol; }
Quat rot_at(const Clip& c, const std::string& track, double f) { return euler_to_quat(curve_euler(c, track, f)); }

}  // namespace

TEST(pose_tools_mirror_live_bone) {
    const Skeleton& s = skel();
    Clip clip;
    key_euler(clip, "mElbowLeft", 5, {10, -35, 20});
    Clip by_menu = clip;
    mirror_live(clip, s, 5, {"mElbowLeft"}, false);
    // The same rotation Mirror Bone gives from the evaluated pose (AM-113).
    mirror_bones(by_menu, s, 5, evaluate_curves(s, by_menu, 5), {s.find("mElbowLeft")});
    CHECK(same_rot(rot_at(clip, "mElbowRight", 5), rot_at(by_menu, "mElbowRight", 5), 1e-12));
    CHECK(same_rot(rot_at(clip, "mElbowRight", 5),
                   mirror_rotation(s, s.find("mElbowLeft"), s.find("mElbowRight"), euler_to_quat({10, -35, 20})), 1e-12));
    CHECK(!clip.has_channels("mElbowRight", kPosChannels));  // no position keys on either side: none added

    // Both partners edited (the drag keyed both): neither overwrites the other.
    Clip both;
    key_euler(both, "mElbowLeft", 0, {10, 0, 0});
    key_euler(both, "mElbowRight", 0, {0, 0, 40});
    Clip same = both;
    mirror_live(both, s, 0, {"mElbowLeft", "mElbowRight"}, false);
    CHECK(both == same);
}

TEST(pose_tools_mirror_live_position_and_centre) {
    const Skeleton& s = skel();
    Clip clip;
    key_offset(clip, "mCollarLeft", 0, {0.01, 0.02, 0.03});
    mirror_live(clip, s, 0, {"mCollarLeft"}, false);
    Vec3 o = curve_offset(clip, "mCollarRight", 0);
    CHECK_NEAR(o.x, 0.01, 1e-12);
    CHECK_NEAR(o.y, -0.02, 1e-12);
    CHECK_NEAR(o.z, 0.03, 1e-12);

    // Centre bones stay as edited unless the preference asks for them to mirror in place.
    Clip head;
    key_euler(head, "mHead", 0, {15, 20, 30});  // roll, nod, turn
    Clip kept = head;
    mirror_live(kept, s, 0, {"mHead"}, false);
    CHECK(kept == head);
    mirror_live(head, s, 0, {"mHead"}, true);
    const int h = s.find("mHead");
    Quat q = rot_at(head, "mHead", 0);
    CHECK(same_rot(q, mirror_rotation(s, h, h, q), 1e-9));  // symmetric now
    CHECK(!same_rot(q, euler_to_quat({15, 20, 30}), 1e-3));  // the roll and turn went
    Clip nod;
    key_euler(nod, "mHead", 0, {0, 20, 0});
    Clip nodded = nod;
    mirror_live(nod, s, 0, {"mHead"}, true);
    CHECK(same_rot(rot_at(nod, "mHead", 0), rot_at(nodded, "mHead", 0), 1e-12));  // a nod is already symmetric
}

TEST(pose_tools_mirror_live_ik) {
    const Skeleton& s = skel();
    Clip clip;
    for (int a = 0; a < 3; ++a) clip.curves["ik.ArmLeft"][kPosChannels[a]].set_key(3, 0.1 * (a + 1));
    clip.curves["ik.ArmLeft"]["pole_y"].set_key(3, 0.5);
    clip.curves["ik.ArmLeft"]["blend"].set_key(3, 1);
    key_euler(clip, "ik.ArmLeft", 3, {10, 20, 30});
    clip.curves["ik.ArmRight"]["blend"].set_key(3, 0);
    mirror_live(clip, s, 3, {"ik.ArmLeft"}, false);
    const Track& r = clip.curves.at("ik.ArmRight");
    CHECK_NEAR(r.at("pos_x").evaluate(3), 0.1, 1e-12);
    CHECK_NEAR(r.at("pos_y").evaluate(3), -0.2, 1e-12);
    CHECK_NEAR(r.at("pos_z").evaluate(3), 0.3, 1e-12);
    CHECK_NEAR(r.at("pole_y").evaluate(3), -0.5, 1e-12);
    CHECK_NEAR(r.at("blend").evaluate(3), 0, 1e-12);  // the partner's IK/FK state is its own
    CHECK(same_rot(rot_at(clip, "ik.ArmRight", 3), euler_to_quat({-10, 20, -30}), 1e-12));
}

TEST(pose_tools_scratch_commit) {
    Clip base;
    key_euler(base, "mHead", 0, {0, 0, 0});
    key_euler(base, "mHead", 10, {0, 0, 0});
    base.curves["mNeck"]["rot_z"].set_key(0, 5);  // only rot_z animated
    Clip working = base;
    key_euler(working, "mHead", 4, {0, 30, 0});
    key_euler(working, "mNeck", 4, {1, 2, 3});
    key_euler(working, "mChest", 4, {0, 0, 9});
    working.loop = true;  // a change that is not a curve comes along

    CHECK(scratch_tracks(base, base).empty());
    CHECK((scratch_tracks(base, working) == std::vector<std::string>{"mChest", "mHead", "mNeck"}));

    Clip all = scratch_commit(base, working, 4, false);
    CHECK(all.loop);
    CHECK_NEAR(curve_euler(all, "mHead", 4).y, 30, 1e-9);
    CHECK_NEAR(curve_euler(all, "mChest", 4).z, 9, 1e-9);
    CHECK(all.curves.at("mHead").at("rot_y").keys.size() == 3);

    Clip existing = scratch_commit(base, working, 4, true);
    CHECK(!existing.curves.count("mChest"));                   // never keyed before
    CHECK(!existing.curves.at("mNeck").count("rot_x"));         // only rot_z had keys
    CHECK_NEAR(existing.curves.at("mNeck").at("rot_z").evaluate(4), 3, 1e-9);
    CHECK_NEAR(curve_euler(existing, "mHead", 4).y, 30, 1e-9);  // head channels all had keys
}

TEST(pose_tools_propagate) {
    auto make = [] {
        Clip c;
        for (double f : {0.0, 10.0, 20.0, 30.0}) key_euler(c, "mHead", f, {0, 0, f});
        c.curves["mNeck"]["rot_x"].set_key(5, 1);
        c.curves["mNeck"]["rot_x"].set_key(15, 2);
        c.curves["mHead"]["rot_z"].keys[2].interp = Interp::Linear;
        return c;
    };
    Clip next = make();
    CHECK_EQ(propagate_pose(next, {"mHead", "mNeck"}, 10, PropagateTo::NextKey), 2);  // head z at 20, neck x at 15
    CHECK_NEAR(next.curves.at("mHead").at("rot_z").evaluate(20), 10, 1e-9);
    CHECK_NEAR(next.curves.at("mHead").at("rot_z").evaluate(30), 30, 1e-9);
    CHECK_NEAR(next.curves.at("mNeck").at("rot_x").evaluate(15), make().curves.at("mNeck").at("rot_x").evaluate(10), 1e-9);
    CHECK(next.curves.at("mHead").at("rot_z").keys[2].interp == Interp::Linear);

    Clip end = make();
    CHECK_EQ(propagate_pose(end, {"mHead"}, 5, PropagateTo::End), 3);  // z at 10, 20 and 30; x and y already 0
    const double at5 = make().curves.at("mHead").at("rot_z").evaluate(5);
    for (double f : {10.0, 20.0, 30.0}) CHECK_NEAR(end.curves.at("mHead").at("rot_z").evaluate(f), at5, 1e-9);
    CHECK_NEAR(end.curves.at("mHead").at("rot_z").evaluate(0), 0, 1e-9);

    Clip range = make();
    CHECK_EQ(propagate_pose(range, {"mHead"}, 20, PropagateTo::Range, 0, 30), 1);  // only later keys: 30
    CHECK_NEAR(range.curves.at("mHead").at("rot_z").evaluate(30), 20, 1e-9);
    CHECK_NEAR(range.curves.at("mHead").at("rot_z").evaluate(10), 10, 1e-9);

    Clip ik;
    ik.curves["ik.ArmLeft"]["blend"].set_key(0, 1);
    ik.curves["ik.ArmLeft"]["blend"].set_key(10, 0);
    ik.curves["ik.ArmLeft"]["pos_x"].set_key(0, 0.2);
    ik.curves["ik.ArmLeft"]["pos_x"].set_key(10, 0.4);
    CHECK_EQ(propagate_pose(ik, {"ik.ArmLeft"}, 0, PropagateTo::End), 1);  // the target moves, IK stays off at 10
    CHECK_NEAR(ik.curves.at("ik.ArmLeft").at("pos_x").evaluate(10), 0.2, 1e-12);
    CHECK_NEAR(ik.curves.at("ik.ArmLeft").at("blend").evaluate(10), 0, 1e-12);

    Clip last = make();
    CHECK_EQ(propagate_pose(last, {"mHead"}, 30, PropagateTo::NextKey), 0);
    CHECK(last == make());
}

TEST(pose_tools_curve_buffer) {
    Clip clip;
    key_euler(clip, "mHead", 0, {0, 0, 10});
    CurveBuffer buf;
    CHECK(!buf.swap(clip));
    CHECK(buf.curve("mHead", "rot_z") == nullptr);
    buf.snapshot(clip);
    const Clip before = clip;
    key_euler(clip, "mHead", 0, {0, 0, 50});
    CHECK_NEAR(buf.curve("mHead", "rot_z")->evaluate(0), 10, 1e-12);
    CHECK(buf.swap(clip));
    CHECK(clip == before);
    CHECK_NEAR(buf.curve("mHead", "rot_z")->evaluate(0), 50, 1e-12);
    buf.clear();
    CHECK(buf.curve("mHead", "rot_z") == nullptr);
}

// Sit on This: from the Sitting starter pose, floating as it starts, onto a chair's 45 cm seat as the sit tutorial
// does by hand (its hips end near -0.43 m): feet on the floor and held there, thighs on the seat.
TEST(sit_on_seat_puts_the_sitting_pose_on_the_seat_with_its_feet_held) {
    const Skeleton& s = skel();
    const Rig rig(s);
    Clip clip;
    const auto& poses = builtin_poses(s);
    const auto sit = std::find_if(poses.begin(), poses.end(), [](const LibraryItem& it) { return it.id == "builtin:body-sit"; });
    CHECK(sit != poses.end());
    apply_pose(clip, s, *sit, 0, false);
    const double floor = sole_floor(s, nullptr), seat = floor + 0.45;
    CHECK(sole_height(s, evaluate(rig, clip, 0, nullptr).globals) - floor > 0.3);  // floating, as the newcomer found it
    std::string report;
    CHECK(sit_on_seat(clip, rig, 0, seat, nullptr, report));
    CHECK(!report.empty());
    const Evaluation e = evaluate(rig, clip, 0, nullptr);
    CHECK_NEAR(sole_height(s, e.globals), floor, 0.02);  // the feet on the floor
    CHECK(pin_at(clip, rig, s.find("mAnkleLeft"), 0) >= 0 && pin_at(clip, rig, s.find("mAnkleRight"), 0) >= 0);
    CHECK_NEAR(curve_offset(clip, "mPelvis", 0).z, -0.43, 0.06);
    // The thighs rest on the seat: their line a thigh's half-thickness above it, hip to knee.
    const std::vector<Vec3> thigh = thigh_points(rig, clip, 0, nullptr);
    CHECK(thigh.size() == 8);
    for (const Vec3& p : thigh) CHECK(p.z > seat && p.z < seat + 0.15);
    // A lower seat (a stool at 38 cm): the hips go lower, the feet stay held.
    Clip low;
    apply_pose(low, s, *sit, 0, false);
    CHECK(sit_on_seat(low, rig, 0, floor + 0.38, nullptr, report));
    CHECK(curve_offset(low, "mPelvis", 0).z < curve_offset(clip, "mPelvis", 0).z - 0.05);
    CHECK_NEAR(sole_height(s, evaluate(rig, low, 0, nullptr).globals), floor, 0.03);
}

// The Hand Poser's curl (spec 06 4.6): what a drag keys reads back as the curl the readout shows, and with the finger
// limits a long drag stops where a finger stops instead of folding back through the hand.
TEST(hand_poser_curl_reads_back_and_keeps_to_the_finger_limits) {
    const Skeleton& s = skel();
    const RigConstraints limits = template_limits(s);
    const std::vector<std::string> index = {"mHandIndex1Left", "mHandIndex2Left", "mHandIndex3Left"};
    auto drag = [&](double curl, bool limited) {
        Clip clip;
        for (size_t n = 0; n < index.size(); ++n)
            key_rotation(clip, index[n], 0,
                         finger_segment_rotation(s, s.find(index[n]), Quat{}, curl, 0, n == 0,
                                                 limited ? limits.find(index[n]) : nullptr, nullptr));
        return finger_curl_degrees(s, clip, index, 0);
    };
    CHECK_NEAR(drag(0, false), 0, 1e-6);
    CHECK_NEAR(drag(30, false), 90, 0.5);  // 30 degrees on each of the three
    CHECK_NEAR(drag(30, true), 90, 0.5);   // well inside the limits: untouched
    CHECK_NEAR(drag(-20, false), -60, 0.5);
    // A long drag: 170 degrees a joint would fold the finger round through the back of the hand.
    const double folded = drag(170, true);
    CHECK(folded > 200 && folded <= 85 + 100 + 85 + 1);
    CHECK_NEAR(drag(245, true), folded, 1e-6);  // a 400-pixel drag: still curled, not wrapped round to bent back
    CHECK(drag(245, false) > 0);
    CHECK(drag(-90, true) > -86);  // bent back: the middle and end joints not at all, the knuckle within its cone
    // The thumb curls about its slanted axis and reads back the same.
    const std::vector<std::string> thumb = {"mHandThumb2Right"};
    Clip clip;
    key_rotation(clip, thumb[0], 0, finger_segment_rotation(s, s.find(thumb[0]), Quat{}, 40, 0, false, nullptr, nullptr));
    CHECK_NEAR(finger_curl_degrees(s, clip, thumb, 0), 40, 0.5);
}
