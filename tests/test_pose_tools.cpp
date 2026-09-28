#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/pose_ops.h"
#include "vats/pose_tools.h"

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
