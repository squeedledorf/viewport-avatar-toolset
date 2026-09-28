#include <algorithm>
#include <cmath>

#include "check.h"
#include "fixtures.h"

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
