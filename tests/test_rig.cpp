#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/bvh.h"
#include "vats/edit.h"
#include "vats/rig.h"

using namespace vats;

namespace {

double angle_between(const Quat& a, const Quat& b) { return (a.conj() * b).angle(); }
double dist(const Vec3& a, const Vec3& b) { return (a - b).length(); }
bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

int node(const char* name) { return skel().find(name); }

// A bent FK pose of the limb at frame 0: root and end turned freely, mid bent about its hinge.
Clip bent_limb(const Rig& rig, int limb, double bend_deg) {
    const LimbInfo& l = rig.limbs()[limb];
    Clip c;
    key_euler(c, skel()[l.root].name, 0, {20, -30, 40});
    key_rotation(c, skel()[l.mid].name, 0, Quat::axis_angle(l.hinge, bend_deg * kDegToRad));
    key_euler(c, skel()[l.end].name, 0, {10, 20, 30});
    return c;
}

// A clip whose hips travel and turn over 30 frames, with an arm moving under them.
Clip moving_hips() {
    Clip c;
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 30, {0.3, -0.2, -0.1});
    key_euler(c, "mPelvis", 0, {0, 0, 0});
    key_euler(c, "mPelvis", 30, {10, 20, 45});
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 30, {0, 40, 0});
    return c;
}

// Hips that sway within an arm's reach, and a left arm bent at the elbow, so a held wrist stays reachable:
// a pin on a limb end holds it through the limb's IK (a straight arm cannot follow anything).
Clip pin_hips() {
    Clip c;
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 30, {0.06, -0.04, -0.05});
    key_euler(c, "mPelvis", 0, {0, 0, 0});
    key_euler(c, "mPelvis", 30, {3, 5, 12});
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 30, {0, 20, 0});
    key_euler(c, "mElbowLeft", 0, {0, 0, -100});
    return c;
}

}  // namespace

TEST(rig_limb_table) {
    Rig rig(skel());
    CHECK_EQ(rig.limbs().size(), size_t(19));
    int arm = rig.find_limb("ArmLeft");
    CHECK(arm >= 0);
    CHECK_EQ(rig.limbs()[arm].label, std::string("Left Arm"));
    CHECK_EQ(rig.limb_of_bone(node("mElbowLeft")), arm);
    CHECK_EQ(rig.limb_of_bone(node("mWristLeft")), arm);
    CHECK_EQ(rig.limb_of_bone(node("mCollarLeft")), -1);
    int spine = rig.find_limb("Spine");
    CHECK(rig.limbs()[spine].spine);
    CHECK_EQ(rig.limb_of_bone(node("mChest")), spine);
    CHECK_EQ(rig.limb_of_bone(node("mNeck")), -1);  // only places the target
    int index = rig.find_limb("IndexRight");
    CHECK(rig.limbs()[index].finger);
    CHECK_EQ(rig.limbs()[index].label, std::string("Right Index"));
    CHECK_NEAR(rig.limbs()[index].hinge.length(), 1.0, 1e-12);
    CHECK_EQ(rig.find_limb("HindLegRight") >= 0, true);
    CHECK_EQ(rig.find_limb("WingLeft") >= 0, true);
    CHECK_EQ(rig.find_limb("Tail"), -1);
}

TEST(ik_reachable_target) {
    Rig rig(skel());
    for (const char* name : {"ArmLeft", "ArmRight", "LegLeft", "LegRight", "HindLegLeft", "WingRight", "IndexLeft",
                             "ThumbRight"}) {
        int limb = rig.find_limb(name);
        const LimbInfo& l = rig.limbs()[limb];
        Evaluation fk = evaluate(rig, bent_limb(rig, limb, 60), 0, nullptr);
        Xform target = fk.globals[l.end];
        target.rot = (Quat::axis_angle({1, 2, 3}, 0.7) * target.rot).normalized();
        Vec3 pole = derive_pole(rig, limb, fk.globals) + Vec3{0.01, -0.02, 0.03};

        Clip c;
        key_blend(c, rig, 0, limb, 1);
        key_limb_target(c, rig, 0, limb, target, nullptr);
        key_limb_pole(c, rig, 0, limb, pole, nullptr);
        Evaluation ev = evaluate(rig, c, 0, nullptr);
        CHECK(ev.limbs[limb].ik_on && ev.limbs[limb].uses_ik);
        CHECK(dist(ev.globals[l.end].pos, target.pos) < 1e-4);
        CHECK(angle_between(ev.globals[l.end].rot, target.rot) < 1e-6);
        CHECK(dist(ev.limbs[limb].pole, pole) < 1e-9);
        // The mid joint is on the pole's side of the root-target line.
        Vec3 a = ev.globals[l.root].pos, u = (target.pos - a).normalized();
        auto side = [&](const Vec3& p) { return (p - a) - u * (p - a).dot(u); };
        CHECK(side(ev.globals[l.mid].pos).dot(side(pole)) > 0);
    }
}

TEST(ik_unreachable_target_clamps) {
    Rig rig(skel());
    int limb = rig.find_limb("LegLeft");
    const LimbInfo& l = rig.limbs()[limb];
    Evaluation rest = evaluate(rig, Clip{}, 0, nullptr);
    Vec3 a = rest.globals[l.root].pos;
    double reach = skel()[l.mid].pos.length() + skel()[l.end].pos.length();
    for (Vec3 t : {a + Vec3{0.3, 0.2, -10}, a, a + Vec3{0, 0, -1e-7}}) {
        Clip c;
        key_blend(c, rig, 0, limb, 1);
        key_limb_target(c, rig, 0, limb, {Quat{}, t}, nullptr);
        Evaluation ev = evaluate(rig, c, 0, nullptr);
        for (int i : {l.root, l.mid, l.end}) {
            CHECK(finite(ev.globals[i].pos));
            CHECK(std::isfinite(ev.pose.rot[i].w));
        }
        double d = dist(ev.globals[l.end].pos, a);
        CHECK(d <= reach * 0.9999 + 1e-6);
        if (dist(t, a) > reach) {
            CHECK(d > reach * 0.99);  // the leg's rest pose is slightly bent: its full reach is a bit short
            CHECK((ev.globals[l.end].pos - a).normalized().dot((t - a).normalized()) > 0.99999);
        }
    }
}

TEST(ik_spine_leans_toward_target) {
    Rig rig(skel());
    int spine = rig.find_limb("Spine");
    Evaluation rest = evaluate(rig, Clip{}, 0, nullptr);
    Xform target{Quat::axis_angle({0, 1, 0}, 0.3), rest.globals[node("mNeck")].pos + Vec3{0.08, 0.05, -0.02}};
    Clip c;
    key_blend(c, rig, 0, spine, 1);
    key_limb_target(c, rig, 0, spine, target, nullptr);
    Evaluation ev = evaluate(rig, c, 0, nullptr);
    CHECK(angle_between(ev.pose.rot[node("mTorso")], ev.pose.rot[node("mChest")]) < 1e-9);
    CHECK(dist(ev.globals[node("mNeck")].pos, target.pos) < dist(rest.globals[node("mNeck")].pos, target.pos));
    CHECK(finite(ev.globals[node("mHead")].pos));
}

TEST(ik_switch_round_trip_keeps_pose) {
    Rig rig(skel());
    for (const char* name : {"ArmLeft", "LegRight", "MiddleLeft"}) {
        int limb = rig.find_limb(name);
        const LimbInfo& l = rig.limbs()[limb];
        Clip c = bent_limb(rig, limb, 45);
        key_euler(c, skel()[l.root].name, 20, {-10, 5, 0});
        key_euler(c, "mPelvis", 20, {0, 0, 30});
        Evaluation before = evaluate(rig, c, 10, nullptr);

        switch_to_ik(c, rig, 10, limb, nullptr);
        CHECK(uses_ik(c, l));
        Evaluation ik = evaluate(rig, c, 10, nullptr);
        CHECK(ik.limbs[limb].ik_on);
        CHECK(!evaluate(rig, c, 9, nullptr).limbs[limb].ik_on);  // AM-54: FK before the switch
        switch_to_fk(c, rig, 10, limb, nullptr);
        Evaluation after = evaluate(rig, c, 10, nullptr);
        CHECK(!after.limbs[limb].ik_on);
        for (int i : {l.root, l.mid, l.end}) {
            CHECK(angle_between(ik.pose.rot[i], before.pose.rot[i]) < 1e-4);
            CHECK(angle_between(after.pose.rot[i], before.pose.rot[i]) < 1e-4);
        }
    }
}

TEST(ik_blend_keys_are_stepped) {
    Rig rig(skel());
    Clip c;
    key_blend(c, rig, 12, 0, 1);
    const FCurve& b = c.curves.at("ik.ArmLeft").at("blend");
    CHECK_EQ(b.keys.size(), size_t(2));
    CHECK_EQ(b.keys[0].value, 0.0);
    CHECK(b.keys[0].interp == Interp::Constant && b.keys[1].interp == Interp::Constant);
    CHECK_EQ(b.evaluate(11.9), 0.0);
    CHECK_EQ(b.evaluate(12), 1.0);
}

TEST(pin_world_holds_while_hips_move) {
    Rig rig(skel());
    Clip c = pin_hips();
    int wrist = node("mWristLeft"), head = node("mHead");
    std::string why;
    CHECK(pin_here(c, rig, 0, wrist, -1, nullptr, why));
    CHECK_EQ(c.pins.size(), size_t(1));
    CHECK_EQ(c.pins[0].via, std::string("mWristLeft"));
    CHECK_EQ(c.pins[0].to, -1);
    CHECK_EQ(pin_at(c, rig, wrist, 7), 0);
    Vec3 held = evaluate(rig, pin_hips(), 0, nullptr).globals[wrist].pos;
    for (int f = 0; f <= 30; f += 3) CHECK(dist(evaluate(rig, c, f, nullptr).globals[wrist].pos, held) < 1e-4);

    // A pin to a bone rides that bone.
    Clip d = pin_hips();
    CHECK(pin_here(d, rig, 0, wrist, head, nullptr, why));
    Evaluation e0 = evaluate(rig, d, 0, nullptr);
    Xform rel = e0.globals[head].inverse() * e0.globals[wrist];
    for (int f = 0; f <= 30; f += 5) {
        Evaluation e = evaluate(rig, d, f, nullptr);
        Xform r = e.globals[head].inverse() * e.globals[wrist];
        CHECK(dist(r.pos, rel.pos) < 1e-4);
        CHECK(angle_between(r.rot, rel.rot) < 1e-4);
    }

    // Holding also works against a body shape.
    Clip m = pin_hips();
    const Shape* male = &skel().male_shape();
    CHECK(pin_here(m, rig, 0, wrist, -1, male, why));
    Vec3 mh = evaluate(rig, pin_hips(), 0, male).globals[wrist].pos;
    CHECK(dist(evaluate(rig, m, 30, male).globals[wrist].pos, mh) < 1e-4);
}

// A bound limb end drags its whole limb along through IK: two-handed recoil (the left hand bound to the
// right) keeps the grip, the left elbow follows, and no bone stretches or detaches.
TEST(pin_on_limb_end_moves_the_limb) {
    Rig rig(skel());
    Clip c;
    c.end_frame = 10;
    key_euler(c, "mShoulderRight", 0, {0, 0, -70});
    key_euler(c, "mElbowRight", 0, {0, 0, 80});
    key_euler(c, "mShoulderLeft", 0, {0, 0, 60});
    key_euler(c, "mElbowLeft", 0, {0, 0, -80});
    key_euler(c, "mShoulderRight", 10, {0, -35, -70});  // the kick
    const int lw = node("mWristLeft"), rw = node("mWristRight"), le = node("mElbowLeft"), ls = node("mShoulderLeft");
    Evaluation e0 = evaluate(rig, c, 0, nullptr);
    const Xform grip = e0.globals[rw].inverse() * e0.globals[lw];
    const double upper = dist(e0.globals[ls].pos, e0.globals[le].pos), fore = dist(e0.globals[le].pos, e0.globals[lw].pos);
    std::string why;
    CHECK(pin_here(c, rig, 0, lw, rw, nullptr, why));
    CHECK_EQ(pin_limb(rig, c.pins[0]), rig.find_limb("ArmLeft"));
    CHECK(dist(evaluate(rig, c, 0, nullptr).globals[le].pos, e0.globals[le].pos) < 1e-6);  // no jump when pinned
    for (int f = 0; f <= 10; ++f) {
        Evaluation e = evaluate(rig, c, f, nullptr);
        Xform r = e.globals[rw].inverse() * e.globals[lw];
        CHECK(dist(r.pos, grip.pos) < 1e-3);
        CHECK(angle_between(r.rot, grip.rot) < 1e-3);
        CHECK(std::fabs(dist(e.globals[le].pos, e.globals[lw].pos) - fore) < 1e-4);
        CHECK(std::fabs(dist(e.globals[ls].pos, e.globals[le].pos) - upper) < 1e-4);
        CHECK(e.pose.offset[lw] == Vec3{});  // the wrist is turned, never slid
    }
    CHECK(dist(evaluate(rig, c, 10, nullptr).globals[le].pos, e0.globals[le].pos) > 0.05);  // the elbow moved

    // Exports as rotations on the whole arm, with no wrist position track.
    AnimExportOptions opt;
    AnimExportResult r = export_anim(skel(), c, opt);
    CHECK(r.errors.empty());
    bool elbow = false;
    for (auto& j : r.file.joints) {
        elbow = elbow || j.name == "mElbowLeft";
        if (j.name == "mWristLeft") CHECK(j.pos.empty());
    }
    CHECK(elbow);
}

// An ankle held in the world while the hips drop: the knee bends and the foot stays put.
TEST(pin_ankle_bends_the_knee) {
    Rig rig(skel());
    Clip c;
    c.end_frame = 20;
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 20, {0, 0, -0.15});
    key_euler(c, "mKneeLeft", 0, {0, 5, 0});
    const int ankle = node("mAnkleLeft"), knee = node("mKneeLeft");
    Evaluation e0 = evaluate(rig, c, 0, nullptr);
    std::string why;
    CHECK(pin_here(c, rig, 0, ankle, -1, nullptr, why));
    Evaluation e = evaluate(rig, c, 20, nullptr);
    CHECK(dist(e.globals[ankle].pos, e0.globals[ankle].pos) < 1e-4);
    CHECK(angle_between(e.globals[ankle].rot, e0.globals[ankle].rot) < 1e-4);
    CHECK(angle_between(e.pose.rot[knee], e0.pose.rot[knee]) > 0.2);  // the knee bent to absorb the drop
}

TEST(pin_refusals) {
    Rig rig(skel());
    Clip c;
    std::string why;
    CHECK(!pin_here(c, rig, 0, 0, -1, nullptr, why));
    CHECK(!why.empty());
    CHECK(!pin_here(c, rig, 0, node("mShoulderLeft"), node("mWristLeft"), nullptr, why));  // rides itself
    CHECK(!pin_here(c, rig, 0, node("mShoulderLeft"), node("mShoulderLeft"), nullptr, why));
    CHECK(c.pins.empty());
    CHECK(!unpin_here(c, rig, 0, node("mShoulderLeft"), nullptr, why));
}

TEST(unpin_has_no_jump) {
    Rig rig(skel());
    Clip c = pin_hips();
    int wrist = node("mWristLeft");
    std::string why;
    CHECK(pin_here(c, rig, 5, wrist, -1, nullptr, why));
    CHECK(unpin_here(c, rig, 20, wrist, nullptr, why));
    CHECK_EQ(c.pins[0].to, 19);
    CHECK_EQ(c.pins[0].release_key, 20);
    CHECK_EQ(c.pins[0].start_key, 5);
    CHECK(has_key_at(c, "mWristLeft", 20) && has_key_at(c, "mWristLeft", 5));
    Evaluation a = evaluate(rig, c, 19, nullptr), b = evaluate(rig, c, 20, nullptr);
    CHECK(dist(a.globals[wrist].pos, b.globals[wrist].pos) < 1e-4);
    CHECK_EQ(pin_at(c, rig, wrist, 20), -1);

    // Unpinning at the first frame deletes the pin and its keys (E-11, AM-85).
    CHECK(unpin_here(c, rig, 5, wrist, nullptr, why));
    CHECK(c.pins.empty());
    CHECK(!has_key_at(c, "mWristLeft", 20) && !has_key_at(c, "mWristLeft", 5));
}

TEST(pin_split_and_band_drags) {
    Rig rig(skel());
    Clip c = pin_hips();
    int wrist = node("mWristLeft");
    std::string why;
    CHECK(pin_here(c, rig, 0, wrist, -1, nullptr, why));
    CHECK(unpin_here(c, rig, 20, wrist, nullptr, why));
    // Pinning again inside the pin splits it; the new pin inherits the end and the release key.
    CHECK(pin_here(c, rig, 10, wrist, -1, nullptr, why));
    CHECK_EQ(c.pins.size(), size_t(2));
    CHECK_EQ(c.pins[0].to, 9);
    CHECK_EQ(c.pins[0].release_key, -1);
    CHECK_EQ(c.pins[1].from, 10);
    CHECK_EQ(c.pins[1].to, 19);
    CHECK_EQ(c.pins[1].release_key, 20);
    Evaluation a = evaluate(rig, c, 9, nullptr), b = evaluate(rig, c, 10, nullptr);
    CHECK(dist(a.globals[wrist].pos, b.globals[wrist].pos) < 1e-4);
    Evaluation r19 = evaluate(rig, c, 19, nullptr), r20 = evaluate(rig, c, 20, nullptr);
    CHECK(dist(r19.globals[wrist].pos, r20.globals[wrist].pos) < 1e-4);

    // Band drags stay clear of the neighbouring pin (E-18).
    move_pin_end(c, rig, 0, 15, nullptr);
    CHECK_EQ(c.pins[0].to, 9);
    move_pin_start(c, rig, 1, 3, nullptr);
    CHECK_EQ(c.pins[1].from, 10);
    move_pin_start(c, rig, 1, 12, nullptr);
    CHECK_EQ(c.pins[1].from, 12);
    CHECK_EQ(c.pins[1].start_key, 12);
    // Moving the end re-keys the release, still without a jump.
    move_pin_end(c, rig, 1, 25, nullptr);
    CHECK_EQ(c.pins[1].to, 25);
    CHECK_EQ(c.pins[1].release_key, 26);
    CHECK(!has_key_at(c, "mWristLeft", 20));
    Evaluation r25 = evaluate(rig, c, 25, nullptr), r26 = evaluate(rig, c, 26, nullptr);
    CHECK(dist(r25.globals[wrist].pos, r26.globals[wrist].pos) < 1e-4);
    // Past the last frame is open-ended.
    move_pin_end(c, rig, 1, c.end_frame + 5, nullptr);
    CHECK_EQ(c.pins[1].to, -1);
    CHECK_EQ(c.pins[1].release_key, -1);
    CHECK(!has_key_at(c, "mWristLeft", 26));

    delete_pin(c, rig, 1);
    delete_pin(c, rig, 0);
    CHECK(c.pins.empty());
    CHECK(!has_key_at(c, "mWristLeft", 12));
}

TEST(pinned_point_keys_its_offset) {
    Rig rig(skel());
    Clip c = pin_hips();
    int wrist = node("mWristLeft");
    std::string why;
    CHECK(pin_here(c, rig, 0, wrist, -1, nullptr, why));
    Xform want = evaluate(rig, c, 10, nullptr).globals[wrist];
    want.pos += Vec3{0, 0, 0.1};
    want.rot = (Quat::axis_angle({0, 0, 1}, 0.4) * want.rot).normalized();
    CHECK(key_pinned_point(c, rig, 10, wrist, want, nullptr));
    CHECK(c.curves.count("pin:mWristLeft"));
    CHECK(!has_key_at(c, "mWristLeft", 10));
    Evaluation ev = evaluate(rig, c, 10, nullptr);
    CHECK(dist(ev.globals[wrist].pos, want.pos) < 1e-4);
    CHECK(angle_between(ev.globals[wrist].rot, want.rot) < 1e-4);
    CHECK(!key_pinned_point(c, rig, 10, node("mElbowLeft"), want, nullptr));
    delete_pin(c, rig, 0);
    CHECK(!c.curves.count("pin:mWristLeft"));  // no other pin on that joint
}

TEST(follow_bake_keep_offset) {
    Rig rig(skel());
    Clip c = moving_hips();
    key_euler(c, "mShoulderRight", 0, {0, 0, 0});
    key_euler(c, "mShoulderRight", 30, {30, -20, 10});
    int target = node("mWristRight"), follower = node("Left Hand"), keyed = node("mWristLeft");
    std::string why;
    Evaluation e0 = evaluate(rig, c, 5, nullptr);
    Xform rel = e0.globals[target].inverse() * e0.globals[follower];
    CHECK(follow_bake(c, rig, target, follower, 5, 25, true, nullptr, why));
    CHECK(has_key_at(c, "mWristLeft", 5) && has_key_at(c, "mWristLeft", 25));
    CHECK(!has_key_at(c, "mWristLeft", 26));
    for (int f = 5; f <= 25; ++f) {
        Evaluation e = evaluate(rig, c, f, nullptr);
        Xform r = e.globals[target].inverse() * e.globals[follower];
        CHECK(dist(r.pos, rel.pos) < 1e-6);
        CHECK(angle_between(r.rot, rel.rot) < 1e-6);
    }
    // Without keep-offset the follower snaps onto the target.
    CHECK(follow_bake(c, rig, target, keyed, 26, 26, false, nullptr, why));
    Evaluation e = evaluate(rig, c, 26, nullptr);
    CHECK(dist(e.globals[keyed].pos, e.globals[target].pos) < 1e-6);

    CHECK(!follow_bake(c, rig, target, 0, 0, 5, true, nullptr, why));
    CHECK(!follow_bake(c, rig, target, node("mChest"), 0, 5, true, nullptr, why));
    CHECK(!follow_bake(c, rig, target, target, 0, 5, true, nullptr, why));
}

TEST(export_bakes_ik_and_pins) {
    Rig rig(skel());
    int limb = rig.find_limb("ArmLeft");
    const LimbInfo& l = rig.limbs()[limb];
    Clip c = moving_hips();
    c.curves.erase("mShoulderLeft");
    Evaluation rest = evaluate(rig, c, 0, nullptr);
    Xform t0 = rest.globals[l.end];
    t0.pos += Vec3{0.2, -0.1, 0.1};
    key_blend(c, rig, 0, limb, 1);
    key_limb_target(c, rig, 0, limb, t0, nullptr);
    t0.pos += Vec3{0.1, 0.1, 0.2};
    key_limb_target(c, rig, 30, limb, t0, nullptr);
    std::string why;
    CHECK(pin_here(c, rig, 0, node("mAnkleRight"), -1, nullptr, why));

    AnimExportOptions opt;
    opt.reduce_rot_deg = opt.reduce_pos_m = 0;
    AnimExportResult r = export_anim(skel(), c, opt);
    CHECK(r.errors.empty());
    bool shoulder = false, elbow = false, wrist = false, ankle = false;
    for (auto& j : r.file.joints) {
        int i = skel().find(j.name);
        shoulder = shoulder || i == l.root;
        elbow = elbow || i == l.mid;
        wrist = wrist || i == l.end;
        if (i == node("mAnkleRight")) {
            ankle = true;
            CHECK(j.pos.empty());  // held through the leg's IK: rotations only, no detached ankle
        }
        CHECK_EQ(j.rot.size(), size_t(31));
        for (int f = 0; f <= 30 && f < static_cast<int>(j.rot.size()); f += 5) {
            Evaluation ev = evaluate(rig, c, f, nullptr);
            Quat want = skel()[i].rest * ev.pose.rot[i];
            CHECK(angle_between(decode_rotation(j.rot[f]), want) < 5e-4);
            if (!j.pos.empty()) {
                Vec3 p = ev.pose.offset[i] + (i == 0 ? Vec3{} : skel()[i].pos);
                CHECK(dist(decode_position(j.pos[f]), p) < 5e-4);
            }
        }
    }
    CHECK(shoulder && elbow && wrist && ankle);

    BvhExportResult b = export_bvh(skel(), c);
    CHECK(b.text.find("JOINT mElbowLeft") != std::string::npos);
    CHECK(b.text.find("JOINT mAnkleRight") != std::string::npos);
}

// A three-bone leg that is not planar about its hinge: the knee sits 0.3 m to the side of the hip.
const Skeleton& skew_leg() {
    static Skeleton s = [] {
        Skeleton k;
        std::string err;
        const char* xml = R"(<linden_skeleton num_bones="4" num_collision_volumes="0" version="2.0">
<bone name="mPelvis" pos="0 0 1" rot="0 0 0" end="0 0 0.1" scale="1 1 1" group="Torso" support="base">
<bone name="mHipLeft" pos="0 0 0" rot="0 0 0" end="0 0.3 -0.4" scale="1 1 1" group="Legs" support="base">
<bone name="mKneeLeft" pos="0 0.3 -0.4" rot="0 0 0" end="0 0 -0.5" scale="1 1 1" group="Legs" support="base">
<bone name="mAnkleLeft" pos="0 0 -0.5" rot="0 0 0" end="0.1 0 0" scale="1 1 1" group="Legs" support="base"/>
</bone></bone></bone></linden_skeleton>)";
        if (!k.load(xml, "<linden_avatar><skeleton/></linden_avatar>", err)) {
            std::fprintf(stderr, "cannot load the test leg: %s\n", err.c_str());
            std::exit(2);
        }
        return k;
    }();
    return s;
}

// 02 section 3.7 steps 1-7 worked by hand for skew_leg: e = (0, 0.3, -0.4), w = (0, 0, -0.5), hinge +y.
// A target sqrt(0.5) straight below the hip needs |e + rot(y, b) w|^2 = 0.5 + 0.4 cos b = 0.5: b = 90 deg.
// Then x1 = (-0.5, 0.3, -0.4) / sqrt(0.5), y1 = x1 x y = (0.4, 0, -0.5) / sqrt(0.41), z1 = x1 x y1; with the
// pole along +x, x2 = -z, y2 = +x, z2 = -y. The knee's F1 coordinates (e.x1, e.y1, e.z1) =
// (sqrt(0.5) / 2, 0.2 / sqrt(0.41), -0.075 / sqrt(0.205)) put it at hip + (0.2 / sqrt(0.41),
// 0.075 / sqrt(0.205), -sqrt(0.5) / 2).
TEST(ik_literal_matches_section_3_7) {
    Rig rig(skew_leg());
    int limb = rig.find_limb("LegLeft");
    CHECK_EQ(rig.limbs().size(), size_t(1));
    const LimbInfo& l = rig.limbs()[limb];
    Vec3 a = skew_leg().global_pose(Pose(skew_leg().size()))[l.root].pos;
    Xform target{Quat::axis_angle({1, -2, 0.5}, 0.8), a + Vec3{0, 0, -std::sqrt(0.5)}};
    for (IkSolve mode : {IkSolve::Literal, IkSolve::VATs}) {
        Clip c;
        c.ik_solve = mode;
        key_blend(c, rig, 0, limb, 1);
        key_limb_target(c, rig, 0, limb, target, nullptr);
        key_limb_pole(c, rig, 0, limb, a + Vec3{1, 0, 0}, nullptr);
        Evaluation ev = evaluate(rig, c, 0, nullptr);
        CHECK(dist(ev.globals[l.end].pos, target.pos) < 1e-9);
        CHECK(angle_between(ev.globals[l.end].rot, target.rot) < 1e-9);
        CHECK(angle_between(ev.pose.rot[l.mid], Quat::axis_angle({0, 1, 0}, kPi / 2)) < 1e-9);
        // VATs puts the knee straight towards the pole instead: hip + (sqrt(0.125), 0, -sqrt(0.125)).
        Vec3 knee = mode == IkSolve::Literal
                        ? Vec3{0.2 / std::sqrt(0.41), 0.075 / std::sqrt(0.205), -std::sqrt(0.5) / 2}
                        : Vec3{std::sqrt(0.125), 0, -std::sqrt(0.125)};
        CHECK(dist(ev.globals[l.mid].pos, a + knee) < 1e-9);
    }
}

// Switch to IK keys a pole that the two-bone solve turns back into the FK pose, in both modes and even for
// straight chains. Only AM-59's reach clamp moves a fully straight arm or finger: at most 0.028 rad.
TEST(ik_switch_keeps_pose_at_any_bend) {
    Rig rig(skel());
    for (IkSolve mode : {IkSolve::VATs, IkSolve::Literal})
        for (size_t k = 0; k < rig.limbs().size(); ++k) {
            int limb = static_cast<int>(k);
            const LimbInfo& l = rig.limbs()[limb];
            if (l.spine) continue;
            for (double bend : {0.0, 5.0, 10.0, 14.0, 16.0, 45.0, 90.0, 170.0}) {
                Clip c = bent_limb(rig, limb, bend);
                c.ik_solve = mode;
                key_euler(c, "mPelvis", 0, {0, 10, 30});
                Evaluation before = evaluate(rig, c, 0, nullptr);
                switch_to_ik(c, rig, 0, limb, nullptr);
                Evaluation ik = evaluate(rig, c, 0, nullptr);
                CHECK(ik.limbs[limb].ik_on);
                switch_to_fk(c, rig, 0, limb, nullptr);
                Evaluation after = evaluate(rig, c, 0, nullptr);
                double jump = 0;
                for (int i : {l.root, l.mid, l.end}) {
                    jump = std::max(jump, angle_between(ik.pose.rot[i], before.pose.rot[i]));
                    CHECK(angle_between(after.pose.rot[i], ik.pose.rot[i]) < 1e-4);
                }
                CHECK(jump < (bend > 0 ? 1e-4 : 0.03));
            }
        }
}
