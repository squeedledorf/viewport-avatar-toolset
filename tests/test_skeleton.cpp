#include <algorithm>
#include <cmath>
#include <set>

#include "check.h"
#include "fixtures.h"
#include "view_math.h"
#include "vats/dae.h"
#include "../tools/mech_rig.h"

using namespace vats;

namespace {

void check_pos(const std::vector<Xform>& g, const char* name, Vec3 want, double tol) {
    int i = skel().find(name);
    CHECK(i >= 0);
    if (i < 0) return;
    if ((g[i].pos - want).length() > tol)
        check::fail(__FILE__, __LINE__,
                    std::string(name) + " at " + std::to_string(g[i].pos.x) + ", " + std::to_string(g[i].pos.y) +
                        ", " + std::to_string(g[i].pos.z));
}

}  // namespace

TEST(skeleton_counts_and_order) {
    const Skeleton& s = skel();
    CHECK_EQ(s.joint_count(), 133);
    CHECK_EQ(static_cast<int>(s.volumes().size()), 26);
    CHECK_EQ(s.size(), 180 + 26);  // joints, attachment points, collision volumes (README decision 12)
    CHECK_EQ(s[0].name, std::string("mPelvis"));
    for (int i = 1; i < s.size(); ++i) CHECK(s[i].parent < i);
    // Pelvis children in document order.
    const char* kids[] = {"mSpine1", "mHipRight", "mHipLeft", "mTail1", "mGroin", "mHindLimbsRoot"};
    CHECK_EQ(s[0].children.size(), size_t(6) + 2 + 2);  // plus points Pelvis and Stomach, volumes PELVIS and BUTT
    for (int k = 0; k < 6; ++k) CHECK_EQ(s[s[0].children[k]].name, std::string(kids[k]));
    int base = 0;
    for (int i = 0; i < s.joint_count(); ++i) base += s[i].base;
    CHECK_EQ(base, 26);
    // First and last attachment points (ascending id, HUD excluded).
    CHECK_EQ(s[133].name, std::string("Chest"));
    CHECK_EQ(s[179].name, std::string("Right Hind Foot"));
}

// README decision 12: the 26 collision volumes follow the attachment points as animatable nodes.
TEST(skeleton_volume_nodes) {
    const Skeleton& s = skel();
    CHECK_EQ(s.volume_start(), 180);
    CHECK_EQ(s[180].name, std::string("PELVIS"));
    CHECK_EQ(s[181].name, std::string("BUTT"));
    CHECK_EQ(s[s.size() - 1].name, std::string("L_FOOT"));
    for (int v = 0; v < 26; ++v) {
        const CollisionVolume& cv = s.volumes()[v];
        const Node& n = s[cv.node];
        CHECK_EQ(cv.node, 180 + v);
        CHECK_EQ(n.name, cv.name);
        CHECK_EQ(n.parent, cv.joint);
        CHECK(n.volume && n.attachment && n.category == Category::CollisionVolumes);
        CHECK(std::find(s[cv.joint].children.begin(), s[cv.joint].children.end(), cv.node) != s[cv.joint].children.end());
    }
    CHECK_EQ(s[s.find("BELLY")].parent, s.find("mTorso"));
    CHECK_EQ(s.find_viewer("BELLY"), s.find("BELLY"));
    CHECK_EQ(s.mirror(s.find("L_UPPER_ARM")), s.find("R_UPPER_ARM"));
    CHECK_EQ(s.mirror(s.find("LEFT_PEC")), s.find("RIGHT_PEC"));
    CHECK_EQ(s.mirror(s.find("BELLY")), s.find("BELLY"));
    // FK: owning joint x (pos, rot), with the joint's shape scale applied to the offset (SK-I5).
    Pose p(s.size());
    int torso = s.find("mTorso"), belly = s.find("BELLY");
    p.rot[torso] = Quat::axis_angle({0, 0, 1}, 0.5);
    p.rot[belly] = Quat::axis_angle({1, 0, 0}, 0.3);
    auto g = s.global_pose(p);
    const CollisionVolume& bv = s.volumes()[s.find_volume("BELLY")];
    Xform want = g[torso] * Xform{bv.rot * p.rot[belly], bv.pos};
    CHECK_NEAR((g[belly].pos - want.pos).length(), 0, 1e-12);
    CHECK_NEAR(1 - std::fabs(g[belly].rot.dot(want.rot)), 0, 1e-12);
    const Shape& male = s.male_shape();
    auto gm = s.global_pose(Pose(s.size()), &male);
    Vec3 local = gm[torso].inverse().apply(gm[belly].pos);
    CHECK_NEAR((local - bv.pos.mul(male.scale[torso])).length(), 0, 1e-12);
}

// SK-21: the display bone frame turns the nearest signed principal axis onto the tail.
TEST(skeleton_bone_frame) {
    auto near = [](const Vec3& a, const Vec3& b) { return (a - b).length() < 1e-9; };
    CHECK(Skeleton::bone_frame({0, 0, 0}) == Quat{});
    CHECK(near(Skeleton::bone_frame({0, 0.2, 0}).rotate({1, 0, 0}), {1, 0, 0}));
    for (Vec3 e : {Vec3{0.1, 0.02, -0.01}, Vec3{-0.03, 0.01, 0.09}, Vec3{0.0, -0.05, 0.049}}) {
        Quat b = Skeleton::bone_frame(e);
        int a = 0;
        for (int i = 1; i < 3; ++i)
            if (std::fabs(e[i]) > std::fabs(e[a])) a = i;
        Vec3 axis;
        axis[a] = e[a] > 0 ? 1 : -1;
        CHECK(near(b.rotate(axis), e.normalized()));
        CHECK(near(b.rotate(axis.cross(e).normalized()), axis.cross(e).normalized()));  // shortest arc
    }
    // A finger: the gizmo's X axis lies along the bone.
    const Skeleton& s = skel();
    int f = s.find("mHandIndex2Left");
    CHECK(near(s.bone_frame(f).rotate({0, 1, 0}), s[f].end.normalized()) ||
          near(s.bone_frame(f).rotate({1, 0, 0}), s[f].end.normalized()));
}

TEST(skeleton_categories) {
    int count[kCategoryCount] = {};
    for (auto& n : skel().nodes()) ++count[static_cast<int>(n.category)];
    int want[kCategoryCount] = {30, 30, 46, 11, 6, 9, 1, 47, 26};
    for (int c = 0; c < kCategoryCount; ++c) CHECK_EQ(count[c], want[c]);
}

// SK-7 test vectors.
TEST(skeleton_name_lookup) {
    const Skeleton& s = skel();
    auto name = [&](const char* q) {
        int i = s.find(q);
        return i < 0 ? std::string("<none>") : s[i].name;
    };
    CHECK_EQ(name("Chest"), std::string("Chest"));
    CHECK(s[s.find("Chest")].attachment);
    CHECK_EQ(name("chest"), std::string("mChest"));
    CHECK_EQ(name("CHEST"), std::string("CHEST"));  // the collision volume: exact names win (decision 12)
    CHECK_EQ(name("mchest"), std::string("mChest"));
    CHECK_EQ(name("Neck"), std::string("Neck"));
    CHECK_EQ(name("neck"), std::string("mNeck"));
    CHECK_EQ(name("hip"), std::string("mPelvis"));
    CHECK_EQ(name("lFoot"), std::string("mAnkleLeft"));
    CHECK_EQ(name("avatar_mSkull"), std::string("mSkull"));
    CHECK_EQ(name("Avatar Center"), std::string("Avatar Center"));
    CHECK_EQ(name("left hand"), std::string("Left Hand"));
    CHECK_EQ(name("mRoot"), std::string("<none>"));
    CHECK_EQ(name("mScreen"), std::string("<none>"));
    // Collision volumes are nodes (README decision 12) and match exactly; case-insensitively only where no
    // joint, alias or attachment point has the name.
    CHECK_EQ(name("PELVIS"), std::string("PELVIS"));
    CHECK_EQ(name("pelvis"), std::string("Pelvis"));
    CHECK_EQ(name("BELLY"), std::string("BELLY"));
    CHECK_EQ(name("belly"), std::string("BELLY"));
    // The viewer's lookup is case-sensitive and knows '_' spellings of attachment points.
    CHECK_EQ(s.find_viewer("chest"), s.find("mChest"));
    CHECK_EQ(s.find_viewer("CHEST"), s.find("CHEST"));  // the viewer's tree search finds the volume
    CHECK_EQ(s.find_viewer("Left_Hand"), s.find("Left Hand"));
}

TEST(skeleton_mirror_names) {
    const Skeleton& s = skel();
    auto m = [&](const char* a) { return Skeleton::mirror_name(a); };
    CHECK_EQ(m("mHandThumb2Left"), std::string("mHandThumb2Right"));
    CHECK_EQ(m("Left Ear"), std::string("Right Ear"));
    CHECK_EQ(m("Alt Left Eye"), std::string("Alt Right Eye"));
    CHECK_EQ(m("L Lower Leg"), std::string("R Lower Leg"));
    CHECK_EQ(m("mFaceLipCornerLeft"), std::string("mFaceLipCornerRight"));
    CHECK_EQ(m("mWing4FanLeft"), std::string("mWing4FanRight"));
    CHECK_EQ(m("pin:Left Hand"), std::string("pin:Right Hand"));
    for (const char* c : {"mPelvis", "mFaceNoseCenter", "Chest", "Tail Tip"}) {
        int i = s.find(c);
        CHECK_EQ(s.mirror(i), i);
    }
    CHECK_EQ(s.mirror(s.find("mHindLimb3Left")), s.find("mHindLimb3Right"));
    CHECK_EQ(s.mirror(s.find("L Upper Arm")), s.find("R Upper Arm"));
}

// SK-1: the remaining bone and volume attributes are read.
TEST(skeleton_xml_attributes) {
    const Skeleton& s = skel();
    const Node& w = s[s.find("mWristLeft")];
    CHECK(w.connected);
    CHECK(!s[s.find("mPelvis")].connected);
    CHECK_NEAR((w.pivot - Vec3{0, 0.204846, 0}).length(), 0, 1e-9);
    CHECK_NEAR((w.scale - Vec3{1, 1, 1}).length(), 0, 1e-9);
    bool grouped = true;
    for (auto& v : s.volumes()) grouped = grouped && !v.group.empty();
    CHECK(grouped);
}

// SK-11: attachment rest rotations use the viewer's setQuat order.
TEST(skeleton_attachment_rest) {
    const Skeleton& s = skel();
    auto near = [](const Quat& a, const Quat& b) { return 1.0 - std::fabs(a.dot(b)) < 1e-7; };
    CHECK(near(s[s.find("Chest")].rest, {0.5, 0.5, 0.5, 0.5}));
    CHECK(near(s[s.find("Spine")].rest, {0.5, -0.5, -0.5, 0.5}));
    CHECK(near(s[s.find("Skull")].rest, {0.70710678, 0, 0, 0.70710678}));
    // Chest's +Z points out of the chest (+X).
    CHECK_NEAR((s[s.find("Chest")].rest.rotate({0, 0, 1}) - Vec3{1, 0, 0}).length(), 0, 1e-9);
    // Avatar Center has no parent and sits at the pelvis rest position.
    const Node& ac = s[s.find("Avatar Center")];
    CHECK_EQ(ac.parent, -1);
    CHECK_NEAR((ac.pos - Vec3{0, 0, 1.067}).length(), 0, 1e-9);
}

// SK-16 and SK-28 test positions.
TEST(skeleton_forward_kinematics) {
    const Skeleton& s = skel();
    Pose zero(s.size());
    auto g = s.global_pose(zero);
    const double tol = 6e-4;
    check_pos(g, "mPelvis", {0, 0, 1.067}, tol);
    check_pos(g, "mTorso", {0, 0, 1.151}, tol);
    check_pos(g, "mChest", {-0.015, 0, 1.356}, tol);
    check_pos(g, "mNeck", {-0.025, 0, 1.607}, tol);
    check_pos(g, "mHead", {-0.025, 0, 1.683}, tol);
    check_pos(g, "mEyeLeft", {0.073, 0.036, 1.762}, tol);
    check_pos(g, "mShoulderLeft", {-0.036, 0.164, 1.521}, tol);
    check_pos(g, "mWristLeft", {-0.036, 0.617, 1.521}, tol);
    check_pos(g, "mHipLeft", {0.034, 0.127, 1.026}, tol);
    check_pos(g, "mAnkleLeft", {0.004, 0.082, 0.067}, tol);
    check_pos(g, "mToeLeft", {0.225, 0.082, 0.006}, tol);
    check_pos(g, "mTail6", {-0.829, 0, 1.114}, tol);
    check_pos(g, "mHindLimb4Left", {-0.32, 0.08, 0.006}, tol);

    auto m = s.global_pose(zero, &s.male_shape());
    check_pos(m, "mPelvis", {0, 0, 1.1138}, tol);
    check_pos(m, "mChest", {-0.015, 0, 1.413}, tol);
    check_pos(m, "mHead", {-0.0255, 0, 1.7678}, tol);
    check_pos(m, "mShoulderLeft", {-0.037, 0.1998, 1.5863}, tol);
    check_pos(m, "mWristLeft", {-0.037, 0.7602, 1.5863}, tol);
    check_pos(m, "mHipLeft", {0.034, 0.127, 1.0728}, tol);
    check_pos(m, "mKneeLeft", {0.033, 0.0787, 0.5818}, tol);
    check_pos(m, "mFootLeft", {0.1145, 0.0797, 0.006}, tol);
    check_pos(m, "mWing1Left", {-0.1787, 0.105, 1.594}, tol);
}

TEST(skeleton_pose_rotates_children) {
    const Skeleton& s = skel();
    Pose p(s.size());
    int shoulder = s.find("mShoulderLeft");
    p.rot[shoulder] = euler_to_quat({0, 0, 90});  // swing the left arm forward
    auto g = s.global_pose(p);
    Vec3 arm = g[s.find("mWristLeft")].pos - g[shoulder].pos;
    CHECK_NEAR(arm.x, -0.453, 1e-3);
    CHECK_NEAR(arm.y, 0, 1e-3);
}

// 08 FP-1: stick bones join each joint to its child joints where the body has them, not where SL's lengths say.
TEST(stick_segments_follow_moved_joints) {
    const Skeleton& s = skel();
    Shape shape;
    shape.scale.assign(s.size(), Vec3{1, 1, 1});
    shape.offset.assign(s.size(), Vec3{});
    const int knee = s.find("mKneeLeft"), ankle = s.find("mAnkleLeft"), tail = s.find("mTail1"), skull = s.find("mSkull");
    shape.offset[knee] = {0.30, 0.05, 0.10};   // a creature's leg: the joints far from SL's
    shape.offset[ankle] = {-0.25, 0, -0.30};
    shape.offset[tail] = {-0.40, 0, 0.20};
    Pose pose(s.size());
    pose.rot[s.find("mHipLeft")] = Quat::axis_angle({0, 1, 0}, 0.6);
    const std::vector<Xform> g = s.global_pose(pose, &shape);
    const std::vector<StickSegment> segs = stick_segments(s, g, &shape);
    int joined = 0, stubs = 0;
    std::vector<int> children(s.size(), 0), lines(s.size(), 0);
    for (int i = 0; i < s.joint_count(); ++i)
        for (int c : s[i].children) children[i] += c < s.joint_count();
    for (const StickSegment& k : segs) {
        CHECK(k.node >= 0 && k.node < s.joint_count());
        CHECK((k.a - g[k.node].pos).length() < 1e-12);  // every stick starts on its joint
        ++lines[k.node];
        if (k.stub) {
            ++stubs;
            CHECK(children[k.node] == 0);
            CHECK((k.b - k.a).length() <= 0.05 + 1e-9 && (k.b - k.a).length() > 1e-5);
            continue;
        }
        bool ends_on_child = false;
        for (int c : s[k.node].children) ends_on_child = ends_on_child || (c < s.joint_count() && (k.b - g[c].pos).length() < 1e-12);
        CHECK(ends_on_child);
        ++joined;
    }
    for (int i = 0; i < s.joint_count(); ++i) CHECK(lines[i] == (children[i] ? children[i] : lines[i] ? 1 : 0));
    CHECK(joined == s.joint_count() - 1);  // every joint but the pelvis hangs off its parent by one stick
    CHECK(stubs > 0 && lines[skull] == 1);
    // The moved knee: SL's own thigh bone end ends well short of it, the stick ends on it.
    const int hip = s.find("mHipLeft");
    CHECK((bone_tail(s, g, &shape, hip) - g[knee].pos).length() > 0.2);
    // A fitted rig tail is the stub, whole.
    shape.tails.assign(s.size(), Vec3{});
    shape.axes.assign(s.size(), Quat{});
    shape.tails[skull] = {0, 0, 0.31};
    for (const StickSegment& k : stick_segments(s, g, &shape))
        if (k.node == skull) CHECK_NEAR((k.b - k.a).length(), 0.31, 1e-9);
    // Hidden joints take their sticks with them, and the joint above a hidden one ends in a stub.
    std::vector<bool> shown(s.size(), true);
    shown[ankle] = false;
    for (const StickSegment& k : stick_segments(s, g, &shape, shown)) {
        CHECK(k.node != ankle);
        if (k.node == knee) CHECK(k.stub);
    }
}

namespace {

struct Pt { double x, y; };

// The app's picking (App::pick_node) with every node shown.
int test_pick_stick_bone(const Skeleton& skel, const std::vector<Xform>& globals, const Shape* shape,
                         const Projector& pr, Pt m, std::vector<int>* ranked = nullptr) {
    std::vector<int> hits = pick_sticks(
        skel, globals, shape, std::vector<bool>(skel.size(), true), true,
        [&](const Vec3& p, double& x, double& y) { return pr.to_screen(p, x, y); }, m.x, m.y);
    if (ranked) *ranked = hits;
    return hits.empty() ? -1 : hits.front();
}

}  // namespace

TEST(stick_bone_picking_sl_avatar_and_mech) {
    const Skeleton& s = skel();
    Camera cam;
    cam.target = {0, 0, 1.0};
    cam.distance = 2.5;
    cam.yaw = 0.0;
    cam.pitch = 0.0;
    Projector pr;
    pr.w = 1600;
    pr.h = 1000;
    pr.x0 = 0;
    pr.y0 = 0;
    pr.view_proj = cam.projection(1600.0 / 1000.0) * cam.view();

    // 1. SL Avatar picking
    Pose rest(s.size());
    const std::vector<Xform> g = s.global_pose(rest);
    const int knee = s.find("mKneeLeft"), hip = s.find("mHipLeft");
    double kx, ky, hx, hy;
    CHECK(pr.to_screen(g[knee].pos, kx, ky));
    CHECK(pr.to_screen(g[hip].pos, hx, hy));

    // A click on the knee joint dot picks mKneeLeft
    std::vector<int> ranked;
    int picked = test_pick_stick_bone(s, g, nullptr, pr, Pt{kx, ky}, &ranked);
    CHECK_EQ(picked, knee);

    // A click halfway along the thigh stick (from hip to knee) picks mHipLeft (the owner of the segment)
    Pt mid_thigh{(hx + kx) * 0.5, (hy + ky) * 0.5};
    picked = test_pick_stick_bone(s, g, nullptr, pr, mid_thigh, &ranked);
    CHECK_EQ(picked, hip);

    // Stacked Bento spine bones: mPelvis, mTorso, mSpine1..4 at rest
    const int pelvis = s.find("mPelvis");
    double px, py;
    CHECK(pr.to_screen(g[pelvis].pos, px, py));
    picked = test_pick_stick_bone(s, g, nullptr, pr, Pt{px, py}, &ranked);
    CHECK_EQ(picked, pelvis);
    // Stacked bones present in ranked list
    CHECK(ranked.size() >= 2);
    // VP-23: clicking the spot again walks the whole stack, one bone a click, and comes back round.
    std::vector<int> walked{picked};
    for (size_t k = 1; k < ranked.size(); ++k) walked.push_back(next_stacked(ranked, walked.back(), picked));
    CHECK(std::set<int>(walked.begin(), walked.end()).size() == ranked.size());
    CHECK_EQ(next_stacked(ranked, walked.back(), picked), pelvis);
    CHECK_EQ(next_stacked(ranked, s.find("mHead"), picked), picked);  // the current bone isn't under the pointer

    // 2. Mech picking
    DaeModel m;
    std::string err;
    DaeReport r;
    CHECK(load_dae(mech::dae(s, mech::build(s)), "", s, m, r, err));
    Shape mech_sh;
    shape_from_binds(s, {&m}, nullptr, mech_sh);
    rig_axes_from_parts(s, {&m}, mech_sh);
    const std::vector<Xform> mg = s.global_pose(rest, &mech_sh);
    const int hind1 = s.find("mHindLimb1Left"), hind2 = s.find("mHindLimb2Left");
    double h1x, h1y, h2x, h2y;
    CHECK(pr.to_screen(mg[hind1].pos, h1x, h1y));
    CHECK(pr.to_screen(mg[hind2].pos, h2x, h2y));
    // Click on hind2 joint dot picks hind2
    picked = test_pick_stick_bone(s, mg, &mech_sh, pr, Pt{h2x, h2y}, &ranked);
    CHECK_EQ(picked, hind2);
    // Click along stick segment of hind1 picks hind1
    Pt mid_hind{(h1x + h2x) * 0.5, (h1y + h2y) * 0.5};
    picked = test_pick_stick_bone(s, mg, &mech_sh, pr, mid_hind, &ranked);
    CHECK_EQ(picked, hind1);
}

// A finger's dot wins inside its drawn size over its parent's stick, which runs right into it (VP-21): seen from above
// at hand distance, a click 3 px off each dot's centre toward its parent takes that dot's bone.
TEST(stick_picking_short_bone_dot_beats_parent_stick) {
    const Skeleton& s = skel();
    const std::vector<Xform> g = s.global_pose(Pose(s.size()));
    Camera cam;
    cam.target = g[s.find("mWristRight")].pos;
    cam.distance = 1.2, cam.yaw = 0, cam.pitch = 1.4;
    Projector pr;
    pr.w = 608, pr.h = 430;
    pr.view_proj = cam.projection(608.0 / 430) * cam.view();
    int tried = 0;
    for (int i = 0; i < s.joint_count(); ++i) {
        const std::string& n = s[i].name;
        const int parent = s[i].parent;
        if (n.find("Hand") == std::string::npos || n.find("Right") == std::string::npos || parent < 0) continue;
        double cx, cy, px, py;
        CHECK(pr.to_screen(g[i].pos, cx, cy) && pr.to_screen(g[parent].pos, px, py));
        const double len = std::hypot(px - cx, py - cy);
        if (len < 1e-6) continue;
        const int got = test_pick_stick_bone(s, g, nullptr, pr, Pt{cx + (px - cx) / len * 3, cy + (py - cy) / len * 3});
        if (got != i) check::fail(__FILE__, __LINE__, n + "'s dot picked " + (got < 0 ? "nothing" : s[got].name));
        ++tried;
    }
    CHECK(tried >= 15);
}
