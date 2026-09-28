#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/project.h"
#include "vats/ragdoll.h"

using namespace vats;

namespace {

const Rig& rig() {
    static Rig r(skel());
    return r;
}

double deg(const Quat& q) { return q.angle() * kRadToDeg; }

// Standing, leaning 10 degrees forward, then let go from frame 0.
Clip fall_clip(int end = 90) {
    Clip c;
    c.end_frame = end;
    c.loop_out = end;
    key_euler(c, "mPelvis", 0, {0, 10, 0});
    Ragdoll r;
    r.start = 0;
    r.frames = end;
    r.blend_in = 0;
    c.ragdoll = r;
    return c;
}

bool same_pose(const Pose& a, const Pose& b, int n) {
    return deg(a.rot[n].conj() * b.rot[n]) < 1e-9 && (a.offset[n] - b.offset[n]).length() < 1e-12;
}

}  // namespace

TEST(ragdoll_deterministic) {
    Clip a = fall_clip(40), b = a;
    bake_ragdoll(a, rig(), nullptr);
    bake_ragdoll(b, rig(), nullptr);
    CHECK(a == b);
}

TEST(ragdoll_standing_falls_and_rests) {
    Clip c = fall_clip();
    auto frames = simulate_ragdoll(rig(), c, nullptr);
    CHECK_EQ(int(frames.size()), c.end_frame + 1);
    const int pelvis = skel().find("mPelvis");
    double worst_limit = 0, lowest = 1e9;
    for (const Pose& p : frames) {
        worst_limit = std::max(worst_limit, ragdoll_limit_excess(skel(), p));
        auto g = skel().global_pose(p);
        for (const std::string& name : ragdoll_joint_names()) lowest = std::min(lowest, g[skel().find(name)].pos.z);
    }
    CHECK(worst_limit < 0.01);  // the written pose is always inside the joint limits
    CHECK(lowest > -0.02);    // nothing sinks below the ground
    auto first = skel().global_pose(frames[0]), last = skel().global_pose(frames.back());
    CHECK(first[pelvis].pos.z > 0.9);
    CHECK(last[pelvis].pos.z < 0.35);  // it fell
    // It came to rest: the last five frames barely move.
    const Pose &a = frames[frames.size() - 6], &b = frames.back();
    for (const std::string& name : ragdoll_joint_names()) {
        int n = skel().find(name);
        CHECK(deg(a.rot[n].conj() * b.rot[n]) < 2);
    }
    CHECK((a.offset[pelvis] - b.offset[pelvis]).length() < 0.01);
}

TEST(ragdoll_selected_arm_leaves_the_rest_alone) {
    Clip c;
    c.end_frame = 45;
    key_euler(c, "mTorso", 0, {0, 0, 0});
    key_euler(c, "mTorso", 30, {0, 0, 30});
    key_euler(c, "mNeck", 20, {10, 0, 0});
    Ragdoll r;
    r.whole_body = false;
    r.bones = {"mShoulderLeft"};
    r.start = 5;
    r.frames = 40;
    r.blend_in = 0;
    c.ragdoll = r;
    auto frames = simulate_ragdoll(rig(), c, nullptr);
    const std::vector<std::string> arm = {"mShoulderLeft", "mElbowLeft", "mWristLeft"};
    double moved = 0;
    for (int f = 0; f <= c.end_frame; ++f) {
        Pose anim = evaluate(rig(), c, f, nullptr).pose;
        for (int n = 0; n < skel().size(); ++n) {
            bool in_arm = std::find(arm.begin(), arm.end(), skel()[n].name) != arm.end();
            if (!in_arm) CHECK(same_pose(frames[f], anim, n));
            else moved = std::max(moved, deg(frames[f].rot[n].conj() * anim.rot[n]));
        }
    }
    CHECK(moved > 20);  // the arm dropped from the T-pose
}

TEST(ragdoll_blend_in_starts_at_the_pose) {
    Clip c = fall_clip(40);
    c.ragdoll->start = 10;
    c.ragdoll->blend_in = 6;
    auto frames = simulate_ragdoll(rig(), c, nullptr);
    for (int f = 0; f <= 10; ++f) {
        Pose anim = evaluate(rig(), c, f, nullptr).pose;
        for (int n = 0; n < skel().size(); ++n) CHECK(same_pose(frames[f], anim, n));
    }
    // Held close to the animation just after the start, then free.
    const int pelvis = skel().find("mPelvis");
    Pose anim11 = evaluate(rig(), c, 11, nullptr).pose;
    CHECK((frames[11].offset[pelvis] - anim11.offset[pelvis]).length() < 0.02);
    CHECK((frames[40].offset[pelvis] - anim11.offset[pelvis]).length() > 0.2);
}

TEST(ragdoll_bake_matches_simulation) {
    Clip c = fall_clip(45);
    auto frames = simulate_ragdoll(rig(), c, nullptr);
    Clip baked = c;
    bake_ragdoll(baked, rig(), nullptr);
    CHECK(baked.ragdoll->baked);
    const int pelvis = skel().find("mPelvis");
    for (int f = 0; f <= c.end_frame; ++f) {
        Pose e = evaluate(rig(), baked, f, nullptr).pose;
        for (const std::string& name : ragdoll_joint_names()) {
            int n = skel().find(name);
            // Within the reducer's 0.1 degrees, plus the gap between its nlerp measure and the curves' linear
            // Euler keys on big swings.
            CHECK(deg(e.rot[n].conj() * frames[f].rot[n]) < 0.5);
        }
        CHECK((e.offset[pelvis] - frames[f].offset[pelvis]).length() < 0.001);
    }
    Clip again = baked;  // re-baking starts from the source tracks
    bake_ragdoll(again, rig(), nullptr);
    CHECK(again == baked);
    unbake_ragdoll(again, skel());
    CHECK(again.curves == c.curves);
    CHECK(!again.ragdoll->baked);
}

// Limbs in IK or pinned go FK over the fall, so the baked keys show there, and come back after it; Clear puts all
// of it back.
TEST(ragdoll_bake_frees_ik_and_pinned_limbs) {
    Clip c = fall_clip(45);
    c.ragdoll->start = 10, c.ragdoll->frames = 20, c.ragdoll->blend_out = 5;
    int arm = -1;
    for (int l = 0; l < int(rig().limbs().size()); ++l)
        if (rig().limbs()[l].name == "ArmLeft") arm = l;
    CHECK(arm >= 0);
    switch_to_ik(c, rig(), 0, arm, nullptr);
    std::string why;
    CHECK(pin_here(c, rig(), 0, skel().find("mWristRight"), -1, nullptr, why));
    const Clip before = c;
    auto frames = simulate_ragdoll(rig(), c, nullptr);
    Clip baked = c;
    bake_ragdoll(baked, rig(), nullptr);
    const char* arms[] = {"mShoulderLeft", "mElbowLeft", "mWristLeft", "mShoulderRight", "mElbowRight", "mWristRight"};
    for (int f = 0; f <= c.end_frame; ++f) {
        Evaluation e = evaluate(rig(), baked, f, nullptr);
        for (const char* name : arms) {
            int n = skel().find(name);
            CHECK(deg(e.pose.rot[n].conj() * frames[f].rot[n]) < 0.5);  // the fall shows, nothing jumps
        }
        CHECK(e.limbs[arm].ik_on == (f < 10 || f > 30));  // IK back after the fall
    }
    CHECK(baked.pins.size() == 2);  // cut round the fall
    Clip again = baked;
    bake_ragdoll(again, rig(), nullptr);
    CHECK(again == baked);
    unbake_ragdoll(again, skel());
    CHECK(again.curves == before.curves);
    CHECK(again.pins == before.pins);
}

TEST(ragdoll_project_round_trip) {
    Project p;
    p.clip = fall_clip(30);
    p.clip.ragdoll->whole_body = false;
    p.clip.ragdoll->bones = {"mHipLeft", "mShoulderRight"};
    p.clip.ragdoll->stiffness = 0.2;
    p.clip.ragdoll->extra.set("future", 3);
    bake_ragdoll(p.clip, rig(), nullptr);
    Project q;
    std::string err;
    CHECK(load_project(save_project(p), q, err, ""));
    CHECK(q.clip.ragdoll == p.clip.ragdoll);
}

// A limp body let go standing upright pitches over and lies out (head well away from the feet) in the
// direction asked for, instead of dropping straight down into a crouch.
TEST(ragdoll_limp_standing_sprawls) {
    for (const char* dir : {"forward", "back", "left", "right", "random"}) {
        Clip c;
        c.end_frame = 90;
        Ragdoll r;
        r.start = 0;
        r.frames = 90;
        r.blend_in = 0;
        r.extra.set("fall_direction", dir);
        c.ragdoll = r;
        auto g = skel().global_pose(simulate_ragdoll(rig(), c, nullptr).back());
        const Vec3 head = g[skel().find("mHead")].pos, pelvis = g[skel().find("mPelvis")].pos;
        const Vec3 feet = (g[skel().find("mAnkleLeft")].pos + g[skel().find("mAnkleRight")].pos) * 0.5;
        CHECK(pelvis.z < 0.35);
        CHECK(head.z < 0.4);
        CHECK(std::hypot(head.x - feet.x, head.y - feet.y) > 0.9);  // a crouch keeps the head over the knees (~0.7)
    }
    // Forward means forward: the head ends up ahead of where the body stood.
    Clip c;
    c.end_frame = 60;
    c.ragdoll = Ragdoll{};
    c.ragdoll->blend_in = 0;
    auto g = skel().global_pose(simulate_ragdoll(rig(), c, nullptr).back());
    CHECK(g[skel().find("mHead")].pos.x > 0.8);
}

// The shoulder's limits against real ranges (flexion and abduction to overhead, extension about 60 degrees, horizontal
// abduction a little behind the shoulder line, about 90 degrees of turn either way). Known-good poses stay inside,
// known-bad ones are reported, on both arms. Euler as the Rotation fields; `turn` about the arm first (degrees).
TEST(ragdoll_shoulder_limits_known_poses) {
    struct P {
        const char* what;
        Vec3 euler;
        double turn;
        bool good;
    } poses[] = {
        {"T-pose", {0, 0, 0}, 0, true},
        {"hanging at the side", {-90, 0, 0}, 0, true},
        {"forward, raised from the side", {-90, -90, 0}, 0, true},
        {"forward just above horizontal", {-78, -100, 0}, 0, true},
        {"forward and up 40", {-78, -130, 0}, 0, true},
        {"overhead by flexion", {-90, -180, 0}, 0, true},
        {"overhead by abduction", {90, 0, 0}, 0, true},
        {"forward, palm down", {0, 0, -90}, 0, true},
        {"across the chest", {0, 0, -130}, 0, true},
        {"extension 50", {-90, 50, 0}, 0, true},
        {"hanging, turned out 80", {-90, 0, 0}, 80, true},
        {"hanging, turned in 80", {-90, 0, 0}, -80, true},
        {"forward, turned 70", {-90, -90, 0}, -70, true},
        {"hand behind the head", {60, 0, 0}, 0, true},
        {"extension 120", {-90, 120, 0}, 0, false},
        {"straight back, horizontal", {0, 0, 100}, 0, false},
        {"hanging, turned 180", {-90, 0, 0}, 180, false},
        {"T-pose, turned 150", {0, 0, 0}, 150, false},
        {"forward, turned 150 the other way", {-90, -90, 0}, -150, false},
        {"overhead and 60 across", {150, 0, 0}, 0, false},
    };
    for (const char* side : {"mShoulderLeft", "mShoulderRight"}) {
        const int n = skel().find(side);
        for (const P& p : poses) {
            Quat q = euler_to_quat(p.euler) * Quat::axis_angle({0, 1, 0}, p.turn * kDegToRad);
            if (side[9] == 'R') q = {q.w, -q.x, q.y, -q.z};  // mirrored across the body's middle
            Pose pose(skel().size());
            pose.rot[n] = q;
            double excess = 0;
            for (const LimitExcess& e : ragdoll_limit_excesses(skel(), pose, 0))
                if (e.node == n) excess = e.deg;
            if (p.good ? excess > 5 : excess < 8) std::fprintf(stderr, "  %s %s: %.1f degrees past\n", side, p.what, excess);
            CHECK(p.good ? excess <= 5 : excess >= 8);
        }
    }
}
