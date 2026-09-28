#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/dynamics.h"
#include "vats/edit.h"
#include "vats/loop_tools.h"
#include "vats/overlap.h"

using namespace vats;

namespace {

const Rig& rig() {
    static Rig r(skel());
    return r;
}

std::vector<int> arm() {
    return {skel().find("mShoulderLeft"), skel().find("mElbowLeft"), skel().find("mWristLeft")};
}

// The upper arm and forearm both raise 40 degrees at frame 20 and lower again by 40.
Clip raise_clip() {
    Clip c;
    c.end_frame = c.loop_out = 60;
    for (const char* b : {"mShoulderLeft", "mElbowLeft"}) {
        key_euler(c, b, 0, {0, 0, 0});
        key_euler(c, b, 20, {0, 0, 40});
        key_euler(c, b, 40, {0, 0, 0});
    }
    return c;
}

double z_at(const Clip& c, const char* track, double f) { return curve_euler(c, track, f).z; }

int peak_frame(const Clip& c, const char* track) {
    int best = 0;
    for (int f = 1; f <= c.end_frame; ++f)
        if (z_at(c, track, f) > z_at(c, track, best) + 1e-6) best = f;
    return best;
}

}  // namespace

TEST(overlap_forearm_peaks_later) {
    for (int n : {1, 2, 3}) {
        Clip c = raise_clip();
        OverlapSettings s;
        s.shift = n;
        apply_overlap(c, skel(), arm(), s);
        CHECK_EQ(peak_frame(c, "mShoulderLeft"), 20);
        CHECK_EQ(peak_frame(c, "mElbowLeft"), 20 + n);
        CHECK(!c.has_channels("mWristLeft", kRotChannels));  // no keys of its own: left alone
    }
    // Fractional: frame f of the forearm holds what frame f - 1.5 held.
    Clip c = raise_clip(), before = c;
    OverlapSettings s;
    s.shift = 1.5;
    apply_overlap(c, skel(), arm(), s);
    for (int f = 2; f <= 60; ++f) CHECK(std::fabs(z_at(c, "mElbowLeft", f) - z_at(before, "mElbowLeft", f - 1.5)) < 0.2);
}

TEST(overlap_falloff_scales_swing) {
    Clip c = raise_clip(), before = c;
    OverlapSettings s;
    s.shift = 2, s.falloff = 0.5;
    apply_overlap(c, skel(), arm(), s);
    auto range = [](const Clip& k, const char* t) {
        double lo = 1e9, hi = -1e9;
        for (int f = 0; f <= k.end_frame; ++f) lo = std::min(lo, z_at(k, t, f)), hi = std::max(hi, z_at(k, t, f));
        return hi - lo;
    };
    CHECK(std::fabs(range(c, "mElbowLeft") - 0.5 * range(before, "mElbowLeft")) < 0.3);
    CHECK(c.curves.at("mShoulderLeft") == before.curves.at("mShoulderLeft"));  // the root is untouched
}

TEST(overlap_loop_seam_clean) {
    Clip c;
    c.end_frame = 40;
    c.loop = true;
    c.loop_in = 0, c.loop_out = 40;
    for (const char* b : {"mTail1", "mTail2", "mTail3"}) {
        key_euler(c, b, 0, {0, 0, -25});
        key_euler(c, b, 20, {0, 0, 25});
        key_euler(c, b, 40, {0, 0, -25});
    }
    CHECK(loop_seam_jumps(c, 0.05).empty());
    const std::vector<int> tail = {skel().find("mTail1"), skel().find("mTail2"), skel().find("mTail3")};
    Clip before = c;
    OverlapSettings s;
    s.shift = 2.5;
    apply_overlap(c, skel(), tail, s);
    CHECK(loop_seam_jumps(c, 0.05).empty());
    // It wrapped: frame 1 of mTail3 (5 frames late) holds what frame 36 held.
    CHECK(std::fabs(z_at(c, "mTail3", 1) - z_at(before, "mTail3", 36)) < 0.2);
    CHECK_EQ(peak_frame(c, "mTail3"), 25);
}

TEST(overlap_settle_lands_on_end_pose) {
    Clip c = raise_clip();
    c.end_frame = 38;  // the forearm is still coming down at the end
    Clip before = c;
    OverlapSettings s;
    s.shift = 3, s.settle = true;
    apply_overlap(c, skel(), arm(), s);
    CHECK(std::fabs(z_at(c, "mElbowLeft", 38) - z_at(before, "mElbowLeft", 38)) < 0.1);
    CHECK_EQ(peak_frame(c, "mElbowLeft"), 23);
}

TEST(overlap_refuses_ik) {
    Clip c = raise_clip();
    CHECK(overlap_refusal(c, rig(), arm()).empty());
    CHECK(!overlap_refusal(c, rig(), {skel().find("mElbowLeft")}).empty());  // one bone is no chain
    key_blend(c, rig(), 0, rig().find_limb("ArmLeft"), 1);
    CHECK(!overlap_refusal(c, rig(), arm()).empty());
}

TEST(overlap_dynamics_preset) {
    DynChain d = dyn_preset("overlap", "mShoulderLeft", 3);
    CHECK(d.gravity == 0 && d.stiffness > dyn_preset("tail", "mTail1", 6).stiffness);
}
