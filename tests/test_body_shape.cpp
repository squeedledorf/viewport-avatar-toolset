// Spec 08 BD-3: a mesh body's bind pose sets the joint positions IK and pins preview against.
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/dae.h"
#include "vats/rig.h"

using namespace vats;

namespace {

// A rigged "devkit" whose binds are the SL rest, except the knees 5 cm and the ankles 10 cm lower: longer legs.
DaeModel long_legs(const Skeleton& s) {
    DaeModel m;
    m.rigged = true;
    m.binds = s.global_pose(Pose(s.size()));
    for (const char* side : {"Left", "Right"}) {
        m.binds[s.find(std::string("mKnee") + side)].pos.z -= 0.05;
        m.binds[s.find(std::string("mAnkle") + side)].pos.z -= 0.10;
    }
    return m;
}

}  // namespace

TEST(body_shape_from_binds_moves_overridden_joints) {
    const Skeleton& s = skel();
    DaeModel m = long_legs(s);
    Shape out;
    CHECK(shape_from_binds(s, {&m}, nullptr, out));
    auto rest = s.global_pose(Pose(s.size()));
    auto g = s.global_pose(Pose(s.size()), &out);
    int knee = s.find("mKneeLeft"), ankle = s.find("mAnkleLeft"), foot = s.find("mFootLeft"), hip = s.find("mHipLeft");
    // The whole body rises 10 cm so the longer legs still stand on the ground (SK-28).
    const Vec3 up{0, 0, 0.10};
    CHECK((g[knee].pos - (m.binds[knee].pos + up)).length() < 1e-9);
    CHECK((g[ankle].pos - (m.binds[ankle].pos + up)).length() < 1e-9);
    CHECK((g[hip].pos - (rest[hip].pos + up)).length() < 1e-9);  // not overridden: only lifted
    CHECK_NEAR(g[foot].pos.z, rest[foot].pos.z, 1e-9);            // unbound child follows its moved parent

    // A body bound exactly at the SL rest overrides nothing: the base shape stays.
    DaeModel plain;
    plain.rigged = true;
    plain.binds = rest;
    Shape keep;
    CHECK(!shape_from_binds(s, {&plain}, &s.male_shape(), keep));
    CHECK(keep.offset == s.male_shape().offset);
}

TEST(body_shape_changes_the_ik_result) {
    const Skeleton& s = skel();
    Rig rig(s);
    DaeModel m = long_legs(s);
    Shape body;
    CHECK(shape_from_binds(s, {&m}, nullptr, body));
    int limb = -1;
    for (int i = 0; i < int(rig.limbs().size()); ++i)
        if (rig.limbs()[i].name == "LegLeft") limb = i;
    CHECK(limb >= 0);
    // The same world target for the ankle, 45 cm up and forward: the longer, lifted leg reaches it with a
    // different knee bend, so the preview follows the body's proportions.
    Clip c;
    c.fps = 30, c.end_frame = 10;
    int ankle = s.find("mAnkleLeft"), knee = s.find("mKneeLeft");
    Xform target = s.global_pose(Pose(s.size()))[ankle];
    target.pos += Vec3{0.15, 0, 0.45};
    switch_to_ik(c, rig, 0, limb, nullptr);
    key_limb_target(c, rig, 0, limb, target, nullptr);
    Evaluation plain = evaluate(rig, c, 0, nullptr), longer = evaluate(rig, c, 0, &body);
    CHECK((longer.globals[ankle].pos - target.pos).length() < 1e-3);  // it still reaches the target
    double bend_plain = 2 * std::acos(std::min(1.0, std::fabs(plain.pose.rot[knee].w)));
    double bend_long = 2 * std::acos(std::min(1.0, std::fabs(longer.pose.rot[knee].w)));
    CHECK(std::fabs(bend_long - bend_plain) > 5 * kDegToRad);
}
