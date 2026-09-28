// Simplify Curves (08 SC).
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/footlock.h"
#include "vats/simplify.h"

using namespace vats;

namespace {

// Four seconds keyed on every frame: the elbow swings 40 degrees with a 40-frame period (peaks at 10, 50, 90,
// troughs at 30, 70, 110) and the hips bob 5 cm on a 60-frame period.
Clip dense_sine() {
    Clip c;
    c.fps = 30;
    c.end_frame = 120;
    for (int f = 0; f <= 120; ++f) {
        key_euler(c, "mElbowLeft", f, {40 * std::sin(2 * kPi * f / 40), 10, 0});
        key_offset(c, "mPelvis", f, {0, 0, 0.05 * std::sin(2 * kPi * f / 60)});
    }
    return c;
}

int keys(const Clip& c) {
    int n = 0;
    for (auto& [name, t] : c.curves)
        for (auto& [ch, cv] : t) n += int(cv.keys.size());
    return n;
}

// The largest difference at a whole frame between two clips' rot_* (degrees) and pos_* (millimetres) curves.
void deviation(const Clip& a, const Clip& b, double& deg, double& mm) {
    deg = mm = 0;
    for (auto& [name, t] : a.curves)
        for (auto& [ch, cv] : t)
            for (int f = 0; f <= a.end_frame; ++f) {
                const double d = std::fabs(cv.evaluate(f) - b.curves.at(name).at(ch).evaluate(f));
                if (ch.rfind("rot_", 0) == 0) deg = std::max(deg, d);
                else mm = std::max(mm, d * 1000);
            }
}

bool has_key(const Clip& c, const char* track, const char* ch, int f) {
    return c.curves.at(track).at(ch).find(f) >= 0;
}

}  // namespace

TEST(simplify_dense_sine_few_keys_within_tolerance) {
    const Clip before = dense_sine();
    Clip c = before;
    SimplifyOptions o;
    o.tol_deg = 0.25, o.tol_mm = 0.5;
    const SimplifyResult r = simplify_curves(c, {}, o);
    CHECK_EQ(r.before, keys(before));
    CHECK_EQ(r.after, keys(c));
    CHECK(r.after * 5 < r.before);  // 726 keys; a quarter swing needs about two
    double deg, mm;
    deviation(before, c, deg, mm);
    CHECK(deg <= 0.25 + 1e-9);
    CHECK(mm <= 0.5 + 1e-9);
    // Extrema keep their keys, at their values.
    for (int f : {10, 30, 50, 70, 90, 110}) {
        CHECK(has_key(c, "mElbowLeft", "rot_x", f));
        CHECK_NEAR(c.curves.at("mElbowLeft").at("rot_x").evaluate(f), before.curves.at("mElbowLeft").at("rot_x").evaluate(f), 1e-9);
    }
    for (int f : {15, 45, 75, 105}) CHECK(has_key(c, "mPelvis", "pos_z", f));
    // A constant curve keeps only its ends.
    CHECK_EQ(c.curves.at("mElbowLeft").at("rot_y").keys.size(), size_t(2));
}

TEST(simplify_is_idempotent_ish) {
    Clip c = dense_sine();
    SimplifyOptions o;
    simplify_curves(c, {}, o);
    const Clip once = c;
    const SimplifyResult r = simplify_curves(c, {}, o);
    CHECK(r.after <= r.before);  // never more keys
    CHECK(r.after + 4 >= r.before);  // and not many fewer: the first pass left what the curve needs
    double deg, mm;
    deviation(once, c, deg, mm);
    CHECK(deg <= o.tol_deg + 1e-9);
    CHECK(mm <= o.tol_mm + 1e-9);
}

TEST(simplify_range_and_tracks) {
    const Clip before = dense_sine();
    Clip c = before;
    SimplifyOptions o;
    o.from = 20, o.to = 80;
    simplify_curves(c, {"mElbowLeft"}, o);
    CHECK(c.curves.at("mPelvis") == before.curves.at("mPelvis"));  // not listed
    const FCurve &x = c.curves.at("mElbowLeft").at("rot_x"), &x0 = before.curves.at("mElbowLeft").at("rot_x");
    for (int f : {0, 5, 18, 82, 100, 120}) CHECK(x.find(f) >= 0);  // outside the range: untouched
    int inside = 0;
    for (const Key& k : x.keys) inside += k.frame >= 20 && k.frame <= 80;
    CHECK(inside < 20);
    for (int f = 0; f <= 120; ++f) CHECK_NEAR(x.evaluate(f), x0.evaluate(f), o.tol_deg + 1e-9);
}

TEST(simplify_keeps_planted_frames) {
    // The foot-lock shuffle, keyed on every frame as a capture would be.
    Clip c;
    c.fps = 30;
    c.end_frame = 60;
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 60, {0.10, 0, 0});
    for (const char* side : {"Left", "Right"})
        for (int f : {0, 60}) {
            key_euler(c, std::string("mHip") + side, f, {0, -25, 0});
            key_euler(c, std::string("mKnee") + side, f, {0, 50, 0});
            key_euler(c, std::string("mAnkle") + side, f, {0, -25, 0});
        }
    for (int f : {18, 40}) key_euler(c, "mHipLeft", f, {0, -25, 0}), key_euler(c, "mKneeLeft", f, {0, 50, 0});
    for (int f : {24, 34}) key_euler(c, "mHipLeft", f, {0, -70, 0}), key_euler(c, "mKneeLeft", f, {0, 120, 0});
    Clip dense = c;
    for (auto& [name, t] : dense.curves)
        for (auto& [ch, cv] : t)
            for (int f = 0; f <= 60; ++f) cv.set_key(f, c.curves.at(name).at(ch).evaluate(f));

    Rig rig(skel());
    const std::vector<FootContact> contacts = find_foot_contacts(rig, dense, {});
    CHECK(!contacts.empty());
    SimplifyOptions o;
    for (const FootContact& k : contacts) o.keep.push_back(k.from), o.keep.push_back(k.to);
    Clip off = dense, on = dense;
    simplify_curves(off, {}, {});
    simplify_curves(on, {}, o);
    bool missing_without = false;
    for (int f : o.keep) {
        CHECK(has_key(on, "mKneeLeft", "rot_y", f) && has_key(on, "mPelvis", "pos_x", f));
        missing_without |= !has_key(off, "mKneeLeft", "rot_y", f) || !has_key(off, "mPelvis", "pos_x", f);
    }
    CHECK(missing_without);  // the option is what kept them
}

// The largest turn at a whole frame between two clips' rotations of a track (degrees).
double turn_between(const Clip& a, const Clip& b, const char* track) {
    double worst = 0;
    for (int f = 0; f <= a.end_frame; ++f) {
        const Quat qa = euler_to_quat(curve_euler(a, track, f)), qb = euler_to_quat(curve_euler(b, track, f));
        worst = std::max(worst, 2 * std::acos(std::min(1.0, std::fabs(qa.dot(qb)))) * kRadToDeg);
    }
    return worst;
}

// Near gimbal lock (rot_y within 5 degrees of +-90) the rotation is fitted as a rotation: the three channels together,
// on the same frames, to the turn between the rotations. It used to be left as it was. Stepped keys still are.
TEST(simplify_fits_gimbal_rotations_and_leaves_stepped) {
    Clip c;
    c.end_frame = 60;
    for (int f = 0; f <= 60; ++f) {
        key_euler(c, "mShoulderLeft", f, {f * 2.0, 88, f * -1.0});  // rot_y 2 degrees from gimbal lock
        key_euler(c, "mWristLeft", f, {f * 0.5, 0, 0});
    }
    for (Key& k : c.curves["mWristLeft"]["rot_x"].keys) k.interp = Interp::Constant;
    const Clip before = c;
    const SimplifyResult r = simplify_curves(c, {}, {});
    const size_t n = c.curves.at("mShoulderLeft").at("rot_x").keys.size();
    CHECK(n < 20);
    CHECK(c.curves.at("mShoulderLeft").at("rot_y").keys.size() == n && c.curves.at("mShoulderLeft").at("rot_z").keys.size() == n);
    CHECK(turn_between(c, before, "mShoulderLeft") <= 0.25 + 1e-6);
    CHECK(c.curves.at("mWristLeft").at("rot_x") == before.curves.at("mWristLeft").at("rot_x"));
    CHECK(c.curves.at("mWristLeft").at("rot_y").keys.size() == 2);  // its other channels still simplify
    CHECK_EQ(r.skipped.size(), size_t(1));
}

// A running knee bends past 90 degrees on rot_y: it simplifies like any other bone (it was left alone, "near gimbal
// lock"), within the tolerance as a rotation.
TEST(simplify_running_knee) {
    Clip c;
    c.end_frame = 16;
    for (int f = 0; f <= 16; ++f) key_euler(c, "mKneeLeft", f, {0, 20 + 71 * std::sin(kPi * f / 16), 0});  // 91 at 8
    const Clip before = c;
    const SimplifyResult r = simplify_curves(c, {}, {});
    CHECK(r.skipped.empty());
    CHECK(r.after < r.before);
    CHECK(c.curves.at("mKneeLeft").at("rot_y").keys.size() < 17);
    CHECK(turn_between(c, before, "mKneeLeft") <= 0.25 + 1e-6);
}

TEST(simplify_leaves_sparse_curves) {
    Clip c;
    c.end_frame = 60;
    key_euler(c, "mHead", 0, {10, 0, 0});
    key_euler(c, "mHead", 30, {-5, 20, 5});
    key_euler(c, "mHead", 60, {10, 0, 0});
    const Clip before = c;
    const SimplifyResult r = simplify_curves(c, {}, {});
    CHECK(c == before);  // three keys already: nothing smaller fits
    CHECK_EQ(r.after, r.before);
}
