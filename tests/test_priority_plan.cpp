// The priority planner (spec 08 PP): SL's per-joint rule, reading context clips from disk, and the lint.
#include <filesystem>
#include <fstream>
#include <random>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_file.h"
#include "vats/priority_plan.h"
#include "vats/project.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

PlanClip clip(const char* name, std::map<std::string, int> joints, bool own = false, bool stand = false) {
    PlanClip c;
    c.name = name, c.joints = std::move(joints), c.own = own, c.stand = stand;
    return c;
}

AnimJoint joint(const char* name, int priority, bool keys = true) {
    AnimJoint j;
    j.name = name, j.priority = priority;
    if (keys) j.rot.push_back({0, 32767, 32767, 32767});
    return j;
}

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("vats-plan-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const char* name) const { return (path / name).generic_string(); }
};

void write(const std::string& path, const std::string& bytes) { std::ofstream(path, std::ios::binary) << bytes; }

}  // namespace

TEST(plan_higher_priority_wins_in_any_order) {
    PlanClip ao = clip("stand", {{"mPelvis", 2}, {"mHead", 2}});
    PlanClip dance = clip("dance", {{"mPelvis", 4}, {"mHead", 4}});
    std::vector<PlanClip> a{ao, dance}, b{dance, ao};
    CHECK_EQ(plan_winner(a, "mPelvis"), 1);
    CHECK_EQ(plan_winner(b, "mPelvis"), 0);  // started first, still wins: 4 beats 2
    CHECK_EQ(plan_winner(a, "mWristLeft"), -1);  // nobody claims it
}

TEST(plan_equal_priority_goes_to_the_last_started) {
    PlanClip x = clip("x", {{"mHead", 3}}), y = clip("y", {{"mHead", 3}}), z = clip("z", {{"mHead", 3}});
    CHECK_EQ(plan_winner({x, y}, "mHead"), 1);
    CHECK_EQ(plan_winner({y, x}, "mHead"), 1);
    CHECK_EQ(plan_winner({x, y, z}, "mHead"), 2);
    // A lower one started last does not take it.
    CHECK_EQ(plan_winner({x, y, clip("low", {{"mHead", 2}})}, "mHead"), 1);
}

TEST(plan_per_joint_priority_takes_one_bone) {
    // The hand hold of the wiki: priority 2 with the wrist at 5, against a dance at 4.
    PlanClip dance = clip("dance", {{"mElbowRight", 4}, {"mWristRight", 4}, {"mPelvis", 4}});
    PlanClip hold = clip("hold", {{"mElbowRight", 2}, {"mWristRight", 5}}, true);
    std::vector<PlanClip> clips{hold, dance};
    auto w = plan_winners(clips);
    CHECK_EQ(w.size(), size_t(3));
    CHECK_EQ(w["mWristRight"], 0);
    CHECK_EQ(w["mElbowRight"], 1);
    CHECK_EQ(w["mPelvis"], 1);
    auto f = plan_lint(skel(), clips);
    CHECK_EQ(f.size(), size_t(1));
    if (f.size() == 1) {
        CHECK_EQ(f[0].rule, std::string("loses"));
        CHECK_EQ(f[0].clip, 1);
        CHECK(f[0].bones == std::vector<std::string>{"mElbowRight"});
        CHECK(f[0].message == "Loses mElbowRight to dance at a higher priority (4 over 2)");
    }
    // Equal priority, started later: the message says so; moved last, the own clip keeps it.
    clips = {clip("mine", {{"mHead", 3}}, true), clip("ao", {{"mHead", 3}})};
    f = plan_lint(skel(), clips);
    CHECK(f.size() == 1 && f[0].message == "Loses mHead to ao at the same priority (3), started later");
    std::swap(clips[0], clips[1]);
    CHECK(plan_lint(skel(), clips).empty());
}

TEST(plan_lint_for_ao_makers) {
    std::vector<PlanClip> clips{clip("Stand 1", {{"mPelvis", 4}, {"mHead", 4}, {"mFaceJaw", 2}}, false, true),
                                clip("Dance", {{"mPelvis", 4}, {"mShoulderLeft", 4}, {"mFaceJaw", 6}, {"mHandIndex1Left", 5}}),
                                clip("Blink", {{"mFaceEyeLidUpperLeft", 6}})};  // not whole-body: fine at 6
    auto f = plan_lint(skel(), clips);
    CHECK_EQ(f.size(), size_t(2));
    if (f.size() == 2) {
        CHECK(f[0].rule == "stand_priority" && f[0].clip == 0);
        CHECK(f[0].bones == (std::vector<std::string>{"mHead", "mPelvis"}));  // body joints only
        CHECK(f[1].rule == "face_hands_high" && f[1].clip == 1);
        CHECK(f[1].bones == (std::vector<std::string>{"mFaceJaw", "mHandIndex1Left"}));
    }
    clips[0].joints = {{"mPelvis", 3}};  // a stand at 3 is fine
    clips[1].joints["mFaceJaw"] = 4, clips[1].joints["mHandIndex1Left"] = 4;
    CHECK(plan_lint(skel(), clips).empty());
}

TEST(plan_clip_from_anim_resolves_priorities) {
    AnimFile a;
    a.base_priority = 3;
    a.joints = {joint("mHead", -1), joint("mWristRight", 5), joint("mNeck", 2, false), joint("mNotAJoint", 4)};
    PlanClip c = plan_clip_from_anim(skel(), a, "x");
    CHECK_EQ(c.name, std::string("x"));
    CHECK_EQ(c.joints.size(), size_t(2));  // the keyless and the unknown record claim nothing
    CHECK_EQ(c.joints["mHead"], 3);
    CHECK_EQ(c.joints["mWristRight"], 5);
}

TEST(plan_loads_anim_and_project_files) {
    TempDir dir;
    AnimFile a;
    a.duration = 1, a.base_priority = 2;
    a.joints = {joint("mPelvis", -1), joint("mHead", 4)};
    auto bytes = write_anim(a);
    write(dir.file("AO Stand.anim"), std::string(bytes.begin(), bytes.end()));

    Project p;
    p.clip.priority = 3;
    p.clip.end_frame = 10;
    for (const char* b : {"mChest", "mWristLeft"}) {
        p.clip.curves[b]["rot_x"].set_key(0, 0);
        p.clip.curves[b]["rot_x"].set_key(10, 30);
    }
    p.clip.joint_priority["mWristLeft"] = 6;
    write(dir.file("dance.vat"), save_project(p));
    write(dir.file("broken.anim"), "not an animation");

    PlanClip c;
    std::string err;
    CHECK(load_plan_clip(skel(), dir.file("AO Stand.anim"), c, err));
    CHECK_EQ(c.name, std::string("AO Stand"));
    CHECK(c.stand);  // "stand" in the name
    CHECK(c.joints == (std::map<std::string, int>{{"mHead", 4}, {"mPelvis", 2}}));

    CHECK(load_plan_clip(skel(), dir.file("dance.vat"), c, err));
    CHECK_EQ(c.name, std::string("dance"));
    CHECK(!c.stand);
    CHECK(c.joints == (std::map<std::string, int>{{"mChest", 3}, {"mWristLeft", 6}}));

    CHECK(!load_plan_clip(skel(), dir.file("broken.anim"), c, err));
    CHECK(!err.empty());
    CHECK(!load_plan_clip(skel(), dir.file("missing.vat"), c, err));
}
