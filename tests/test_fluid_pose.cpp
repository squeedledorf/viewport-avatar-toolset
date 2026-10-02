// Posing by dragging the body (spec 08 FP): surface picking from skin weights, whole-body drags, follow-through.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "../tools/mech_rig.h"
#include "check.h"
#include "fixtures.h"
#include "vats/avatar_mesh.h"
#include "vats/dae.h"
#include "vats/edit.h"
#include "vats/fluid_pose.h"
#include "vats/rig_constraints.h"
#include "vats/skeleton.h"

using namespace vats;

namespace {

bool load_mech(DaeModel& m, bool extras = true) {
    DaeReport r;
    std::string err;
    const bool ok = load_dae(mech::dae(skel(), mech::build(skel(), extras)), "", skel(), m, r, err);
    if (!ok) std::fprintf(stderr, "mech: %s\n", err.c_str());
    return ok;
}

AvatarMesh& avatar_mesh() {
    static AvatarMesh m = [] {
        AvatarMesh a;
        std::string err;
        if (!a.load(skel(), VATS_DATA_DIR, err)) {
            std::fprintf(stderr, "cannot load avatar mesh: %s\n", err.c_str());
            std::exit(2);
        }
        return a;
    }();
    return m;
}

// What the SL avatar's mesh weights (it has no hind legs, wings or tail), as App::is_joint_weighted reads it.
std::function<bool(int)> sl_avatar_weights() {
    AvatarMesh& am = avatar_mesh();
    if (am.influences().empty()) am.build(Body::Female);
    return [&am](int node) {
        for (const Influence& i : am.influences())
            if ((i.a == node && i.blend < 1) || (i.b == node && i.blend > 0)) return true;
        return false;
    };
}

std::vector<std::string> sorted(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

bool has_track_starting(const Clip& clip, const std::string& prefix) {
    for (const auto& [name, track] : clip.curves)
        if (name.rfind(prefix, 0) == 0) return true;
    return false;
}

}  // namespace

TEST(whole_body_drag_on_the_sl_avatar_keys_only_its_legs) {
    const Skeleton& s = skel();
    Rig rig(s);
    Clip clip;
    const int pelvis = s.find("mPelvis");

    // The bare skeleton's hind feet stand at the floor too, but the SL avatar has no hind legs to plant.
    BodyDrag drag = begin_body_drag(rig, clip, 0, pelvis, nullptr, {sl_avatar_weights(), {}});
    CHECK_EQ(int(drag.feet.size()), 2);
    for (const PlantedFoot& f : drag.feet) CHECK(!f.held);

    // Drag pelvis forward by 8 cm and down by 4 cm
    const Vec3 target = drag.start.globals[pelvis].pos + Vec3{0.08, 0, -0.04};
    const std::vector<std::string> keyed = key_body_drag(clip, rig, 0, drag, target, true, nullptr);
    const std::vector<std::string> legs = {"mAnkleLeft", "mAnkleRight", "mHipLeft",  "mHipRight",
                                           "mKneeLeft",  "mKneeRight",  "mPelvis"};
    CHECK(sorted(keyed) == legs);
    CHECK(!has_track_starting(clip, "mHindLimb"));

    Evaluation ev = evaluate(rig, clip, 0, nullptr);
    CHECK((ev.globals[pelvis].pos - target).length() < 0.001);
    for (const PlantedFoot& f : drag.feet) {
        CHECK((ev.globals[f.node].pos - f.at.pos).length() < 0.001);
        CHECK((ev.globals[f.node].rot.conj() * f.at.rot).angle() < 0.001);
    }
}

TEST(whole_body_drag_plants_hooves_by_the_skin) {
    // The hoofed mech's hind foot joints stand 17 cm above the floor its hooves stand on.
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_dae(mech::dae(skel(), mech::build(skel(), false, true)), "", skel(), m, r, err));
    Shape shape;
    shape_from_binds(skel(), {&m}, nullptr, shape);
    rig_axes_from_parts(skel(), {&m}, shape);
    Rig rig(skel());
    const int pelvis = skel().find("mPelvis");
    const int hind = skel().find("mHindLimb3Left");

    auto planted = [&](const BodyDrag& d, int node) {
        return std::any_of(d.feet.begin(), d.feet.end(), [&](const PlantedFoot& f) { return f.node == node; });
    };
    Clip clip;
    CHECK(!planted(begin_body_drag(rig, clip, 0, pelvis, &shape), hind));  // by the joints: in the air

    const BodyDrag drag = begin_body_drag(rig, clip, 0, pelvis, &shape, {nullptr, {&m}});
    CHECK_EQ(int(drag.feet.size()), 4);
    CHECK(planted(drag, hind));
    const Vec3 target = drag.start.globals[pelvis].pos + Vec3{0.05, 0, -0.05};
    const std::vector<std::string> keyed = key_body_drag(clip, rig, 0, drag, target, true, &shape);
    for (const char* bone : {"mHindLimb1Left", "mHindLimb2Left", "mHindLimb3Left", "mHindLimb3Right"})
        CHECK(std::find(keyed.begin(), keyed.end(), bone) != keyed.end());

    Evaluation ev = evaluate(rig, clip, 0, &shape);
    for (const PlantedFoot& f : drag.feet) CHECK((ev.globals[f.node].pos - f.at.pos).length() < 0.001);
}

TEST(whole_body_drag_without_planting_keeps_pins_and_moves_the_rest) {
    const Skeleton& s = skel();
    Rig rig(s);
    Clip clip;
    const int pelvis = s.find("mPelvis");
    const int ankle_l = s.find("mAnkleLeft"), ankle_r = s.find("mAnkleRight");

    std::string why;
    CHECK(pin_here(clip, rig, 0, ankle_l, -1, nullptr, why));
    BodyDrag drag = begin_body_drag(rig, clip, 0, pelvis, nullptr, {sl_avatar_weights(), {}});
    for (const PlantedFoot& f : drag.feet) CHECK_EQ(f.held, f.node == ankle_l);

    // Not planted, only the pin holds a foot: the free one goes with the hips.
    const Vec3 delta = {0.06, 0, -0.03};
    key_body_drag(clip, rig, 0, drag, drag.start.globals[pelvis].pos + delta, false, nullptr);
    Evaluation ev = evaluate(rig, clip, 0, nullptr);
    CHECK((ev.globals[ankle_l].pos - drag.start.globals[ankle_l].pos).length() < 0.001);
    CHECK((ev.globals[ankle_r].pos - (drag.start.globals[ankle_r].pos + delta)).length() < 0.001);
}

TEST(whole_body_drag_letting_go_of_the_floor_takes_the_legs_back_to_the_press) {
    const Skeleton& s = skel();
    Rig rig(s);
    Clip clip;
    const int pelvis = s.find("mPelvis");
    const int knee = s.find("mKneeLeft");
    key_rotation(clip, "mKneeLeft", 0, Quat::axis_angle({0, 1, 0}, 0.2));  // keyed before the drag

    BodyDrag drag = begin_body_drag(rig, clip, 0, pelvis, nullptr, {sl_avatar_weights(), {}});
    const Vec3 delta = {0.1, 0, -0.1};
    const Vec3 target = drag.start.globals[pelvis].pos + delta;
    key_body_drag(clip, rig, 0, drag, target, true, nullptr);  // planted: the knees bend
    CHECK((evaluate(rig, clip, 0, nullptr).pose.rot[knee].conj() * drag.start.pose.rot[knee]).angle() > 0.1);

    key_body_drag(clip, rig, 0, drag, target, false, nullptr);  // Alt mid-drag
    Evaluation ev = evaluate(rig, clip, 0, nullptr);
    for (const PlantedFoot& f : drag.feet) {
        for (int b : f.chain.bones) CHECK((ev.pose.rot[b].conj() * drag.start.pose.rot[b]).angle() < 1e-6);
        CHECK((ev.globals[f.node].pos - (f.at.pos + delta)).length() < 0.001);  // the feet move rigidly
    }
}

TEST(whole_body_drag_keeps_a_leg_keyed_past_its_limit_planted_and_reports_clamps) {
    const Skeleton& s = skel();
    Rig rig(s);
    const int pelvis = s.find("mPelvis");
    const int ankle = s.find("mAnkleLeft");
    RigConstraints rc;
    JointLimit lim;
    lim.kind = JointLimitKind::Hinge;
    lim.axis = {0, 1, 0};
    lim.min_angle = -0.1;
    lim.max_angle = 0.1;
    rc.limits["mKneeLeft"] = lim;
    rc.limits["mKneeRight"] = lim;

    {  // The left knee keyed at 20 degrees, past its 6: a 1 mm drag leaves its foot where it was.
        Clip clip;
        key_rotation(clip, "mKneeLeft", 0, Quat::axis_angle({0, 1, 0}, 0.35));
        key_rotation(clip, "mHipLeft", 0, Quat::axis_angle({0, 1, 0}, -0.17));
        BodyDrag drag = begin_body_drag(rig, clip, 0, pelvis, nullptr, {sl_avatar_weights(), {}});
        key_body_drag(clip, rig, 0, drag, drag.start.globals[pelvis].pos + Vec3{0.001, 0, 0}, true, nullptr, &rc);
        Evaluation ev = evaluate(rig, clip, 0, nullptr, &rc);
        CHECK((ev.globals[ankle].pos - drag.start.globals[ankle].pos).length() < 0.001);
    }
    {  // A squat the stiff knees cannot make: the clamps are reported.
        Clip clip;
        BodyDrag drag = begin_body_drag(rig, clip, 0, pelvis, nullptr, {sl_avatar_weights(), {}});
        ClampReport rep;
        key_body_drag(clip, rig, 0, drag, drag.start.globals[pelvis].pos + Vec3{0, 0, -0.15}, true, nullptr, &rc, &rep);
        CHECK(rep.contains("mKneeLeft"));
        CHECK(rep.contains("mKneeRight"));
    }
}

TEST(ik_target_drag_reports_the_limit_that_stops_it) {
    Rig rig(skel());
    const int limb = rig.find_limb("LegLeft");
    const LimbInfo& l = rig.limbs()[limb];
    RigConstraints rc;
    JointLimit lim;
    lim.kind = JointLimitKind::Hinge;
    lim.axis = {0, 1, 0};
    lim.min_angle = 0.0;
    lim.max_angle = 1.0;
    rc.limits["mKneeLeft"] = lim;

    Clip c;
    switch_to_ik(c, rig, 0, limb, nullptr);
    Xform target = evaluate(rig, c, 0, nullptr).globals[l.end];
    ClampReport within;
    key_limb_target(c, rig, 0, limb, target, nullptr, &rc, &within);  // where it is: nothing stops it
    CHECK(within.empty());
    target.pos.z += 0.35;  // a knee bent past 1 radian
    ClampReport past;
    key_limb_target(c, rig, 0, limb, target, nullptr, &rc, &past);
    CHECK(past.contains("mKneeLeft"));
}

TEST(whole_body_drag_chest_leans_spine) {
    const Skeleton& s = skel();
    Rig rig(s);
    Clip clip;
    const int chest = s.find("mChest");
    const int torso = s.find("mTorso");
    const int head = s.find("mHead");
    CHECK(chest >= 0 && torso >= 0 && head >= 0);

    BodyDrag drag = begin_body_drag(rig, clip, 0, chest, nullptr, {sl_avatar_weights(), {}});
    CHECK(!drag.spine.bones.empty());
    CHECK_EQ(drag.head, head);

    const Vec3 delta = {0.04, 0, -0.02};
    const Vec3 target = drag.start.globals[chest].pos + delta;
    const std::vector<std::string> keyed = key_body_drag(clip, rig, 0, drag, target, true, nullptr);
    const std::vector<std::string> want = {"mAnkleLeft", "mAnkleRight", "mHead",  "mHipLeft",
                                           "mHipRight",  "mKneeLeft",   "mKneeRight", "mPelvis", "mTorso"};
    CHECK(sorted(keyed) == want);  // the chest itself is carried, not turned

    Evaluation ev = evaluate(rig, clip, 0, nullptr);
    CHECK((ev.globals[chest].pos - target).length() < 0.001);

    CHECK((ev.pose.rot[torso].conj() * drag.start.pose.rot[torso]).angle() > 0.01);

    double head_dot = std::fabs(ev.globals[head].rot.dot(drag.start.globals[head].rot));
    CHECK(head_dot > 0.999);

    for (const PlantedFoot& f : drag.feet) {
        double dist = (ev.globals[f.node].pos - f.at.pos).length();
        CHECK(dist < 0.001);
    }
}

TEST(ray_surface_hits_and_barycentrics) {
    // A single triangle in the XY plane at Z=0: (0,0,0), (1,0,0), (0,1,0).
    const std::vector<float> pos = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::vector<std::uint32_t> idx = {0, 1, 2};

    // Ray from (0.2, 0.3, 1) straight down: hits at t=1, u=0.2 (corner 1), v=0.3 (corner 2).
    SurfaceHit h = ray_surface({0.2, 0.3, 1.0}, {0, 0, -1.0}, pos, idx);
    CHECK_EQ(h.triangle, 0);
    CHECK(std::fabs(h.t - 1.0) < 1e-6);
    CHECK(std::fabs(h.u - 0.2) < 1e-6);
    CHECK(std::fabs(h.v - 0.3) < 1e-6);

    // Ray outside the triangle: misses.
    SurfaceHit miss = ray_surface({0.8, 0.8, 1.0}, {0, 0, -1.0}, pos, idx);
    CHECK_EQ(miss.triangle, -1);
    CHECK(miss.t >= 1e30);
}

TEST(surface_joint_on_mech_picks_correct_bones) {
    DaeModel m;
    CHECK(load_mech(m, true));
    CHECK(m.rigged);

    std::vector<float> skinned_pos, skinned_nrm;
    Shape shape;
    shape_from_binds(skel(), {&m}, nullptr, shape);
    const std::vector<Xform> r0 = skel().global_pose(Pose(skel().size()), &shape);
    skin_prop(m, skel(), r0, &shape, skinned_pos, skinned_nrm);

    const mech::Body body = mech::build(skel(), true);
    auto shoot_bone = [&](const char* name, const Vec3& offset_dir) {
        int n = skel().find(name);
        const mech::Bone* b = body.find(n);
        CHECK(b != nullptr);
        Vec3 center = (b->head + b->tail) * 0.5;
        return ray_surface(center + offset_dir * 1.0, -offset_dir, skinned_pos, m.indices);
    };

    // Raycast from the front (+X) into mHead.
    SurfaceHit h_head = shoot_bone("mHead", {1.0, 0, 0});
    CHECK(h_head.triangle >= 0);
    CHECK_EQ(surface_joint(skel(), m, h_head), skel().find("mHead"));

    // Raycast from the front (+X) into BELLY: maps to the joint BELLY hangs on.
    int belly_vol = -1;
    for (const auto& v : skel().volumes()) {
        if (v.name == "BELLY") { belly_vol = v.joint; break; }
    }
    CHECK(belly_vol >= 0);
    SurfaceHit h_belly = shoot_bone("BELLY", {1.0, 0, 0});
    CHECK(h_belly.triangle >= 0);
    CHECK_EQ(surface_joint(skel(), m, h_belly), belly_vol);

    // Raycast from the left (+Y) into mWristLeft.
    SurfaceHit h_wrist = shoot_bone("mWristLeft", {0, 1.0, 0});
    CHECK(h_wrist.triangle >= 0);
    CHECK_EQ(surface_joint(skel(), m, h_wrist), skel().find("mWristLeft"));

    // Raycast into a hind limb bone: mHindLimb2Left along its normal axis.
    const mech::Bone* b_hind = body.find(skel().find("mHindLimb2Left"));
    CHECK(b_hind != nullptr);
    const Vec3 hind_z = b_hind->axes.rotate({0, 0, 1});
    SurfaceHit h_hind = shoot_bone("mHindLimb2Left", hind_z);
    CHECK(h_hind.triangle >= 0);
    CHECK_EQ(surface_joint(skel(), m, h_hind), skel().find("mHindLimb2Left"));
}

TEST(surface_joint_on_avatar_mesh_picks_correct_bones) {
    const Skeleton& s = skel();
    AvatarMesh& am = avatar_mesh();
    am.build(Body::Female);
    const std::vector<Xform> rest = s.global_pose(Pose(s.size()));
    std::vector<float> pos, nrm;
    am.skin(rest, nullptr, pos, nrm);

    // Raycast into the head from the front.
    const Vec3 head_target = rest[s.find("mHead")].pos + Vec3{0.08, 0, 0.05};
    SurfaceHit h_head = ray_surface(head_target + Vec3{1.0, 0, 0}, {-1.0, 0, 0}, pos, am.indices());
    CHECK(h_head.triangle >= 0);
    CHECK_EQ(surface_joint(s, am, h_head), s.find("mHead"));

    // Raycast into the chest from the front.
    const Vec3 chest_target = rest[s.find("mChest")].pos + Vec3{0.12, 0, 0};
    SurfaceHit h_chest = ray_surface(chest_target + Vec3{1.0, 0, 0}, {-1.0, 0, 0}, pos, am.indices());
    CHECK(h_chest.triangle >= 0);
    CHECK_EQ(surface_joint(s, am, h_chest), s.find("mChest"));
}

TEST(follow_through_chains_detection_and_exclusion) {
    const Skeleton& s = skel();
    Clip clip;

    // All loose parts detected with empty moving and no weighted filter
    auto chains = follow_through_chains(s, clip, {}, {});
    CHECK(!chains.empty());
    bool has_tail = false, has_wing = false, has_ear = false, has_belly = false;
    for (const DynChain& c : chains) {
        CHECK_EQ(c.gravity, 0.0);
        if (c.root == "mTail1") has_tail = true;
        if (c.root == "mWing1Left") has_wing = true;
        if (c.root == "mFaceEar1Left") has_ear = true;
        if (c.root == "BELLY") has_belly = true;
    }
    CHECK(has_tail);
    CHECK(has_wing);
    CHECK(has_ear);
    CHECK(has_belly);

    // If moving contains mTail1, tail chain is excluded
    int tail1 = s.find("mTail1");
    CHECK(tail1 >= 0);
    auto chains_moving = follow_through_chains(s, clip, {tail1}, {});
    for (const DynChain& c : chains_moving) {
        CHECK(c.root != "mTail1");
    }

    // Weighted filter: only belly weighted
    int belly = s.find("BELLY");
    CHECK(belly >= 0);
    auto chains_weighted = follow_through_chains(s, clip, {}, [&](int node) { return node == belly; });
    CHECK_EQ(int(chains_weighted.size()), 1);
    CHECK_EQ(chains_weighted[0].root, "BELLY");
}

TEST(follow_through_settles_to_rest_and_an_edit_started_mid_swing_keys_none_of_it) {
    const Skeleton& s = skel();
    Rig rig(s);
    Clip clip;
    key_euler(clip, "mPelvis", 0, {0, 0, 0});
    const int tail2 = s.find("mTail2");

    auto chains = follow_through_chains(s, clip, {}, {});
    CHECK(!chains.empty());

    Evaluation base_ev = evaluate(rig, clip, 0, nullptr);
    FollowThrough ft(s, chains, base_ev.globals);
    CHECK(!ft.settled());

    Pose pose = base_ev.pose;

    // Move the pelvis to induce motion in follow-through chains
    for (int step = 0; step < 30; ++step) {
        Evaluation moving_ev = base_ev;
        for (Xform& x : moving_ev.globals) {
            x.pos.y += std::sin(step * 0.2) * 0.1;  // across the tail, so it swings
        }
        pose = base_ev.pose;  // the shown pose: the keys plus this frame's swing
        ft.step(moving_ev.globals, 0.016, pose);
    }

    // Motion was induced: the shown tail swings away from its keys.
    CHECK(!ft.settled());
    CHECK((pose.rot[tail2].conj() * base_ev.pose.rot[tail2]).angle() > 0.01);

    // A rotate edit begun now, as App::capture_edit_start begins one: from the keys' evaluation, not the shown pose.
    // Turning by nothing keys the bone as it was keyed; started from the shown pose it would key the swing.
    auto keyed_turn = [&](const Pose& from) {
        Clip edited = clip;
        key_rotation(edited, "mTail2", 0, from.rot[tail2]);
        return (evaluate(rig, edited, 0, nullptr).pose.rot[tail2].conj() * base_ev.pose.rot[tail2]).angle();
    };
    CHECK(keyed_turn(evaluate(rig, clip, 0, nullptr).pose) < 1e-6);
    CHECK(keyed_turn(pose) > 0.01);

    // Hold the pose still and let it settle
    bool did_settle = false;
    for (int step = 0; step < 200; ++step) {
        pose = base_ev.pose;
        if (!ft.step(base_ev.globals, 0.016, pose)) {
            did_settle = true;
            break;
        }
    }
    CHECK(did_settle);
    CHECK(ft.settled());

    // Settled pose matches animated rest pose
    const int tail1 = s.find("mTail1");
    if (tail1 >= 0) {
        CHECK((pose.rot[tail1].conj() * base_ev.pose.rot[tail1]).angle() < 0.001);
    }
    const int belly = s.find("BELLY");
    if (belly >= 0) {
        CHECK((pose.offset[belly] - base_ev.pose.offset[belly]).length() < 0.0001);
    }
}

static Shape mech_shape(const DaeModel& m) {
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);
    return s;
}

static double deg_between(const Quat& a, const Quat& b) {
    return (a.conj() * b).angle() * kRadToDeg;
}

TEST(ik_fk_round_trip_mech_hind_leg_and_bent_rigged_leg) {
    const Rig rig(skel());
    const int limb = rig.find_limb("HindLegLeft");
    CHECK(limb >= 0);
    const LimbInfo& l = rig.limbs()[limb];

    // 1. Mech hind leg with rig axes
    {
        DaeModel m;
        CHECK(load_mech(m));
        const Shape s = mech_shape(m);

        Clip c;
        Evaluation start = evaluate(rig, c, 0, &s);
        const int foot = l.end;
        const AutoIkChain ch = auto_ik_chain(rig, c, 0, foot);
        const Vec3 target = start.globals[foot].pos + Vec3{0.05, 0.02, 0.08};
        key_auto_ik(c, rig, 0, ch, start, target, &s);

        Evaluation before = evaluate(rig, c, 0, &s);
        switch_to_ik(c, rig, 0, limb, &s);
        Evaluation ik = evaluate(rig, c, 0, &s);
        switch_to_fk(c, rig, 0, limb, &s);
        Evaluation after = evaluate(rig, c, 0, &s);

        for (int b : {l.root, l.mid, l.end}) {
            CHECK(deg_between(ik.pose.rot[b], before.pose.rot[b]) < 0.1);
            CHECK(deg_between(after.pose.rot[b], before.pose.rot[b]) < 0.1);
        }
    }

    // 2. Bent-rigged leg without rig axes (flat binds)
    {
        mech::Body flat = mech::build(skel());
        for (mech::Bone& b : flat.bones) b.axes = Quat{};
        DaeModel fm;
        DaeReport r;
        std::string err;
        CHECK(load_dae(mech::dae(skel(), flat), "", skel(), fm, r, err));
        Shape fs;
        shape_from_binds(skel(), {&fm}, nullptr, fs);
        CHECK(fs.axes.empty());

        Clip c;
        Evaluation start = evaluate(rig, c, 0, &fs);
        const int foot = l.end;
        const AutoIkChain ch = auto_ik_chain(rig, c, 0, foot);
        const Vec3 target = start.globals[foot].pos + Vec3{0.05, 0.02, 0.08};
        key_auto_ik(c, rig, 0, ch, start, target, &fs);

        Evaluation before = evaluate(rig, c, 0, &fs);
        switch_to_ik(c, rig, 0, limb, &fs);
        Evaluation ik = evaluate(rig, c, 0, &fs);
        switch_to_fk(c, rig, 0, limb, &fs);
        Evaluation after = evaluate(rig, c, 0, &fs);

        for (int b : {l.root, l.mid, l.end}) {
            CHECK(deg_between(ik.pose.rot[b], before.pose.rot[b]) < 0.1);
            CHECK(deg_between(after.pose.rot[b], before.pose.rot[b]) < 0.1);
        }
    }
}

