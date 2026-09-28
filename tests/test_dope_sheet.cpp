// Dope sheet (spec 08 DS): rows, the summary row, and key edits through the selection the graph shares.
#include <algorithm>
#include <string>
#include <vector>

#include "check.h"
#include "fixtures.h"
#include "vats/dope_sheet.h"
#include "vats/edit.h"
#include "vats/rig.h"

using namespace vats;

namespace {

// Keys on a shoulder (0, 10), an elbow (5, 10) and a wrist pin (10, a hair off the whole frame).
Clip arm_clip() {
    Clip c;
    c.end_frame = 30;
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 10, {0, 0, 40});
    key_euler(c, "mElbowLeft", 5, {0, 20, 0});
    key_euler(c, "mElbowLeft", 10, {0, 50, 0});
    c.curves["pin:mWristLeft"]["pos_x"].set_key(10 + 1e-7, 0.1);
    return c;
}

const std::vector<std::string> kArm = {"mShoulderLeft", "mElbowLeft", "pin:mWristLeft"};

}  // namespace

TEST(dope_summary_row_aggregates_every_track) {
    const Clip c = arm_clip();
    CHECK_EQ(keyed_frames(c, kArm), (std::vector<double>{0, 5, 10}));  // the pin's 10 + 1e-7 counts as frame 10
    CHECK_EQ(keyed_frames(c, {"mElbowLeft"}), (std::vector<double>{5, 10}));
    CHECK(keyed_frames(c, {"mHead", "no such track"}).empty());
    // A row's keys at one frame: every channel of every track in it.
    const std::vector<KeyRef> at10 = keys_between(c, kArm, 10, 10);
    CHECK_EQ(at10.size(), size_t(3 + 3 + 1));
}

TEST(dope_rows_group_by_body_part) {
    const std::vector<DopeRow> rows =
        dope_rows(skel(), {"mElbowLeft", "ik.ArmLeft", "mHead", "mPelvis", "pin:mWristLeft", "mShoulderLeft", "bogus"});
    std::vector<std::string> labels;
    for (auto& r : rows) labels.push_back(r.label);
    CHECK_EQ(labels, (std::vector<std::string>{"Torso", "Head", "Left Arm", "Left Hand", "Other"}));
    CHECK_EQ(rows[2].tracks, (std::vector<std::string>{"mShoulderLeft", "mElbowLeft", "ik.ArmLeft"}));
    CHECK_EQ(rows[3].tracks, (std::vector<std::string>{"pin:mWristLeft"}));
}

TEST(dope_move_and_scale_through_the_shared_selection) {
    Clip c = arm_clip();
    // Clicking the summary row's key at 10 selects every key there; dragging it 3 frames right moves them all.
    std::vector<KeyRef> sel = keys_between(c, kArm, 10, 10);
    Clip before = c;
    move_keys(c, before, sel, 3.4, 0, true);
    finish_transform(c, sel);
    CHECK_EQ(keyed_frames(c, kArm), (std::vector<double>{0, 5, 13}));
    for (const KeyRef& k : sel) CHECK_NEAR(c.curves.at(k.track).at(k.channel).keys[k.index].frame, 13, 1e-6);
    CHECK_NEAR(c.curves.at("mShoulderLeft").at("rot_z").keys[1].value, 40, 1e-9);  // time only

    // The side handle: everything from 5 to 13 scaled x2 about 5 (the other side), values untouched.
    sel = keys_between(c, kArm, 5, 13);
    before = c;
    scale_keys(c, before, sel, 5, 0, 2, 1, true);
    finish_transform(c, sel);
    CHECK_EQ(keyed_frames(c, kArm), (std::vector<double>{0, 5, 21}));
    CHECK_NEAR(c.curves.at("mElbowLeft").at("rot_y").keys[1].value, 50, 1e-9);
}

TEST(dope_selected_limb_brings_its_ik_controls) {
    Rig rig(skel());
    Clip c;
    c.curves["ik.LegLeft"]["pos_x"].set_key(0, 0.1);  // a keyed foot target
    c.curves["ik.ArmLeft"]["pos_x"].set_key(0, 0.1);
    const std::vector<std::string> t = with_limb_controls(rig, c, {"mHipLeft", "mKneeLeft", "mAnkleLeft", "mHead"});
    CHECK(std::find(t.begin(), t.end(), "ik.LegLeft") != t.end());
    CHECK(std::find(t.begin(), t.end(), "ik.ArmLeft") == t.end());   // no arm bone selected
    CHECK(std::find(t.begin(), t.end(), "ik.LegRight") == t.end());  // not keyed
    CHECK_EQ(with_limb_controls(rig, c, {"mKneeLeft", "ik.LegLeft"}).size(), size_t(2));  // already there: once
}
