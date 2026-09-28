// Tween, pose blend and easing presets (08 TW-1..TW-3).
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/tween.h"

using namespace vats;

namespace {

double ch(const Clip& c, const char* track, const char* channel, double f) {
    return c.curves.at(track).at(channel).evaluate(f);
}

double angle_between(const Quat& a, const Quat& b) { return (a.conj() * b).angle(); }

}  // namespace

TEST(tween_breakdown_between_neighbour_keys) {
    Clip c;
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 20, {0, 0, 90});
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 20, {1, 0, 0});
    // The pose half-way between the keys, whatever the curve's easing says at frame 5.
    CHECK(tween(c, {"mShoulderLeft", "mPelvis"}, 5, 0.5, TweenMode::Breakdown) == 2);
    CHECK_NEAR(ch(c, "mShoulderLeft", "rot_z", 5), 45, 1e-6);
    CHECK_NEAR(ch(c, "mPelvis", "pos_x", 5), 0.5, 1e-9);
    // Keying at 5 makes it the previous key for frame 10; t runs past either key.
    tween(c, {"mShoulderLeft"}, 10, 1.2, TweenMode::Breakdown);
    CHECK_NEAR(ch(c, "mShoulderLeft", "rot_z", 10), 45 + 45 * 1.2, 1e-6);
    tween(c, {"mShoulderLeft"}, 10, -0.2, TweenMode::Breakdown);  // on a key: prev 5, next 20
    CHECK_NEAR(ch(c, "mShoulderLeft", "rot_z", 10), 45 - 45 * 0.2, 1e-6);
    tween(c, {"mShoulderLeft"}, 10, 5.0, TweenMode::Breakdown);  // clamped to 120%
    CHECK_NEAR(ch(c, "mShoulderLeft", "rot_z", 10), 45 + 45 * 1.2, 1e-6);
    // No key on one side: nothing to tween between.
    CHECK(tween(c, {"mShoulderLeft"}, 25, 0.5, TweenMode::Breakdown) == 0);
    CHECK(tween(c, {"mNeck"}, 5, 0.5, TweenMode::Breakdown) == 0);
}

TEST(tween_slerps_then_nearest_euler) {
    Clip c;
    const Vec3 e0{90, 0, 0}, e1{0, 90, 0};
    key_euler(c, "mElbowLeft", 0, e0);
    key_euler(c, "mElbowLeft", 10, e1);
    tween(c, {"mElbowLeft"}, 5, 0.5, TweenMode::Breakdown);
    // Half of the one rotation between the keys, not the Euler channels averaged.
    const Quat q0 = euler_to_quat(e0), q1 = euler_to_quat(e1), q = euler_to_quat(curve_euler(c, "mElbowLeft", 5));
    CHECK_NEAR(angle_between(q0, q), angle_between(q0, q1) / 2, 1e-6);
    CHECK_NEAR(angle_between(q, q1), angle_between(q0, q1) / 2, 1e-6);
    CHECK(angle_between(q, euler_to_quat({45, 45, 0})) > 0.05);

    // Multi-turn curves stay on their turn: 700 and 710 give 705, not -15.
    Clip s;
    key_euler(s, "mTorso", 0, {0, 0, 700});
    key_euler(s, "mTorso", 10, {0, 0, 710});
    tween(s, {"mTorso"}, 5, 0.5, TweenMode::Breakdown);
    CHECK_NEAR(ch(s, "mTorso", "rot_z", 5), 705, 1e-6);
}

TEST(tween_relax_toward_curve) {
    Clip c;
    key_euler(c, "mNeck", 0, {0, 0, 0});
    key_euler(c, "mNeck", 10, {0, 40, 0});  // a spike the neighbours do not follow
    key_euler(c, "mNeck", 20, {0, 0, 0});
    Clip half = c;
    CHECK(tween(half, {"mNeck"}, 10, 0.5, TweenMode::Relax) == 1);
    CHECK_NEAR(ch(half, "mNeck", "rot_y", 10), 20, 1e-6);
    CHECK(tween(c, {"mNeck"}, 10, 1.0, TweenMode::Relax) == 1);
    CHECK_NEAR(ch(c, "mNeck", "rot_y", 10), 0, 1e-6);
    // Relax acts on existing keys only.
    CHECK(tween(c, {"mNeck"}, 5, 1.0, TweenMode::Relax) == 0);
}

TEST(tween_ik_limbs_key_their_controller) {
    const Rig rig(skel());
    Clip c;
    const int arm = rig.find_limb("ArmLeft");
    CHECK(arm >= 0);
    // FK: bones are tweened as bones.
    auto fk = tween_tracks(rig, c, 5, {"mElbowLeft", "mShoulderLeft", "mNeck"});
    CHECK((fk == std::vector<std::string>{"mElbowLeft", "mShoulderLeft", "mNeck"}));
    key_blend(c, rig, 0, arm, 1);
    Track& ik = c.curves["ik.ArmLeft"];
    for (const char* ch : {"pos_x", "pos_y", "pos_z", "pole_x", "pole_y", "pole_z"}) ik[ch].set_key(0, 0);
    ik["pos_x"].set_key(10, 0.4);
    ik["pole_y"].set_key(10, -0.2);
    // IK-on: the arm's bones become its controller, once.
    auto on = tween_tracks(rig, c, 5, {"mElbowLeft", "mShoulderLeft", "mNeck"});
    CHECK((on == std::vector<std::string>{"ik.ArmLeft", "mNeck"}));
    const FCurve blend = ik["blend"];
    CHECK(tween(c, on, 5, 0.25, TweenMode::Breakdown) == 1);
    CHECK_NEAR(ch(c, "ik.ArmLeft", "pos_x", 5), 0.1, 1e-9);
    CHECK_NEAR(ch(c, "ik.ArmLeft", "pole_y", 5), -0.05, 1e-9);
    CHECK(c.curves["ik.ArmLeft"]["blend"] == blend);  // the IK switch is not a pose
    CHECK(!c.curves.count("mElbowLeft"));
}

TEST(pose_blend_scales_the_applied_pose) {
    Clip before;
    key_euler(before, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(before, "mShoulderLeft", 10, {0, 0, 0});
    key_euler(before, "mHead", 0, {0, 10, 0});
    Clip after = before;
    key_euler(after, "mShoulderLeft", 5, {0, 0, 60});
    key_euler(after, "mWristLeft", 5, {30, 0, 0});  // a track the pose adds: blended from rest
    Clip c = after;
    CHECK(blend_pose(c, before, after, 5, 0.5) == 2);
    CHECK_NEAR(ch(c, "mShoulderLeft", "rot_z", 5), 30, 1e-6);
    CHECK_NEAR(ch(c, "mWristLeft", "rot_x", 5), 15, 1e-6);
    CHECK(c.curves.at("mHead") == before.curves.at("mHead"));
    // Another value starts again from the applied pose; 150% pushes past it.
    blend_pose(c, before, after, 5, 1.5);
    CHECK_NEAR(ch(c, "mShoulderLeft", "rot_z", 5), 90, 1e-6);
    blend_pose(c, before, after, 5, 1.0);
    CHECK(c == after);
    blend_pose(c, before, after, 5, 0.0);
    CHECK_NEAR(ch(c, "mShoulderLeft", "rot_z", 5), 0, 1e-6);
}

TEST(ease_functions_start_and_end) {
    for (EaseShape s : {EaseShape::Quad, EaseShape::Cubic, EaseShape::Sine, EaseShape::Back, EaseShape::Elastic,
                        EaseShape::Bounce})
        for (EaseDir d : {EaseDir::In, EaseDir::Out, EaseDir::InOut}) {
            CHECK_NEAR(ease(s, d, 0), 0, 1e-12);
            CHECK_NEAR(ease(s, d, 1), 1, 1e-12);
        }
    CHECK_NEAR(ease(EaseShape::Quad, EaseDir::In, 0.5), 0.25, 1e-12);
    CHECK_NEAR(ease(EaseShape::Cubic, EaseDir::Out, 0.5), 0.875, 1e-12);
    CHECK_NEAR(ease(EaseShape::Sine, EaseDir::InOut, 0.5), 0.5, 1e-12);
    double lo = 1, hi = 0;
    for (int i = 0; i <= 1000; ++i) {
        lo = std::min(lo, ease(EaseShape::Back, EaseDir::In, i / 1000.0));
        hi = std::max(hi, ease(EaseShape::Bounce, EaseDir::Out, i / 1000.0));
    }
    CHECK_NEAR(lo, -0.1, 1e-3);  // Back dips 10%
    CHECK(hi <= 1 + 1e-12);      // a bounce never passes the floor
    CHECK_NEAR(ease(EaseShape::Bounce, EaseDir::Out, 1 / 2.75), 1, 1e-12);  // first landing
}

TEST(ease_handles_draw_the_shape) {
    auto run = [](EaseShape s, EaseDir d, double tol) {
        Clip c;
        FCurve& f = c.curves["mNeck"]["rot_x"];
        f.set_key(0, 0);
        f.set_key(30, 10);
        f.set_key(40, 0);
        std::vector<KeyRef> sel{{"mNeck", "rot_x", 0}, {"mNeck", "rot_x", 1}};
        CHECK(apply_ease(c, sel, s, d) == 1);  // 0..30 only: the last selected key ends the span
        CHECK(f.keys.size() == 3);
        double worst = 0;
        for (int i = 0; i <= 30; ++i) worst = std::max(worst, std::fabs(f.evaluate(i) - 10 * ease(s, d, i / 30.0)));
        if (worst > tol) std::fprintf(stderr, "  shape %d dir %d: off by %g\n", int(s), int(d), worst);
        CHECK(worst <= tol);
        f.recompute_handles();  // the handles are Free: a recompute keeps them
        double again = 0;
        for (int i = 0; i <= 30; ++i) again = std::max(again, std::fabs(f.evaluate(i) - 10 * ease(s, d, i / 30.0)));
        CHECK_NEAR(again, worst, 1e-12);
    };
    run(EaseShape::Quad, EaseDir::In, 1e-4);  // exact up to the evaluator's 1e-5 frame tolerance
    run(EaseShape::Cubic, EaseDir::Out, 1e-4);
    run(EaseShape::Sine, EaseDir::In, 0.12);  // a cubic fit: within about 1% of the span
    run(EaseShape::Quad, EaseDir::InOut, 0.2);
    run(EaseShape::Cubic, EaseDir::InOut, 0.3);
    run(EaseShape::Sine, EaseDir::InOut, 0.05);
}

TEST(ease_baked_keys_every_frame) {
    Clip c;
    FCurve& f = c.curves["mPelvis"]["pos_z"];
    f.set_key(0, 1);
    f.set_key(12, 0);
    f.set_key(20, 0.5);
    std::vector<KeyRef> sel{{"mPelvis", "pos_z", 0}};  // one key: the segment after it
    CHECK(apply_ease(c, sel, EaseShape::Bounce, EaseDir::Out) == 1);
    CHECK(f.keys.size() == 3 + 11);
    for (int i = 0; i <= 12; ++i) CHECK_NEAR(f.evaluate(i), 1 - ease(EaseShape::Bounce, EaseDir::Out, i / 12.0), 1e-9);
    CHECK_NEAR(f.evaluate(16), 0.25, 1e-9);  // the next segment is untouched (it was Bezier from 0 to 0.5)
    CHECK(sel[0].index == 0);
    std::vector<KeyRef> last{{"mPelvis", "pos_z", int(f.keys.size()) - 1}};
    CHECK(apply_ease(c, last, EaseShape::Elastic, EaseDir::In) == 0);  // nothing after the last key
}
