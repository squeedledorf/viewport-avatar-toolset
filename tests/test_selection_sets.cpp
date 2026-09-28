// Selection sets (08 SS-1..SS-3).
#include <string>

#include "check.h"
#include "fixtures.h"
#include "vats/project.h"
#include "vats/selection_sets.h"

using namespace vats;

TEST(selection_sets_save_and_recall) {
    std::vector<SelectionSet> sets;
    CHECK(store_selection_set(sets, "Arms", {"mShoulderLeft", "mElbowLeft", "mNoSuchBone", "mElbowLeft"}) == 0);
    CHECK(store_selection_set(sets, "Head", {"mNeck", "mHead"}) == 1);
    // Recall: the skeleton's nodes in the set's order, unknown names and repeats skipped.
    const std::vector<int> arms = recall_selection_set(skel(), sets[0]);
    CHECK(arms.size() == 2);
    CHECK(arms[0] == skel().find("mShoulderLeft") && arms[1] == skel().find("mElbowLeft"));
    // Saving under a name in use replaces that set.
    CHECK(store_selection_set(sets, "Arms", {"mWristRight"}) == 0);
    CHECK(sets.size() == 2 && sets[0].bones == std::vector<std::string>{"mWristRight"});
    // Right-click add and remove.
    CHECK(edit_selection_set(sets[1], {"mHead", "mChest"}, true) == 1);
    CHECK(edit_selection_set(sets[1], {"mNeck", "mPelvis"}, false) == 1);
    CHECK((sets[1].bones == std::vector<std::string>{"mHead", "mChest"}));
}

TEST(selection_sets_survive_project_save) {
    Project p;
    store_selection_set(p.clip.selection_sets, "Left Arm", {"mCollarLeft", "mShoulderLeft", "mElbowLeft", "mWristLeft"});
    store_selection_set(p.clip.selection_sets, "Face", {"mFaceJaw"});
    const std::string text = save_project(p);
    Project back;
    std::string err;
    CHECK(load_project(text, back, err));
    CHECK(back.clip.selection_sets == p.clip.selection_sets);
    CHECK(recall_selection_set(skel(), back.clip.selection_sets[0]).size() == 4);
    // No sets, no field; a malformed one is an error.
    CHECK(save_project(Project{}).find("selection_sets") == std::string::npos);
    std::string bad = text;
    bad.replace(bad.find("\"mFaceJaw\""), 10, "7");
    CHECK(!load_project(bad, back, err));

    // Each actor keeps its own sets, and a loaded animation does not replace them (GR-6).
    Project g;
    g.actors.resize(2);
    store_selection_set(g.clip.selection_sets, "Mine", {"mHead"});
    load_into_actor(g, 0, Clip{});
    CHECK(g.clip.selection_sets.size() == 1);
}

TEST(selection_sets_library_file) {
    std::vector<SelectionSet> sets = {{"Hands", {"mWristLeft", "mWristRight"}}, {"Empty", {}}};
    const std::string text = save_selection_sets(sets);
    std::vector<SelectionSet> back;
    std::string err;
    CHECK(load_selection_sets(text, back, err));
    CHECK(back == sets);
    CHECK(!load_selection_sets(R"({"format": "vats-pose-library", "sets": []})", back, err));
    CHECK(back == sets);  // unchanged on failure
    CHECK(!load_selection_sets(R"({"format": "vats-selection-sets", "sets": [{"name": 1, "bones": []}]})", back, err));
}
