// Spec 08 BD-3: a mesh body's bind pose sets the joint positions IK and pins preview against.
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/dae.h"
#include "vats/edit.h"
#include "vats/rig.h"

using namespace vats;

namespace {

// A rigged "devkit" whose binds are the SL rest, except the knees 5 cm and the ankles 10 cm lower: longer legs.
DaeModel long_legs(const Skeleton& s) {
    DaeModel m;
    m.rigged = true;
    m.binds = s.global_pose(Pose(s.size()));
    for (const char* side : {"Left", "Right"}) {
        m.binds[s.find(std::string("mKnee") + side)].pos.z -= 0.05;
        m.binds[s.find(std::string("mAnkle") + side)].pos.z -= 0.10;
    }
    return m;
}

}  // namespace

TEST(body_shape_from_binds_moves_overridden_joints) {
    const Skeleton& s = skel();
    DaeModel m = long_legs(s);
    Shape out;
    CHECK(shape_from_binds(s, {&m}, nullptr, out));
    auto rest = s.global_pose(Pose(s.size()));
    auto g = s.global_pose(Pose(s.size()), &out);
    int knee = s.find("mKneeLeft"), ankle = s.find("mAnkleLeft"), foot = s.find("mFootLeft"), hip = s.find("mHipLeft");
    // The whole body rises 10 cm so the longer legs still stand on the ground (SK-28).
    const Vec3 up{0, 0, 0.10};
    CHECK((g[knee].pos - (m.binds[knee].pos + up)).length() < 1e-9);
    CHECK((g[ankle].pos - (m.binds[ankle].pos + up)).length() < 1e-9);
    CHECK((g[hip].pos - (rest[hip].pos + up)).length() < 1e-9);  // not overridden: only lifted
    CHECK_NEAR(g[foot].pos.z, rest[foot].pos.z, 1e-9);            // unbound child follows its moved parent

    // A body bound exactly at the SL rest overrides nothing: the base shape stays.
    DaeModel plain;
    plain.rigged = true;
    plain.binds = rest;
    Shape keep;
    CHECK(!shape_from_binds(s, {&plain}, &s.male_shape(), keep));
    CHECK(keep.offset == s.male_shape().offset);
}

TEST(body_shape_changes_the_ik_result) {
    const Skeleton& s = skel();
    Rig rig(s);
    DaeModel m = long_legs(s);
    Shape body;
    CHECK(shape_from_binds(s, {&m}, nullptr, body));
    int limb = -1;
    for (int i = 0; i < int(rig.limbs().size()); ++i)
        if (rig.limbs()[i].name == "LegLeft") limb = i;
    CHECK(limb >= 0);
    // The same world target for the ankle, 45 cm up and forward: the longer, lifted leg reaches it with a
    // different knee bend, so the preview follows the body's proportions.
    Clip c;
    c.fps = 30, c.end_frame = 10;
    int ankle = s.find("mAnkleLeft"), knee = s.find("mKneeLeft");
    Xform target = s.global_pose(Pose(s.size()))[ankle];
    target.pos += Vec3{0.15, 0, 0.45};
    switch_to_ik(c, rig, 0, limb, nullptr);
    key_limb_target(c, rig, 0, limb, target, nullptr);
    Evaluation plain = evaluate(rig, c, 0, nullptr), longer = evaluate(rig, c, 0, &body);
    CHECK((longer.globals[ankle].pos - target.pos).length() < 1e-3);  // it still reaches the target
    double bend_plain = 2 * std::acos(std::min(1.0, std::fabs(plain.pose.rot[knee].w)));
    double bend_long = 2 * std::acos(std::min(1.0, std::fabs(longer.pose.rot[knee].w)));
    CHECK(std::fabs(bend_long - bend_plain) > 5 * kDegToRad);
}

TEST(body_swap_export_keys_positions_at_the_swapped_bodys_joints) {
    // Spec 09 build 32: with View > Body swapping a mesh body in (the viewer), Bake shape "Your avatar" is that body for
    // both IK and positions (App::export_shape, export_positions): a moving joint's position keys carry the body's own
    // joint position, from its binds (shape_from_binds), not the SL default's.
    const Skeleton& s = skel();
    DaeModel m = long_legs(s);
    Shape body;
    CHECK(shape_from_binds(s, {&m}, nullptr, body));
    const int knee = s.find("mKneeLeft");
    CHECK(body.offset[knee].length() > 0.04);  // the longer shin: the knee sits 5 cm lower than the SL default's
    Clip c;
    c.fps = 30, c.end_frame = 10;
    c.curves["mKneeLeft"]["pos_x"].set_key(0, 0);
    c.curves["mKneeLeft"]["pos_x"].set_key(10, 0.02);
    AnimExportOptions swapped{0, 0, 60};
    swapped.shape = swapped.positions = &body;
    const AnimImportResult on = import_anim(s, export_anim(s, c, swapped).file);
    CHECK((curve_offset(on.clip, "mKneeLeft", 0) - body.offset[knee]).length() < 2e-4);
    CHECK((curve_offset(on.clip, "mKneeLeft", 10) - body.offset[knee] - Vec3{0.02, 0, 0}).length() < 2e-4);
    // A Mesh body bake shape without the swap keeps its old meaning: IK on the body, positions from the SL default.
    AnimExportOptions ik_only{0, 0, 60};
    ik_only.shape = &body;
    const AnimImportResult off = import_anim(s, export_anim(s, c, ik_only).file);
    CHECK(curve_offset(off.clip, "mKneeLeft", 0).length() < 2e-4);
}

TEST(body_swap_skins_on_the_live_pose_with_its_own_joints) {
    // Spec 09 build 34: in the viewer's real-avatar modes the swapped body follows your avatar as the world moves it.
    // The host reads the avatar's joints back (Skeleton::pose_from_live); the body is skinned on that pose over its own
    // shape: every rotation and the hip's travel are the avatar's, every joint position the body's own bind.
    const Skeleton& s = skel();
    DaeModel m = long_legs(s);
    Shape body;
    CHECK(shape_from_binds(s, {&m}, nullptr, body));
    const int pelvis = 0, knee = s.find("mKneeLeft"), ankle = s.find("mAnkleLeft");
    // One vertex, bound at the left ankle's bind, all its weight on the ankle.
    m.positions = {float(m.binds[ankle].pos.x), float(m.binds[ankle].pos.y), float(m.binds[ankle].pos.z)};
    m.normals = {0, 0, 1};
    m.joints = {ankle, dae_root(s), dae_root(s), dae_root(s)};
    m.weights = {1, 0, 0, 0};
    m.binds.resize(size_t(dae_index_count(s)));
    std::vector<float> pos, nrm;

    // The avatar walked 2 m forward and 0.5 m left, sank 0.3 m (a sit), and turned a quarter to its left.
    const Vec3 rest_pelvis = s[pelvis].pos, travel{2.0, 0.5, -0.3}, up{0, 0, 0.10};  // up: the longer legs' lift
    const Quat turn = Quat::axis_angle({0, 0, 1}, kPi / 2);
    std::vector<Quat> local(size_t(s.size()));
    for (int i = 0; i < s.size(); ++i) local[size_t(i)] = s[i].rest;
    local[pelvis] = turn;
    Pose live = s.pose_from_live(local, rest_pelvis + travel);
    std::vector<Xform> g = s.global_pose(live, &body);
    CHECK((g[pelvis].pos - (rest_pelvis + travel + up)).length() < 1e-9);  // the hip's travel, on the body's own height
    CHECK(std::fabs(std::fabs(g[pelvis].rot.dot(turn)) - 1) < 1e-12);
    skin_prop(m, s, g, &body, pos, nrm);
    const Vec3 vertex{pos[0], pos[1], pos[2]};
    const Vec3 expect = rest_pelvis + travel + up + turn.rotate(m.binds[ankle].pos - rest_pelvis);
    CHECK((vertex - expect).length() < 1e-5);  // where the live pose puts it, on the body's longer leg

    // The knee bends 60 degrees: the ankle swings about the knee, at the distance the body's binds give its shin.
    local[size_t(knee)] = s[knee].rest * Quat::axis_angle({0, 1, 0}, kPi / 3);
    live = s.pose_from_live(local, rest_pelvis + travel);
    g = s.global_pose(live, &body);
    skin_prop(m, s, g, &body, pos, nrm);
    const Vec3 bent{pos[0], pos[1], pos[2]};
    const double shin = (m.binds[ankle].pos - m.binds[knee].pos).length();
    CHECK(std::fabs((bent - g[knee].pos).length() - shin) < 1e-5);
    CHECK((bent - vertex).length() > 0.2);                    // it did swing
    CHECK((g[ankle].pos - bent).length() < 1e-5);              // and stays on the body's ankle
    // Every joint turns as the live avatar's does, whatever the body's proportions.
    const std::vector<Xform> avatar = s.global_pose(live);
    for (int i : {pelvis, knee, ankle}) CHECK(std::fabs(std::fabs(g[size_t(i)].rot.dot(avatar[size_t(i)].rot)) - 1) < 1e-12);
}
