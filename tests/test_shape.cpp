#include <fstream>
#include <iterator>

#include "check.h"
#include "fixtures.h"
#include "vats/shape.h"

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

Vec3 joint(const Shape* s, const char* name) { return skel().global_pose(Pose(skel().size()), s)[skel().find(name)].pos; }

}  // namespace

bool has_morph(const BodyShape& b, int mesh, const char* name, float w) {
    for (auto& [n, v] : b.morphs[mesh])
        if (n == name) return std::fabs(v - w) < 1e-6f;
    return false;
}

TEST(shape_sl_default_female) {
    BodyShape b = sl_default_shape(skel(), params(), false);
    const Shape* s = &b.shape;
    // Param defaults reached directly (Arm Length .6, Shoulders -.5) and through drivers (Head Size .5).
    CHECK_NEAR(s->scale[skel().find("mShoulderLeft")].y, 1.12, 1e-6);
    CHECK_NEAR(s->scale[skel().find("mHead")].x, 0.925, 1e-6);
    CHECK_NEAR(s->offset[skel().find("mCollarLeft")].y, -0.01, 1e-6);
    // Morphs: closed lips (Express_Closed_Mouth 1), nothing male.
    CHECK(has_morph(b, ShapeHead, "Express_Closed_Mouth", 1));
    CHECK(has_morph(b, ShapeEyelashes, "Eye_Spread", 2.0f / 3));
    CHECK(!has_morph(b, ShapeHead, "Male_Head", 1));
    CHECK(b.morphs[ShapeUpperBody].empty());

    // Differs from the unshaped skeleton; feet planted; symmetric; plausible heights.
    Vec3 sh = joint(s, "mShoulderLeft"), wr = joint(s, "mWristLeft");
    CHECK(std::fabs(sh.y - joint(nullptr, "mShoulderLeft").y) > 0.01);
    CHECK((wr - sh).length() > (joint(nullptr, "mWristLeft") - joint(nullptr, "mShoulderLeft")).length() + 0.02);
    CHECK_NEAR(joint(s, "mFootLeft").z, joint(nullptr, "mFootLeft").z, 1e-9);
    CHECK_NEAR(joint(s, "mFootRight").z, joint(nullptr, "mFootRight").z, 1e-9);
    CHECK_NEAR(joint(s, "mWristRight").y, -wr.y, 1e-9);
    CHECK_NEAR(joint(s, "mPelvis").z, 1.067, 1e-3);
    CHECK_NEAR(joint(s, "mHead").z, 1.683, 1e-3);
    CHECK_NEAR(sh.y, 0.151, 1e-3);
    CHECK_NEAR(wr.y, 0.6707, 1e-3);
}

TEST(shape_sl_default_male) {
    BodyShape f = sl_default_shape(skel(), params(), false), m = sl_default_shape(skel(), params(), true);
    CHECK(joint(&m.shape, "mPelvis").z > joint(&f.shape, "mPelvis").z + 0.03);
    CHECK_NEAR(joint(&m.shape, "mFootLeft").z, joint(nullptr, "mFootLeft").z, 1e-9);
    // The legs and spine carry no default besides "male": they match SK-28's male shape.
    for (const char* j : {"mPelvis", "mChest", "mKneeLeft", "mFootLeft"})
        CHECK_NEAR((joint(&m.shape, j) - joint(&skel().male_shape(), j)).length(), 0, 1e-9);
    CHECK(has_morph(m, ShapeHead, "Male_Head", 1));
    CHECK(has_morph(m, ShapeUpperBody, "Male_Torso", 1));
    CHECK(has_morph(m, ShapeLowerBody, "Male_Legs", 1));
    CHECK(has_morph(m, ShapeHead, "Express_Closed_Mouth", 1));
}

TEST(shape_sex_gated_param) {
    // Male_Package (driver 879 -> skeleton 30879, sex="male") only applies to a male avatar.
    int groin = skel().find("mGroin");
    BodyShape f = evaluate_shape(skel(), params(), {{879, 2.0f}});
    BodyShape m = evaluate_shape(skel(), params(), {{879, 2.0f}, {80, 1.0f}});
    CHECK_NEAR(f.shape.scale[groin].x, 1, 1e-9);
    CHECK_NEAR(m.shape.scale[groin].x, 2, 1e-6);
    // Overrides are clamped to the param's range, as the viewer's setWeight does.
    BodyShape c = evaluate_shape(skel(), params(), {{879, 50.0f}, {80, 1.0f}});
    CHECK_NEAR(c.shape.scale[groin].x, 2, 1e-6);
}
