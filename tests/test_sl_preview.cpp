// Preview as SL plays it and the upload meter (08 SP, UM).
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/edit.h"
#include "vats/rig.h"
#include "vats/sl_preview.h"

using namespace vats;

namespace {

// A waving left arm, a bobbing hip, a turning head, a smiling mouth corner and a keyed Chest attachment point.
Clip wave() {
    Clip c;
    c.fps = 30;
    c.end_frame = 60;
    for (int f = 0; f <= 60; f += 15) {
        key_euler(c, "mShoulderLeft", f, {f % 30 ? 60.0 : 0.0, 0, f * 1.5});
        key_euler(c, "mElbowLeft", f, {0, 0, f % 30 ? 80.0 : 10.0});
        key_offset(c, "mPelvis", f, {f * 0.002, 0, f % 30 ? -0.1 : 0.0});
        key_euler(c, "Chest", f, {0, f % 30 ? 45.0 : 0.0, 0});
    }
    key_euler(c, "mHead", 0, {0, 0, -30});
    key_euler(c, "mHead", 60, {0, 0, 30});
    key_euler(c, "mFaceLipCornerLeft", 0, {0, 0, 0});
    key_euler(c, "mFaceLipCornerLeft", 60, {0, 20, 0});
    return c;
}

// Keys every step frames straight into the curve (set_key recomputes every handle each time).
void fill(Clip& c, const std::string& track, const char* ch, int frames, const std::function<double(int)>& value,
          int step = 1, Interp interp = Interp::Linear) {
    FCurve& curve = c.curves[track][ch];
    for (int f = 0; f <= frames; f += step) {
        Key k;
        k.frame = f, k.value = value(f), k.interp = interp;
        curve.keys.push_back(k);
    }
    curve.recompute_handles();
}

const BoneDeviation& deviation_of(const std::vector<BoneDeviation>& d, const char* name) {
    const int n = skel().find(name);
    return *std::find_if(d.begin(), d.end(), [&](const BoneDeviation& x) { return x.node == n; });
}

}  // namespace

TEST(sl_preview_keeps_every_frame_within_quantisation) {
    // Reduction off: SL's playback is the clip but for the 16-bit quantiser, on every bone, IK-free or not.
    const Clip c = wave();
    Rig rig(skel());
    AnimFile f = export_anim(skel(), c, {0, 0, 60}).file;
    std::string err;
    AnimFile back;
    CHECK(parse_anim(write_anim(f), back, err));  // played from the bytes, as SL reads them
    auto d = anim_deviation(rig, c, back, nullptr);
    CHECK(d.size() == size_t(skel().volume_start()));
    CHECK(d.front().mm < 1.0);   // the position quantiser: 10 m over 65535 codes, down a chain
    CHECK(deviation_of(d, "mShoulderLeft").deg < 0.05);
    CHECK(deviation_of(d, "mHead").deg < 0.05);
    for (size_t i = 1; i < d.size(); ++i) CHECK(d[i - 1].mm >= d[i].mm);  // worst first
}

TEST(sl_preview_interpolates_rotations_as_sl_does) {
    // Two keys 120 degrees apart. The clip turns at an even rate; SL nlerps between the two keys it gets, which lags
    // by about 2.2 degrees a quarter of the way. The preview shows SL's, and the table says by how much.
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    fill(c, "mShoulderLeft", "rot_z", 30, [](int f) { return f * 4.0; }, 30);
    Rig rig(skel());
    AnimFile f = export_anim(skel(), c, {5, 0.05, 60}).file;
    const auto& j = *std::find_if(f.joints.begin(), f.joints.end(), [](const AnimJoint& x) { return x.name == "mShoulderLeft"; });
    CHECK(j.rot.size() == 2);  // the tolerance keeps the ends only
    const int n = skel().find("mShoulderLeft");
    const Quat sl = (skel()[n].rest * anim_pose(skel(), f, 0.25, nullptr).rot[n]).normalized();
    const Quat want = nlerp(decode_rotation(j.rot[0]), decode_rotation(j.rot[1]), 0.25 / f.duration);
    CHECK(std::fabs(std::fabs(sl.dot(want)) - 1) < 1e-12);
    // Outside the keys the first and last hold.
    const Quat end = (skel()[n].rest * anim_pose(skel(), f, 5.0, nullptr).rot[n]).normalized();
    CHECK(std::fabs(std::fabs(end.dot(decode_rotation(j.rot[1]))) - 1) < 1e-12);
    const auto devs_1 = anim_deviation(rig, c, f, nullptr);  // kept: the reference below points into it
    const auto& dev = deviation_of(devs_1, "mShoulderLeft");
    CHECK(dev.deg > 1.5 && dev.deg < 3.0);
    CHECK(dev.frame_deg > 0 && dev.frame_deg < 30);
}

TEST(sl_preview_positions_come_back_from_the_worn_avatar) {
    // With Bake shape "Your avatar" the position keys hold the worn joint positions; the preview takes them off again.
    const Skeleton& s = skel();
    Clip c;
    c.end_frame = 10;
    key_offset(c, "mFaceLipCornerLeft", 0, {0, 0, 0});
    key_offset(c, "mFaceLipCornerLeft", 10, {0, 0.004, 0});
    Shape worn;
    worn.scale.assign(s.size(), Vec3{1, 1, 1});
    worn.offset.assign(s.size(), Vec3{});
    const int corner = s.find("mFaceLipCornerLeft");
    worn.offset[corner] = {0.01, -0.02, 0.005};
    AnimExportOptions opt{0, 0, 60};
    opt.positions = &worn;
    AnimFile f = export_anim(s, c, opt).file;
    CHECK((anim_pose(s, f, 10 / 30.0, &worn).offset[corner] - Vec3{0, 0.004, 0}).length() < 2e-4);
    CHECK((anim_pose(s, f, 10 / 30.0, nullptr).offset[corner] - Vec3{0.01, -0.016, 0.005}).length() < 2e-4);
}

TEST(upload_meter_accounts_for_every_byte) {
    const Clip c = wave();
    AnimFile f = export_anim(skel(), c, {0, 0, 60}).file;
    AnimCost cost = anim_cost(skel(), f);
    CHECK(cost.total == write_anim(f).size());
    CHECK(cost.header + cost.records + cost.rot + cost.pos == cost.total);
    size_t keys_rot = 0, keys_pos = 0, parts = 0;
    for (const AnimJoint& j : f.joints) keys_rot += j.rot.size(), keys_pos += j.pos.size();
    CHECK(cost.rot == 8 * keys_rot);
    CHECK(cost.pos == 8 * keys_pos);
    for (const auto& p : cost.parts) parts += p.bytes;
    CHECK(parts == cost.records + cost.rot + cost.pos);
    auto has = [&](const char* name) {
        return std::any_of(cost.parts.begin(), cost.parts.end(), [&](const AnimCost::Part& p) { return p.name == name; });
    };
    CHECK(has("Face") && has("Attachment points") && has("Left Arm") && has("Torso") && has("Head"));
    for (size_t i = 1; i < cost.parts.size(); ++i) CHECK(cost.parts[i - 1].bytes >= cost.parts[i].bytes);
}

TEST(fit_to_250kb_raises_the_tolerance_until_it_fits) {
    // 30 bones keyed every 10 frames for 40 s: over the limit with every frame kept, smooth between the keys. (The
    // keys themselves always stay, so a clip keyed on every frame gets nothing from the tolerance.)
    Clip c;
    c.end_frame = 1200;
    for (int b = 1; b <= 30; ++b)
        fill(c, skel()[b].name, "rot_x", 1200, [&](int f) { return 30 * std::sin(f * 0.02 + b); }, 10, Interp::Bezier);
    Rig rig(skel());
    AnimExportOptions opt{0, 0, 60};
    CHECK(write_anim(export_anim(skel(), c, opt).file).size() >= kAnimMaxUploadBytes);
    BudgetFit fit = fit_anim_budget(rig, c, opt);
    CHECK(fit.fits && !fit.too_long);
    CHECK(fit.steps > 0);
    CHECK(fit.bytes < kAnimMaxUploadBytes);
    AnimExportOptions got{fit.rot_deg, fit.pos_m, 60};
    CHECK(write_anim(export_anim(skel(), c, got).file).size() == fit.bytes);  // what the export will write
    CHECK(fit.max_deg > 0 && fit.max_deg < fit.rot_deg * 30);  // reported, and bounded by the tolerance down a chain
    CHECK(!fit.worst_deg.empty());
}

TEST(fit_to_250kb_gives_up_on_noise_and_on_long_clips) {
    Clip noisy;
    noisy.end_frame = 1500;
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> deg(-40, 40);
    for (int b = 1; b <= 30; ++b) fill(noisy, skel()[b].name, "rot_x", 1500, [&](int) { return deg(rng); });
    Rig rig(skel());
    BudgetFit fit = fit_anim_budget(rig, noisy, {0.05, 0.0005, 60});
    CHECK(!fit.fits && !fit.too_long);
    CHECK_NEAR(fit.rot_deg, 5, 1e-12);
    CHECK_NEAR(fit.pos_m, 0.05, 1e-12);
    Clip longer = wave();
    longer.end_frame = 61 * 30;
    CHECK(fit_anim_budget(rig, longer, {}).too_long);
}

TEST(sl_preview_hash_follows_what_reaches_the_file) {
    const Clip c = wave();
    const std::uint64_t h = anim_hash(c);
    CHECK(anim_hash(c) == h);
    Clip k = c;
    k.curves["mHead"]["rot_z"].keys[1].value += 1e-9;
    CHECK(anim_hash(k) != h);
    Clip r = c;
    r.export_settings.set("reduce", Json::array());
    CHECK(anim_hash(r) != h);
    Clip m = c;
    m.mirror_export = true;
    CHECK(anim_hash(m) != h);
    Clip p = c;
    p.props.push_back(Prop{});  // props never reach the .anim
    CHECK(anim_hash(p) == h);
    Shape s;
    s.scale.assign(skel().size(), Vec3{1, 1, 1});
    s.offset.assign(skel().size(), Vec3{});
    CHECK(anim_hash(c, &s) != h);
    const std::uint64_t hs = anim_hash(c, &s);
    s.offset[3].x = 0.01;
    CHECK(anim_hash(c, &s) != hs);
}
