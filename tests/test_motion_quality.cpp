// Motion quality numbers (08 MQ): stable, and each clean-up tool lowers the number it targets.
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/curve_filter.h"
#include "vats/edit.h"
#include "vats/footlock.h"
#include "vats/loop_tools.h"
#include "vats/motion_quality.h"
#include "vats/simplify.h"

using namespace vats;

namespace {

// Deterministic noise in -1..1.
struct Noise {
    unsigned s = 777;
    double operator()() {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) / double(1u << 24) * 2 - 1;
    }
};

// A two-second looping shuffle keyed on every frame, as a capture: the hips glide 10 cm forward (so the planted
// right foot slides and the loop drifts), the left leg steps between frames 24 and 34, the arm swings with
// jitter, and the arm ends 6 degrees off where it starts (a seam jump).
Clip take() {
    Clip c;
    c.fps = 30;
    c.end_frame = 60;
    c.loop = true, c.loop_in = 0, c.loop_out = 60;
    Noise noise;
    auto lift = [](int f) { return f < 18 || f > 40 ? 0.0 : 0.5 - 0.5 * std::cos(2 * kPi * (f - 18) / 22); };
    for (int f = 0; f <= 60; ++f) {
        key_offset(c, "mPelvis", f, {0.10 * f / 60, 0, 0});
        key_euler(c, "mHipRight", f, {0, -25, 0});
        key_euler(c, "mKneeRight", f, {0, 50, 0});
        key_euler(c, "mHipLeft", f, {0, -25 - 45 * lift(f), 0});
        key_euler(c, "mKneeLeft", f, {0, 50 + 70 * lift(f), 0});
        for (const char* a : {"mAnkleLeft", "mAnkleRight"}) key_euler(c, a, f, {0, -25, 0});
        key_euler(c, "mShoulderLeft", f, {0, 20 * std::sin(2 * kPi * f / 60) + 0.1 * f + noise(), 0});
    }
    return c;
}

bool same(const MotionQuality& a, const MotionQuality& b) {
    if (a.shake.size() != b.shake.size()) return false;
    for (size_t i = 0; i < a.shake.size(); ++i)
        if (a.shake[i].track != b.shake[i].track || a.shake[i].rot != b.shake[i].rot || a.shake[i].pos != b.shake[i].pos) return false;
    return a.jerk == b.jerk && a.foot_slide == b.foot_slide && a.contacts == b.contacts && a.hip_drift == b.hip_drift &&
           a.loops == b.loops && a.seam_deg == b.seam_deg && a.seam_mm == b.seam_mm && a.bytes == b.bytes && a.keys == b.keys;
}

}  // namespace

TEST(quality_numbers_are_stable) {
    Rig rig(skel());
    const Clip c = take();
    const MotionQuality a = measure_quality(rig, c, {}), b = measure_quality(rig, c, {});
    CHECK(same(a, b));
    CHECK(a.jerk > 0 && a.foot_slide > 0.01 && a.contacts >= 2 && a.bytes > 0);
    CHECK_NEAR(a.hip_drift, 0.10, 1e-9);
    CHECK(a.seam_deg > 4 && a.seam_deg < 8);  // 6 degrees, give or take the jitter
    CHECK_NEAR(a.seam_mm, 100, 1e-6);  // the hips' travel is a position jump too
    CHECK_EQ(a.keys, 61 * 7 * 3 + 61 * 3);
}

TEST(quality_filter_lowers_jerk) {
    Rig rig(skel());
    Clip c = take();
    const double before = measure_quality(rig, c, {}).jerk;
    filter_curves(c, {{"mShoulderLeft", "rot_y"}}, 0, 60, FilterSettings{});
    CHECK(measure_quality(rig, c, {}).jerk < before * 0.5);
}

TEST(quality_foot_clean_up_lowers_slide) {
    Rig rig(skel());
    Clip c = take();
    const double before = measure_quality(rig, c, {}).foot_slide;
    lock_feet(c, rig, {});
    CHECK(measure_quality(rig, c, {}).foot_slide < before * 0.2);
}

TEST(quality_loop_tools_lower_drift_and_seam) {
    Rig rig(skel());
    Clip c = take();
    remove_travel(c);
    MotionQuality q = measure_quality(rig, c, {});
    CHECK(q.hip_drift < 1e-6);
    CHECK(q.seam_mm < 1e-3);
    make_loop_seamless(c, 0);
    q = measure_quality(rig, c, {});
    CHECK(q.seam_deg < 1e-6);
}

TEST(quality_simplify_lowers_keys_and_fit_lowers_bytes) {
    Rig rig(skel());
    Clip c = take();
    const MotionQuality before = measure_quality(rig, c, {});
    simplify_curves(c, {}, {});
    const MotionQuality simple = measure_quality(rig, c, {});
    CHECK(simple.keys * 3 < before.keys);
    CHECK(simple.bytes <= before.bytes);
    // What Fit to 250 KB does: a looser key reduction. Export keeps the clip's own keys, so it works once they are few.
    AnimExportOptions loose;
    loose.reduce_rot_deg = 2, loose.reduce_pos_m = 0.01;
    CHECK(measure_quality(rig, c, loose).bytes < simple.bytes);
}
