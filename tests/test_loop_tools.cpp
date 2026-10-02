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
