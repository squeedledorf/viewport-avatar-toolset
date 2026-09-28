// Centre of mass and Auto-Balance (08 CM-1, CM-2).
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/balance.h"
#include "vats/edit.h"

using namespace vats;

namespace {

Vec3 ankle(const std::vector<Xform>& g, const char* side) { return g[skel().find(std::string("mAnkle") + side)].pos; }

int limb(const Rig& rig, const char* name) { return rig.find_limb(name); }

// Ten frames standing with both feet held by leg IK where they rest, the whole body leaning forward by deg
// about the point between the ankles.
Clip leaning(const Rig& rig, double deg) {
    Clip c;
    c.end_frame = 10;
    for (const char* leg : {"LegLeft", "LegRight"}) switch_to_ik(c, rig, 0, limb(rig, leg), nullptr);
    const std::vector<Xform> rest = skel().global_pose(Pose(skel().size()));
    const Vec3 a = (ankle(rest, "Left") + ankle(rest, "Right")) * 0.5, p = rest[skel().find("mPelvis")].pos;
    const Vec3 moved = a + Quat::axis_angle({0, 1, 0}, deg * kDegToRad).rotate(p - a);
    for (int f : {0, 10}) {
        key_euler(c, "mPelvis", f, {0, deg, 0});
        key_offset(c, "mPelvis", f, moved - p);
    }
    return c;
}

}  // namespace

TEST(balance_rest_pose_is_over_the_feet) {
    Rig rig(skel());
    Clip c;
    const std::vector<Xform> g = evaluate(rig, c, 0, nullptr).globals;
    Balance b = balance_of(skel(), g, nullptr);
    CHECK(b.contact);
    CHECK(b.inside());
    CHECK_EQ(b.support.size(), size_t(4));
    const Vec3 mid = (ankle(g, "Left") + ankle(g, "Right")) * 0.5;
    CHECK(std::hypot(b.ground.x - mid.x, b.ground.y - mid.y) < 0.03);
    CHECK(b.com.z > 0.9 && b.com.z < 1.2);  // about the hips' height
}

TEST(balance_forward_lean_falls_outside) {
    Rig rig(skel());
    Clip c = leaning(rig, 30);
    Balance b = balance_of(skel(), evaluate(rig, c, 5, nullptr).globals, nullptr);
    CHECK(b.contact);
    CHECK(!b.inside());
    CHECK(b.ground.x > b.support[0].x);  // in front
}

TEST(balance_no_contact_shows_nothing) {
    Rig rig(skel());
    Clip c;
    key_offset(c, "mPelvis", 0, {0, 0, 0.5});  // half a metre off the ground
    Balance b = balance_of(skel(), evaluate(rig, c, 0, nullptr).globals, nullptr);
    CHECK(!b.contact);
    CHECK(b.support.empty());
}

TEST(auto_balance_brings_the_lean_back_inside) {
    Rig rig(skel());
    Clip c = leaning(rig, 30);
    std::vector<std::vector<Xform>> before;
    for (int f = 0; f <= 10; ++f) before.push_back(evaluate(rig, c, f, nullptr).globals);
    AutoBalanceOptions opt;
    const std::string report = auto_balance(c, rig, opt);
    CHECK(report.find("still off balance") == std::string::npos);
    for (int f = 0; f <= 10; ++f) {
        const std::vector<Xform> g = evaluate(rig, c, f, nullptr).globals;
        Balance b = balance_of(skel(), g, nullptr);
        CHECK(b.inside());
        CHECK(b.margin > opt.margin - 0.005);
        for (const char* side : {"Left", "Right"}) CHECK((ankle(g, side) - ankle(before[f], side)).length() < 0.001);
        CHECK_NEAR(g[0].pos.z, before[f][0].pos.z, 1e-6);  // X and Y only
    }
    // The pelvis rotation (the lean itself) is untouched.
    CHECK_NEAR(curve_euler(c, "mPelvis", 5).y, 30, 1e-6);
}

TEST(auto_balance_counter_lean_turns_the_torso_against_the_hips) {
    Rig rig(skel());
    Clip c = leaning(rig, 30), plain = c;
    const Vec3 start = curve_offset(c, "mPelvis", 5);
    AutoBalanceOptions opt;
    auto_balance(plain, rig, opt);
    opt.counter_lean = true;
    auto_balance(c, rig, opt);
    CHECK(balance_of(skel(), evaluate(rig, c, 5, nullptr).globals, nullptr).inside());
    CHECK(curve_euler(c, "mTorso", 5).y < -1);  // leans back, towards the feet
    CHECK((curve_offset(c, "mPelvis", 5) - start).length() < (curve_offset(plain, "mPelvis", 5) - start).length() - 0.01);
}

TEST(auto_balance_keeps_frames_outside_the_range) {
    Rig rig(skel());
    Clip c = leaning(rig, 30);
    c.end_frame = 24;
    const Vec3 lean = curve_offset(c, "mPelvis", 10);
    key_offset(c, "mPelvis", 0, lean + Vec3{-0.05, 0.02, 0});  // the hips sway, so the curves have shape
    key_offset(c, "mPelvis", 16, lean + Vec3{0.03, -0.02, 0});
    key_offset(c, "mPelvis", 24, lean);
    const Clip before = c;
    AutoBalanceOptions opt;
    opt.from = 6, opt.to = 11;
    auto_balance(c, rig, opt);
    for (int f : {0, 1, 2, 3, 4, 5, 12, 13, 15, 16, 20, 24})
        CHECK((curve_offset(c, "mPelvis", f) - curve_offset(before, "mPelvis", f)).length() < 1e-9);
    for (double f : {2.5, 4.5, 12.5, 14.5}) CHECK((curve_offset(c, "mPelvis", f) - curve_offset(before, "mPelvis", f)).length() < 1e-9);
    CHECK(balance_of(skel(), evaluate(rig, c, 8, nullptr).globals, nullptr).inside());
}
