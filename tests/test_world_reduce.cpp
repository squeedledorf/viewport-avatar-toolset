// World-space key reduction (08 WR, idea 11).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <vector>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/edit.h"
#include "vats/rig.h"
#include "vats/sl_preview.h"
#include "vats/world_reduce.h"

using namespace vats;

namespace {

// Smooth motion down several chains (hip, spine, head, both arms, a finger, a leg, the jaw), keyed every 40 frames
// with auto tangents, so there is room to reduce between the keys.
Clip dance() {
    Clip c;
    c.fps = 30;
    c.end_frame = 160;
    const char* bones[] = {"mTorso", "mChest", "mNeck", "mHead", "mShoulderLeft", "mElbowLeft", "mWristLeft",
                           "mHandIndex1Left", "mHandIndex2Left", "mShoulderRight", "mElbowRight", "mHipLeft",
                           "mKneeLeft", "mFaceJaw"};
    for (int f = 0; f <= 160; f += 40) {
        for (int b = 0; b < int(std::size(bones)); ++b) {
            const double s = std::sin(f * 0.04 + b), t = std::cos(f * 0.03 + 2 * b);
            key_euler(c, bones[b], f, {10 * s, 25 * t, 35 * s * t});
        }
        key_offset(c, "mPelvis", f, {0.05 * std::sin(f * 0.05), 0.04 * std::cos(f * 0.04), -0.06 * std::sin(f * 0.03)});
        key_euler(c, "mPelvis", f, {0, 0, 20 * std::sin(f * 0.02)});
    }
    return c;
}

size_t key_count(const AnimFile& f) {
    size_t n = 0;
    for (const AnimJoint& j : f.joints) n += j.rot.size() + j.pos.size();
    return n;
}

// The largest world distance, in metres, between SL's playback of f and the clip, over every bone and frame.
double measured(const Rig& rig, const Clip& c, const AnimFile& f) {
    double worst = 0;
    for (const BoneDeviation& d : anim_deviation(rig, c, f, nullptr)) worst = std::max(worst, d.mm / 1000);
    return worst;
}

std::vector<int> frames_of(const AnimFile& f, const std::vector<std::array<std::uint16_t, 4>>& keys, int fps) {
    std::vector<int> out;
    for (const auto& k : keys) out.push_back(int(std::lround(u16_to_f32(k[0], 0.f, f.duration) * fps)));
    return out;
}

AnimExportOptions world(double mm) {
    AnimExportOptions o;
    o.reduce_world_m = mm / 1000;
    return o;
}

}  // namespace

TEST(world_reduce_rdp_keeps_anchors_ends_and_the_gap) {
    // A straight line needs only its ends, but anchors stay and no run is longer than the gap.
    std::vector<char> anchors(200, 0);
    anchors[17] = anchors[90] = 1;
    auto flat = [](int, int, int) { return 0.0; };
    std::vector<int> k = rdp_keys(200, 0.001, 60, anchors, flat);
    CHECK(k.front() == 0 && k.back() == 199);
    CHECK(std::count(k.begin(), k.end(), 17) == 1 && std::count(k.begin(), k.end(), 90) == 1);
    for (size_t i = 1; i < k.size(); ++i) CHECK(k[i] - k[i - 1] <= 60);
    CHECK(k.size() <= 7);
    CHECK_EQ(rdp_keys(10, 0, 60, {}, flat).size(), size_t(10));  // 0 keeps every frame
}

TEST(world_reduce_budgets_add_up_to_the_tolerance_down_every_chain) {
    std::vector<char> rot(skel().size(), 0), pos(skel().size(), 0);
    const int pelvis = skel().find("mPelvis"), wrist = skel().find("mWristLeft"), head = skel().find("mHead");
    for (int n = wrist; n >= 0; n = skel()[n].parent) rot[n] = 1;
    rot[head] = pos[pelvis] = 1;
    const std::vector<double> b = world_budgets(skel(), rot, pos, 0.01);
    double chain = 0;
    for (int n = wrist; n >= 0; n = skel()[n].parent) chain += b[n] * (rot[n] + pos[n]);
    CHECK(chain <= 0.01 + 1e-12);
    CHECK(b[head] > b[wrist]);  // the head's chain is shorter, so it gets more
    CHECK(b[skel().find("mHandIndex1Left")] == 0);  // nothing written
}

TEST(world_reduce_keeps_no_more_keys_than_local_at_the_same_world_error) {
    const Clip c = dance();
    Rig rig(skel());
    const AnimFile w = export_anim(skel(), c, world(2)).file;
    const double err = measured(rig, c, w);
    CHECK(err > 0 && err <= 0.002 + 2e-4);
    // The local reducer's best at that error: the fewest keys over tolerances whose measured error is no larger.
    size_t local = SIZE_MAX;
    for (double deg = 0.005; deg < 5; deg *= 1.2) {
        AnimExportOptions o;
        o.reduce_rot_deg = deg, o.reduce_pos_m = deg / 100;  // 1 deg ~ 1 cm at the hand, from the hip
        const AnimFile f = export_anim(skel(), c, o).file;
        if (measured(rig, c, f) <= err) local = std::min(local, key_count(f));
    }
    CHECK(local != SIZE_MAX);
    CHECK(key_count(w) <= local);
}

TEST(world_reduce_stays_within_the_error_after_import) {
    const Clip c = dance();
    Rig rig(skel());
    for (double mm : {1.0, 3.0}) {
        const AnimFile w = export_anim(skel(), c, world(mm)).file;
        AnimFile parsed;
        std::string err;
        CHECK(parse_anim(write_anim(w), parsed, err));
        const Clip back = import_anim(skel(), parsed).clip;
        CHECK_EQ(back.end_frame, c.end_frame);
        const double quant = 2e-4;  // 16-bit rotations and positions
        double worst = 0, played = 0;
        for (int fr = 0; fr <= c.end_frame; ++fr) {
            const std::vector<Xform> want = evaluate(rig, c, fr, nullptr).globals;
            const std::vector<Xform> got = skel().global_pose(evaluate_curves(skel(), back, fr));
            const std::vector<Xform> sl = skel().global_pose(anim_pose(skel(), parsed, double(fr) / c.fps));
            for (int n = 0; n < skel().volume_start(); ++n) {
                worst = std::max(worst, (want[n].pos - got[n].pos).length());
                played = std::max(played, (want[n].pos - sl[n].pos).length());
            }
        }
        CHECK(played <= mm / 1000 + quant);  // as SL plays it
        CHECK(worst <= mm / 1000 + quant);   // imported back as a clip
    }
}

TEST(world_reduce_keeps_set_keys_and_the_60_frame_rule) {
    Clip c;
    c.fps = 30;
    c.end_frame = 300;
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 17, {0, 0, 0.001});  // a key that changes nothing is still kept
    key_euler(c, "mShoulderLeft", 43, {0, 0, 40});
    key_euler(c, "mShoulderLeft", 300, {0, 0, 40});
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 250, {0.2, 0, 0});
    const AnimFile f = export_anim(skel(), c, world(5)).file;
    for (const AnimJoint& j : f.joints) {
        const std::vector<int> rot = frames_of(f, j.rot, c.fps), pos = frames_of(f, j.pos, c.fps);
        if (j.name == "mShoulderLeft")
            for (int k : {0, 17, 43, 300}) CHECK(std::count(rot.begin(), rot.end(), k) == 1);
        if (j.name == "mPelvis")
            for (int k : {0, 250}) CHECK(std::count(pos.begin(), pos.end(), k) == 1);
        for (const auto* keys : {&rot, &pos})
            for (size_t i = 1; i < keys->size(); ++i) CHECK((*keys)[i] - (*keys)[i - 1] <= 60);
    }
}

TEST(fit_to_250kb_raises_the_world_tolerance_when_that_mode_is_on) {
    Clip c;
    c.end_frame = 1200;
    for (int b = 1; b <= 30; ++b) {
        FCurve& curve = c.curves[skel()[b].name]["rot_x"];
        for (int f = 0; f <= 1200; f += 10) {
            Key k;
            k.frame = f, k.value = 30 * std::sin(f * 0.02 + b), k.interp = Interp::Bezier;
            curve.keys.push_back(k);
        }
        curve.recompute_handles();
    }
    Rig rig(skel());
    AnimExportOptions opt{0, 0, 60};
    opt.reduce_world_m = 1e-6;
    CHECK(write_anim(export_anim(skel(), c, opt).file).size() >= kAnimMaxUploadBytes);
    BudgetFit fit = fit_anim_budget(rig, c, opt);
    CHECK(fit.fits && fit.steps > 0);
    CHECK(fit.world_m > opt.reduce_world_m);
    CHECK(fit.rot_deg == 0 && fit.pos_m == 0);  // the per-bone tolerances are left alone
    AnimExportOptions got{0, 0, 60};
    got.reduce_world_m = fit.world_m;
    CHECK(write_anim(export_anim(skel(), c, got).file).size() == fit.bytes);
    CHECK(fit.max_mm <= fit.world_m * 1000 + 0.2);  // within what was asked, anywhere on the body
}
