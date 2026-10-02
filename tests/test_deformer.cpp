// Spec 09 section 0l: the deformer tool. End at rest, Hold without sinking (mSkull counter-keys) and the undeformer.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "check.h"
#include "fixtures.h"
#include "vats/deformer.h"
#include "vats/edit.h"

using namespace vats;

namespace {

// A neck that grows `up` metres over a second (keys at 0 and 30), and a head scale of `head` on the bake shape.
Clip neck_stretch(double up) {
    Clip c;
    c.fps = 30, c.end_frame = 30, c.priority = 3;
    key_offset(c, "mNeck", 0, {});
    key_offset(c, "mNeck", 30, {0, 0, up});
    key_euler(c, "mShoulderLeft", 0, {});
    key_euler(c, "mShoulderLeft", 30, {0, 0, 20});
    return c;
}

Shape head_scaled(double head) {
    Shape s;
    s.scale.assign(size_t(skel().size()), Vec3{1, 1, 1});
    s.offset.assign(size_t(skel().size()), Vec3{});
    s.scale[size_t(skel().find("mHead"))] = {head, head, head};
    return s;
}

// The largest change of SL's height from rest over the clip's frames, on shape.
double worst_height_change(const Clip& c, const Shape* shape) {
    const double rest = sl_body_size(skel(), nullptr, shape).height;
    double worst = 0;
    for (int f = 0; f <= c.end_frame; ++f) {
        const Pose p = evaluate_curves(skel(), c, f);
        worst = std::max(worst, std::fabs(sl_body_size(skel(), &p, shape).height - rest));
    }
    return worst;
}

const AnimJoint* joint(const AnimFile& f, const char* name) {
    for (const AnimJoint& j : f.joints)
        if (j.name == name) return &j;
    return nullptr;
}

// An in-world test, 2026-09-29 (longneck.anim and longneck_skullcomp.anim, verified: the neck
// long, the feet on the ground): the neck's key times and Z codes, and the mSkull Z codes counter-keyed by hand.
const std::uint16_t kTimes[] = {0,     2184,  4369,  6553,  8738,  10922, 13107, 15291, 17476, 19660,
                                21845, 24029, 26214, 28398, 37136, 39321, 41505, 43690, 45874, 48059,
                                50243, 52428, 54612, 56797, 58981, 61166, 63350, 65535};
const std::uint16_t kNeckZ[] = {34413, 34442, 34527, 34665, 34850, 35079, 35349, 35654, 35992, 36357,
                                36746, 37156, 37581, 38019, 39808, 40246, 40671, 41080, 41470, 41835,
                                42173, 42478, 42747, 42977, 43162, 43299, 43385, 43414};
const std::uint16_t kSkullZ[] = {33285, 33264, 33204, 33107, 32976, 32814, 32623, 32407, 32168, 31910,
                                 31635, 31345, 31045, 30735, 29470, 29160, 28860, 28571, 28295, 28037,
                                 27798, 27582, 27392, 27229, 27098, 27001, 26941, 26920};

}  // namespace

TEST(deformer_hold_keeps_sl_height_at_rest) {
    for (double up : {0.5, 1.374, -0.15}) {  // taller, the in-world test's neck, and shorter (the skull goes up)
        for (double head : {1.0, 1.2}) {
            const Shape shape = head_scaled(head);
            Clip c = neck_stretch(up);
            CHECK(worst_height_change(c, &shape) > std::fabs(up) - 1e-9);
            const HoldResult h = hold_without_sinking(c, skel(), &shape);
            CHECK(h.keyed && h.refused.empty() && h.head_scale_known && !h.combined && h.short_frames.empty());
            CHECK(std::fabs(h.head_scale - head) < 1e-12);
            CHECK(worst_height_change(c, &shape) < 4e-4);  // the bake's 0.2 mm, times the √2 × head it counts
            // The skull moved by the growth over √2 × head scale, the other way.
            CHECK(std::fabs(curve_offset(c, "mSkull", 30).z + up / (std::sqrt(2.0) * head)) < 3e-4);
            CHECK(std::fabs(curve_offset(c, "mSkull", 0).z) < 3e-4);
            CHECK(curve_offset(c, "mNeck", 30).z == up);  // the neck stays long
        }
    }
    // The export counters on the worn avatar's head (positions: Your avatar) rather than the bake shape's.
    const Shape worn = head_scaled(1.1), bake = head_scaled(0.925);
    AnimExportOptions yours;
    yours.shape = &bake, yours.positions = &worn, yours.hold_without_sinking = true;
    const AnimJoint* sk = joint(export_anim(skel(), neck_stretch(0.5), yours).file, "mSkull");
    const double base = skel()[skel().find("mSkull")].pos.z;
    CHECK(sk && std::fabs(decode_position(sk->pos.back()).z - (base - 0.5 / (std::sqrt(2.0) * 1.1))) < 5e-4);
    // Without a bake shape: head scale 1, assumed, and said so.
    Clip c = neck_stretch(0.5);
    const HoldResult h = hold_without_sinking(c, skel(), nullptr);
    CHECK(h.keyed && !h.head_scale_known && h.head_scale == 1);
    CHECK(worst_height_change(c, nullptr) < 4e-4);
    // Applying it again changes nothing (the height is already at rest).
    const Clip again = c;
    CHECK(!hold_without_sinking(c, skel(), nullptr).keyed);
    CHECK(c.curves == again.curves);
}

TEST(deformer_hold_combines_with_own_skull_keys_and_refuses_skull_only) {
    // mSkull already keyed (sideways and up): the counter-move is added to its own keys.
    Clip c = neck_stretch(0.4);
    key_offset(c, "mSkull", 0, {0.01, 0, 0});
    key_offset(c, "mSkull", 30, {0.01, 0, 0.05});
    const HoldResult h = hold_without_sinking(c, skel(), nullptr);
    CHECK(h.keyed && h.combined);
    CHECK(worst_height_change(c, nullptr) < 4e-4);
    CHECK(std::fabs(curve_offset(c, "mSkull", 30).z - (0.05 - (0.4 + std::sqrt(2.0) * 0.05) / std::sqrt(2.0))) < 3e-4);
    CHECK(std::fabs(curve_offset(c, "mSkull", 15).x - 0.01) < 3e-4);  // its own sideways move stays
    // Only mSkull changes the height: countering it on mSkull would undo it, so nothing happens and it says why.
    Clip skull;
    skull.fps = 30, skull.end_frame = 30;
    key_offset(skull, "mSkull", 0, {0, 0, 0.1});
    const Clip before = skull;
    const HoldResult r = hold_without_sinking(skull, skel(), nullptr);
    CHECK(!r.keyed && r.refused.find("only mSkull") != std::string::npos);
    CHECK(skull.curves == before.curves);
    // Nothing that counts keyed (a spine stretch): nothing to do, no refusal.
    Clip spine;
    spine.fps = 30, spine.end_frame = 30;
    key_offset(spine, "mSpine2", 30, {0, 0, 0.3});
    const HoldResult s = hold_without_sinking(spine, skel(), nullptr);
    CHECK(!s.keyed && s.refused.empty() && !spine.curves.count("mSkull"));
}

TEST(deformer_hold_stops_at_the_5m_limit) {
    // 16 m taller would need the skull 11 m down: it stops at -5 m (written), and the frames past it are reported.
    Clip c;
    c.fps = 30, c.end_frame = 30;
    for (const char* b : {"mTorso", "mChest", "mNeck", "mHead"}) {
        key_offset(c, b, 0, {});
        key_offset(c, b, 30, {0, 0, 4.0});
    }
    const HoldResult h = hold_without_sinking(c, skel(), nullptr);
    CHECK(h.keyed && !h.short_frames.empty() && h.short_frames.back() == 30);
    const double base = skel()[skel().find("mSkull")].pos.z;
    CHECK(std::fabs(base + curve_offset(c, "mSkull", 30).z + 5.0) < 1e-6);
    CHECK(std::fabs(h.shortfall - (16.0 - std::sqrt(2.0) * (5.0 + base))) < 1e-3);
    // The export says so.
    AnimExportOptions opt;
    opt.hold_without_sinking = true;
    Clip raw;
    raw.fps = 30, raw.end_frame = 30;
    for (const char* b : {"mTorso", "mChest", "mNeck", "mHead"}) key_offset(raw, b, 30, {0, 0, 4.0}), key_offset(raw, b, 0, {});
    const AnimExportResult r = export_anim(skel(), raw, opt);
    CHECK(std::any_of(r.warnings.begin(), r.warnings.end(), [](const std::string& w) { return w.find("5 m position limit") != std::string::npos; }));
}

TEST(deformer_hold_reproduces_inworld_longneck_skullcomp) {
    // longneck.anim's neck, imported; Hold without sinking with no bake shape (head scale 1); exported with every
    // frame kept. mSkull's Z codes at its 28 key times match the hand-made file's within the quantiser's step
    // (0.15 mm).
    AnimFile f;
    f.duration = 1, f.base_priority = 3, f.ease_in = f.ease_out = 0.3f, f.loop_out = 1;
    AnimJoint neck;
    neck.name = "mNeck";
    neck.priority = 3;
    for (size_t i = 0; i < std::size(kTimes); ++i) neck.pos.push_back({kTimes[i], 32705, 32767, kNeckZ[i]});
    f.joints.push_back(neck);
    AnimImportResult imp = import_anim(skel(), f, 30);
    Clip c = imp.clip;
    CHECK(c.end_frame == 30);
    AnimExportOptions opt;
    opt.reduce_pos_m = 0, opt.reduce_rot_deg = 0;
    opt.hold_without_sinking = true;
    const AnimExportResult r = export_anim(skel(), c, opt);
    CHECK(r.errors.empty());
    const AnimJoint* skull = joint(r.file, "mSkull");
    CHECK(skull && skull->pos.size() == 31 && skull->priority == 3);
    if (!skull) return;
    int worst = 0;
    for (size_t i = 0; i < std::size(kTimes); ++i) {
        const auto k = std::find_if(skull->pos.begin(), skull->pos.end(), [&](const auto& p) { return p[0] == kTimes[i]; });
        CHECK(k != skull->pos.end());
        if (k == skull->pos.end()) continue;
        worst = std::max(worst, std::abs(int((*k)[3]) - int(kSkullZ[i])));
        CHECK(std::abs(int((*k)[1]) - 32768) <= 1 && std::abs(int((*k)[2]) - 32768) <= 1);  // x and y at rest
    }
    CHECK(worst <= 2);  // the viewer's quantiser drops up to 2 codes re-encoding (anim_code)
    // The neck is written as it came.
    const AnimJoint* n = joint(r.file, "mNeck");
    CHECK(n && n->pos.size() == 31 && n->pos.back()[3] == kNeckZ[std::size(kNeckZ) - 1]);
}

TEST(deformer_end_at_rest_adds_rest_keys) {
    Clip c = neck_stretch(0.3);
    key_offset(c, "mHead", 0, {});
    key_offset(c, "mHead", 10, {0, 0, 0.1});  // held from frame 10: the rest key must not bend the hold
    key_offset(c, "mPelvis", 0, {});
    key_offset(c, "mPelvis", 30, {0.1, 0, -0.05});  // the hip: pelvis_fix takes it back, so it is left alone
    key_offset(c, "mSpine2", 30, {0, 0, 0.1});       // a bone the height ignores still gets its rest key
    key_offset(c, "mKneeLeft", 0, {});
    key_offset(c, "mKneeLeft", 15, {0, 0, 0.05});
    key_offset(c, "mKneeLeft", 30, {});  // already at rest on the last frame: nothing to add
    const Clip before = c;
    const std::vector<std::string> keyed = end_at_rest(c, skel());
    CHECK((std::set<std::string>(keyed.begin(), keyed.end()) == std::set<std::string>{"mNeck", "mHead", "mSpine2"}));
    CHECK(c.end_frame == 31);
    for (const char* b : {"mNeck", "mHead", "mSpine2", "mKneeLeft"}) {
        CHECK(curve_offset(c, b, 31).length() < 1e-12);  // at rest one frame after the old last one
        for (int f = 0; f <= 30; ++f) CHECK((curve_offset(c, b, f) - curve_offset(before, b, f)).length() < 1e-9);
    }
    CHECK(curve_offset(c, "mPelvis", 31).length() > 0.1);
    // The export option: one more frame, and every position-keyed bone but the hip written at rest at the end.
    AnimExportOptions opt;
    opt.end_at_rest = true;
    const AnimExportResult r = export_anim(skel(), before, opt);
    CHECK(std::fabs(r.file.duration - 31.0f / 30) < 1e-6);
    for (const AnimJoint& j : r.file.joints) {
        if (j.pos.empty() || j.name == "mPelvis") continue;
        const Vec3 last = decode_position(j.pos.back());
        CHECK(j.pos.back()[0] == 65535);
        CHECK((last - skel()[skel().find(j.name)].pos).length() < 5e-4);  // within 2 codes a component
    }
    // Both together: rest at the end and no sink on any frame.
    opt.hold_without_sinking = true;
    const Clip both = with_deformer_options(skel(), before, opt);
    CHECK(worst_height_change(both, nullptr) < 4e-4);
    CHECK(curve_offset(both, "mSkull", 31).length() < 3e-4 && curve_offset(both, "mNeck", 31).length() < 1e-12);
    // A loop stops wherever it is: the export says the rest key helps only when it plays to the end.
    Clip loop = before;
    loop.loop = true, loop.loop_in = 0, loop.loop_out = 30;
    opt.hold_without_sinking = false;
    const AnimExportResult lr = export_anim(skel(), loop, opt);
    CHECK(std::any_of(lr.warnings.begin(), lr.warnings.end(), [](const std::string& w) { return w.find("loops") != std::string::npos; }));
}

TEST(deformer_undeformer_rests_exactly_the_deformed_joints) {
    Clip c = neck_stretch(0.5);
    key_offset(c, "mHead", 30, {0, 0, 0.1});
    key_offset(c, "mSpine2", 30, {0, 0.02, 0.1});
    key_offset(c, "mPelvis", 30, {0.2, 0, 0});  // hip travel, not a deformation
    c.joint_priority["mHead"] = 5;
    c.priority = 4;
    AnimExportOptions opt;
    opt.hold_without_sinking = true;
    const AnimExportResult r = export_anim(skel(), c, opt);
    const AnimFile u = make_undeformer(skel(), r.file);
    std::set<std::string> names;
    for (const AnimJoint& j : u.joints) {
        names.insert(j.name);
        CHECK(j.rot.empty() && j.pos.size() == 2);
        CHECK(j.pos[0][0] == 0 && j.pos[1][0] == 65535);
        const Vec3 at = decode_position(j.pos[1]);
        CHECK((at - skel()[skel().find(j.name)].pos).length() < 5e-4);
        CHECK(j.pos[0][1] == j.pos[1][1] && j.pos[0][2] == j.pos[1][2] && j.pos[0][3] == j.pos[1][3]);
        CHECK(j.priority == joint(r.file, j.name.c_str())->priority);
    }
    CHECK((names == std::set<std::string>{"mNeck", "mHead", "mSkull", "mSpine2"}));
    CHECK(u.base_priority == 4 && joint(u, "mHead")->priority == 5);
    CHECK(std::fabs(u.duration - kUndeformSeconds) < 1e-9 && u.loop == 0 && u.ease_in == 0 && u.ease_out == 0);
    CHECK(validate_anim(u, skel(), true).empty());
    // Written from the worn avatar's joint positions when the export uses them.
    Shape worn = head_scaled(1);
    worn.offset[size_t(skel().find("mNeck"))] = {0, 0, 0.03};
    const AnimFile yours = make_undeformer(skel(), r.file, &worn);
    CHECK(std::fabs(decode_position(joint(yours, "mNeck")->pos[0]).z - (skel()[skel().find("mNeck")].pos.z + 0.03)) < 5e-4);
    CHECK(undeformer_name("dab_1") == "dab_1_undeform");
    // No position keys: no joints.
    Clip turn;
    key_euler(turn, "mShoulderLeft", 30, {0, 0, 20});
    CHECK(make_undeformer(skel(), export_anim(skel(), turn, {}).file).joints.empty());
}
