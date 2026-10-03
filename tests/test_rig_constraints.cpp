#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "check.h"
#include "fixtures.h"
#include "../tools/mech_rig.h"
#include "vats/anim_convert.h"
#include "vats/json.h"
#include "vats/pose_ops.h"
#include "vats/anim_file.h"
#include "vats/dae.h"
#include "vats/edit.h"
#include "vats/project.h"
#include "vats/rig.h"
#include "vats/rig_constraints.h"
#include "vats/skeleton.h"
#include "vats/suggest_limits.h"

using namespace vats;

namespace {

int node(const char* name) {
    return skel().find(name);
}

double angle_between(const Quat& a, const Quat& b) {
    return (a.conj() * b).angle();
}

bool load_mech(DaeModel& m) {
    DaeReport r;
    std::string err;
    return load_dae(mech::dae(skel(), mech::build(skel()), 0, false), "", skel(), m, r, err);
}

Shape mech_shape(const DaeModel& m) {
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);
    return s;
}

}  // namespace

TEST(old_project_loads_unlimited) {
    const char* json = R"({
        "format": "vats-project",
        "version": 1,
        "fps": 30
    })";
    Project p;
    std::string err;
    CHECK(load_project(json, p, err));
    CHECK(p.joint_limits.empty());
    CHECK(p.body_constraints("") == nullptr);
    CHECK(p.body_constraints("mesh_body_1") == nullptr);
}

TEST(joint_limits_round_trip) {
    Project p;
    RigConstraints& rc = p.get_or_create_constraints("");

    JointLimit knee;
    knee.kind = JointLimitKind::Hinge;
    knee.min_angle = -0.1;
    knee.max_angle = 2.2;
    knee.axis = Vec3{0, 1, 0};
    rc.get_or_create("mKneeLeft") = knee;

    JointLimit shoulder;
    shoulder.kind = JointLimitKind::Cone;
    shoulder.bone_axis = Vec3{0, 0, 1};
    shoulder.cone_angle = 1.2;
    shoulder.twist_min = -0.5;
    shoulder.twist_max = 0.7;
    rc.get_or_create("mShoulderLeft") = shoulder;

    std::string saved = save_project(p);
    CHECK(saved.find("\"joint_limits\"") != std::string::npos);
    CHECK(saved.find("\"mKneeLeft\"") != std::string::npos);
    CHECK(saved.find("\"hinge\"") != std::string::npos);
    CHECK(saved.find("\"mShoulderLeft\"") != std::string::npos);
    CHECK(saved.find("\"cone\"") != std::string::npos);

    Project loaded;
    std::string err;
    CHECK(load_project(saved, loaded, err));
    const RigConstraints* lrc = loaded.body_constraints("");
    CHECK(lrc != nullptr);
    const JointLimit* lknee = lrc->find("mKneeLeft");
    CHECK(lknee != nullptr);
    CHECK_EQ(lknee->kind, JointLimitKind::Hinge);
    CHECK(std::fabs(lknee->min_angle - (-0.1)) < 1e-4);
    CHECK(std::fabs(lknee->max_angle - 2.2) < 1e-4);
    CHECK((lknee->axis - Vec3{0, 1, 0}).length() < 1e-4);

    const JointLimit* lshoulder = lrc->find("mShoulderLeft");
    CHECK(lshoulder != nullptr);
    CHECK_EQ(lshoulder->kind, JointLimitKind::Cone);
    CHECK(std::fabs(lshoulder->cone_angle - 1.2) < 1e-4);
    CHECK(std::fabs(lshoulder->twist_min - (-0.5)) < 1e-4);
    CHECK(std::fabs(lshoulder->twist_max - 0.7) < 1e-4);

    // Empty limits omit the joint_limits key
    p.joint_limits.clear();
    std::string empty_saved = save_project(p);
    CHECK(empty_saved.find("\"joint_limits\"") == std::string::npos);
}

TEST(anim_export_identical_with_and_without_limits) {
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    c.curves["mPelvis"]["pos_z"].set_key(0, 0.0);
    c.curves["mPelvis"]["pos_z"].set_key(30, -0.1);
    c.curves["mKneeLeft"]["rot_y"].set_key(0, 0.2);
    c.curves["mKneeLeft"]["rot_y"].set_key(30, 0.8);
    c.curves["mShoulderLeft"]["rot_x"].set_key(0, 0.5);
    c.curves["mShoulderLeft"]["rot_x"].set_key(30, 1.2);

    // Export without limits
    const AnimExportResult r1 = export_anim(skel(), c);
    const std::vector<uint8_t> bytes1 = write_anim(r1.file);

    // Project with limits does not alter the clip or the raw export
    Project p;
    p.clip = c;
    RigConstraints& rc = p.get_or_create_constraints("");
    JointLimit knee;
    knee.kind = JointLimitKind::Hinge;
    knee.min_angle = 0.0;
    knee.max_angle = 1.0;
    knee.axis = Vec3{0, 1, 0};
    rc.get_or_create("mKneeLeft") = knee;

    const AnimExportResult r2 = export_anim(skel(), p.clip);
    const std::vector<uint8_t> bytes2 = write_anim(r2.file);

    CHECK_EQ(bytes1, bytes2);
}

TEST(clamp_joint_rotation_respects_limits) {
    RigConstraints rc;
    JointLimit hinge;
    hinge.kind = JointLimitKind::Hinge;
    hinge.axis = Vec3{0, 1, 0};
    hinge.min_angle = 0.0;
    hinge.max_angle = 1.57;  // ~90 deg
    int knee_node = node("mKneeLeft");

    // Rotation inside limit (45 deg = ~0.785 rad about Y)
    Quat q_in = Quat::axis_angle({0, 1, 0}, 0.785);
    CHECK(is_rotation_within_limits(hinge, q_in, nullptr, knee_node));
    Quat q_clamped = clamp_joint_rotation(hinge, q_in, nullptr, knee_node);
    CHECK(angle_between(q_in, q_clamped) < 1e-4);

    // Rotation outside limit (forward hyperextension: -30 deg = -0.52 rad about Y)
    Quat q_out = Quat::axis_angle({0, 1, 0}, -0.52);
    CHECK(!is_rotation_within_limits(hinge, q_out, nullptr, knee_node));
    Quat q_fixed = clamp_joint_rotation(hinge, q_out, nullptr, knee_node);
    CHECK(is_rotation_within_limits(hinge, q_fixed, nullptr, knee_node));
    // Should be clamped to 0.0 rad (identity)
    CHECK(angle_between(q_fixed, Quat{}) < 1e-3);

    // Rotation past max limit (120 deg = ~2.09 rad about Y)
    Quat q_past = Quat::axis_angle({0, 1, 0}, 2.09);
    CHECK(!is_rotation_within_limits(hinge, q_past, nullptr, knee_node));
    Quat q_past_fixed = clamp_joint_rotation(hinge, q_past, nullptr, knee_node);
    CHECK(is_rotation_within_limits(hinge, q_past_fixed, nullptr, knee_node));
    CHECK(angle_between(q_past_fixed, Quat::axis_angle({0, 1, 0}, 1.57)) < 1e-3);

    // Cone + twist
    JointLimit cone;
    cone.kind = JointLimitKind::Cone;
    cone.bone_axis = Vec3{0, 0, 1};
    cone.cone_angle = 0.5;
    cone.twist_min = -0.2;
    cone.twist_max = 0.2;
    int shoulder_node = node("mShoulderLeft");

    Quat q_cone_ok = Quat::axis_angle({1, 0, 0}, 0.3);
    CHECK(is_rotation_within_limits(cone, q_cone_ok, nullptr, shoulder_node));

    Quat q_cone_wide = Quat::axis_angle({1, 0, 0}, 1.2);
    CHECK(!is_rotation_within_limits(cone, q_cone_wide, nullptr, shoulder_node));
    Quat q_cone_fixed = clamp_joint_rotation(cone, q_cone_wide, nullptr, shoulder_node);
    CHECK(is_rotation_within_limits(cone, q_cone_fixed, nullptr, shoulder_node));
    CHECK(angle_between(q_cone_fixed, Quat::axis_angle({1, 0, 0}, 0.5)) < 1e-3);
}

TEST(auto_ik_respects_knee_hinge_limit) {
    const Rig rig(skel());
    Clip c;
    const int ankle = node("mAnkleLeft");
    const int knee = node("mKneeLeft");
    const Evaluation start = evaluate(rig, c, 0, nullptr);
    const AutoIkChain ch = auto_ik_chain(rig, c, 0, ankle);

    // Knee limit: can only bend backwards up to 0.4 rad (~23 deg)
    RigConstraints rc;
    JointLimit limit;
    limit.kind = JointLimitKind::Hinge;
    limit.axis = Vec3{0, 1, 0};
    limit.min_angle = 0.0;
    limit.max_angle = 0.4;
    rc.get_or_create("mKneeLeft") = limit;

    // Target that bends the knee significantly: pull ankle up and back
    const Vec3 bend_target = start.globals[ankle].pos + Vec3{-0.2, 0, 0.25};

    Clip c_unconstrained;
    key_auto_ik(c_unconstrained, rig, 0, ch, start, bend_target, nullptr, nullptr, nullptr);
    const Evaluation ev_unconstrained = evaluate(rig, c_unconstrained, 0, nullptr);
    const Quat rot_unconstrained = ev_unconstrained.pose.rot[knee];
    // In unconstrained solve, knee bends beyond 0.4 rad:
    CHECK(!is_rotation_within_limits(limit, rot_unconstrained, nullptr, knee));

    // Now solve with constraints:
    Clip c_constrained;
    key_auto_ik(c_constrained, rig, 0, ch, start, bend_target, nullptr, nullptr, &rc);
    const Evaluation ev_constrained = evaluate(rig, c_constrained, 0, nullptr, &rc);
    const Quat rot_constrained = ev_constrained.pose.rot[knee];
    CHECK(is_rotation_within_limits(limit, rot_constrained, nullptr, knee));

    // 2) Pull ankle slightly to a reachable target within the 0.4 rad range:
    const Vec3 gentle_target = start.globals[ankle].pos + Vec3{-0.05, 0, 0.02};
    Clip c_reach;
    key_auto_ik(c_reach, rig, 0, ch, start, gentle_target, nullptr, nullptr, &rc);
    const Evaluation ev_reach = evaluate(rig, c_reach, 0, nullptr, &rc);
    CHECK(is_rotation_within_limits(limit, ev_reach.pose.rot[knee], nullptr, knee));
    // Reaches the target accurately
    CHECK((ev_reach.globals[ankle].pos - gentle_target).length() < 0.02);
}

TEST(two_bone_ik_respects_joint_limits) {
    Rig rig(skel());
    int limb = rig.find_limb("LegLeft");
    CHECK(limb >= 0);
    const LimbInfo& l = rig.limbs()[limb];

    RigConstraints rc;
    JointLimit limit;
    limit.kind = JointLimitKind::Hinge;
    limit.axis = Vec3{0, 1, 0};
    limit.min_angle = 0.2;   // min bend 0.2 rad
    limit.max_angle = 1.0;   // max bend 1.0 rad (~57 deg)
    rc.get_or_create("mKneeLeft") = limit;

    Clip c;
    switch_to_ik(c, rig, 0, limb, nullptr);
    Evaluation start = evaluate(rig, c, 0, nullptr);

    // Target pulled high/close, which would normally bend knee to > 90 deg (> 1.57 rad)
    Xform target = start.globals[l.end];
    target.pos.z += 0.35;
    key_limb_target(c, rig, 0, limb, target, nullptr);

    Evaluation ev = evaluate(rig, c, 0, nullptr, &rc);
    Quat knee_rot = ev.pose.rot[l.mid];
    CHECK(is_rotation_within_limits(limit, knee_rot, nullptr, l.mid));

    // Verify angle doesn't exceed 1.0 rad
    const Vec3 local_axis = limit.axis.normalized();
    double angle = 2.0 * std::atan2(Vec3{knee_rot.x, knee_rot.y, knee_rot.z}.dot(local_axis), knee_rot.w);
    CHECK(angle <= 1.0 + 1e-3);
}

TEST(typed_values_warn_without_clamping) {
    JointLimit limit;
    limit.kind = JointLimitKind::Hinge;
    limit.axis = Vec3{0, 1, 0};
    limit.min_angle = 0.0;
    limit.max_angle = 1.57;
    RigConstraints rc;
    rc.limits["mKneeLeft"] = limit;

    // A knee typed past its limit (Properties keys exactly what was typed).
    Clip c;
    key_rotation(c, "mKneeLeft", 0, Quat::axis_angle({0, 1, 0}, -0.5));
    const Rig rig(skel());
    const Evaluation e = evaluate(rig, c, 0, nullptr, &rc);
    const Quat shown = e.pose.rot[node("mKneeLeft")];
    // Posing with the limits on still shows it as typed (limits steer the posing tools, never keys), and Properties
    // flags it as outside.
    CHECK(angle_between(shown, Quat::axis_angle({0, 1, 0}, -0.5)) < 1e-4);
    CHECK(!is_rotation_within_limits(*rc.find("mKneeLeft"), shown, nullptr, node("mKneeLeft")));
    CHECK(is_rotation_within_limits(*rc.find("mKneeLeft"), Quat::axis_angle({0, 1, 0}, 0.5), nullptr, node("mKneeLeft")));
}

TEST(suggest_limits_sl_avatar_knees_and_elbows) {
    const std::vector<SuggestedLimitRow> rows = suggest_joint_limits(skel(), nullptr);
    CHECK(!rows.empty());

    // Lowest confidence rows come first
    for (size_t i = 1; i < rows.size(); ++i) {
        CHECK(rows[i - 1].limit.confidence <= rows[i].limit.confidence + 1e-4);
    }

    auto find_row = [&](const std::string& j) -> const JointLimit* {
        for (const auto& r : rows)
            if (r.joint == j) return &r.limit;
        return nullptr;
    };

    // Knees: one-way hinges bending backward (positive rotation about {0, 1, 0})
    const JointLimit* kl = find_row("mKneeLeft");
    const JointLimit* kr = find_row("mKneeRight");
    CHECK(kl && kl->kind == JointLimitKind::Hinge);
    CHECK(kr && kr->kind == JointLimitKind::Hinge);
    CHECK_NEAR((kl->axis - Vec3{0, 1, 0}).length(), 0.0, 1e-4);
    CHECK_NEAR((kr->axis - Vec3{0, 1, 0}).length(), 0.0, 1e-4);
    CHECK_NEAR(kl->min_angle, 0.0, 1e-4);
    CHECK(kl->max_angle > 2.0);
    CHECK_NEAR(kr->min_angle, 0.0, 1e-4);
    CHECK(kr->max_angle > 2.0);

    // Elbows: one-way hinges bending forward
    const JointLimit* el = find_row("mElbowLeft");
    const JointLimit* er = find_row("mElbowRight");
    CHECK(el && el->kind == JointLimitKind::Hinge);
    CHECK(er && er->kind == JointLimitKind::Hinge);
    CHECK_NEAR((el->axis - Vec3{0, 0, -1}).length(), 0.0, 1e-4);
    CHECK_NEAR((er->axis - Vec3{0, 0, 1}).length(), 0.0, 1e-4);
    CHECK_NEAR(el->min_angle, 0.0, 1e-4);
    CHECK(el->max_angle > 2.0);
    CHECK_NEAR(er->min_angle, 0.0, 1e-4);
    CHECK(er->max_angle > 2.0);

    // Face bones stay free
    for (const auto& r : rows) {
        CHECK(r.joint.rfind("mFace", 0) != 0);
    }
}

TEST(suggest_limits_mech_hind_legs_are_bent_plane_hinges) {
    DaeModel m;
    DaeReport rep;
    std::string err;
    CHECK(load_dae(mech::dae(skel(), mech::build(skel(), true)), "", skel(), m, rep, err));
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);

    const std::vector<SuggestedLimitRow> rows = suggest_joint_limits(skel(), &s, {&m});
    auto find_row = [&](const std::string& j) -> const JointLimit* {
        for (const auto& r : rows)
            if (r.joint == j) return &r.limit;
        return nullptr;
    };

    // Hind leg knees and hocks come out as hinges in their bent plane
    for (const char* j : {"mHindLimb2Left", "mHindLimb2Right", "mHindLimb3Left", "mHindLimb3Right"}) {
        const JointLimit* lim = find_row(j);
        CHECK(lim);
        if (!lim) continue;
        CHECK(lim->kind == JointLimitKind::Hinge);
        CHECK(lim->source == JointLimitSource::BindPose);
        // Authored rig axes have X along the hinge
        CHECK(std::fabs(lim->axis.x) > 0.95);
        // Can fold further (max > 0) and straighten (min < 0)
        CHECK(lim->min_angle < -0.1);
        CHECK(lim->max_angle > 0.5);
    }
}

namespace {

RigConstraints suggested(const Shape* shape, const std::vector<const DaeModel*>& models = {}) {
    RigConstraints rc;
    for (const SuggestedLimitRow& r : suggest_joint_limits(skel(), shape, models)) rc.limits[r.joint] = r.limit;
    return rc;
}

bool load_mech_with_hind_legs(DaeModel& m) {
    DaeReport r;
    std::string err;
    return load_dae(mech::dae(skel(), mech::build(skel(), true)), "", skel(), m, r, err);
}

}  // namespace

TEST(suggest_limits_hip_cone_keeps_a_stop_per_direction) {
    // SL: the thighs nearly touch side by side at rest. One cone at the tightest direction's stop made the hips 11
    // degrees every way.
    const RigConstraints sl = suggested(nullptr);
    const JointLimit* hip = sl.find("mHipLeft");
    CHECK(hip && hip->kind == JointLimitKind::Cone);
    if (hip) {
        CHECK(cone_stop(*hip, {1, 0, 0}) > 75 * kDegToRad);                        // forward
        CHECK(cone_stop(*hip, Vec3{1, -1, 0}.normalized()) > 75 * kDegToRad);      // forward and in
        for (int k = 0; k < 16; ++k) CHECK(cone_stop(*hip, cone_stop_dir(*hip, k, 16)) > 45 * kDegToRad);
    }

    // The mech: its torso stops the thigh swinging forward and in, while out to the side stays free. How wide the
    // torso is comes from its mesh (bone radii from the skin weights): without the mesh there is no stop.
    DaeModel m;
    CHECK(load_mech(m));
    const Shape s = mech_shape(m);
    const int n = node("mHipLeft");
    const Vec3 fwd_in = to_joint_frame(&s, n, Vec3{1, -1, 0}.normalized()), out = to_joint_frame(&s, n, Vec3{0, 1, 0});
    const RigConstraints with_mesh = suggested(&s, {&m}), bare = suggested(&s);
    const JointLimit* mh = with_mesh.find("mHipLeft");
    const JointLimit* bh = bare.find("mHipLeft");
    CHECK(mh && bh);
    if (mh && bh) {
        CHECK(mh->source == JointLimitSource::Collision);
        CHECK(cone_stop(*mh, fwd_in) < 50 * kDegToRad);
        CHECK(cone_stop(*mh, out) > 75 * kDegToRad);
        CHECK(cone_stop(*bh, fwd_in) > 75 * kDegToRad);
    }
}

TEST(suggest_limits_are_sane_on_sl_and_the_mech) {
    DaeModel m;
    CHECK(load_mech_with_hind_legs(m));
    const Shape s = mech_shape(m);
    for (const Shape* sh : {static_cast<const Shape*>(nullptr), &s}) {
        const RigConstraints rc = sh ? suggested(sh, {&m}) : suggested(nullptr);
        for (const auto& [name, lim] : rc.limits) {
            const int n = skel().find(name);
            // Rest always fits, and no range is upside down (a creature's: mSpine4 [-178, -28], mSpine2 -5 > -29).
            CHECK(is_rotation_within_limits(lim, Quat{}, sh, n, 1e-6));
            CHECK(lim.min_angle <= lim.max_angle && lim.twist_min <= lim.twist_max);
            for (double a : lim.cone_stops) CHECK(a >= 0 && a <= lim.cone_angle + 1e-9);
            // A ball joint rigged at an angle is no hinge: the bind pose rule once made hinges of the foot, the tail
            // root, the wing roots, the hind legs' root and the fingers' first joints.
            if (lim.source == JointLimitSource::BindPose)
                for (const char* p : {"mFoot", "mTail", "mWing1", "mWingsRoot", "mHindLimbsRoot", "mSpine"})
                    CHECK(name.rfind(p, 0) != 0);
            if (name.rfind("mHand", 0) == 0 && name.find('1') != std::string::npos)
                CHECK(lim.source != JointLimitSource::BindPose);
            // Left and right are mirror images: a clamp on one side mirrors the other's.
            if (name.ends_with("Left")) {
                const int r = skel().mirror(n);
                const JointLimit* right = rc.find(skel()[r].name);
                CHECK(right);
                if (!right) continue;
                for (int k = 0; k < 40; ++k) {
                    const Quat q = Quat::axis_angle(Vec3{std::sin(k * 1.3), std::cos(k * 0.7), std::sin(k * 2.1)}.normalized(),
                                                    0.05 * k);
                    const Quat a = mirror_rotation(skel(), n, r, clamp_joint_rotation(lim, q, sh, n));
                    const Quat b = clamp_joint_rotation(*right, mirror_rotation(skel(), n, r, q), sh, r);
                    CHECK(angle_between(a, b) < 0.2 * kDegToRad);
                }
            }
        }
    }
}

TEST(joint_limit_cone_stops_clamp_save_and_mirror) {
    JointLimit cone;
    cone.kind = JointLimitKind::Cone;
    cone.bone_axis = {0, 0, -1};
    cone.cone_angle = 80 * kDegToRad;
    cone.cone_ref = {1, 0, 0};
    cone.cone_stops = std::vector<double>(8, 80 * kDegToRad);
    cone.cone_stops[2] = 10 * kDegToRad;  // a quarter turn from forward
    const Vec3 d2 = cone_stop_dir(cone, 2, 8);
    CHECK_NEAR(std::fabs(d2.y), 1.0, 1e-9);
    CHECK_NEAR(cone_stop(cone, d2), 10 * kDegToRad, 1e-9);
    CHECK_NEAR(cone_stop(cone, {1, 0, 0}), 80 * kDegToRad, 1e-9);
    CHECK_NEAR(cone_stop(cone, (d2 + cone_stop_dir(cone, 1, 8)).normalized()), 45 * kDegToRad, 1e-9);  // halfway

    // A swing toward the tight side stops at 10 degrees, the other way it runs to 60.
    auto swing_to = [&](const Vec3& tip, double a) { return Quat::axis_angle(cone.bone_axis.cross(tip), a); };
    CHECK(!is_frame_rotation_within_limits(cone, swing_to(d2, 30 * kDegToRad)));
    CHECK(angle_between(clamp_frame_rotation(cone, swing_to(d2, 30 * kDegToRad)), swing_to(d2, 10 * kDegToRad)) < 1e-6);
    CHECK(is_frame_rotation_within_limits(cone, swing_to(d2 * -1.0, 60 * kDegToRad)));

    // Saved and loaded as they are
    JointLimit back;
    CHECK(limit_from_json(limit_to_json(cone), back));
    CHECK(back == cone);

    // Widening to a pose opens that direction's stops
    JointLimit wide = cone;
    widen_cone_swing(wide, swing_to(d2, 30 * kDegToRad));
    CHECK(is_frame_rotation_within_limits(wide, swing_to(d2, 30 * kDegToRad)));

    // The mirrored limit's tight side is the mirrored direction: the left hip's inward stop is the right hip's
    const int l = node("mHipLeft"), r = node("mHipRight");
    const JointLimit m = mirror_joint_limit(skel(), nullptr, l, r, cone);
    const Vec3 mirrored{d2.x, -d2.y, d2.z};
    CHECK_NEAR(cone_stop(m, mirrored), 10 * kDegToRad, 1e-6);
    CHECK_NEAR(cone_stop(m, d2), 80 * kDegToRad, 1e-6);
}

TEST(hinge_limit_keeps_a_little_off_axis_rotation) {
    // Keyed and mocap knees turn a degree or two off their hinge. Wiping that on every touch made the leg jump.
    JointLimit knee;
    knee.kind = JointLimitKind::Hinge;
    knee.axis = {0, 1, 0};
    knee.min_angle = 0;
    knee.max_angle = 2.4;
    const Quat bent = Quat::axis_angle({1, 0, 0}, 2 * kDegToRad) * Quat::axis_angle({0, 1, 0}, 1.0);
    CHECK(is_frame_rotation_within_limits(knee, bent));
    CHECK(angle_between(clamp_frame_rotation(knee, bent), bent) < 1e-9);
    // Past the tolerance it comes back to it, and the hinge angle is kept
    const Quat twisted = Quat::axis_angle({1, 0, 0}, 20 * kDegToRad) * Quat::axis_angle({0, 1, 0}, 1.0);
    const Quat fixed = clamp_frame_rotation(knee, twisted);
    CHECK(is_frame_rotation_within_limits(knee, fixed));
    CHECK(angle_between(fixed, Quat::axis_angle({1, 0, 0}, kHingeOffAxis) * Quat::axis_angle({0, 1, 0}, 1.0)) < 1e-6);
}

TEST(auto_ik_drag_past_a_limit_stays_continuous) {
    // The mech's hind leg with a tight hip cone, dragged out sideways and up past what the limits reach: the knee and
    // hock swung up to 126 degrees from one step to the next.
    DaeModel m;
    CHECK(load_mech_with_hind_legs(m));
    const Shape s = mech_shape(m);
    RigConstraints rc = suggested(&s, {&m});
    rc.limits["mHindLimb1Left"].cone_angle = 13 * kDegToRad;
    const Rig rig(skel());
    const int foot = node("mHindLimb4Left");
    Clip c;
    const AutoIkChain chain = auto_ik_chain(rig, c, 0, foot);
    Evaluation cur = evaluate(rig, c, 0, &s);
    const Vec3 start = cur.globals[foot].pos;
    for (const Vec3& way : {Vec3{0, 0.7, 0.5}, Vec3{-0.8, 0, 0.2}, Vec3{0.6, 0, 0.3}}) {
        cur = evaluate(rig, c, 0, &s);
        Pose prev = cur.pose;
        double worst = 0;
        for (int i = 1; i <= 80; ++i) {
            Clip step;
            key_auto_ik(step, rig, 0, chain, cur, start + way * (i / 80.0), &s, &cur.pose, &rc);
            for (int b : chain.bones) {
                if (i > 1) worst = std::max(worst, angle_between(prev.rot[b], cur.pose.rot[b]));
                CHECK(is_rotation_within_limits(*rc.find(skel()[b].name), cur.pose.rot[b], &s, b, 1e-3));
            }
            prev = cur.pose;
        }
        CHECK(worst < 15 * kDegToRad);
    }
}

TEST(auto_ik_leaves_passive_spine_bones_alone) {
    // mSpine1..4 are passed through, never turned or keyed: a limit on one (a creature's came out excluding rest) must
    // not turn it in the solve either, or the keyed pose misses by what that turn moved.
    const Rig rig(skel());
    Clip c;
    const int wrist = node("mWristLeft"), spine = node("mSpine4");
    const AutoIkChain chain = auto_ik_chain(rig, c, 0, wrist, 5);
    CHECK(std::find(chain.bones.begin(), chain.bones.end(), spine) != chain.bones.end());
    RigConstraints rc;
    JointLimit lim;
    lim.kind = JointLimitKind::Hinge;
    lim.axis = {0, 1, 0};
    lim.min_angle = -1.0;
    lim.max_angle = -0.5;
    rc.limits["mSpine4"] = lim;
    const Evaluation start = evaluate(rig, c, 0, nullptr);
    const Vec3 target = start.globals[wrist].pos + Vec3{0.05, -0.05, 0.05};
    Pose solved;
    key_auto_ik(c, rig, 0, chain, start, target, nullptr, &solved, &rc);
    CHECK(angle_between(solved.rot[spine], Quat{}) < 1e-9);
    CHECK((evaluate(rig, c, 0, nullptr).globals[wrist].pos - target).length() < 0.005);
}

TEST(limb_ik_with_a_hip_cone_still_reaches) {
    // The knee turned toward a pole off to the side and the hip was clamped after: the foot missed by 0.2-0.3 m though
    // the limited leg reaches. Now the knee turns toward the pole only as far as the hip's cone allows.
    const Rig rig(skel());
    const int limb = rig.find_limb("LegLeft");
    const LimbInfo& l = rig.limbs()[limb];
    RigConstraints rc;
    JointLimit hip;
    hip.kind = JointLimitKind::Cone;
    hip.bone_axis = {0, 0, -1};
    hip.cone_angle = 45 * kDegToRad;
    hip.twist_min = -0.3;
    hip.twist_max = 0.3;
    rc.limits["mHipLeft"] = hip;
    JointLimit knee;
    knee.kind = JointLimitKind::Hinge;
    knee.axis = {0, 1, 0};
    knee.min_angle = 0;
    knee.max_angle = 2.4;
    rc.limits["mKneeLeft"] = knee;
    const Pose rest(skel().size());
    const std::vector<Xform> g = skel().global_pose(rest, nullptr);
    Xform target = g[l.end];
    target.pos += Vec3{0.15, 0, 0.10};
    for (double side : {0.0, 1.0, 2.0}) {
        const Vec3 pole = g[l.mid].pos + Vec3{1, side, 0};
        for (int vats_solve = 0; vats_solve < 2; ++vats_solve) {
            Quat out[3];
            if (vats_solve)
                solve_limb_vats(rig, limb, nullptr, target, pole, rest, g, out, &rc);
            else
                solve_two_bone(skel(), l, IkSolve::VATs, rest, g, nullptr, target, pole, out, &rc);
            Pose p = rest;
            const int bones[3] = {l.root, l.mid, l.end};
            for (int k = 0; k < 3; ++k) p.rot[bones[k]] = (skel()[bones[k]].rest.conj() * out[k]).normalized();
            CHECK((skel().global_pose(p, nullptr)[l.end].pos - target.pos).length() < 0.005);
            CHECK(is_rotation_within_limits(hip, p.rot[l.root], nullptr, l.root, 1e-3));
        }
    }
}

TEST(set_from_pose_widens_hinge_and_cone_limits) {
    const Skeleton& s = skel();
    const int knee = node("mKneeLeft"), shoulder = node("mShoulderLeft");

    // A hinge widens to the pose on whichever side it is past, and keeps the rest of its range.
    JointLimit hinge;
    hinge.kind = JointLimitKind::Hinge;
    hinge.axis = Vec3{0, 1, 0};
    hinge.max_angle = 60.0 * kDegToRad;
    hinge = widen_limit_to_pose(s, nullptr, knee, &hinge, Quat::axis_angle({0, 1, 0}, 80.0 * kDegToRad));
    CHECK_NEAR(hinge.max_angle, 80.0 * kDegToRad, 1e-4);
    CHECK_NEAR(hinge.min_angle, 0.0, 1e-4);
    hinge = widen_limit_to_pose(s, nullptr, knee, &hinge, Quat::axis_angle({0, 1, 0}, -20.0 * kDegToRad));
    CHECK_NEAR(hinge.min_angle, -20.0 * kDegToRad, 1e-4);
    CHECK_NEAR(hinge.max_angle, 80.0 * kDegToRad, 1e-4);
    CHECK(hinge.source == JointLimitSource::Manual);

    // A pose inside the range changes nothing but the source.
    JointLimit inside = widen_limit_to_pose(s, nullptr, knee, &hinge, Quat::axis_angle({0, 1, 0}, 30.0 * kDegToRad));
    CHECK_NEAR(inside.min_angle, hinge.min_angle, 1e-9);
    CHECK_NEAR(inside.max_angle, hinge.max_angle, 1e-9);

    // A cone widens its swing and the twist side the pose is past.
    JointLimit cone;
    cone.kind = JointLimitKind::Cone;
    cone.bone_axis = Vec3{0, 1, 0};
    cone.cone_angle = 40.0 * kDegToRad;
    cone.twist_min = -25.0 * kDegToRad;
    cone.twist_max = 25.0 * kDegToRad;
    const Quat pose_cone = Quat::axis_angle({1, 0, 0}, 55.0 * kDegToRad) * Quat::axis_angle({0, 1, 0}, 35.0 * kDegToRad);
    cone = widen_limit_to_pose(s, nullptr, shoulder, &cone, pose_cone);
    CHECK_NEAR(cone.cone_angle, 55.0 * kDegToRad, 1e-4);
    CHECK_NEAR(cone.twist_max, 35.0 * kDegToRad, 1e-4);
    CHECK_NEAR(cone.twist_min, -25.0 * kDegToRad, 1e-4);

    // An unlimited joint starts from its suggestion (the knee's template hinge), not a frozen 0..0 hinge.
    const JointLimit fresh = widen_limit_to_pose(s, nullptr, knee, nullptr, Quat{});
    const auto templates = template_limits(s);  // kept: tmpl points into it
    const JointLimit* tmpl = templates.find("mKneeLeft");
    CHECK(tmpl && fresh.kind == tmpl->kind);
    if (tmpl) CHECK_NEAR(fresh.max_angle - fresh.min_angle, tmpl->max_angle - tmpl->min_angle, 1e-9);

    // On a body with its own bone axes the pose (keys are in SL's frame) is read in the limit's frame: a knee bent
    // 90 degrees about the hinge widens to 90, not 0.2.
    DaeModel m;
    CHECK(load_mech(m));
    const Shape ms = mech_shape(m);
    CHECK(has_rig_axes(&ms, knee));
    JointLimit rigged;
    rigged.kind = JointLimitKind::Hinge;
    rigged.axis = Vec3{1, 0, 0};
    rigged = widen_limit_to_pose(s, &ms, knee, &rigged, from_joint_frame(&ms, knee, Quat::axis_angle({1, 0, 0}, kPi / 2)));
    CHECK_NEAR(rigged.max_angle, kPi / 2, 1e-4);
}

TEST(project_joint_limits_storage_and_clearing) {
    Project p;
    CHECK(p.joint_limits.empty());
    CHECK(!p.body_constraints("sl-default"));

    // Add limits for sl-default
    RigConstraints& sl_rc = p.get_or_create_constraints("sl-default");
    JointLimit knee;
    knee.kind = JointLimitKind::Hinge;
    knee.axis = {0, 1, 0};
    knee.min_angle = 0.0;
    knee.max_angle = 2.0;
    sl_rc.limits["mKneeLeft"] = knee;

    CHECK(p.body_constraints("sl-default"));
    CHECK(p.body_constraints("sl-default")->find("mKneeLeft"));

    // Clear limit
    sl_rc.limits.erase("mKneeLeft");
    CHECK(!sl_rc.find("mKneeLeft"));
    CHECK(sl_rc.empty());
}

TEST(auto_ik_performance_stays_interactive) {
    const Rig rig(skel());
    Clip c_base;
    const int ankle = node("mAnkleLeft");
    const Evaluation start = evaluate(rig, c_base, 0, nullptr);
    const AutoIkChain ch = auto_ik_chain(rig, c_base, 0, ankle);
    RigConstraints rc;
    JointLimit limit;
    limit.kind = JointLimitKind::Hinge;
    limit.axis = Vec3{0, 1, 0};
    limit.min_angle = 0.0;
    limit.max_angle = 0.4;
    rc.get_or_create("mKneeLeft") = limit;

    const Vec3 target = start.globals[ankle].pos + Vec3{-0.15, 0, 0.2};

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 200; ++i) {
        Clip c = c_base;
        key_auto_ik(c, rig, 0, ch, start, target, nullptr, nullptr, &rc);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms_total = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double ms_per_solve = ms_total / 200.0;
    CHECK(ms_per_solve < 20.0);  // loose: the Windows VM and a busy host run slower; --bench gives the real number
}

TEST(joint_limit_handle_maths_hinge) {
    JointLimit lim;
    lim.kind = JointLimitKind::Hinge;
    lim.axis = Vec3{0, 1, 0};
    lim.min_angle = 0.0;
    lim.max_angle = 90.0 * kDegToRad;

    const double snap5 = 5.0 * kDegToRad;

    // Drag min angle: snaps to 5 degrees
    lim = set_hinge_min(lim, -23.4 * kDegToRad, snap5);
    CHECK_NEAR(lim.min_angle, -25.0 * kDegToRad, 1e-4);

    // Clamps min angle so it cannot exceed max_angle
    lim = set_hinge_min(lim, 120.0 * kDegToRad);
    CHECK_NEAR(lim.min_angle, 90.0 * kDegToRad, 1e-4);

    // Drag max angle: snaps to 5 degrees
    lim = set_hinge_max(lim, 114.0 * kDegToRad, snap5);
    CHECK_NEAR(lim.max_angle, 115.0 * kDegToRad, 1e-4);

    // Clamps max angle so it cannot go below min_angle
    lim = set_hinge_max(lim, 30.0 * kDegToRad);
    CHECK_NEAR(lim.max_angle, lim.min_angle, 1e-4);

    // Drag hinge axis: normalized and set
    lim = set_hinge_axis(lim, Vec3{1, 1, 0});
    CHECK_NEAR((lim.axis - Vec3{1, 1, 0}.normalized()).length(), 0.0, 1e-5);
    CHECK_EQ(lim.source, JointLimitSource::Manual);
}

TEST(joint_limit_handle_maths_cone_widening_and_edge) {
    JointLimit lim;
    lim.kind = JointLimitKind::Cone;
    lim.bone_axis = Vec3{0, 1, 0};
    lim.cone_angle = 30.0 * kDegToRad;
    lim.twist_min = -20.0 * kDegToRad;
    lim.twist_max = 20.0 * kDegToRad;

    const double snap5 = 5.0 * kDegToRad;

    // Widen cone from 30° to 55° with snapping
    lim = set_cone_angle(lim, 53.2 * kDegToRad, snap5);
    CHECK_NEAR(lim.cone_angle, 55.0 * kDegToRad, 1e-4);

    // Drag edge point to make cone uneven (aim bone_axis away from {0, 1, 0})
    Vec3 tilted = Vec3{0.3, 1.0, 0.0}.normalized();
    lim = set_cone_edge(lim, tilted);
    CHECK_NEAR((lim.bone_axis - tilted).length(), 0.0, 1e-5);

    // Drag twist arc ends with snapping
    lim = set_twist_min(lim, -36.2 * kDegToRad, snap5);
    CHECK_NEAR(lim.twist_min, -35.0 * kDegToRad, 1e-4);

    lim = set_twist_max(lim, 48.1 * kDegToRad, snap5);
    CHECK_NEAR(lim.twist_max, 50.0 * kDegToRad, 1e-4);

    // Clamping on twist
    lim = set_twist_min(lim, 70.0 * kDegToRad);
    CHECK_NEAR(lim.twist_min, 50.0 * kDegToRad, 1e-4);
}

TEST(joint_limit_mirror_edit) {
    const Skeleton& s = skel();
    int knee_l = node("mKneeLeft");
    int knee_r = node("mKneeRight");
    CHECK(knee_l >= 0 && knee_r >= 0);

    // 1. Hinge mirror: mKneeLeft bending backward [0, 140°] about Y
    JointLimit knee_lim;
    knee_lim.kind = JointLimitKind::Hinge;
    knee_lim.axis = Vec3{0, 1, 0};
    knee_lim.bone_axis = Vec3{0.3, 0.2, -1.0}.normalized();
    knee_lim.min_angle = 0.0;
    knee_lim.max_angle = 140.0 * kDegToRad;
    knee_lim.source = JointLimitSource::Manual;

    JointLimit knee_mirr = mirror_joint_limit(s, nullptr, knee_l, knee_r, knee_lim);
    CHECK_EQ(knee_mirr.kind, JointLimitKind::Hinge);
    CHECK_NEAR((knee_mirr.axis - Vec3{0, 1, 0}).length(), 0.0, 1e-4);
    // bone_axis is mirrored across the sagittal plane (Y = 0): Y is negated, X and Z preserved
    CHECK_NEAR(knee_mirr.bone_axis.x, knee_lim.bone_axis.x, 1e-4);
    CHECK_NEAR(knee_mirr.bone_axis.y, -knee_lim.bone_axis.y, 1e-4);
    CHECK_NEAR(knee_mirr.bone_axis.z, knee_lim.bone_axis.z, 1e-4);
    CHECK_NEAR(knee_mirr.min_angle, 0.0, 1e-4);
    CHECK_NEAR(knee_mirr.max_angle, 140.0 * kDegToRad, 1e-4);

    // 2. Cone mirror: mShoulderLeft
    int shoulder_l = node("mShoulderLeft");
    int shoulder_r = node("mShoulderRight");
    CHECK(shoulder_l >= 0 && shoulder_r >= 0);

    JointLimit shoulder_lim;
    shoulder_lim.kind = JointLimitKind::Cone;
    shoulder_lim.bone_axis = Vec3{0, 0, 1};
    shoulder_lim.cone_angle = 45.0 * kDegToRad;
    shoulder_lim.twist_min = -20.0 * kDegToRad;
    shoulder_lim.twist_max = 40.0 * kDegToRad;

    JointLimit shoulder_mirr = mirror_joint_limit(s, nullptr, shoulder_l, shoulder_r, shoulder_lim);
    CHECK_EQ(shoulder_mirr.kind, JointLimitKind::Cone);
    CHECK_NEAR((shoulder_mirr.bone_axis - Vec3{0, 0, 1}).length(), 0.0, 1e-4);
    CHECK_NEAR(shoulder_mirr.cone_angle, 45.0 * kDegToRad, 1e-4);
    // Twist range is inverted across the mirror plane: [-40°, 20°]
    CHECK_NEAR(shoulder_mirr.twist_min, -40.0 * kDegToRad, 1e-4);
    CHECK_NEAR(shoulder_mirr.twist_max, 20.0 * kDegToRad, 1e-4);
}

TEST(joint_limit_grouping) {
    const Skeleton s = skel();
    auto suggestions = suggest_joint_limits(s, nullptr);
    CHECK(!suggestions.empty());

    std::unordered_map<LimitGroup, int> group_counts;
    for (const auto& r : suggestions) {
        LimitGroup g = joint_limit_group(r.joint);
        CHECK(g != LimitGroup::Other);
        std::string_view name = limit_group_name(g);
        CHECK(!name.empty());
        group_counts[g]++;
    }

    CHECK(group_counts[LimitGroup::SpineNeckHead] >= 4);
    CHECK(group_counts[LimitGroup::LeftArm] >= 4);
    CHECK(group_counts[LimitGroup::RightArm] >= 4);
    CHECK(group_counts[LimitGroup::LeftHand] >= 15);
    CHECK(group_counts[LimitGroup::RightHand] >= 15);
    CHECK(group_counts[LimitGroup::LeftLeg] >= 5);
    CHECK(group_counts[LimitGroup::RightLeg] >= 5);
    CHECK(group_counts[LimitGroup::HindLegs] >= 8);
    CHECK(group_counts[LimitGroup::Tail] >= 6);
    CHECK(group_counts[LimitGroup::Wings] >= 8);

    // Specific joints test
    CHECK_EQ(joint_limit_group("mTorso"), LimitGroup::SpineNeckHead);
    CHECK_EQ(joint_limit_group("mHead"), LimitGroup::SpineNeckHead);
    CHECK_EQ(joint_limit_group("mElbowLeft"), LimitGroup::LeftArm);
    CHECK_EQ(joint_limit_group("mWristRight"), LimitGroup::RightArm);
    CHECK_EQ(joint_limit_group("mHandIndex2Left"), LimitGroup::LeftHand);
    CHECK_EQ(joint_limit_group("mKneeLeft"), LimitGroup::LeftLeg);
    CHECK_EQ(joint_limit_group("mAnkleRight"), LimitGroup::RightLeg);
    CHECK_EQ(joint_limit_group("mHindLimb2Left"), LimitGroup::HindLegs);
    CHECK_EQ(joint_limit_group("mTail3"), LimitGroup::Tail);
    CHECK_EQ(joint_limit_group("mWing1Right"), LimitGroup::Wings);
    CHECK_EQ(joint_limit_group("mFaceJaw"), LimitGroup::Face);

    // Mirror group test
    CHECK_EQ(mirror_limit_group(LimitGroup::LeftArm), LimitGroup::RightArm);
    CHECK_EQ(mirror_limit_group(LimitGroup::RightArm), LimitGroup::LeftArm);
    CHECK_EQ(mirror_limit_group(LimitGroup::LeftLeg), LimitGroup::RightLeg);
    CHECK_EQ(mirror_limit_group(LimitGroup::RightLeg), LimitGroup::LeftLeg);
    CHECK_EQ(mirror_limit_group(LimitGroup::LeftHand), LimitGroup::RightHand);
    CHECK_EQ(mirror_limit_group(LimitGroup::RightHand), LimitGroup::LeftHand);
}

TEST(joint_limit_clamp_report) {
    const Skeleton s = skel();
    const int knee_node = node("mKneeLeft");
    CHECK(knee_node >= 0);

    JointLimit knee_lim;
    knee_lim.kind = JointLimitKind::Hinge;
    knee_lim.axis = Vec3{0, 1, 0};
    knee_lim.min_angle = 0.0;
    knee_lim.max_angle = 140.0 * kDegToRad;

    ClampedJoint c;
    // 1. Check unclamped within range: 90 deg -> false
    Quat q_in = Quat::axis_angle({0, 1, 0}, 90.0 * kDegToRad);
    CHECK(!check_joint_clamp("mKneeLeft", knee_node, knee_lim, q_in, nullptr, c));

    // 2. Check unclamped beyond max: 155 deg -> true, HingeMax, message
    Quat q_over = Quat::axis_angle({0, 1, 0}, 155.0 * kDegToRad);
    CHECK(check_joint_clamp("mKneeLeft", knee_node, knee_lim, q_over, nullptr, c));
    CHECK_EQ(c.joint, std::string("mKneeLeft"));
    CHECK_EQ(c.reason, ClampReason::HingeMax);
    CHECK_NEAR(c.angle_deg, 140.0, 0.1);
    CHECK_EQ(c.message, std::string("mKneeLeft stopped at 140 degrees (hinge max)"));

    // 3. Check unclamped below min: -15 deg -> true, HingeMin, message
    Quat q_under = Quat::axis_angle({0, 1, 0}, -15.0 * kDegToRad);
    CHECK(check_joint_clamp("mKneeLeft", knee_node, knee_lim, q_under, nullptr, c));
    CHECK_EQ(c.joint, std::string("mKneeLeft"));
    CHECK_EQ(c.reason, ClampReason::HingeMin);
    CHECK_NEAR(c.angle_deg, 0.0, 0.1);
    CHECK_EQ(c.message, std::string("mKneeLeft stopped at 0 degrees (hinge min)"));

    // 4. Query clamped pose
    RigConstraints rc;
    rc.limits["mKneeLeft"] = knee_lim;
    Pose pose(s.size());
    pose.rot[knee_node] = Quat::axis_angle({0, 1, 0}, 140.0 * kDegToRad);
    ClampReport rep = query_clamped_joints(s, nullptr, rc, pose);
    CHECK(!rep.empty());
    CHECK(rep.contains("mKneeLeft"));
    CHECK_EQ(rep.clamped.front().reason, ClampReason::HingeMax);

    // 5. Auto IK solve clamp report
    Rig rig_obj(s);
    Clip clip;
    const int ankle = node("mAnkleLeft");
    const AutoIkChain ch = auto_ik_chain(rig_obj, clip, 0, ankle);
    const Evaluation start = evaluate(rig_obj, clip, 0, nullptr);

    RigConstraints rc_ik;
    JointLimit knee_ik_lim = knee_lim;
    knee_ik_lim.max_angle = 0.4;
    rc_ik.limits["mKneeLeft"] = knee_ik_lim;

    const Vec3 target = start.globals[ankle].pos + Vec3{-0.2, 0, 0.25};
    Pose solved_pose;
    ClampReport ik_report;
    key_auto_ik(clip, rig_obj, 0, ch, start, target, nullptr, &solved_pose, &rc_ik, &ik_report);
    CHECK(!ik_report.empty());
    CHECK(ik_report.contains("mKneeLeft"));
    CHECK_EQ(ik_report.find("mKneeLeft")->reason, ClampReason::HingeMax);
}

TEST(joint_limit_pending_set) {
    JointLimit knee;
    knee.kind = JointLimitKind::Hinge;
    knee.max_angle = 120.0 * kDegToRad;
    JointLimit elbow = knee;
    elbow.axis = {0, 0, -1};
    elbow.max_angle = 140.0 * kDegToRad;

    // The project already has the knee; the suggestions agree on it and add the elbow and an unlimited face bone.
    Project project;
    RigConstraints& applied = project.get_or_create_constraints("sl-default");
    applied.limits["mKneeLeft"] = knee;
    PendingLimits pending;
    pending.start("sl-default", {{"mKneeLeft", knee}, {"mElbowLeft", elbow}, {"mFaceJaw", JointLimit{}}});

    // Which set posing and the limit tools use.
    const RigConstraints* app = project.body_constraints("sl-default");
    CHECK(posing_limits(true, true, LimitPreview::Suggested, pending, "sl-default", app) == &pending.limits);
    CHECK(posing_limits(true, true, LimitPreview::Applied, pending, "sl-default", app) == app);
    CHECK(posing_limits(true, true, LimitPreview::Off, pending, "sl-default", app) == nullptr);
    CHECK(posing_limits(false, true, LimitPreview::Suggested, pending, "sl-default", app) == nullptr);
    CHECK(posing_limits(true, false, LimitPreview::Suggested, pending, "sl-default", app) == app);  // panel closed
    CHECK(edits_pending_limits(true, LimitPreview::Suggested, pending, "sl-default"));
    CHECK(!edits_pending_limits(true, LimitPreview::Applied, pending, "sl-default"));
    // Another body (an actor or mesh body switched to, or a project opened) never poses with these suggestions.
    CHECK(!pending.for_body("mech-body"));
    CHECK(!edits_pending_limits(true, LimitPreview::Suggested, pending, "mech-body"));
    CHECK(posing_limits(true, true, LimitPreview::Suggested, pending, "mech-body", nullptr) == nullptr);

    // Counting and applying take only what changes: the elbow (the knee is the same, the face bone stays free).
    CHECK_EQ(count_limit_changes(app, pending.limits), 1);
    CHECK_EQ(count_limit_changes(app, pending.limits, [](const std::string& j) { return j != "mElbowLeft"; }), 0);
    CHECK_EQ(apply_all_limits(applied, pending.limits), 1);
    CHECK(applied.find("mElbowLeft") && !applied.limits.count("mFaceJaw"));
    CHECK_EQ(apply_all_limits(applied, pending.limits), 0);
    CHECK_EQ(count_limit_changes(app, pending.limits), 0);
    // An unlimited suggestion over an applied limit removes it, and counts.
    pending.limits.limits["mFaceJaw"] = JointLimit{};
    applied.limits["mFaceJaw"] = knee;
    CHECK_EQ(apply_selected_limits(applied, pending.limits, {"mFaceJaw"}), 1);
    CHECK(!applied.limits.count("mFaceJaw"));
    CHECK_EQ(apply_group_limits(applied, pending.limits, LimitGroup::LeftArm), 0);

    // Edits to the suggestions undo on their own, in turn with the project's undo steps (depth = project steps).
    RigConstraints before = pending.limits;
    pending.limits.limits["mKneeLeft"].max_angle = 150.0 * kDegToRad;  // a handle drag with 3 project steps
    CHECK(pending.record(before, 3));
    CHECK(!pending.record(pending.limits, 3));  // no change, no step
    CHECK(!pending.can_undo(4));                // a project edit came after it: Ctrl+Z undoes that first
    CHECK(pending.undo_edit(3));
    CHECK_NEAR(pending.limits.find("mKneeLeft")->max_angle, 120.0 * kDegToRad, 1e-9);
    CHECK(!pending.can_undo(3));
    CHECK(pending.redo_edit(3));
    CHECK_NEAR(pending.limits.find("mKneeLeft")->max_angle, 150.0 * kDegToRad, 1e-9);
    // Reset goes back to the suggestion, as its own step.
    before = pending.limits;
    pending.limits.limits["mKneeLeft"] = pending.suggested.limits["mKneeLeft"];
    CHECK(pending.record(before, 3));
    CHECK_NEAR(pending.limits.find("mKneeLeft")->max_angle, 120.0 * kDegToRad, 1e-9);
    CHECK(pending.undo_edit(3));
    CHECK_NEAR(pending.limits.find("mKneeLeft")->max_angle, 150.0 * kDegToRad, 1e-9);

    // A cancelled drag on the suggestions puts them back without touching the project.
    const auto project_before = project.joint_limits;
    auto snap = LimitDragSnapshot::capture(true, project.joint_limits, pending.limits);
    pending.limits.limits["mKneeLeft"].max_angle = 175.0 * kDegToRad;
    CHECK(!snap.restore(project.joint_limits, pending.limits));
    CHECK_NEAR(pending.limits.find("mKneeLeft")->max_angle, 150.0 * kDegToRad, 1e-9);
    CHECK(project.joint_limits == project_before);

    // Discard drops them, and their undo, and the project keeps what was applied.
    pending.clear();
    CHECK(pending.limits.empty() && pending.undo.empty() && !pending.for_body("sl-default"));
    CHECK(project.body_constraints("sl-default")->find("mElbowLeft"));
}

TEST(joint_limit_edits_honour_mirror) {
    const Skeleton& s = skel();
    const int l = node("mKneeLeft"), r = node("mKneeRight");
    JointLimit knee;
    knee.kind = JointLimitKind::Hinge;
    knee.max_angle = 140.0 * kDegToRad;
    RigConstraints rc;
    set_joint_limit(rc, s, nullptr, l, knee, true);
    CHECK(rc.find("mKneeLeft") && rc.find("mKneeRight"));
    CHECK(*rc.find("mKneeRight") == mirror_joint_limit(s, nullptr, l, r, knee));

    // Clear (or Kind: None) with Mirror on clears both sides; with it off only this one.
    set_joint_limit(rc, s, nullptr, l, JointLimit{}, true);
    CHECK(rc.limits.empty());
    set_joint_limit(rc, s, nullptr, l, knee, true);
    set_joint_limit(rc, s, nullptr, r, JointLimit{}, false);
    CHECK(rc.find("mKneeLeft") && !rc.limits.count("mKneeRight"));

    // A joint on the midline has no other side.
    set_joint_limit(rc, s, nullptr, node("mTorso"), knee, true);
    CHECK_EQ(rc.limits.size(), size_t(2));
}

TEST(clamp_frame_rotation_continuous_near_180) {
    JointLimit hinge;
    hinge.kind = JointLimitKind::Hinge;
    hinge.axis = Vec3{0, 1, 0};
    hinge.min_angle = 0.0;
    hinge.max_angle = 2.2;

    auto get_angle = [](const JointLimit& lim, const Quat& q) -> double {
        Quat sw, tw;
        decompose_swing_twist(q, lim.axis, sw, tw);
        const Vec3 tv{tw.x, tw.y, tw.z};
        double a = 2.0 * std::atan2(tv.dot(lim.axis), tw.w);
        while (a > kPi) a -= 2.0 * kPi;
        while (a < -kPi) a += 2.0 * kPi;
        return a;
    };

    // Angle at 179° (3.124 rad) -> should clamp to max_angle (2.2)
    Quat q179 = Quat::axis_angle({0, 1, 0}, 179.0 * kDegToRad);
    Quat c179 = clamp_frame_rotation(hinge, q179);
    CHECK_NEAR(get_angle(hinge, c179), 2.2, 1e-4);

    // Angle at 181° (3.159 rad) -> should STILL clamp to max_angle (2.2), NOT jump to 0.0!
    Quat q181 = Quat::axis_angle({0, 1, 0}, 181.0 * kDegToRad);
    Quat c181 = clamp_frame_rotation(hinge, q181);
    CHECK_NEAR(get_angle(hinge, c181), 2.2, 1e-4);

    // 2. Cone with twist near 180°: twist_min = 0.0, twist_max = 1.0 (ref = 0.5)
    JointLimit cone;
    cone.kind = JointLimitKind::Cone;
    cone.bone_axis = Vec3{0, 0, 1};
    cone.cone_angle = 1.0;
    cone.twist_min = 0.0;
    cone.twist_max = 1.0;

    // Twist at 181° should clamp to twist_max (1.0), not jump to 0.0
    Quat q_twist181 = Quat::axis_angle({0, 0, 1}, 181.0 * kDegToRad);
    Quat c_twist181 = clamp_frame_rotation(cone, q_twist181);
    Quat sw, tw;
    decompose_swing_twist(c_twist181, cone.bone_axis, sw, tw);
    const Vec3 tv{tw.x, tw.y, tw.z};
    double tw_ang = 2.0 * std::atan2(tv.dot(cone.bone_axis), tw.w);
    tw_ang = wrap_near_pi(tw_ang, 0.5);
    CHECK_NEAR(tw_ang, 1.0, 1e-4);

    // 3. Swing near 180°: swing axis must not jump to arbitrary fallback
    Vec3 swing_axis = Vec3{1, 0, 0};
    Quat q_swing179 = Quat::axis_angle(swing_axis, 179.99 * kDegToRad);
    Quat c_swing179 = clamp_frame_rotation(cone, q_swing179);
    Quat expected = Quat::axis_angle(swing_axis, 1.0);
    CHECK(angle_between(c_swing179, expected) < 1e-3);
}

TEST(auto_ik_drag_near_joint_limits_does_not_flip) {
    const Skeleton s = skel();
    const Rig rig(s);

    auto get_hinge_angle = [&](const Shape* shape, int node_idx, const JointLimit& limit, const Quat& local_rot) -> double {
        const Quat frame_rot = to_joint_frame(shape, node_idx, local_rot);
        const Vec3 axis = limit.axis.length() > 1e-6 ? limit.axis.normalized() : Vec3{0, 1, 0};
        Quat swing, twist;
        decompose_swing_twist(frame_rot, axis, swing, twist);
        const Vec3 tv{twist.x, twist.y, twist.z};
        double angle = 2.0 * std::atan2(tv.dot(axis), twist.w);
        while (angle > kPi) angle -= 2.0 * kPi;
        while (angle < -kPi) angle += 2.0 * kPi;
        return angle;
    };

    // Case 1: SL leg with knee hinge and hip cone
    {
        const int ankle = node("mAnkleLeft");
        const int knee = node("mKneeLeft");
        const int hip = node("mHipLeft");
        CHECK(ankle >= 0 && knee >= 0 && hip >= 0);

        RigConstraints rc;
        JointLimit knee_lim;
        knee_lim.kind = JointLimitKind::Hinge;
        knee_lim.axis = Vec3{0, 1, 0};
        knee_lim.min_angle = 0.0;
        knee_lim.max_angle = 1.4;  // ~80 degrees
        rc.limits["mKneeLeft"] = knee_lim;

        JointLimit hip_lim;
        hip_lim.kind = JointLimitKind::Cone;
        hip_lim.bone_axis = Vec3{0, 0, -1};
        hip_lim.cone_angle = 25.0 * kDegToRad;
        hip_lim.twist_min = -30.0 * kDegToRad;
        hip_lim.twist_max = 30.0 * kDegToRad;
        rc.limits["mHipLeft"] = hip_lim;

        auto run_drag = [&](std::vector<Pose>& out_poses) {
            Clip clip;
            key_rotation(clip, "mKneeLeft", 0, Quat::axis_angle({0, 1, 0}, 30.0 * kDegToRad));
            const AutoIkChain chain = auto_ik_chain(rig, clip, 0, ankle);
            Evaluation cur = evaluate(rig, clip, 0, nullptr);
            const Vec3 start_pos = cur.globals[ankle].pos;
            const Vec3 hip_pos = cur.globals[hip].pos;

            const Vec3 drag_dir = (Vec3{hip_pos.x, hip_pos.y - 0.2, hip_pos.z + 0.1} - start_pos).normalized();
            const int steps = 50;
            const double total_dist = 0.6;

            Pose prev_pose = cur.pose;
            bool reached_limit = false;
            for (int i = 1; i <= steps; ++i) {
                const Vec3 target = start_pos + drag_dir * (total_dist * (double(i) / steps));
                key_auto_ik(clip, rig, 0, chain, cur, target, nullptr, &cur.pose, &rc);
                cur.globals = s.global_pose(cur.pose, nullptr);
                out_poses.push_back(cur.pose);

                for (int b : chain.bones) {
                    const double turn_deg = angle_between(cur.pose.rot[b], prev_pose.rot[b]) * kRadToDeg;
                    CHECK(turn_deg < 10.0);
                }

                const double knee_ang = get_hinge_angle(nullptr, knee, knee_lim, cur.pose.rot[knee]);
                if (std::fabs(knee_ang - knee_lim.max_angle) < 1e-3) {
                    reached_limit = true;
                }
                if (reached_limit) {
                    CHECK_NEAR(knee_ang, knee_lim.max_angle, 1e-3);
                }
                prev_pose = cur.pose;
            }
            CHECK(reached_limit);
        };

        std::vector<Pose> run1, run2;
        run_drag(run1);
        run_drag(run2);

        CHECK_EQ(run1.size(), run2.size());
        for (size_t i = 0; i < run1.size(); ++i) {
            for (size_t b = 0; b < s.size(); ++b) {
                CHECK(angle_between(run1[i].rot[b], run2[i].rot[b]) < 1e-5);
            }
        }
    }

    // Case 2: Mech hind leg with knee hinge and hip cone
    {
        DaeModel m;
        CHECK(load_mech(m));
        const Shape mech_s = mech_shape(m);

        const int foot = node("mHindLimb4Left");
        const int knee = node("mHindLimb2Left");
        const int hip = node("mHindLimb1Left");
        CHECK(foot >= 0 && knee >= 0 && hip >= 0);

        auto get_cone_swing = [&](const Shape* shape, int node_idx, const JointLimit& limit, const Quat& local_rot) -> double {
            const Quat frame_rot = to_joint_frame(shape, node_idx, local_rot);
            const Vec3 u = limit.bone_axis.length() > 1e-6 ? limit.bone_axis.normalized() : Vec3{0, 0, 1};
            Quat swing, twist;
            decompose_swing_twist(frame_rot, u, swing, twist);
            const Vec3 sv{swing.x, swing.y, swing.z};
            return 2.0 * std::atan2(sv.length(), swing.w);
        };

        RigConstraints rc;
        JointLimit hip_lim;
        hip_lim.kind = JointLimitKind::Cone;
        hip_lim.bone_axis = to_joint_frame(&mech_s, hip, Vec3{0, 0, -1});
        hip_lim.cone_angle = 20.0 * kDegToRad;
        hip_lim.twist_min = -25.0 * kDegToRad;
        hip_lim.twist_max = 25.0 * kDegToRad;
        rc.limits["mHindLimb1Left"] = hip_lim;

        auto run_drag = [&](std::vector<Pose>& out_poses) {
            Clip clip;
            const AutoIkChain chain = auto_ik_chain(rig, clip, 0, foot);
            Evaluation cur = evaluate(rig, clip, 0, &mech_s);
            const Vec3 start_pos = cur.globals[foot].pos;

            const Vec3 drag_dir = Vec3{0.05, 0.35, 0.15}.normalized();
            const int steps = 50;
            const double total_dist = 0.35;

            Pose prev_pose = cur.pose;
            bool reached_limit = false;
            for (int i = 1; i <= steps; ++i) {
                const Vec3 target = start_pos + drag_dir * (total_dist * (double(i) / steps));
                key_auto_ik(clip, rig, 0, chain, cur, target, &mech_s, &cur.pose, &rc);
                cur.globals = s.global_pose(cur.pose, &mech_s);
                out_poses.push_back(cur.pose);

                for (int b : chain.bones) {
                    const double turn_deg = angle_between(cur.pose.rot[b], prev_pose.rot[b]) * kRadToDeg;
                    CHECK(turn_deg < 10.0);
                }

                const double hip_swing = get_cone_swing(&mech_s, hip, hip_lim, cur.pose.rot[hip]);
                if (std::fabs(hip_swing - hip_lim.cone_angle) < 1e-3) {
                    reached_limit = true;
                }
                if (reached_limit) {
                    CHECK_NEAR(hip_swing, hip_lim.cone_angle, 1e-3);
                }
                prev_pose = cur.pose;
            }
            CHECK(reached_limit);
        };

        std::vector<Pose> run1, run2;
        run_drag(run1);
        run_drag(run2);

        CHECK_EQ(run1.size(), run2.size());
        for (size_t i = 0; i < run1.size(); ++i) {
            for (size_t b = 0; b < s.size(); ++b) {
                CHECK(angle_between(run1[i].rot[b], run2[i].rot[b]) < 1e-5);
            }
        }
    }
}


// A hand-edited .vat with min > max or a negative cone loads in order, so std::clamp never gets lo > hi.
TEST(joint_limit_from_json_orders_bad_values) {
    Json j;
    std::string err;
    CHECK(parse_json(R"({"kind":"hinge","axis":[0,1,0],"min":1.5,"max":-0.5})", j, err));
    JointLimit h;
    CHECK(limit_from_json(j, h));
    CHECK(h.min_angle <= h.max_angle);
    CHECK(parse_json(R"({"kind":"cone","bone_axis":[0,1,0],"cone":-1,"twist_min":2,"twist_max":-2})", j, err));
    JointLimit c;
    CHECK(limit_from_json(j, c));
    CHECK(c.cone_angle >= 0 && c.twist_min <= c.twist_max);
}

// Template axes follow the skeleton: every cone points along its bone, and a hinge's positive angle is the flex
// (forearm forward, shin back, finger tips toward the palm).
TEST(suggest_limits_template_axes_follow_the_bones) {
    const RigConstraints rc = template_limits(skel(), nullptr);
    for (const auto& [name, lim] : rc.limits) {
        const int n = skel().find(name);
        if (lim.kind == JointLimitKind::Cone && skel()[n].end.length() > 1e-6)
            CHECK(lim.bone_axis.dot(rest_bone_dir(skel(), nullptr, n)) > 0.99);
    }
    auto flex_moves_tip = [&](const char* bone, const Vec3& way) {
        const int n = skel().find(bone);
        const JointLimit* lim = rc.find(bone);
        if (!lim) return false;
        const Vec3 tip = Quat::axis_angle(lim->axis, 0.5).rotate(skel()[n].end);
        return (tip - skel()[n].end).dot(way) > 0;
    };
    CHECK(flex_moves_tip("mElbowLeft", {1, 0, 0}));
    CHECK(flex_moves_tip("mElbowRight", {1, 0, 0}));
    CHECK(flex_moves_tip("mKneeLeft", {-1, 0, 0}));
    CHECK(flex_moves_tip("mHandIndex2Left", {0, 0, -1}));
    CHECK(flex_moves_tip("mHandIndex2Right", {0, 0, -1}));
}
