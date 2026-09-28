// Full-body reach (08 RC-1).
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/project.h"
#include "vats/reach.h"

using namespace vats;

namespace {

// The right arm in IK with its target 40 cm further out than the arm reaches, forward and down.
Clip out_of_reach(const Rig& rig, int arm, Vec3& target) {
    Clip c;
    switch_to_ik(c, rig, 0, arm, nullptr);
    const LimbInfo& l = rig.limbs()[arm];
    const std::vector<Xform> g = evaluate(rig, c, 0, nullptr).globals;
    const double len = (g[l.mid].pos - g[l.root].pos).length() + (g[l.end].pos - g[l.mid].pos).length();
    target = g[l.root].pos + Vec3{0.8, -0.2, -0.5}.normalized() * (len + 0.4);
    key_limb_target(c, rig, 0, arm, {g[l.end].rot, target}, nullptr);
    return c;
}

}  // namespace

TEST(reach_pull_one_reaches_the_target) {
    Rig rig(skel());
    const int arm = rig.find_limb("ArmRight");
    Vec3 target;
    Clip c = out_of_reach(rig, arm, target);
    const LimbInfo& l = rig.limbs()[arm];
    CHECK((evaluate(rig, c, 0, nullptr).globals[l.end].pos - target).length() > 0.3);
    const double moved = reach_with_body(c, rig, 0, arm, 1.0, nullptr);
    CHECK(moved > 0.01);
    const Evaluation e = evaluate(rig, c, 0, nullptr);
    CHECK((e.globals[l.end].pos - target).length() < 0.001);
    CHECK(curve_euler(c, "mTorso", 0).length() > 1);  // the spine leaned first, as FK keys
    CHECK(!e.limbs[rig.find_limb("Spine")].uses_ik);
    CHECK_NEAR(curve_offset(c, "mPelvis", 0).length(), moved, 1e-9);
}

TEST(reach_pull_zero_changes_nothing) {
    Rig rig(skel());
    const int arm = rig.find_limb("ArmRight");
    Vec3 target;
    Clip c = out_of_reach(rig, arm, target);
    const Clip before = c;
    CHECK_EQ(reach_with_body(c, rig, 0, arm, 0.0, nullptr), 0.0);
    CHECK(c == before);
}

TEST(reach_half_pull_moves_the_hips_half_as_far) {
    Rig rig(skel());
    const int arm = rig.find_limb("ArmRight");
    Vec3 target;
    Clip full = out_of_reach(rig, arm, target), half = full;
    const double a = reach_with_body(full, rig, 0, arm, 1.0, nullptr), b = reach_with_body(half, rig, 0, arm, 0.5, nullptr);
    CHECK_NEAR(b, a * 0.5, 1e-6);
}

TEST(reach_within_reach_does_nothing) {
    Rig rig(skel());
    const int arm = rig.find_limb("ArmRight");
    Clip c;
    switch_to_ik(c, rig, 0, arm, nullptr);
    const Clip before = c;
    CHECK_EQ(reach_with_body(c, rig, 0, arm, 1.0, nullptr), 0.0);
    CHECK(c == before);
}

TEST(reach_pull_saves_with_the_project) {
    Project p;
    p.clip.ik_pull["ArmRight"] = 0.75;
    Project back;
    std::string err;
    CHECK(load_project(save_project(p), back, err));
    CHECK_NEAR(back.clip.ik_pull["ArmRight"], 0.75, 1e-12);
    Rig rig(skel());
    CHECK_NEAR(ik_pull(back.clip, rig.limbs()[rig.find_limb("ArmRight")]), 0.75, 1e-12);
    CHECK_EQ(ik_pull(back.clip, rig.limbs()[rig.find_limb("ArmLeft")]), 0.0);
}
