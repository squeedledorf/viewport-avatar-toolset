// Loop assists (08 LP-5..LP-8): best loop points, fit to beats, loop-aware tangents, the walk treadmill's gait.
#include <cmath>
#include <fstream>
#include <iterator>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/footlock.h"
#include "vats/loop_assist.h"
#include "vats/loop_tools.h"
#include "vats/project.h"

using namespace vats;

namespace {

// A cycle of `period` frames planted in frames 10..90 of 100, between two held poses that match nothing in it.
// fingers_period > 0: a finger swings on a period of its own, so no pair of frames matches it and the legs.
Clip planted_cycle(int period, int fingers_period = 0) {
    Clip c;
    c.fps = 30;
    c.end_frame = 100;
    for (int f = 0; f <= 100; ++f) {
        if (f < 10 || f > 90) {
            const double s = f < 10 ? 1 : -1;
            key_euler(c, "mHipLeft", f, {0, 60 * s, 0});
            key_euler(c, "mHipRight", f, {0, -60 * s, 0});
            key_euler(c, "mKneeLeft", f, {0, 0, 0});
            key_euler(c, "mKneeRight", f, {0, 0, 0});
            key_euler(c, "mTorso", f, {0, 0, 30 * s});
        } else {
            const double t = 2 * kPi * (f - 10) / period;
            key_euler(c, "mHipLeft", f, {0, 25 * std::sin(t), 0});
            key_euler(c, "mHipRight", f, {0, -25 * std::sin(t), 0});
            key_euler(c, "mKneeLeft", f, {0, 20 + 20 * std::cos(t), 0});
            key_euler(c, "mKneeRight", f, {0, 20 - 20 * std::cos(t), 0});
            key_euler(c, "mTorso", f, {0, 0, 5 * std::sin(t)});
        }
        if (fingers_period) key_euler(c, "mHandMiddle1Left", f, {0, 40 * std::sin(2 * kPi * f / fingers_period), 0});
    }
    return c;
}

// The slope of a curve just after and just before a frame.
double slope_after(const FCurve& c, double f) { return (c.evaluate(f + 1e-3) - c.evaluate(f)) / 1e-3; }
double slope_before(const FCurve& c, double f) { return (c.evaluate(f) - c.evaluate(f - 1e-3)) / 1e-3; }

// One second of an in-place walk: each leg is planted for half the cycle, swinging from 10 degrees forward to
// 10 back with the knee straight, then lifts its foot (knee 70 degrees) and swings forward again.
Clip walk_in_place() {
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    c.loop = true, c.loop_in = 0, c.loop_out = 30;
    for (int f = 0; f <= 30; ++f)
        for (int side = 0; side < 2; ++side) {
            const int p = (f + side * 15) % 30;  // phase: 0..15 planted, 16..29 in the air
            const double hip = p <= 15 ? 10 - 20.0 * p / 15 : -10 + 20.0 * (p - 15) / 15;
            const std::string s = side ? "Right" : "Left";
            key_euler(c, "mHip" + s, f, {0, hip, 0});
            key_euler(c, "mKnee" + s, f, {0, p <= 15 ? 0.0 : 70.0, 0});
        }
    return c;
}

}  // namespace

TEST(loop_points_find_the_planted_cycle) {
    Rig rig(skel());
    const Clip c = planted_cycle(24);
    const auto best = find_loop_points(rig, c, 20);
    CHECK(!best.empty());
    if (best.empty()) return;
    CHECK_EQ(best[0].length() % 24, 0);
    CHECK(best[0].in >= 10 && best[0].out <= 90);
    CHECK(best[0].distance < 0.5);
    for (size_t i = 1; i < best.size(); ++i) {  // best first, and no near-duplicates
        CHECK(best[i].distance >= best[i - 1].distance);
        CHECK(std::abs(best[i].in - best[0].in) > 3 || std::abs(best[i].out - best[0].out) > 3);
    }
}

TEST(loop_points_weigh_legs_over_fingers) {
    Rig rig(skel());
    const Clip c = planted_cycle(30, 17);  // the finger never repeats with the legs
    const auto best = find_loop_points(rig, c, 20, 3);
    CHECK(!best.empty());
    if (best.empty()) return;
    CHECK_EQ(best[0].length() % 30, 0);
    CHECK(best[0].in >= 10 && best[0].out <= 90);
    CHECK(best[0].distance > 0);  // the finger still counts a little
}

// User test: on a finished seamless loop, Find listed other candidates at distance 0.00 and nothing said the loop
// itself already joined. The current loop scores on the same scale: a seamless one as well as the best candidate,
// one cut mid-cycle worse.
TEST(loop_points_score_the_current_loop) {
    Rig rig(skel());
    Clip c;  // two smooth 40-frame cycles, looped over the first
    c.fps = 30, c.end_frame = 80;
    c.loop = true, c.loop_in = 0, c.loop_out = 40;
    for (int f = 0; f <= 80; ++f) {
        const double t = 2 * kPi * f / 40;
        key_euler(c, "mHipLeft", f, {0, 25 * std::sin(t), 0});
        key_euler(c, "mHipRight", f, {0, -25 * std::sin(t), 0});
        key_euler(c, "mTorso", f, {0, 0, 5 * std::cos(t)});
    }
    const auto found = find_loop_points(rig, c, 20);
    const LoopCandidate now = current_loop(rig, c);
    CHECK(now.in == 0 && now.out == 40 && now.distance >= 0);
    CHECK(loop_joins(now, found));
    c.loop_out = 30;  // cut mid-cycle: candidates beat it
    const LoopCandidate cut = current_loop(rig, c);
    CHECK(cut.distance > 1);
    CHECK(!loop_joins(cut, find_loop_points(rig, c, 20)));
    c.loop = false;
    CHECK(!loop_joins(current_loop(rig, c), found));
}

TEST(loop_points_min_length) {
    Rig rig(skel());
    for (auto& k : find_loop_points(rig, planted_cycle(24), 40, 10)) CHECK(k.length() >= 40);
    CHECK(find_loop_points(rig, planted_cycle(24), 200).empty());
}

TEST(beats_fit_whole_frames) {
    BeatFit a = fit_to_beats(120, 30, 8);
    CHECK_EQ(a.frames, 120);
    CHECK_NEAR(a.residual_ms, 0, 1e-9);
    CHECK_EQ(a.loops_to_drift, 0);
    CHECK_EQ(a.suggested_fps, 30);
    BeatFit b = fit_to_beats(100, 30, 4);
    CHECK_EQ(b.frames, 72);
    CHECK_NEAR(b.seconds, 2.4, 1e-12);
    CHECK_NEAR(b.residual_ms, 0, 1e-9);
    CHECK_EQ(b.suggested_fps, 30);
}

TEST(beats_residual_and_drift) {
    // 128 BPM: 8 beats are 3.75 s = 112.5 frames at 30 fps; 113 frames end 16.7 ms late, a frame (33.3 ms) in 2 loops.
    BeatFit r = fit_to_beats(128, 30, 8);
    CHECK_EQ(r.frames, 113);
    CHECK_NEAR(r.residual_ms, 1000.0 / 60, 1e-6);
    CHECK_EQ(r.loops_to_drift, 2);
    CHECK_EQ(r.suggested_fps, 32);  // a beat is 15/32 s
    CHECK_EQ(fit_to_beats(0, 30, 8).frames, 0);
}

TEST(beats_stretch_loop) {
    Clip c;
    c.fps = 30, c.end_frame = 90, c.loop = true, c.loop_in = 10, c.loop_out = 70;
    key_euler(c, "mTorso", 10, {0, 0, 0});
    key_euler(c, "mTorso", 70, {0, 0, 20});
    key_euler(c, "mTorso", 90, {0, 0, 0});
    stretch_loop(c, fit_to_beats(100, 30, 4).frames);
    CHECK_EQ(c.loop_in, 10);
    CHECK_EQ(c.loop_out, 82);
    CHECK_EQ(c.end_frame, 102);
    CHECK(c.curves["mTorso"]["rot_z"].find(82) >= 0);
    CHECK(c.curves["mTorso"]["rot_z"].find(102) >= 0);
}

TEST(loop_tangents_continuous_at_seam) {
    Clip c;
    c.loop = true, c.loop_in = 0, c.loop_out = 30, c.end_frame = 30;
    FCurve& k = c.curves["mHipLeft"]["rot_y"];
    for (auto [f, v] : {std::pair{0.0, 0.0}, {8.0, 20.0}, {20.0, -15.0}, {30.0, 0.0}}) k.set_key(f, v);
    for (int i = 0; i < 4; ++i) k.apply_tangent(i, Tangent::Spline);
    CHECK(std::fabs(slope_after(k, 0) - slope_before(k, 30)) > 0.1);  // one-sided ends: a kink at the seam

    CHECK_EQ(apply_loop_tangents(c), 0);  // off: nothing
    c.loop_tangents = true;
    CHECK_EQ(apply_loop_tangents(c), 2);
    const double s = 35.0 / 18;  // the wrapped neighbours: -15 at frame -10 and 20 at frame 8
    CHECK_NEAR(slope_after(k, 0), s, 0.01);
    CHECK_NEAR(slope_before(k, 30), s, 0.01);
    CHECK_EQ(apply_loop_tangents(c), 0);  // idempotent
}

TEST(loop_tangents_whole_turn_seam) {
    // A torso turning once per loop: auto-clamped ends are flat on their own; across the seam they carry on turning.
    Clip c;
    c.loop = true, c.loop_in = 0, c.loop_out = 30, c.end_frame = 30, c.loop_tangents = true;
    FCurve& k = c.curves["mTorso"]["rot_z"];
    for (auto [f, v] : {std::pair{0.0, 10.0}, {15.0, 190.0}, {30.0, 370.0}}) k.set_key(f, v);
    CHECK_NEAR(slope_after(k, 0), 0, 0.01);
    apply_loop_tangents(c);
    CHECK_NEAR(slope_after(k, 0), 12, 0.05);
    CHECK_NEAR(slope_before(k, 30), 12, 0.05);
}

TEST(loop_tangents_saved_with_project) {
    Project p;
    p.clip.loop_tangents = true;
    Project q;
    std::string err;
    CHECK(load_project(save_project(p), q, err));
    CHECK(q.clip.loop_tangents);
    p.clip.loop_tangents = false;  // an older project has no such field: off
    CHECK(save_project(p).find("loop_tangents") == std::string::npos);
    CHECK(load_project(save_project(p), q, err));
    CHECK(!q.clip.loop_tangents);
}

TEST(treadmill_gait_of_synthetic_walk) {
    Rig rig(skel());
    const Clip c = walk_in_place();
    // The whole leg swings about the hip, 20 degrees in half a second, foot and all: the sole on the ground (the heel,
    // lowest at rest) moves along it at the swing's rate times its depth below the hip.
    Clip rest;
    const Evaluation e = evaluate(rig, rest, 0, nullptr);
    const int hip = skel().find("mHipLeft");
    const double depth = e.globals[hip].pos.z - sole_height(skel(), e.globals);
    const double speed = 20 * kDegToRad / 0.5 * depth;
    const Gait g = measure_gait(rig, c);
    CHECK(g.contacts >= 2);
    CHECK_NEAR(g.cycle, 1.0, 1e-9);
    CHECK_NEAR(g.speed, speed, speed * 0.05);
    CHECK_NEAR(g.stride, g.speed * g.cycle, 1e-9);
}

TEST(treadmill_match_speed) {
    Rig rig(skel());
    Clip c = walk_in_place();
    const Gait g = measure_gait(rig, c);
    CHECK_EQ(match_speed_frames(c, g, g.speed * 2), 15);  // the preview
    CHECK_EQ(match_speed_by_time(c, g, g.speed * 2), 15);  // twice as fast: half the frames
    CHECK_NEAR(measure_gait(rig, c).speed, g.speed * 2, g.speed * 0.2);
    CHECK(!match_speed_by_travel(c, 3.2));  // in place: no travel to scale

    Clip t = walk_in_place();
    key_offset(t, "mPelvis", 0, {0, 0, 0});
    key_offset(t, "mPelvis", 30, {1, 0, 0});
    CHECK(match_speed_by_travel(t, kSlSpeeds[0].mps));
    CHECK_NEAR(t.curves["mPelvis"]["pos_x"].evaluate(30), 3.2, 1e-9);
}

// Stretch Time lands on the speed: the measure is sub-frame (contacts start and end between samples, eight a frame)
// and reads the sole points that are down, so stretching the loop by k divides it by k to within 1%. It used to count
// whole frames at the ankle and read a few percent off after a stretch, up to 15% for a frame more or less of contact.
TEST(treadmill_stretch_lands_on_the_speed) {
    Rig rig(skel());
    std::ifstream in(std::string(VATS_WIKI_DIR) + "/examples/walk-cycle.vat", std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    Project p;
    std::string err;
    CHECK(load_project(text, p, err, ""));
    for (const Clip& walk : {walk_in_place(), p.clip}) {
        const Gait g = measure_gait(rig, walk);
        CHECK(g.speed > 0);
        const LoopRange r = loop_range(walk);
        const int length = r.out - r.in;
        for (int to = length * 2 / 3; to <= length * 3 / 2; ++to) {
            Clip c = walk;
            const double target = g.speed * length / to;  // a speed this many whole frames hit exactly
            CHECK_EQ(match_speed_by_time(c, g, target), to);
            const double got = measure_gait(rig, c).speed;
            if (std::fabs(got / target - 1) > 0.01) std::fprintf(stderr, "  %d frames: %.3f for %.3f\n", to, got, target);
            CHECK_NEAR(got, target, target * 0.01);
        }
    }
}
