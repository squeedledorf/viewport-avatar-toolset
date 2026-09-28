// The self-penetration check (08 SX): capsule overlaps, the Animation Check rule and its Push Out fix.
#include <fstream>
#include <iterator>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/lint.h"
#include "vats/project.h"
#include "vats/self_contact.h"
#include "vats/shape.h"

using namespace vats;

namespace {

// The left arm swings down from the T-pose to just inside straight down, so the hand ends up in the left thigh.
Clip hand_in_thigh() {
    Clip c;
    c.fps = 30, c.end_frame = 10;
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 10, {-95, 0, 0});
    return c;
}

std::vector<SelfContact> contacts(const std::vector<Xform>& globals, bool volumes = false) {
    return SelfContactCheck(skel(), nullptr, volumes).find(globals);
}

bool has_pair(const std::vector<SelfContact>& cs, const char* a, const char* b) {
    const int x = skel().find(a), y = skel().find(b);
    for (const SelfContact& s : cs)
        if ((s.a == x && s.b == y) || (s.a == y && s.b == x)) return true;
    return false;
}

const LintFinding* hand_thigh(const std::vector<LintFinding>& fs) {
    for (const LintFinding& f : fs)
        if (f.rule == "self_contact" && (f.bones == std::vector<std::string>{"mWristLeft", "mHipLeft"} ||
                                         f.bones == std::vector<std::string>{"mHipLeft", "mWristLeft"}))
            return &f;
    return nullptr;
}

}  // namespace

TEST(self_contact_rest_pose_is_clear) {
    const Skeleton& s = skel();
    CHECK(contacts(s.global_pose(Pose(s.size()))).empty());
    CHECK(contacts(s.global_pose(Pose(s.size())), true).empty());  // with the volumes too
}

TEST(self_contact_finds_the_hand_in_the_thigh) {
    const Skeleton& s = skel();
    const Clip c = hand_in_thigh();
    const std::vector<SelfContact> end = contacts(s.global_pose(evaluate_curves(s, c, 10)));
    CHECK(has_pair(end, "mWristLeft", "mHipLeft"));
    CHECK(has_pair(contacts(s.global_pose(evaluate_curves(s, c, 10)), true), "mWristLeft", "mHipLeft"));
    CHECK(contacts(s.global_pose(evaluate_curves(s, c, 0))).empty());
}

TEST(self_contact_lint_rule_and_push_out) {
    const Skeleton& s = skel();
    Clip c = hand_in_thigh();
    std::vector<LintFinding> fs = lint_clip(s, c, {});
    const LintFinding* f = hand_thigh(fs);
    CHECK(f);
    if (!f) return;
    CHECK(f->severity == LintSeverity::Info);
    CHECK(f->frames.back() == 10 && f->frames.front() > 0);  // not at the rest pose of frame 0
    CHECK(f->fix.label == "Push Out");
    CHECK(!hand_thigh(lint_clip(s, c, {}, {"self_contact"})));  // the off switch
    f->fix.apply(c);
    fs = lint_clip(s, c, {});
    CHECK(!hand_thigh(fs));
    CHECK(c.curves.count("mShoulderLeft") && !c.curves.count("ik.ArmLeft"));  // FK arm: keyed as FK
}

TEST(self_contact_push_out_moves_an_ik_arm_target) {
    const Skeleton& s = skel();
    Rig rig(s);
    Clip c = hand_in_thigh();
    const int limb = rig.find_limb("ArmLeft");
    std::vector<Xform> wrist;  // the FK hand, keyed as the IK target on every frame
    for (int f = 0; f <= 10; ++f) wrist.push_back(evaluate(rig, c, f, nullptr).globals[rig.limbs()[limb].end]);
    key_blend(c, rig, 0, limb, 1);
    for (int f = 0; f <= 10; ++f) key_limb_target(c, rig, f, limb, wrist[f], nullptr);
    const int a = s.find("mHipLeft"), b = s.find("mWristLeft");
    std::vector<int> frames;
    for (int f = 0; f <= 10; ++f)
        if (has_pair(contacts(evaluate(rig, c, f, nullptr).globals), "mWristLeft", "mHipLeft"))
            frames.push_back(f);
    CHECK(!frames.empty());
    push_out(c, rig, a, b, frames, nullptr, nullptr);
    for (int f : frames)
        CHECK(!has_pair(contacts(evaluate(rig, c, f, nullptr).globals), "mWristLeft", "mHipLeft"));
}


// The ragdoll's capsules are wider than the body at the hips, so every frame of the default document and of the
// first-wave example was flagged (an arm hanging at the side). Linted as the app does: on the SL default shape.
TEST(self_contact_ordinary_poses_are_clear) {
    std::ifstream lad(std::string(VATS_DATA_DIR) + "/avatar_lad.xml", std::ios::binary);
    AvatarParams params;
    std::string err;
    CHECK(parse_avatar_params(std::string{std::istreambuf_iterator<char>(lad), {}}, params, err));
    const BodyShape female = sl_default_shape(skel(), params, false);
    AnimExportOptions sl_default;
    sl_default.shape = &female.shape;
    auto contacts_in = [&](const Clip& c) {
        int n = 0;
        for (const AnimExportOptions& opt : {AnimExportOptions{}, sl_default})
            for (const LintFinding& f : lint_clip(skel(), c, opt)) n += f.rule == "self_contact";
        return n;
    };
    CHECK(contacts_in(Project{}.clip) == 0);  // the default document
    // first-wave: standing, left arm relaxed at the side, right arm waving. balance-lean: both legs in IK while the
    // body tips forward about the ankles; its knees crossed mid-lean until the example keyed poles in front of them.
    for (const char* example : {"first-wave.vat", "balance-lean.vat"}) {
        std::ifstream f(std::string(VATS_WIKI_DIR) + "/examples/" + example, std::ios::binary);
        Project p;
        CHECK(load_project(std::string{std::istreambuf_iterator<char>(f), {}}, p, err));
        CHECK(!p.clip.curves.empty());
        CHECK(contacts_in(p.clip) == 0);
    }
    CHECK(hand_thigh(lint_clip(skel(), hand_in_thigh(), sl_default)));  // a hand pushed into the thigh still is one
}
