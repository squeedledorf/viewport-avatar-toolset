// Hands against the body's own mesh (mesh_contact.h).
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/mesh_contact.h"

using namespace vats;

namespace {

// The left arm hanging down, as when the hands rest on the hips, every frame.
Clip arm_down() {
    Clip c;
    c.fps = 30, c.end_frame = 10;
    for (int f : {0, 10}) key_rotation(c, "mShoulderLeft", f, Quat::axis_angle({1, 0, 0}, -1.3));
    return c;
}

// A rigged model: a thigh (a cylinder round mHipLeft's bone, carried by it) wide enough that the hanging left hand is
// 3 cm inside it, and a few hand vertices on the wrist.
DaeModel thigh_and_hand(const Skeleton& s, const Rig& rig, const Clip& c) {
    const std::vector<Xform> rest = s.global_pose(Pose(size_t(s.size())));
    const std::vector<Xform> g = s.global_pose(evaluate(rig, c, 0, nullptr).pose);
    const int hip = s.find("mHipLeft"), wrist = s.find("mWristLeft");
    DaeModel m;
    m.rigged = true;
    m.binds.assign(size_t(s.size()) + 1 + s.volumes().size(), Xform{});
    for (int i = 0; i < s.size(); ++i) m.binds[size_t(i)] = rest[size_t(i)];
    auto add = [&](const Vec3& p, const Vec3& n, int joint) {
        m.positions.insert(m.positions.end(), {float(p.x), float(p.y), float(p.z)});
        m.normals.insert(m.normals.end(), {float(n.x), float(n.y), float(n.z)});
        m.joints.insert(m.joints.end(), {joint, s.size(), s.size(), s.size()});
        m.weights.insert(m.weights.end(), {1.f, 0.f, 0.f, 0.f});
    };
    // The thigh, in the rest pose (the legs do not move): its axis the hip's bone, straight down.
    const Vec3 a = rest[size_t(hip)].pos, e = rest[size_t(hip)].apply(s[hip].end);
    const Vec3 hand = g[size_t(wrist)].pos;  // where the hand hangs (skinned with the arm, so rest-pose it below)
    const double r = std::hypot(hand.x - a.x, hand.y - a.y) + 0.03;
    for (int k = 0; k <= 40; ++k)
        for (int i = 0; i < 120; ++i) {
            const double t = k / 40.0, ang = i * 2 * 3.14159265358979 / 120;
            const Vec3 n{std::cos(ang), std::sin(ang), 0};
            add(a + (e - a) * t + n * r, n, hip);
        }
    // The hand: three points at the wrist, in the bind (rest) pose; the arm's pose carries them down to the thigh.
    const Vec3 w = rest[size_t(wrist)].pos;
    for (double dx : {-0.02, 0.0, 0.02}) add(w + Vec3{dx, 0, 0}, {0, 0, -1}, wrist);
    return m;
}

}  // namespace

TEST(mesh_contact_finds_and_pushes_a_hand_out) {
    const Skeleton& s = skel();
    const Rig rig(s);
    Clip c = arm_down();
    const DaeModel m = thigh_and_hand(s, rig, c);
    const MeshContactCheck check(s, {&m}, nullptr);
    CHECK(check.usable());
    const auto before = check.measure(s.global_pose(evaluate(rig, c, 5, nullptr).pose));
    CHECK(before.side[0].depth > 0.02 && before.side[0].depth < 0.04);  // about 3 cm in, the left hand
    CHECK(before.side[0].push.y > 0.8);                                   // out is away from the thigh's axis
    CHECK(before.side[1].depth == 0);                                     // the right hand is nowhere near
    const Vec3 hand_before = s.global_pose(evaluate(rig, c, 5, nullptr).pose)[size_t(s.find("mWristLeft"))].pos;
    const PushOutReport r = push_hands_out(c, rig, {&m}, nullptr, 0, 10);
    CHECK(r.frames[0] > 0 && r.frames[1] == 0);
    for (int f : {0, 5, 10}) CHECK(check.measure(s.global_pose(evaluate(rig, c, f, nullptr).pose)).side[0].depth < 0.005);
    // The hand moved out through the arm (the thigh is untouched).
    const Vec3 hand_after = s.global_pose(evaluate(rig, c, 5, nullptr).pose)[size_t(s.find("mWristLeft"))].pos;
    CHECK(hand_after.y > hand_before.y + 0.02);

    // A thin card (two-sided, no inside) never holds a hand: the same thigh as one flat sheet through its axis.
    DaeModel card = thigh_and_hand(s, rig, arm_down());
    const Vec3 a = s.global_pose(Pose(size_t(s.size())))[size_t(s.find("mHipLeft"))].pos;
    for (size_t v = 0; v + 3 < card.positions.size() / 3; ++v) {
        card.positions[v * 3] = float(a.x);
        card.normals[v * 3] = 0, card.normals[v * 3 + 1] = 0, card.normals[v * 3 + 2] = 1;
    }
    const MeshContactCheck card_check(s, {&card}, nullptr);
    const Clip down = arm_down();
    CHECK(card_check.measure(s.global_pose(evaluate(rig, down, 5, nullptr).pose)).side[0].depth == 0);
}
