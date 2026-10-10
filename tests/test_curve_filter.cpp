// Motion clean-up filters (08 MC-4a): One-Euro, Savitzky-Golay, zero-phase Butterworth, and the shake score.
#include <cmath>

#include "check.h"
#include "vats/curve_filter.h"

using namespace vats;

namespace {

constexpr double kFps = 30;

// Deterministic noise in -1..1.
struct Noise {
    unsigned s = 12345;
    double operator()() {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) / double(1u << 24) * 2 - 1;
    }
};

// A 1 Hz swing of 30 degrees with +-1 degree of tracker jitter, 4 s at 30 fps.
std::vector<double> noisy_sine(std::vector<double>* clean = nullptr) {
    Noise noise;
    std::vector<double> x;
    for (int i = 0; i < 120; ++i) {
        const double v = 30 * std::sin(2 * kPi * i / kFps);
        if (clean) clean->push_back(v);
        x.push_back(v + noise());
    }
    return x;
}

// The 1 Hz component's amplitude over the middle two seconds (whole cycles, so phase lag does not count).
double amplitude(const std::vector<double>& y) {
    double s = 0, c = 0;
    for (int i = 30; i < 90; ++i) s += y[i] * std::sin(2 * kPi * i / kFps), c += y[i] * std::cos(2 * kPi * i / kFps);
    return 2 * std::hypot(s, c) / 60;
}

FilterSettings kind(FilterKind k) {
    FilterSettings s;
    s.kind = k;
    return s;
}

constexpr FilterKind kKinds[] = {FilterKind::OneEuro, FilterKind::SavitzkyGolay, FilterKind::Butterworth};
// How much each filter's defaults cut the shake of the noisy swing, at least. One-Euro is a causal one-pole
// whose cutoff rises with speed, so on a limb that never stops it gives less than the two-sided filters.
constexpr double kJerkDrop[] = {2.5, 4, 4};

}  // namespace

TEST(filter_sine_noise_loses_jerk_keeps_amplitude) {
    std::vector<double> clean;
    const std::vector<double> x = noisy_sine(&clean);
    const double j0 = jerk_rms(x, kFps);
    CHECK(j0 > 10 * jerk_rms(clean, kFps));  // the jitter is the shake
    for (FilterKind k : kKinds) {
        const std::vector<double> y = filter_samples(x, kFps, kind(k));
        CHECK(y.size() == x.size());
        const double j1 = jerk_rms(y, kFps), a = amplitude(y);
        std::printf("    %s: jerk %.0f -> %.0f (x%.1f), amplitude %.2f\n", filter_name(k), j0, j1, j0 / j1, a);
        CHECK(j1 * kJerkDrop[int(k)] < j0);
        CHECK(std::fabs(a - 30) < 30 * 0.05);
    }
}

TEST(filter_one_euro_keeps_a_step) {
    // A 20-degree step, held still either side: within 0.2 s the One-Euro output has 90% of it.
    std::vector<double> x(60, 0.0);
    for (int i = 30; i < 60; ++i) x[i] = 20;
    const std::vector<double> y = filter_samples(x, kFps, kind(FilterKind::OneEuro));
    std::printf("    step: %.2f %.2f %.2f %.2f %.2f %.2f\n", y[30], y[31], y[32], y[33], y[34], y[36]);
    CHECK(std::fabs(y[29]) < 1e-9);  // causal: nothing moves before the edge
    CHECK(y[36] >= 0.9 * 20);
    CHECK(y[59] <= 20 + 1e-9);  // no overshoot
}

TEST(filter_ends_follow_the_trend) {
    // Odd reflection: a straight ramp comes through every filter unchanged, ends included.
    std::vector<double> x;
    for (int i = 0; i < 40; ++i) x.push_back(0.5 * i);
    for (FilterKind k : {FilterKind::SavitzkyGolay, FilterKind::Butterworth}) {
        const std::vector<double> y = filter_samples(x, kFps, kind(k));
        for (size_t i = 0; i < x.size(); ++i) CHECK(std::fabs(y[i] - x[i]) < 1e-6);
    }
}

TEST(filter_curves_loop_seam_stays_clean) {
    // A looping clip, frames 0..60: a swing on one channel, a whole turn plus jitter on another.
    Clip c;
    c.fps = 30, c.end_frame = 60, c.loop = true, c.loop_in = 0, c.loop_out = 60;
    Noise noise;
    for (int f = 0; f <= 60; ++f) {
        const double jitter = f == 60 ? 0 : noise();  // loop_out repeats loop_in's pose (a turn on)
        c.curves["mTorso"]["rot_x"].set_key(f, 20 * std::sin(2 * kPi * f / 60) + jitter);
        c.curves["mTorso"]["rot_z"].set_key(f, 6.0 * f + jitter);
    }
    const double z0 = c.curves["mTorso"]["rot_z"].keys[0].value;
    c.curves["mTorso"]["rot_z"].keys[60].value = z0 + 360;
    c.curves["mTorso"]["rot_x"].keys[60].value = c.curves["mTorso"]["rot_x"].keys[0].value;
    for (auto& [ch, cv] : c.curves["mTorso"]) cv.recompute_handles();
    const auto before = shake_scores(c, {"mTorso"}, 0, 60);
    for (FilterKind k : kKinds) {
        Clip f = c;
        filter_curves(f, {{"mTorso", "rot_x"}, {"mTorso", "rot_z"}}, 0, 60, kind(k));
        for (const char* ch : {"rot_x", "rot_z"}) {
            const FCurve& cv = f.curves["mTorso"][ch];
            CHECK(cv.keys.size() == 61);
            const double drift = std::string(ch) == "rot_z" ? 360 : 0;
            CHECK(std::fabs(cv.evaluate(60) - cv.evaluate(0) - drift) < 1e-9);
            // Across the seam the curve moves as it does next to it: the second difference there is no
            // bigger than the largest one inside the loop.
            auto v = [&](int i) { return i < 0 ? cv.evaluate(60 + i) - drift : cv.evaluate(i); };
            double inside = 0;
            for (int i = 1; i < 59; ++i) inside = std::max(inside, std::fabs(v(i + 1) - 2 * v(i) + v(i - 1)));
            const double seam = std::max(std::fabs(v(1) - 2 * v(0) + v(-1)), std::fabs(v(0) - 2 * v(-1) + v(-2)));
            std::printf("    %s %s: seam %.4f inside %.4f\n", filter_name(k), ch, seam, inside);
            CHECK(seam <= inside + 1e-9);
        }
        const auto after = shake_scores(f, {"mTorso"}, 0, 60);
        CHECK(after.size() == 1 && after[0].rot * 2 < before[0].rot && after[0].pos == 0);
    }
}

TEST(filter_curves_range_keeps_outside) {
    // Filtering frames 10..20 of a keyed curve leaves the curve outside that range where it was.
    Clip c;
    c.fps = 30, c.end_frame = 40;
    FCurve& cv = c.curves["mPelvis"]["pos_z"];
    cv.set_key(0, 0), cv.set_key(15, 0.3), cv.set_key(40, -0.1);
    const Clip was = c;
    filter_curves(c, {{"mPelvis", "pos_z"}, {"mNope", "rot_x"}}, 10, 20, kind(FilterKind::Butterworth));
    for (double f : {0.0, 3.0, 9.0, 21.0, 30.0, 40.0})
        CHECK(std::fabs(c.curves["mPelvis"]["pos_z"].evaluate(f) - was.curves.at("mPelvis").at("pos_z").evaluate(f)) < 1e-6);
    CHECK(!c.curves.count("mNope"));
}

// Motion capture at 60 fps: an arm swinging 40 degrees once a second, a fast punch (90 degrees in 6 frames), and a
// hip bobbing 3 cm. Clean, nothing is a spike; a one-frame flip, a two-frame dip and a hip jump are rebuilt close
// to the motion around them; a quick gesture (a 25-degree flick there and back in a fifth of a second) is motion and stays; a rotation wrapping
// past 180 is not a spike; hand keys are never touched.
TEST(remove_spikes_rebuilds_glitches_only) {
    const double pi = std::acos(-1.0);
    auto swing = [&](double f) { return 40 * std::sin(2 * pi * f / 60); };
    auto punch = [&](double f) { return f < 100 ? 0.0 : f > 106 ? 90.0 : 90 * (f - 100) / 6; };
    auto bob = [&](double f) { return 0.03 * std::sin(2 * pi * f / 30); };
    auto wrap = [&](double f) { double v = 170 + f * 0.5; return v > 180 ? v - 360 : v; };
    auto build = [&] {
        Clip c;
        c.fps = 60, c.end_frame = 200;
        for (int f = 0; f <= 200; ++f) {
            c.curves["mShoulderLeft"]["rot_y"].set_key(f, swing(f), Interp::Linear);
            c.curves["mElbowRight"]["rot_y"].set_key(f, punch(f), Interp::Linear);
            c.curves["mPelvis"]["pos_z"].set_key(f, bob(f), Interp::Linear);
            c.curves["mWristLeft"]["rot_z"].set_key(f, wrap(f), Interp::Linear);
        }
        for (int f = 0; f <= 200; f += 20) c.curves["mHead"]["rot_x"].set_key(f, f == 100 ? 30 : 0);  // a keyed nod
        return c;
    };
    Clip clean = build();
    CHECK_EQ(remove_spikes(clean).samples, 0);

    Clip c = build();
    auto& sh = c.curves["mShoulderLeft"]["rot_y"].keys;
    sh[50].value += 30;                       // a one-frame flip
    sh[150].value -= 25, sh[151].value -= 25;  // a two-frame dip
    auto flick = [&](double f) { return f < 168 || f > 180 ? 0.0 : 25 * std::pow(std::sin(pi * (f - 168) / 12), 2); };
    for (int f = 168; f <= 180; ++f) sh[size_t(f)].value += flick(f);  // a quick real gesture: stays
    c.curves["mPelvis"]["pos_z"].keys[80].value += 0.08;  // the hips jump 8 cm for a frame
    const SpikeReport r = remove_spikes(c);
    CHECK(r.samples >= 4 && r.samples <= 8);
    CHECK_EQ(r.worst, std::string("mShoulderLeft rot_y"));
    const FCurve& s = c.curves["mShoulderLeft"]["rot_y"];
    for (int f : {50, 150, 151}) CHECK(std::fabs(s.evaluate(f) - swing(f)) < 1.0);
    for (int f = 168; f <= 180; ++f) CHECK(std::fabs(s.evaluate(f) - (swing(f) + flick(f))) < 1e-9);
    CHECK(std::fabs(c.curves["mPelvis"]["pos_z"].evaluate(80) - bob(80)) < 0.003);
    for (int f = 0; f <= 200; ++f) CHECK(std::fabs(c.curves["mWristLeft"]["rot_z"].evaluate(f) - wrap(f)) < 1e-9);
    CHECK(std::fabs(c.curves["mHead"]["rot_x"].evaluate(100) - 30) < 1e-9);
    CHECK(std::fabs(c.curves["mElbowRight"]["rot_y"].evaluate(103) - punch(103)) < 1e-9);

    // A glitch on the very first frame, and one across a wrap (175 -> -120 -> 176 is a 65-degree jump, not 295).
    Clip e = build();
    e.curves["mShoulderLeft"]["rot_y"].keys[0].value += 30;
    FCurve& w = e.curves["mWristLeft"]["rot_z"];
    w.keys[100].value = w.keys[100].value - 65 > -180 ? w.keys[100].value - 65 : w.keys[100].value - 65 + 360;
    remove_spikes(e);
    CHECK(std::fabs(e.curves["mShoulderLeft"]["rot_y"].evaluate(0) - swing(0)) < 2.0);
    const double d = std::fabs(w.evaluate(100) - wrap(100));
    CHECK(std::min(d, std::fabs(d - 360)) < 1.0);
    CHECK(std::fabs(w.evaluate(100) - w.evaluate(99)) < 5 || std::fabs(std::fabs(w.evaluate(100) - w.evaluate(99)) - 360) < 5);
}
