// Jump Arc (08 JA-1).
#include <algorithm>
#include <cmath>

#include "check.h"
#include "vats/edit.h"
#include "vats/jump_arc.h"

using namespace vats;

namespace {

Clip crouch_jump() {
    Clip c;
    c.fps = 30;
    c.end_frame = 40;
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 10, {0, 0.02, -0.15});  // crouched at takeoff
    key_offset(c, "mPelvis", 28, {0.60, 0.05, -0.15});  // and at landing, 60 cm on
    key_offset(c, "mPelvis", 40, {0.60, 0.05, 0});
    return c;
}

}  // namespace

TEST(jump_arc_apex_matches_gravity) {
    Clip c = crouch_jump();
    JumpArcOptions opt;
    opt.takeoff = 10, opt.landing = 28;
    std::string msg;
    CHECK(jump_arc(c, opt, msg));
    // 18 frames at 30 fps = 0.6 s; level ends: apex g t^2 / 8 = 44.1 cm, halfway, at frame 19.
    const double z0 = -0.15, apex = 9.81 * 0.36 / 8;
    CHECK_NEAR(jump_apex(0.6, 0, 9.81), apex, 1e-12);
    double top = -1;
    for (int f = 10; f <= 28; ++f) top = std::max(top, curve_offset(c, "mPelvis", f).z);
    CHECK_NEAR(top - z0, apex, 1e-6);
    CHECK_NEAR(curve_offset(c, "mPelvis", 19).z - z0, apex, 1e-6);
    // Every frame on the parabola, forward travel even.
    for (int f = 11; f < 28; ++f) {
        const double t = (f - 10) / 30.0;
        CHECK_NEAR(curve_offset(c, "mPelvis", f).z, z0 + (0.5 * 9.81 * 0.6) * t - 0.5 * 9.81 * t * t, 1e-6);
        CHECK_NEAR(curve_offset(c, "mPelvis", f).x, 0.60 * (f - 10) / 18.0, 1e-6);
    }
    CHECK(msg.find("44.1 cm") != std::string::npos);
}

TEST(jump_arc_endpoints_unchanged) {
    Clip c = crouch_jump(), before = c;
    JumpArcOptions opt;
    opt.takeoff = 10, opt.landing = 28;
    std::string msg;
    CHECK(jump_arc(c, opt, msg));
    for (double f : {0.0, 5.0, 9.5, 10.0, 28.0, 28.5, 34.0, 40.0})
        CHECK((curve_offset(c, "mPelvis", f) - curve_offset(before, "mPelvis", f)).length() < 1e-9);
    // Just inside the ends the arc leaves and lands smoothly: no bulge between the end key and the next.
    for (double f : {10.5, 27.5}) {
        const double lo = std::min(curve_offset(c, "mPelvis", f - 0.5).z, curve_offset(c, "mPelvis", f + 0.5).z);
        const double hi = std::max(curve_offset(c, "mPelvis", f - 0.5).z, curve_offset(c, "mPelvis", f + 0.5).z);
        CHECK(curve_offset(c, "mPelvis", f).z > lo - 1e-3 && curve_offset(c, "mPelvis", f).z < hi + 1e-3);
    }
}

TEST(jump_arc_keeps_lateral_and_forward_when_asked) {
    Clip c = crouch_jump(), before = c;
    JumpArcOptions opt;
    opt.takeoff = 10, opt.landing = 28;
    opt.forward = false;
    std::string msg;
    CHECK(jump_arc(c, opt, msg));
    for (int f = 11; f < 28; ++f) {
        CHECK_NEAR(curve_offset(c, "mPelvis", f).x, curve_offset(before, "mPelvis", f).x, 1e-3);
        CHECK_NEAR(curve_offset(c, "mPelvis", f).y, curve_offset(before, "mPelvis", f).y, 1e-3);
    }
    Clip d = crouch_jump();
    opt.keep_lateral = false;
    CHECK(jump_arc(d, opt, msg));
    CHECK_NEAR(curve_offset(d, "mPelvis", 19).y, 0.035, 1e-6);  // halfway from 2 cm to 5 cm
}

TEST(jump_arc_uneven_ends_and_refusals) {
    // Landing 30 cm lower than takeoff: the apex follows v^2 / 2g with v = dz / t + g t / 2.
    const double t = 0.6, dz = -0.3, v = dz / t + 0.5 * 9.81 * t;
    CHECK_NEAR(jump_apex(t, dz, 9.81), v * v / (2 * 9.81), 1e-12);
    CHECK_NEAR(jump_apex(0.1, 1.0, 9.81), 1.0, 1e-12);  // rising all the way: the landing is the top
    Clip c = crouch_jump();
    JumpArcOptions opt;
    opt.takeoff = 10, opt.landing = 11;
    std::string msg;
    CHECK(!jump_arc(c, opt, msg));
    opt.landing = 50;
    CHECK(!jump_arc(c, opt, msg));
}
