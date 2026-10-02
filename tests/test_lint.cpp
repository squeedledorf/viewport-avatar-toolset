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
    CHECK(lint_rules().size() == 23);
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

// The loops below move positions mid-loop and come with an undeformer, so position_ends_moved stays quiet.
static Clip undeformed(Clip c) {
    c.export_settings.set("undeformer", true);
    return c;
}

TEST(lint_hover_pop) {
    Clip c = undeformed(reference());
    key_offset(c, "mKneeLeft", 0, {});
    key_offset(c, "mKneeLeft", 15, {0, 0.05, 0});
    key_offset(c, "mKneeLeft", 30, {});
    broken("hover_pop", c);
    // Spec 09 0l: SL's height never reads mSpine1 (LLAvatarAppearance::computeBodySize), and it is not in the leg:
    // lifting it makes the body longer above the hip without popping the avatar up or down. No finding at all.
    Clip spine = undeformed(reference());
    key_offset(spine, "mSpine1", 0, {});
    key_offset(spine, "mSpine1", 15, {0, 0, 0.05});
    key_offset(spine, "mSpine1", 30, {});
    CHECK(rules(lint_clip(skel(), spine, {})).empty());
    bool written = false;
    for (const AnimJoint& j : export_anim(skel(), spine, {}).file.joints) written = written || (j.name == "mSpine1" && !j.pos.empty());
    CHECK(written);
    // The right leg changes the leg length too (both legs stand the avatar), though SL's height reads only the left.
    Clip right = undeformed(reference());
    key_offset(right, "mKneeRight", 0, {});
    key_offset(right, "mKneeRight", 15, {0, 0.05, 0});
    key_offset(right, "mKneeRight", 30, {});
    broken("hover_pop", right);
}

// Spec 09 build 35: a neck-stretch deformer. Every SL viewer takes the avatar's height from the neck and head
// positions, animated ones too, and stands it half the growth lower: 50 cm taller sinks the wearer 25 cm. The check
// says so with the hover to set; its Fix, Hold Without Sinking (09 0l), counter-keys mSkull and clears it.
TEST(lint_body_height_neck_stretch) {
    Clip c = reference();
    key_offset(c, "mNeck", 0, {0, 0, 0.30});
    key_offset(c, "mNeck", 30, {0, 0, 0.30});
    key_offset(c, "mHead", 0, {0, 0, 0.20});
    key_offset(c, "mHead", 30, {0, 0, 0.20});
    const auto fs = lint_clip(skel(), c, {});
    CHECK(rules(fs) == std::set<std::string>{"body_height"});
    for (auto& f : fs) {
        CHECK(f.message.find("up to 50.0 cm taller") != std::string::npos);
        CHECK(f.message.find("25.0 cm") != std::string::npos);
        CHECK(f.fix.label == "Hold Without Sinking (mSkull, head scale 1)");  // no bake shape
        CHECK(f.frames.size() == 31);
        CHECK(f.message.find("move the spine bones up") == std::string::npos);  // no torso or chest: no spine hint
    }
    broken("body_height", c);
    // The export options instead of the fix: Hold without sinking clears it; End at rest makes it only while it plays.
    Clip held = c;
    held.export_settings = Json::object();
    held.export_settings.set("hold_no_sink", true);
    CHECK(!rules(lint_clip(skel(), held, {})).count("body_height"));
    Clip rests = c;
    rests.loop = false;
    rests.export_settings = Json::object();
    rests.export_settings.set("end_at_rest", true);
    int ends = 0;
    for (auto& f : lint_clip(skel(), rests, {})) ends += f.rule == "body_height" && f.message.find("only while it plays") != std::string::npos;
    CHECK(ends == 1);
    CHECK(fs.size() == 1 && fs[0].message.find("lasts after it stops") != std::string::npos);
    // Back at rest on the last frame: only while it plays.
    Clip back = c;
    key_offset(back, "mNeck", 30, {});
    key_offset(back, "mHead", 30, {});
    back.loop = false;  // not a seam: a stretch that plays and lets go
    int seen = 0;
    for (auto& f : lint_clip(skel(), back, {}))
        if (f.rule == "body_height") seen += f.message.find("only while it plays") != std::string::npos;
    CHECK(seen == 1);
    // The export writes no hip key for it: mPelvis carries no position at all.
    const AnimExportResult r = export_anim(skel(), c, {});
    for (const AnimJoint& j : r.file.joints) CHECK(j.name != "mPelvis" || j.pos.empty());
    // Sideways (no height) or on a bone SL's height ignores (mSpine3): no finding.
    Clip side = reference();
    key_offset(side, "mNeck", 0, {0.30, 0, 0});
    key_offset(side, "mNeck", 30, {0.30, 0, 0});
    CHECK(!rules(lint_clip(skel(), side, {})).count("body_height"));
    Clip spine = reference();
    key_offset(spine, "mSpine3", 0, {0, 0, 0.30});
    key_offset(spine, "mSpine3", 30, {0, 0, 0.30});
    CHECK(!rules(lint_clip(skel(), spine, {})).count("body_height"));
}

// Spec 09 0l, the spine hint: a taller torso built on mTorso or mChest sinks the wearer, the same body built on the
// spine bones (which SL's height never reads) does not. The body_height finding says so when mTorso or mChest carry
// the growth; an mSpine2 stretch has no finding at all.
TEST(lint_body_height_spine_hint) {
    Clip torso = reference();
    key_offset(torso, "mTorso", 0, {0, 0, 0.20});
    key_offset(torso, "mTorso", 30, {0, 0, 0.20});
    int hints = 0;
    for (auto& f : lint_clip(skel(), torso, {}))
        if (f.rule == "body_height") {
            CHECK(f.message.find("20.0 cm taller") != std::string::npos);
            hints += f.message.find("move the spine bones up instead of mTorso and mChest") != std::string::npos &&
                     f.message.find("never reads them") != std::string::npos;
        }
    CHECK(hints == 1);
    Clip chest = reference();
    key_offset(chest, "mChest", 0, {0, 0, 0.10});
    int chest_hints = 0;
    for (auto& f : lint_clip(skel(), chest, {}))
        chest_hints += f.rule == "body_height" && f.message.find("move the spine bones up") != std::string::npos;
    CHECK(chest_hints == 1);
    Clip spine = reference();
    key_offset(spine, "mSpine2", 0, {0, 0, 0.20});
    key_offset(spine, "mSpine2", 30, {0, 0, 0.20});
    for (auto& f : lint_clip(skel(), spine, {})) {
        CHECK(f.rule != "body_height");
        CHECK(f.message.find("spine bones") == std::string::npos);
    }
}

TEST(lint_frozen_bones) {
    Clip c = reference();
    key_euler(c, "mTail1", 0, {});
    key_euler(c, "mTail1", 30, {});
    broken("frozen_bones", c);
}

TEST(lint_face_positions) {
    Clip c = undeformed(reference());
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

TEST(lint_position_ends_moved) {
    Clip c = reference();
    c.loop = false;
    key_offset(c, "mWristLeft", 0, {});
    key_offset(c, "mWristLeft", 30, {0.05, 0, 0});
    const auto fs = lint_clip(skel(), c, {});
    bool found = false;
    for (const auto& f : fs) {
        if (f.rule == "position_ends_moved") {
            found = true;
            CHECK((f.bones == std::vector<std::string>{"mWristLeft"}));
            CHECK(f.message == "1 joint ends moved by position; the next animation inherits it unless it keys its position");
        }
    }
    CHECK(found);

    // Check 1 finds exactly the joints left moved.
    Clip c2 = c;
    key_offset(c2, "mWristRight", 0, {});
    key_offset(c2, "mWristRight", 30, {0.05, 0, 0});
    for (const auto& f : lint_clip(skel(), c2, {})) {
        if (f.rule == "position_ends_moved") {
            CHECK((f.bones == std::vector<std::string>{"mWristLeft", "mWristRight"}));
            CHECK(f.message == "2 joints end moved by position; the next animation inherits them unless it keys their positions");
        }
    }

    // Pelvis ending moved does not trigger position_ends_moved
    Clip pel = reference();
    pel.loop = false;
    key_offset(pel, "mPelvis", 30, {0, 0, 0.5});
    CHECK(!rules(lint_clip(skel(), pel, {})).count("position_ends_moved"));

    // broken() verifies that the fix ("End at Rest") clears the rule
    broken("position_ends_moved", c);

    // Under one .anim position step (0.15 mm) off rest is written as rest: not flagged.
    Clip tiny = reference();
    tiny.loop = false;
    key_offset(tiny, "mWristLeft", 0, {});
    key_offset(tiny, "mWristLeft", 30, {0.0001, 0, 0});
    CHECK(!rules(lint_clip(skel(), tiny, {}, {})).count("position_ends_moved"));

    // A loop that ends at rest but moves a position in between: it can be stopped anywhere, so it is flagged, and
    // End at Rest is not offered (it would play only if the loop ran to its end); the undeformer is.
    Clip loop = reference();
    key_offset(loop, "mWristLeft", 0, {});
    key_offset(loop, "mWristLeft", 15, {0.05, 0, 0});
    key_offset(loop, "mWristLeft", 30, {});
    bool flagged = false;
    for (const auto& f : lint_clip(skel(), loop, {}))
        if (f.rule == "position_ends_moved") {
            flagged = true;
            CHECK((f.bones == std::vector<std::string>{"mWristLeft"}));
            CHECK(f.fix.label == "Also Export an Undeformer");
        }
    CHECK(flagged);
    broken("position_ends_moved", loop);
}

TEST(lint_position_leftovers) {
    Clip c = reference();
    c.loop = false;
    // c keys rotation on mWristLeft, but not position
    key_euler(c, "mWristLeft", 0, {10, 0, 0});

    // other clip moves mWristLeft by position
    Clip other = reference();
    key_offset(other, "mWristLeft", 0, {0.02, 0, 0});
    key_offset(other, "mWristLeft", 30, {0.02, 0, 0});

    const auto fs = lint_clip(skel(), c, {}, {}, nullptr, "", {}, {&other});
    bool found = false;
    for (const auto& f : fs) {
        if (f.rule == "position_leftovers") {
            found = true;
            CHECK((f.bones == std::vector<std::string>{"mWristLeft"}));
            CHECK(f.message == "mWristLeft keys rotation but not position; other clips move it by position, so it will inherit leftovers");
            CHECK(f.fix.label == "Reset Joint Positions");
            CHECK(bool(f.fix.apply));
        }
    }
    CHECK(found);

    // Applying fix enables reset_positions and clears the finding
    Clip fixed = c;
    for (const auto& f : fs) {
        if (f.rule == "position_leftovers" && f.fix.apply) f.fix.apply(fixed);
    }
    CHECK(!rules(lint_clip(skel(), fixed, {}, {}, nullptr, "", {}, {&other})).count("position_leftovers"));

    // A position keyed at rest is left out of the file (IO-11a), so it still inherits leftovers.
    Clip c_at_rest = c;
    key_offset(c_at_rest, "mWristLeft", 0, {});
    CHECK(rules(lint_clip(skel(), c_at_rest, {}, {}, nullptr, "", {}, {&other})).count("position_leftovers"));
    // A position it moves is written, which replaces the leftover: no warning.
    Clip c_with_pos = c;
    key_offset(c_with_pos, "mWristLeft", 0, {});
    key_offset(c_with_pos, "mWristLeft", 15, {0.01, 0, 0});
    CHECK(!rules(lint_clip(skel(), c_with_pos, {}, {}, nullptr, "", {}, {&other})).count("position_leftovers"));

    // Reset on, but picking other joints: mWristLeft still inherits, and the fix resets the joints the clip turns.
    Clip picked = c;
    picked.export_settings.set("reset_positions", true);
    picked.export_settings.set("reset_positions_mode", "pick");
    Json elbow = Json::array();
    elbow.push("mElbowLeft");
    picked.export_settings.set("reset_positions_joints", elbow);
    bool still = false;
    for (const auto& f : lint_clip(skel(), picked, {}, {}, nullptr, "", {}, {&other}))
        if (f.rule == "position_leftovers") {
            still = true;
            f.fix.apply(picked);
        }
    CHECK(still);
    CHECK(!rules(lint_clip(skel(), picked, {}, {}, nullptr, "", {}, {&other})).count("position_leftovers"));

    // If other clips don't move mWristLeft by position, no leftover warning
    Clip clean_other = reference();
    CHECK(!rules(lint_clip(skel(), c, {}, {}, nullptr, "", {}, {&clean_other})).count("position_leftovers"));
}
