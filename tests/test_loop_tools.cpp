// Loop tools (08 LP-1..LP-4) and onion-skin frame choice (08 ON-1).
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/loop_tools.h"
#include "vats/onion.h"

using namespace vats;

namespace {

constexpr double kPi = 3.14159265358979323846;

// One second of a walk that does not loop: the hips travel 1 m forward with a sway, a hip swings but ends
// off its start, and the torso turns a whole turn plus 2 degrees.
Clip walk() {
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    for (int f = 0; f <= 30; ++f)
        key_offset(c, "mPelvis", f, {f / 30.0 + 0.02 * std::sin(2 * kPi * f / 15), 0.01 * std::sin(2 * kPi * f / 30), 0.03 * std::cos(2 * kPi * f / 15)});
    key_euler(c, "mHipLeft", 0, {0, -20, 0});
    key_euler(c, "mHipLeft", 15, {0, 20, 0});
    key_euler(c, "mHipLeft", 30, {0, -10, 0});
    key_euler(c, "mTorso", 0, {0, 0, 10});
    key_euler(c, "mTorso", 30, {0, 0, 372});
    return c;
}

double ch(const Clip& c, const char* track, const char* channel, double f) {
    return c.curves.at(track).at(channel).evaluate(f);
}

}  // namespace

TEST(loop_seamless_values_and_slopes) {
    Clip c = walk();
    CHECK(!loop_seam_jumps(c).empty());
    CHECK(make_loop_seamless(c, 0) > 0);
    CHECK(loop_seam_jumps(c).empty());
    CHECK(std::fabs(ch(c, "mHipLeft", "rot_y", 30) - ch(c, "mHipLeft", "rot_y", 0)) < 1e-6);
    // A whole turn is a seam too: the torso ends at 370, not back at 10.
    CHECK(std::fabs(ch(c, "mTorso", "rot_z", 30) - 370) < 1e-6);
    // No kink: the slope leaving frame 0 equals the slope arriving at frame 30.
    for (const char* t : {"mHipLeft", "mTorso"}) {
        const char* chn = std::string(t) == "mHipLeft" ? "rot_y" : "rot_z";
        const double s0 = (ch(c, t, chn, 0.01) - ch(c, t, chn, 0)) / 0.01;
        const double s1 = (ch(c, t, chn, 30) - ch(c, t, chn, 29.99)) / 0.01;
        CHECK(std::fabs(s0 - s1) < 0.05);
    }
}

TEST(loop_seamless_blend_keeps_the_middle) {
    Clip c = walk();
    const double mid = ch(c, "mHipLeft", "rot_y", 15);
    make_loop_seamless(c, 6);
    CHECK(loop_seam_jumps(c).empty());
    CHECK(std::fabs(ch(c, "mHipLeft", "rot_y", 15) - mid) < 1e-6);  // before the blend window: untouched
}

TEST(loop_remove_and_add_travel) {
    Clip c = walk();
    const Clip before = c;
    const Travel t = remove_travel(c);
    CHECK(std::fabs(t.vx - 1.0) < 1e-6);
    CHECK(std::fabs(ch(c, "mPelvis", "pos_x", 30) - ch(c, "mPelvis", "pos_x", 0)) < 0.001);
    for (double f : {7.0, 11.5, 22.0}) {  // the sway stays: only the straight-line travel is gone
        CHECK(std::fabs(ch(c, "mPelvis", "pos_x", f) - (ch(before, "mPelvis", "pos_x", f) - f / 30.0)) < 0.001);
        CHECK(std::fabs(ch(c, "mPelvis", "pos_z", f) - ch(before, "mPelvis", "pos_z", f)) < 1e-9);  // height untouched
    }
    add_travel(c, t);
    for (double f : {0.0, 7.0, 30.0})
        CHECK(std::fabs(ch(c, "mPelvis", "pos_x", f) - ch(before, "mPelvis", "pos_x", f)) < 1e-6);
}

TEST(loop_travel_carries_ik_targets) {
    // A planted foot's IK target has to move with the hips, or the leg stretches for where the foot was.
    Clip c = walk();
    FCurve& ik = c.curves["ik.LegLeft"]["pos_x"];
    ik.set_key(5, 0.3, Interp::Linear);
    ik.set_key(20, 0.3, Interp::Linear);
    FCurve& pole = c.curves["ik.LegLeft"]["pole_x"];
    pole.set_key(5, 0.5, Interp::Linear);
    pole.set_key(20, 0.5, Interp::Linear);
    const Clip before = c;
    remove_travel(c);
    for (double f : {5.0, 12.0, 20.0}) {  // moved by exactly the hips' travel removed there
        const double moved = ch(c, "mPelvis", "pos_x", f) - ch(before, "mPelvis", "pos_x", f);
        CHECK(std::fabs(ch(c, "ik.LegLeft", "pos_x", f) - ch(before, "ik.LegLeft", "pos_x", f) - moved) < 1e-3);
        CHECK(std::fabs(ch(c, "ik.LegLeft", "pole_x", f) - ch(before, "ik.LegLeft", "pole_x", f) - moved) < 1e-3);
    }
}

TEST(loop_seamless_keys_do_not_reshape_the_curve) {
    // Sparse automatic keys around the loop points: the keys Make Loop Seamless adds at loop-in and loop-out must
    // not reshape the curve before it measures the seam, or the ends still miss each other (they did, by 0.08).
    for (int blend : {0, 4, kBlendWholeLoop}) {
        Clip c;
        c.fps = 60, c.end_frame = 150, c.loop = true, c.loop_in = 1, c.loop_out = 62;
        FCurve& x = c.curves["ik.LegRight"]["pos_x"];
        x.set_key(0, -2.44), x.set_key(8, -2.69), x.set_key(40, -1.74), x.set_key(68, -2.61);
        FCurve& r = c.curves["mHipLeft"]["rot_y"];
        r.set_key(0, 5), r.set_key(8, -20), r.set_key(40, 30), r.set_key(68, 12);
        make_loop_seamless(c, blend);
        CHECK(loop_seam_jumps(c).empty());
        CHECK(std::fabs(x.evaluate(62) - x.evaluate(1)) < 1e-9);
    }
}

TEST(loop_seamless_whole_loop_spreads_the_correction) {
    // Motion capture: a key every frame and an end that misses the start by 6 degrees and arrives faster.
    Clip c;
    c.fps = 30, c.end_frame = 60, c.loop = true, c.loop_in = 0, c.loop_out = 60;
    FCurve& x = c.curves["mHipLeft"]["rot_y"];
    for (int f = 0; f <= 60; ++f) x.set_key(f, 20 * std::sin(f * std::acos(-1.0) / 30) + 0.1 * f, Interp::Linear);
    const Clip before = c;
    CHECK(make_loop_seamless(c, kBlendWholeLoop) == 1);
    CHECK(loop_seam_jumps(c).empty());
    // The slopes meet at the seam: leaving frame 0 as fast as arriving at frame 60.
    const double s0 = x.evaluate(1) - x.evaluate(0), s1 = x.evaluate(60) - x.evaluate(59);
    CHECK(std::fabs(s0 - s1) < 0.02);
    // No frame changes by more than a fraction of a degree from the one before it beyond what the motion did.
    double worst = 0;
    for (int f = 1; f <= 60; ++f) {
        const auto& y = before.curves.at("mHipLeft").at("rot_y");
        worst = std::max(worst, std::fabs((x.evaluate(f) - x.evaluate(f - 1)) - (y.evaluate(f) - y.evaluate(f - 1))));
    }
    CHECK(worst < 0.2);
}

TEST(loop_remove_path_keeps_a_pacing_take_in_place) {
    // Twenty seconds of pacing at a walk: 6 m one way, 6 m back, twice, with a 2 cm sway every step (0.5 s). Start and end
    // meet, so a straight line removes nothing; following the path keeps the hips over one spot.
    Clip c;
    c.fps = 30, c.end_frame = 600;
    const double pi = std::acos(-1.0);
    auto y = [&](double f) { return 3 * std::sin(2 * pi * f / 300) + 0.02 * std::sin(2 * pi * f / 15); };
    for (int f = 0; f <= 600; ++f) {
        c.curves["mPelvis"]["pos_y"].set_key(f, y(f), Interp::Linear);
        c.curves["mPelvis"]["pos_x"].set_key(f, 0, Interp::Linear);
    }
    FCurve& ik = c.curves["ik.LegLeft"]["pos_y"];
    for (int f = 0; f <= 600; f += 10) ik.set_key(f, y(f), Interp::Linear);
    FCurve& finger = c.curves["ik.IndexLeft"]["pos_y"];  // wrist space: never moved
    finger.set_key(0, 0.05, Interp::Linear);
    finger.set_key(600, 0.05, Interp::Linear);
    const Clip before = c;
    CHECK(remove_path(c, 0.5) > 1.0);
    double worst = 0;
    for (int f = 0; f <= 600; ++f) worst = std::max(worst, std::fabs(ch(c, "mPelvis", "pos_y", f)));
    CHECK(worst < 0.05);  // was 3 m
    // The planted target moved exactly as the hips did.
    for (double f : {40.0, 150.0, 460.0})
        CHECK(std::fabs((ch(c, "ik.LegLeft", "pos_y", f) - ch(before, "ik.LegLeft", "pos_y", f)) -
                        (ch(c, "mPelvis", "pos_y", f) - ch(before, "mPelvis", "pos_y", f))) < 1e-9);
    CHECK(std::fabs(ch(c, "ik.IndexLeft", "pos_y", 150) - 0.05) < 1e-12);

    // A pose held off-centre is not travel: it stays where it is.
    Clip held;
    held.fps = 30, held.end_frame = 90;
    held.curves["mPelvis"]["pos_y"].set_key(0, 0.2);
    held.curves["mPelvis"]["pos_y"].set_key(90, 0.2);
    remove_path(held);
    for (double f : {0.0, 45.0, 90.0}) CHECK(std::fabs(ch(held, "mPelvis", "pos_y", f) - 0.2) < 1e-9);
}

TEST(loop_move_to_origin) {
    Clip c = walk();
    remove_travel(c);
    for (auto& k : c.curves["mPelvis"]["pos_x"].keys) k.value += 2.5, k.ly += 2.5, k.ry += 2.5;  // recorded off to one side
    c.curves["ik.LegLeft"]["pos_x"].set_key(10, 2.8, Interp::Linear);
    c.pins.push_back(Pin{"mWristLeft", "mWristLeft", "", 0, 10});
    c.pins.back().pos = {2.6, 0.3, 1.0};
    const Vec3 m = center_on_origin(c);
    CHECK(std::fabs(m.x - 2.5) < 0.02);
    double sum = 0;
    for (int f = 0; f <= 30; ++f) sum += ch(c, "mPelvis", "pos_x", f);
    CHECK(std::fabs(sum / 31) < 1e-9);  // the average now sits on the origin
    CHECK(std::fabs(ch(c, "ik.LegLeft", "pos_x", 10) - (2.8 - m.x)) < 1e-9);
    CHECK(std::fabs(c.pins.back().pos.x - (2.6 - m.x)) < 1e-9);
    CHECK(std::fabs(c.pins.back().pos.z - 1.0) < 1e-12);  // height untouched
}

TEST(loop_cycle_offset_moves_values) {
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    FCurve& x = c.curves["mHipLeft"]["rot_y"];
    const double v[] = {0, 10, 25, 5, -15, -5, 0};  // seamless, keyed every 5 frames, linear
    for (int i = 0; i < 7; ++i) x.set_key(i * 5, v[i], Interp::Linear);
    const Clip before = c;
    CHECK(!cycle_offset(c, 0) && !cycle_offset(c, 30));
    CHECK(cycle_offset(c, 10));
    for (int g = 0; g <= 30; ++g) {
        const double old = before.curves.at("mHipLeft").at("rot_y").evaluate((g + 10) % 30);
        CHECK(std::fabs(x.evaluate(g) - old) < 1e-9);
    }
}

TEST(onion_frames_every_and_keyed) {
    Clip c = walk();
    OnionSettings s;
    s.before = 2, s.after = 2, s.step = 3;
    auto f = onion_frames(c, 10, s);
    CHECK_EQ(f.size(), size_t(4));
    CHECK(f[0].frame == 7 && f[0].offset == -1 && f[1].frame == 4 && f[1].offset == -2);
    CHECK(f[2].frame == 13 && f[3].frame == 16);
    CHECK(f[0].weight > f[1].weight && f[1].weight > 0);
    s.before = 5;
    CHECK_EQ(onion_frames(c, 4, s).size(), size_t(1 + 2));  // clamped at frame 0
    Clip sparse;
    sparse.end_frame = 30;
    key_euler(sparse, "mHead", 0, {}), key_euler(sparse, "mHead", 12, {}), key_euler(sparse, "mHead", 25, {});
    s.keyed_only = true, s.before = 2, s.after = 2;
    f = onion_frames(sparse, 12, s);
    CHECK_EQ(f.size(), size_t(2));
    CHECK(f[0].frame == 0 && f[1].frame == 25);
}

// User test: in a loop the ghosts stopped at its ends, where the seam needs them most. Looping, they wrap round the
// loop as it plays (Loop out is Loop in's pose, so a ghost past it starts after Loop in); outside the loop they stop.
TEST(onion_frames_wrap_round_a_loop) {
    Clip c = walk();
    c.loop = true, c.loop_in = 0, c.loop_out = 30;
    OnionSettings s;
    s.before = 2, s.after = 2, s.step = 3;
    auto f = onion_frames(c, 29, s);
    CHECK_EQ(f.size(), size_t(4));
    CHECK(f[0].frame == 26 && f[1].frame == 23 && f[2].frame == 2 && f[3].frame == 5);
    f = onion_frames(c, 1, s);
    CHECK(f[0].frame == 28 && f[1].frame == 25 && f[2].frame == 4);
    c.loop_in = 10;  // frame 1 is before the loop: no wrapping
    CHECK_EQ(onion_frames(c, 1, s).size(), size_t(2));

    Clip sparse;  // keys at 0, 12 and 25 of a 0..30 loop
    sparse.end_frame = 30, sparse.loop = true, sparse.loop_in = 0, sparse.loop_out = 30;
    key_euler(sparse, "mHead", 0, {}), key_euler(sparse, "mHead", 12, {}), key_euler(sparse, "mHead", 25, {});
    s.keyed_only = true;
    f = onion_frames(sparse, 27, s);  // after it: the seam (30, the pose of 0), then 12 round the loop
    CHECK_EQ(f.size(), size_t(4));
    CHECK(f[0].frame == 25 && f[1].frame == 12 && f[2].frame == 30 && f[3].frame == 12);
}
