// Height-variant exports (08 HV, idea 43).
#include <cmath>
#include <fstream>
#include <iterator>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/edit.h"
#include "vats/height_variant.h"
#include "vats/rig.h"
#include "vats/sl_preview.h"

using namespace vats;

namespace {

const AvatarParams& params() {
    static AvatarParams p = [] {
        std::ifstream f(std::string(VATS_DATA_DIR) + "/avatar_lad.xml", std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        AvatarParams out;
        std::string err;
        if (!parse_avatar_params(text, out, err)) {
            std::fprintf(stderr, "cannot parse avatar_lad.xml: %s\n", err.c_str());
            std::exit(2);
        }
        return out;
    }();
    return p;
}

// Hips that sway and drop, the left arm bent at the elbow so a held wrist stays within reach.
Clip sway() {
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 30, {0.06, -0.04, -0.05});
    key_euler(c, "mPelvis", 0, {0, 0, 0});
    key_euler(c, "mPelvis", 30, {3, 5, 12});
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 30, {0, 20, 0});
    key_euler(c, "mElbowLeft", 0, {0, 0, -100});
    return c;
}

}  // namespace

TEST(height_shape_reaches_each_preset) {
    const Shape* none = nullptr;
    const BodyShape def = sl_default_shape(skel(), params(), false);
    const double ruth = avatar_height(skel(), &def.shape);
    CHECK_NEAR(ruth, 1.8835, 1e-3);  // the shape editor's 1.88 m
    CHECK(avatar_height(skel(), none) > 1.8);
    for (bool male : {false, true})
        for (double h : kHeightPresets) {
            double reached = 0;
            const BodyShape b = height_shape(skel(), params(), male, h, &reached);
            CHECK_NEAR(reached, h, 1e-3);
            CHECK_NEAR(avatar_height(skel(), &b.shape), reached, 1e-9);
            // Feet stay on the ground (SK-28): only the hips and what is above them move up.
            const int foot = skel().find("mFootLeft");
            CHECK_NEAR(skel().global_pose(Pose(skel().size()), &b.shape)[foot].pos.z,
                       skel().global_pose(Pose(skel().size()))[foot].pos.z, 1e-9);
        }
    double low = 0, high = 0;  // out of the slider's range: its ends
    height_shape(skel(), params(), false, 0.5, &low);
    height_shape(skel(), params(), false, 5, &high);
    CHECK(low > 0.5 && high < 5 && low < 1.75 && high > 2.15);
}

TEST(height_variant_names_and_count) {
    ExportNaming n{"Wave", 1, "Right", "[NAME]_[#]_[SIDE]", ""};
    Json ex = Json::object();
    Json hs = Json::array();
    for (double h : {1.75, 2.153, 9.0, 1.95}) hs.push(h);  // 9 m clamps to 2.4; the fourth is one too many
    ex.set("heights", hs);
    const std::vector<double> heights = export_heights(ex);
    CHECK_EQ(heights.size(), size_t(3));
    CHECK_NEAR(heights[2], kHeightMax, 1e-12);
    CHECK_EQ(height_tag(2.153), std::string("H215"));
    CHECK(export_heights(Json::object()).empty());

    const std::vector<AnimVariant> one = anim_variants(false, false, {});
    CHECK_EQ(one.size(), size_t(1));  // off: the export as before
    const std::vector<AnimVariant> v = anim_variants(false, true, {1.75, 2.15});
    CHECK_EQ(v.size(), size_t(6));
    std::vector<std::string> names;
    for (const AnimVariant& a : v) names.push_back(variant_file_name(n, "", a, "anim"));
    const char* want[] = {"Wave_01_Right.anim",      "Wave_01_Left.anim",      "Wave_01_Right_H175.anim",
                          "Wave_01_Left_H175.anim", "Wave_01_Right_H215.anim", "Wave_01_Left_H215.anim"};
    for (size_t i = 0; i < names.size(); ++i) CHECK_EQ(names[i], std::string(want[i]));
    n.side.clear();
    n.actor = "Lead";
    CHECK_EQ(variant_file_name(n, "", {true, 1.95}, "anim"), std::string("Wave_01_Lead_mirrored_H195.anim"));
}

TEST(height_variant_pinned_hand_stays_on_its_target) {
    Rig rig(skel());
    Clip c = sway();
    const int wrist = skel().find("mWristLeft");
    std::string why;
    CHECK(pin_here(c, rig, 0, wrist, -1, nullptr, why));
    const Vec3 held = c.pins[0].pos;
    for (double h : kHeightPresets) {
        const BodyShape body = height_shape(skel(), params(), false, h);
        AnimExportOptions opt{0, 0, 60};
        opt.shape = &body.shape;  // re-solved on this body
        AnimFile f = export_anim(skel(), c, opt).file;
        double off = 0;
        for (int fr = 0; fr <= c.end_frame; ++fr) {
            const Pose p = anim_pose(skel(), f, double(fr) / c.fps);
            off = std::max(off, (skel().global_pose(p, &body.shape)[wrist].pos - held).length());
        }
        CHECK(off < 1e-3);
        // Baked on the default body instead, the tall and short ones miss the target.
        opt.shape = nullptr;
        f = export_anim(skel(), c, opt).file;
        const Pose p = anim_pose(skel(), f, 1.0);
        if (std::fabs(h - avatar_height(skel(), nullptr)) > 0.1)
            CHECK((skel().global_pose(p, &body.shape)[wrist].pos - held).length() > 0.01);
    }
}
