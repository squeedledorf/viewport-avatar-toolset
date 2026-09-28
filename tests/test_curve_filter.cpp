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
