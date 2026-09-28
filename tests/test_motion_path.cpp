// Motion paths (spec 08 MP): the frames sampled, the keyed dots, and positions against the FK evaluation.
#include <algorithm>
#include <vector>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/motion_path.h"

using namespace vats;

namespace {

Clip swinging_arm() {
    Clip c;
    c.end_frame = 20;
    key_euler(c, "mShoulderLeft", 0, {0, 0, -30});
    key_euler(c, "mShoulderLeft", 20, {10, 0, 60});
    key_euler(c, "mElbowLeft", 0, {0, 10, 0});
    key_euler(c, "mElbowLeft", 12, {0, 70, 0});
    key_euler(c, "mWristLeft", 4, {20, 0, 0});
    return c;
}

}  // namespace

TEST(motion_path_frames_around_the_playhead) {
    Clip c;
    c.end_frame = 30;
    CHECK_EQ(motion_path_frames(c, 10.6, {3, 2, false}), (std::vector<double>{7, 8, 9, 10, 11, 12}));
    CHECK_EQ(motion_path_frames(c, 1, {5, 1, false}), (std::vector<double>{0, 1, 2}));     // clamped at 0
    CHECK_EQ(motion_path_frames(c, 29, {1, 5, false}), (std::vector<double>{28, 29, 30}));  // and at the end
    CHECK_EQ(motion_path_frames(c, 12, {3, 3, true}).size(), size_t(31));
}

TEST(motion_path_matches_fk_evaluation) {
    const Rig rig(skel());
    const Clip c = swinging_arm();
    const int wrist = skel().find("mWristLeft"), elbow = skel().find("mElbowLeft");
    const std::vector<MotionPath> paths = motion_paths(rig, c, nullptr, {wrist, elbow}, 10, {4, 4, false});
    CHECK_EQ(paths.size(), size_t(2));
    CHECK_EQ(paths[0].node, wrist);
    CHECK_EQ(paths[0].points.size(), size_t(9));  // frames 6..14
    for (const MotionPath& p : paths)
        for (const PathPoint& pt : p.points) {
            const std::vector<Xform> g = skel().global_pose(evaluate_curves(skel(), c, pt.frame));
            const Vec3 tip = g[p.node].apply(skel()[p.node].end);
            CHECK_NEAR((pt.pos - tip).length(), 0, 1e-9);
        }
    // The path moves: the shoulder swings the whole arm.
    CHECK((paths[0].points.front().pos - paths[0].points.back().pos).length() > 0.05);
}

TEST(motion_path_keyed_dots_follow_the_chain) {
    const Rig rig(skel());
    const Clip c = swinging_arm();
    const int wrist = skel().find("mWristLeft"), elbow = skel().find("mElbowLeft");
    const std::vector<MotionPath> paths = motion_paths(rig, c, nullptr, {wrist, elbow}, 0, {1, 20, true});
    std::vector<double> wk, ek;
    for (const PathPoint& p : paths[0].points)
        if (p.keyed) wk.push_back(p.frame);
    for (const PathPoint& p : paths[1].points)
        if (p.keyed) ek.push_back(p.frame);
    CHECK_EQ(wk, (std::vector<double>{0, 4, 12, 20}));  // its own key at 4, the elbow's and shoulder's above it
    CHECK_EQ(ek, (std::vector<double>{0, 12, 20}));     // the wrist's key at 4 does not move the elbow
    // IK controls that end on or above the node count too.
    const std::vector<std::string> t = motion_path_tracks(rig, wrist);
    CHECK(std::find(t.begin(), t.end(), "ik.ArmLeft") != t.end());
    CHECK(std::find(t.begin(), t.end(), "pin:mElbowLeft") != t.end());
}
