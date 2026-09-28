// Pose-matched insertion and transitions (08 PM-1..PM-3), the library-pose ghost (08 ON-5) and the target ghost.
#include <algorithm>
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/onion.h"
#include "vats/pose_match.h"
#include "vats/pose_ops.h"

using namespace vats;

namespace {

// Frames first..first+length of one motion in which every joint turns about one axis at its own steady rate and the
// hips walk forward along their heading's start, turning slowly: no two frames match, and the speed never changes.
// turn: the whole clip turned this many degrees about Z (hips rotation and travel).
Clip steady_motion(int first, int length, double turn = 0) {
    Clip c;
    c.fps = 30;
    c.end_frame = length;
    const Quat r = Quat::axis_angle({0, 0, 1}, turn * kDegToRad);
    for (int k = 0; k <= length; ++k) {
        const double t = first + k;
        key_rotation(c, "mPelvis", k, r * euler_to_quat({0, 0, 10 + 0.8 * t}));
        key_offset(c, "mPelvis", k, r.rotate({0.02 * t, 0.01 * t, 0.05}));
        key_euler(c, "mTorso", k, {0, 0.9 * t, 0});
        key_euler(c, "mHipLeft", k, {0, -0.7 * t, 0});
        key_euler(c, "mKneeLeft", k, {0, 0.5 * t, 0});
        key_euler(c, "mShoulderLeft", k, {1.1 * t, 0, 0});
    }
    return c;
}

double angle_deg(const Quat& a, const Quat& b) { return std::acos(std::min(1.0, std::fabs(a.dot(b)))) * 2 * kRadToDeg; }

// How far the pose moves from frame f to f + 1: every joint's turn in degrees plus the hips' travel in cm.
double speed(const Rig& rig, const Clip& c, int f) {
    const Evaluation a = evaluate(rig, c, f, nullptr), b = evaluate(rig, c, f + 1, nullptr);
    double s = (b.globals[rig.skeleton().find("mPelvis")].pos - a.globals[rig.skeleton().find("mPelvis")].pos).length() * 100;
    for (int j = 0; j < rig.skeleton().joint_count(); ++j) s += angle_deg(a.pose.rot[j], b.pose.rot[j]);
    return s;
}

}  // namespace

TEST(pose_match_finds_the_shift_and_aligns_the_hips) {
    Rig rig(skel());
    const int hips = skel().find("mPelvis");
    // The current clip is frames 0..72 of the motion; the incoming one is frames 48..120, turned 90 degrees.
    Clip current = steady_motion(0, 72);
    const Clip incoming = steady_motion(48, 72, 90);
    MatchOptions o;
    o.search = 24, o.blend = 12;
    const PoseMatch m = match_poses(rig, current, incoming, o);
    CHECK_EQ(m.into, 12);  // 12 frames into the incoming clip, on the current clip's frame 60
    CHECK_EQ(m.cut, 60);
    CHECK(m.distance < 0.01);
    CHECK_NEAR(m.yaw, -90, 1e-6);

    // Aligned, the incoming hips at `into` sit on the current ones at `cut`.
    Clip aligned = incoming;
    align_hips(aligned, m);
    const Xform want = evaluate(rig, current, m.cut, nullptr).globals[hips];
    const Xform got = evaluate(rig, aligned, m.into, nullptr).globals[hips];
    CHECK((got.pos - want.pos).length() < 0.001);
    CHECK(angle_deg(got.rot, want.rot) < 0.1);

    // Joined, the clip is the whole motion, 0..120, hips included.
    const Clip before = current;
    join_matched(current, incoming, m, o);
    CHECK_EQ(current.end_frame, 120);
    const Clip whole = steady_motion(0, 120);
    for (int f : {30, 60, 66, 72, 90, 120}) {
        const Xform a = evaluate(rig, current, f, nullptr).globals[hips], b = evaluate(rig, whole, f, nullptr).globals[hips];
        CHECK((a.pos - b.pos).length() < 0.001);
        CHECK(angle_deg(a.rot, b.rot) < 0.1);
    }
    // No pop at the join: no frame around it moves faster than the current clip's median frame.
    std::vector<double> speeds;
    for (int f = 0; f < before.end_frame; ++f) speeds.push_back(speed(rig, before, f));
    std::nth_element(speeds.begin(), speeds.begin() + speeds.size() / 2, speeds.end());
    const double median = speeds[speeds.size() / 2];
    for (int f = m.cut - 2; f <= m.cut + o.blend + 2; ++f) CHECK(speed(rig, current, f) <= median * 1.001);
}

TEST(pose_match_without_align_keeps_the_heading) {
    Rig rig(skel());
    Clip current = steady_motion(0, 40);
    const Clip incoming = steady_motion(30, 40, 90);
    MatchOptions o;
    o.align = false, o.search = 20, o.blend = 4;
    const PoseMatch m = match_poses(rig, current, incoming, o);
    CHECK_NEAR(m.yaw, 0, 1e-12);
    join_matched(current, incoming, m, o);
    // The incoming hips keep their own turn after the blend.
    const Vec3 fwd = euler_to_quat(curve_euler(current, "mPelvis", current.end_frame)).rotate({1, 0, 0});
    const double t = 30 + 40;
    CHECK_NEAR(std::atan2(fwd.y, fwd.x) * kRadToDeg, 10 + 0.8 * t + 90, 1e-6);
}

TEST(transition_endpoints_match_the_poses) {
    Clip c;
    c.end_frame = 40;
    key_euler(c, "mTorso", 0, {0, 30, 0});
    key_euler(c, "mTorso", 20, {0, -10, 0});
    key_euler(c, "mTorso", 10, {50, 0, 0});  // a key in between, which the transition replaces
    key_euler(c, "mNeck", 0, {0, 0, 20});
    key_euler(c, "mNeck", 20, {0, 0, 60});
    const Clip from = c;
    CHECK_EQ(make_transition(c, c, 0, c, 20, 0, 20), 2);
    CHECK(std::fabs(curve_euler(c, "mTorso", 0).y - 30) < 1e-6);
    CHECK(std::fabs(curve_euler(c, "mTorso", 20).y + 10) < 1e-6);
    CHECK(std::fabs(curve_euler(c, "mTorso", 10).x) < 1e-6);   // the old key is gone
    CHECK(std::fabs(curve_euler(c, "mTorso", 10).y - 10) < 1e-6);  // halfway, In-Out
    CHECK(std::fabs(curve_euler(c, "mNeck", 20).z - 60) < 1e-6);

    // From a library pose: the pose at the start, the frame's pose at the end; a bone only the pose has goes to rest.
    LibraryItem pose;
    pose.kind = "pose";
    pose.bones["mTorso"] = {0, 45, 0};
    pose.bones["mHead"] = {0, 0, 25};
    Clip posed = from;
    apply_pose(posed, skel(), pose, 12, false);
    Clip d = from;
    make_transition(d, posed, 12, d, 20, 12, 8);
    CHECK(std::fabs(curve_euler(d, "mTorso", 12).y - 45) < 1e-6);
    CHECK(std::fabs(curve_euler(d, "mHead", 12).z - 25) < 1e-6);
    CHECK(std::fabs(curve_euler(d, "mTorso", 20).y + 10) < 1e-6);
    CHECK(std::fabs(curve_euler(d, "mHead", 20).z) < 1e-6);
    CHECK(curve_euler(d, "mTorso", 10) == curve_euler(from, "mTorso", 10));  // keys before the transition stay
}

TEST(pose_ghost_shows_the_pose_and_leaves_the_clip) {
    Rig rig(skel());
    Clip c;
    key_euler(c, "mTorso", 0, {0, 10, 0});
    LibraryItem pose;
    pose.kind = "pose";
    pose.bones["mShoulderLeft"] = {0, 0, 70};
    const Clip was = c;
    const auto g = pose_ghost(rig, c, 0, nullptr, pose);
    CHECK(c == was);
    Clip posed = c;
    apply_pose(posed, skel(), pose, 0, false);
    const auto want = evaluate(rig, posed, 0, nullptr).globals;
    const int hand = skel().find("mWristLeft");
    CHECK((g[hand].pos - want[hand].pos).length() < 1e-9);
    CHECK((g[hand].pos - evaluate(rig, c, 0, nullptr).globals[hand].pos).length() > 0.05);
}

// The target ghost follows the edited clip frame for frame and holds its first and last frames outside its length.
TEST(target_frame_maps_one_to_one_and_clamps) {
    Clip t;
    t.end_frame = 24;
    CHECK(target_frame(t, 0) == 0);
    CHECK(target_frame(t, 12) == 12);
    CHECK(target_frame(t, 12.5) == 12.5);  // between frames while scrubbing or playing
    CHECK(target_frame(t, 24) == 24);
    CHECK(target_frame(t, 40) == 24);  // a longer animation: the target's last pose
    CHECK(target_frame(t, -3) == 0);
}

// Match distance: the bone's own rotation against the target's, relative to its parent.
TEST(bone_angle_apart_measures_the_bone_not_its_parent) {
    Rig rig(skel());
    const int elbow = skel().find("mElbowLeft"), shoulder = skel().find("mShoulderLeft");
    auto globals = [&](const Clip& c) { return evaluate(rig, c, 0, nullptr).globals; };
    Clip a, b;
    key_euler(a, "mElbowLeft", 0, {0, 0, 10});
    key_euler(b, "mElbowLeft", 0, {0, 0, 40});
    CHECK(std::fabs(bone_angle_apart(skel(), globals(a), globals(b), elbow) - 30) < 1e-6);
    CHECK(std::fabs(bone_angle_apart(skel(), globals(b), globals(a), elbow) - 30) < 1e-6);  // either way round
    CHECK(bone_angle_apart(skel(), globals(a), globals(a), elbow) < 1e-6);
    // The shoulder turned in one pose only: the elbow still matches, the shoulder is 50 degrees off.
    key_euler(b, "mElbowLeft", 0, {0, 0, 10});
    key_euler(b, "mShoulderLeft", 0, {0, 50, 0});
    CHECK(bone_angle_apart(skel(), globals(a), globals(b), elbow) < 1e-6);
    CHECK(std::fabs(bone_angle_apart(skel(), globals(a), globals(b), shoulder) - 50) < 1e-6);
    // The shortest way round: 330 and -20 degrees are 10 apart.
    Clip c, d;
    key_euler(c, "mElbowLeft", 0, {0, 0, 330});
    key_euler(d, "mElbowLeft", 0, {0, 0, -20});
    CHECK(std::fabs(bone_angle_apart(skel(), globals(c), globals(d), elbow) - 10) < 1e-6);
    CHECK(bone_angle_apart(skel(), globals(a), globals(b), -1) == 0);  // no bone selected
}
