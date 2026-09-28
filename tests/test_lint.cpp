// The Animation Check (08 CK): one broken clip per rule, the rule's fix, and a clean clip that passes.
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/lint.h"

using namespace vats;

namespace {

// A clean one-second loop: the hips and shoulders swing and come back, priority 4, short eases.
Clip reference() {
    Clip c;
    c.fps = 30, c.end_frame = 30;
    c.loop = true, c.loop_in = 0, c.loop_out = 30;
    c.priority = 4;
    c.ease_in = c.ease_out = 0.3;
    for (auto [bone, axis, amount] : {std::tuple{"mHipLeft", 1, 20.0}, {"mHipRight", 1, -20.0},
                                      {"mShoulderLeft", 0, 30.0}, {"mShoulderRight", 0, -30.0}}) {
        Vec3 e;
        key_euler(c, bone, 0, e);
        e[axis] = amount;
        key_euler(c, bone, 15, e);
        key_euler(c, bone, 30, {});
    }
    return c;
}

std::set<std::string> rules(const std::vector<LintFinding>& fs) {
    std::set<std::string> r;
    for (auto& f : fs) r.insert(f.rule);
    return r;
}

// The broken clip reports this rule alone; applying its fixes clears it. Returns the fixed clip.
Clip broken(const char* rule, Clip c, const AnimExportOptions& opt = {}) {
    auto fs = lint_clip(skel(), c, opt);
    if (rules(fs) != std::set<std::string>{rule}) {
        std::string got;
        for (auto& f : fs) got += " " + f.rule + ": " + f.message + ";";
        check::fail(__FILE__, __LINE__, std::string(rule) + " fixture reports:" + got);
        return c;
    }
    for (auto& f : fs) {
        CHECK(!f.message.empty());
        CHECK(bool(f.fix.apply));
        if (f.fix.apply) f.fix.apply(c);
    }
    auto after = lint_clip(skel(), c, opt);
    if (rules(after).count(rule)) check::fail(__FILE__, __LINE__, std::string(rule) + " survives its fix");
    return c;
}

void rot(Clip& c, const char* bone, int axis, std::initializer_list<std::pair<int, double>> keys) {
    for (auto [f, v] : keys) {
        Vec3 e = curve_euler(c, bone, f);
        e[axis] = v;
        key_euler(c, bone, f, e);
    }
}

}  // namespace

TEST(lint_reference_is_clean) {
    const auto fs = lint_clip(skel(), reference(), {});
    for (auto& f : fs) check::fail(__FILE__, __LINE__, f.rule + ": " + f.message);
    CHECK(lint_rules().size() == 20);
}

// The AO plays its own stands, walks, runs and turns in place of each other: a clip with such an AO state is not
// warned about priority; Typing and Always play over them, and say so.
TEST(lint_ao_priority_follows_the_ao_state) {
    Clip c = reference();
    c.priority = 3;
    for (const char* state : {"Running", "Turning Left", "Walking", "Standing"})
        CHECK(!rules(lint_clip(skel(), c, {}, {}, nullptr, state)).count("ao_priority"));
    CHECK(rules(lint_clip(skel(), c, {}, {}, nullptr, "")).count("ao_priority"));
    const auto fs = lint_clip(skel(), c, {}, {}, nullptr, "Always");
    CHECK(fs.size() == 1 && fs[0].rule == "ao_priority" && fs[0].message.find("Always") != std::string::npos);
}

TEST(lint_rules_can_be_switched_off) {
    Clip c = reference();
    c.priority = 3;
    CHECK(rules(lint_clip(skel(), c, {})).count("ao_priority"));
    CHECK(lint_clip(skel(), c, {}, {"ao_priority"}).empty());
}

TEST(lint_loop_seam) {
    Clip c = reference();
    rot(c, "mShoulderLeft", 0, {{30, 10}});
    broken("loop_seam", c);
}

TEST(lint_hip_drift) {
    Clip c = reference();
    key_offset(c, "mPelvis", 0, {});
    key_offset(c, "mPelvis", 30, {0.5, 0, 0});
    broken("hip_drift", c);
}

TEST(lint_loop_range_reversed) {
    Clip c = reference();
    c.loop_in = 30, c.loop_out = 0;
    broken("loop_range", c);
}

TEST(lint_loop_shorter_than_eases) {
    Clip c = reference();
    c.ease_in = c.ease_out = 0.8;
    c = broken("loop_range", c);
    CHECK(std::fabs(c.ease_in + c.ease_out - 1.0) < 1e-9);  // scaled down together to the 1 s loop
}

TEST(lint_ease_longer_than_the_clip) {
    Clip c = reference();
    c.loop = false;
    c.ease_in = c.ease_out = 0.8;
    broken("ease_long", c);
}

TEST(lint_ease_zero) {
    Clip c = reference();
    c.ease_out = 0;
    broken("ease_zero", c);
}

TEST(lint_subframe_keys) {
    Clip c = reference();
    rot(c, "mShoulderLeft", 1, {{7, 0}});
    c.curves["mShoulderLeft"]["rot_y"].set_key(7.5, 15);
    broken("subframe", c);
}

TEST(lint_nlerp_arc) {
    Clip c = reference();
    rot(c, "mCollarLeft", 1, {{0, 0}, {15, 170}, {30, 0}});  // a twist: the arm stays clear of the body
    c.export_settings.set("reduce", [] {
        Json r = Json::array();
        r.push(20.0), r.push(0.0005);
        return r;
    }());
    broken("nlerp_arc", c);
}

TEST(lint_ao_priority) {
    Clip c = reference();
    c.priority = 3;
    c.joint_priority["mHipLeft"] = 2;
    broken("ao_priority", c);
}

TEST(lint_hover_pop) {
    Clip c = reference();
    key_offset(c, "mKneeLeft", 0, {});
    key_offset(c, "mKneeLeft", 15, {0, 0.05, 0});
    key_offset(c, "mKneeLeft", 30, {});
    broken("hover_pop", c);
}

TEST(lint_frozen_bones) {
    Clip c = reference();
    key_euler(c, "mTail1", 0, {});
    key_euler(c, "mTail1", 30, {});
    broken("frozen_bones", c);
}

TEST(lint_face_positions) {
    Clip c = reference();
    key_offset(c, "mFaceLipCornerLeft", 0, {});
    key_offset(c, "mFaceLipCornerLeft", 15, {0, 0.005, 0});
    key_offset(c, "mFaceLipCornerLeft", 30, {});
    broken("face_positions", c);
    AnimExportOptions yours;  // with the worn avatar's joint positions (Bake shape: Your avatar) they are fine
    Shape worn{std::vector<Vec3>(skel().size(), {1, 1, 1}), std::vector<Vec3>(skel().size())};
    yours.positions = &worn;
    CHECK(lint_clip(skel(), c, yours).empty());
}

TEST(lint_eyes) {
    Clip c = reference();
    rot(c, "mEyeLeft", 2, {{0, 0}, {15, 10}, {30, 0}});
    broken("eyes", c);
}

TEST(lint_expression) {
    Clip c = reference();
    rot(c, "mFaceJaw", 1, {{0, 0}, {15, 10}, {30, 0}});
    c.emote = "express_smile";
    broken("expression", c);
}

TEST(lint_hand_pose) {
    Clip c = reference();
    rot(c, "mHandIndex1Left", 1, {{0, 0}, {15, 30}, {30, 0}});
    c.hand_pose = 3;
    broken("hand_pose", c);
}

TEST(lint_joint_limits) {
    Clip c = reference();
    rot(c, "mKneeLeft", 2, {{0, 0}, {15, 70}, {30, 0}});  // twisted far past the knee's 30 degrees
    broken("joint_limits", c);
}

TEST(lint_ground) {
    Clip below = reference();
    key_offset(below, "mPelvis", 0, {0, 0, -0.1});
    key_offset(below, "mPelvis", 30, {0, 0, -0.1});
    broken("ground", below);
    Clip above = reference();  // no position keys: the fix adds one held key
    for (const char* hip : {"mHipLeft", "mHipRight"}) rot(above, hip, 1, {{0, -30}, {15, -50}, {30, -30}});
    broken("ground", above);
    // The heel sinks about 3 cm while the toes lift: the ankle drops 1.8 cm and no joint goes lower, but the back of
    // the heel does.
    Clip heel = reference();
    key_offset(heel, "mPelvis", 0, {0, 0, -0.018});
    key_offset(heel, "mPelvis", 30, {0, 0, -0.018});
    for (const char* ankle : {"mAnkleLeft", "mAnkleRight"}) rot(heel, ankle, 1, {{0, -30}, {30, -30}});
    const auto fs = lint_clip(skel(), heel, {});
    CHECK(fs.size() == 1 && fs[0].rule == "ground");
    if (!fs.empty()) CHECK(fs[0].message.find("below the ground") != std::string::npos);
    broken("ground", heel);
}

TEST(lint_upload_size) {
    Clip c;
    c.fps = 30, c.end_frame = 900;
    c.ease_in = c.ease_out = 0.3;
    int bones = 0;
    for (int n = 0; n < skel().joint_count() && bones < 60; ++n) {
        const Node& b = skel()[n];
        if (n == 0 || b.name.find("Eye") != std::string::npos || b.category == Category::Face ||
            b.category == Category::Hands || b.name.find("Hip") != std::string::npos ||
            b.name.find("Knee") != std::string::npos || b.name.find("Ankle") != std::string::npos ||
            b.name.find("Foot") != std::string::npos || b.name.find("Toe") != std::string::npos)
            continue;
        ++bones;
        for (int a = 0; a < 3; ++a) {  // a small tremble on every frame, different on every bone (like mocap)
            FCurve& curve = c.curves[b.name][kRotChannels[a]];
            for (int f = 0; f <= c.end_frame; ++f) curve.keys.push_back({double(f), 0.6 * std::sin(f * (1.1 + 0.6 * a) + n * (a + 1))});
            curve.recompute_handles();
        }
    }
    broken("upload_size", c);
}

TEST(lint_duration) {
    Clip c = reference();
    c.loop = false;
    c.end_frame = 61 * 30;
    c = broken("duration", c);
    CHECK(c.end_frame == 1800);
}

TEST(lint_new_project_is_clean_on_eases) {
    Clip c = new_project_clip();
    CHECK(!rules(lint_clip(skel(), c, {})).count("ease_long"));
    c.loop = true;
    CHECK(!rules(lint_clip(skel(), c, {})).count("loop_range"));
    Clip old;  // the struct's (and the file format's) 0.8 s eases are too long for a new one-second clip
    CHECK(rules(lint_clip(skel(), old, {})).count("ease_long"));
}

// Cross-actor contact: one actor's body against another's (self-contact is per actor). An arm reaching forward into a
// partner standing close in front, facing it, is found on the frames it is in; a partner further off is not.
TEST(lint_actor_contact) {
    Clip c;
    c.fps = 30, c.end_frame = 30, c.loop = false, c.priority = 4, c.ease_in = c.ease_out = 0.3;
    key_euler(c, "mShoulderLeft", 0, {-80, 0, 0});    // the left arm hangs at frame 0 and 30,
    key_euler(c, "mShoulderLeft", 15, {-10, 0, -90});  // reaches forward, a little down, at 15
    key_euler(c, "mShoulderLeft", 30, {-80, 0, 0});
    auto partner_at = [&](double x) {
        const Xform place{Quat::axis_angle({0, 0, 1}, kPi), {x, 0, 0}};  // facing the checked actor
        LintPartner p{"Partner", {}};
        std::vector<Xform> g = skel().global_pose(Pose(skel().size()));
        for (Xform& t : g) t = place * t;
        p.frames.assign(31, g);
        return p;
    };
    const auto near = lint_clip(skel(), c, {}, {}, nullptr, "", {partner_at(0.55)});
    int found = 0;
    for (auto& f : near)
        if (f.rule == "actor_contact") {  // one finding per other actor
            ++found;
            CHECK(f.message.find("and Partner's") != std::string::npos);
            CHECK(std::binary_search(f.frames.begin(), f.frames.end(), 15));
            CHECK(!std::binary_search(f.frames.begin(), f.frames.end(), 0));  // the arm down clears the partner
            CHECK(std::find(f.bones.begin(), f.bones.end(), "mWristLeft") != f.bones.end() ||
                  std::find(f.bones.begin(), f.bones.end(), "mElbowLeft") != f.bones.end());
        }
    CHECK_EQ(found, 1);
    CHECK(!rules(lint_clip(skel(), c, {}, {}, nullptr, "", {partner_at(3)})).count("actor_contact"));
    CHECK(!rules(lint_clip(skel(), c, {}, {"actor_contact"}, nullptr, "", {partner_at(0.55)})).count("actor_contact"));
}

// Go to Frame steps through runs of frames: a pose keyed at frame 0 and flagged on every frame goes to 0, not 1.
TEST(lint_goto_frame_steps_through_runs) {
    Clip c = reference();
    c.curves.clear();
    c.loop = false;
    rot(c, "mKneeLeft", 2, {{0, 70}});  // keyed at frame 0 only, held to the end
    const auto fs = lint_clip(skel(), c, {});
    const LintFinding* limit = nullptr;
    for (auto& f : fs)
        if (f.rule == "joint_limits") limit = &f;
    CHECK(limit && limit->frames.size() == 31);
    if (limit) {
        CHECK(lint_goto_frame(limit->frames, 0) == 0);
        CHECK(lint_goto_frame(limit->frames, 12) == 0);
        CHECK(lint_frame_runs(limit->frames) == 1);
    }
    const std::vector<int> two = {3, 4, 5, 20, 21};
    CHECK(lint_goto_frame(two, 0) == 3);
    CHECK(lint_goto_frame(two, 3) == 20);
    CHECK(lint_goto_frame(two, 4) == 20);
    CHECK(lint_goto_frame(two, 10) == 20);
    CHECK(lint_goto_frame(two, 20) == 3);
    CHECK(lint_goto_frame(two, 25) == 3);
    CHECK(lint_frame_runs(two) == 2);
    CHECK(lint_goto_frame({}, 0) == -1);
}
