// Spec 08 BD-3: a mesh body's bind pose sets the joint positions IK and pins preview against.
#include <cmath>
#include <initializer_list>
#include <tuple>

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

TEST(mixed_rig_with_bone_oriented_face_does_not_deform_head) {
    const Skeleton& s = skel();
    auto rest = s.global_pose(Pose(s.size()));
    rest.push_back({});  // mRoot
    for (auto& v : s.volumes()) rest.push_back(rest[v.joint] * Xform{v.rot, v.pos});
    const int count = dae_index_count(s);

    // Synthetic test: a rig whose body is in SL rest frames, but whose face joints are in Blender's
    // bone-orientation convention (Euler 180 or 90 degrees). With the bug, a 50/50 split causes
    // settle_rig to leave the face in bone axes, shape_from_binds inverts the face joints across the
    // centre line (left to right), and skinning tears the head mesh.
    DaeModel m;
    m.rigged = true;
    m.binds = rest;
    std::vector<bool> bound(count, false);
    for (const char* bname : {"mPelvis", "mTorso", "mChest", "mNeck"}) bound[s.find(bname)] = true;
    int fr = s.find("mFaceRoot"), ear_r = s.find("mFaceEar1Right"), ear_l = s.find("mFaceEar1Left");
    bound[fr] = bound[ear_r] = bound[ear_l] = true;

    // Face root turned 180 degrees about Z as Blender exporters do.
    const Quat flip_z = Quat::axis_angle({0, 0, 1}, kPi);
    m.binds[fr].rot = flip_z;
    m.binds[ear_r].rot = Quat::axis_angle({0, 1, 0}, kPi / 2);
    m.binds[ear_l].rot = Quat::axis_angle({0, 1, 0}, -kPi / 2);
    m.binds[ear_r].pos.y -= 0.01;
    m.binds[ear_l].pos.y += 0.01;

    DaeReport rep;
    settle_rig(m, s, bound, rep);

    // After settle_rig, face binds must have SL rest rotations (not inverted bone axes).
    CHECK((m.binds[fr].rot.conj() * rest[fr].rot).angle() < 1e-6);
    CHECK((m.binds[ear_r].rot.conj() * rest[ear_r].rot).angle() < 1e-6);

    Shape body_shape;
    CHECK(shape_from_binds(s, {&m}, nullptr, body_shape));
    auto g = s.global_pose(Pose(s.size()), &body_shape);

    // Right ear must remain on the right (-Y in SL) and left ear on the left (+Y in SL).
    CHECK(g[ear_r].pos.y < -0.05);
    CHECK(g[ear_l].pos.y > 0.05);
}

TEST(mixed_rig_eyes_and_skull_go_with_the_face) {
    // A whole rig (CC0, built here): the body in SL's rest frames, the face, the eyes and mSkull in Blender's bone
    // axes (Y along the bone). The eyes and mSkull are Body joints in SL's skeleton, but a head is rigged with them:
    // counted with the body, they stayed in bone axes and the irises pointed the wrong way.
    const Skeleton& s = skel();
    auto rest = s.global_pose(Pose(s.size()));
    rest.push_back({});  // mRoot
    for (auto& v : s.volumes()) rest.push_back(rest[v.joint] * Xform{v.rot, v.pos});
    const int count = dae_index_count(s);
    auto head_rig = [&](int j) {
        const std::string& n = s[j].name;
        return s[j].category == Category::Face || n.rfind("mFace", 0) == 0 || n == "mEyeLeft" || n == "mEyeRight" ||
               n == "mSkull";
    };
    auto arc = [](Vec3 a, Vec3 b) {  // the shortest turn from a to b
        a = a.normalized(), b = b.normalized();
        const Vec3 c = a.cross(b);
        return Quat{1 + a.dot(b), c.x, c.y, c.z}.normalized();
    };
    DaeModel m;
    m.rigged = true;
    m.binds = rest;
    std::vector<bool> bound(count, false);
    for (int j = 0; j < s.size(); ++j) {
        if (s[j].attachment) continue;
        bound[j] = true;
        if (head_rig(j)) m.binds[j].rot = arc({0, 1, 0}, s[j].end.length() > 1e-6 ? s[j].end : Vec3{0, 0, 1});
    }
    DaeReport rep;
    settle_rig(m, s, bound, rep);
    for (const char* n : {"mEyeLeft", "mEyeRight", "mSkull", "mFaceEyeAltLeft", "mFaceJaw"}) {
        const int j = s.find(n);
        CHECK((m.binds[j].rot.conj() * rest[j].rot).angle() < 1e-6);
    }
    const int pelvis = s.find("mPelvis");
    CHECK((m.binds[pelvis].rot.conj() * rest[pelvis].rot).angle() < 1e-6);  // the body was already in SL's frames
}

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
    Pose live = s.pose_from_live(local, rest_pelvis + travel, rest_pelvis);
    std::vector<Xform> g = s.global_pose(live, &body);
    CHECK((g[pelvis].pos - (rest_pelvis + travel + up)).length() < 1e-9);  // the hip's travel, on the body's own height
    CHECK(std::fabs(std::fabs(g[pelvis].rot.dot(turn)) - 1) < 1e-12);
    skin_prop(m, s, g, &body, pos, nrm);
    const Vec3 vertex{pos[0], pos[1], pos[2]};
    const Vec3 expect = rest_pelvis + travel + up + turn.rotate(m.binds[ankle].pos - rest_pelvis);
    CHECK((vertex - expect).length() < 1e-5);  // where the live pose puts it, on the body's longer leg

    // The knee bends 60 degrees: the ankle swings about the knee, at the distance the body's binds give its shin.
    local[size_t(knee)] = s[knee].rest * Quat::axis_angle({0, 1, 0}, kPi / 3);
    live = s.pose_from_live(local, rest_pelvis + travel, rest_pelvis);
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

TEST(body_swap_feet_stay_on_the_ground_whatever_the_worn_shape) {
    // Spec 09 build 35. The viewer stands your avatar's root sl_body_size().pelvis_to_foot above the ground, and the
    // editor's frame is that root. The swapped body must stand on the same ground at its own height whatever shape you
    // wear (a longer neck, taller hips, shorter legs), and follow your avatar's hip only by what an animation moves it
    // from your avatar's own rest. Build 34 took the frame's origin as the SL default's feet and the hip's travel from
    // the SL default's pelvis, so the body sank or floated by the worn avatar's difference.
    const Skeleton& s = skel();
    DaeModel m = long_legs(s);
    Shape body;
    CHECK(shape_from_binds(s, {&m}, nullptr, body));
    const int foot = s.find("mFootLeft");
    std::vector<Quat> local(size_t(s.size()));
    for (int i = 0; i < s.size(); ++i) local[size_t(i)] = s[i].rest;

    // The SL default's pelvis_to_foot, as LLAvatarAppearance::computeBodySize has it (the hip's height counts
    // negatively): 0.979 m, and its height.
    const SlBodySize def = sl_body_size(s);
    CHECK(std::fabs(def.pelvis_to_foot - (-0.041 + 0.491 + 0.468 + 0.061)) < 2e-3);
    CHECK(def.height > def.pelvis_to_foot + 0.4);

    auto shape = [&](std::initializer_list<std::tuple<const char*, Vec3, Vec3>> joints) {
        Shape w;
        w.scale.assign(size_t(s.size()), Vec3{1, 1, 1});
        w.offset.assign(size_t(s.size()), Vec3{});
        for (auto& [name, off, scale] : joints) {
            w.offset[size_t(s.find(name))] = off;
            w.scale[size_t(s.find(name))] = scale;
        }
        return w;
    };
    const Vec3 one{1, 1, 1};
    const Shape neck = shape({{"mNeck", {0, 0, 0.30}, one}, {"mHead", {0, 0, 0.20}, one}});
    const Shape hips = shape({{"mHipLeft", {0, 0, -0.12}, one}, {"mHipRight", {0, 0, -0.12}, one}});
    const Shape short_legs = shape({{"mHipLeft", {}, {1, 1, 0.8}}, {"mKneeLeft", {}, {1, 1, 0.8}},
                                    {"mHipRight", {}, {1, 1, 0.8}}, {"mKneeRight", {}, {1, 1, 0.8}},
                                    {"mKneeLeft", {0, 0, 0.06}, one}});
    CHECK(std::fabs(sl_body_size(s, nullptr, &neck).height - def.height - 0.50) < 1e-9);  // taller only above the hips
    CHECK(std::fabs(sl_body_size(s, nullptr, &neck).pelvis_to_foot - def.pelvis_to_foot) < 1e-12);
    CHECK(std::fabs(sl_body_size(s, nullptr, &hips).pelvis_to_foot - def.pelvis_to_foot) > 0.1);
    CHECK(std::fabs(sl_body_size(s, nullptr, &short_legs).pelvis_to_foot - def.pelvis_to_foot) > 0.1);

    // Where the body's left foot lands above the ground, the worn avatar's root standing where the viewer puts it.
    // sink: how far your avatar's root is below the frame's (SL's taller body size under the pinned frame, below).
    auto feet = [&](const Shape* worn, const Vec3& hip_travel, double sink) {
        const double root = sl_body_size(s, nullptr, worn).pelvis_to_foot;  // ground at 0
        const Vec3 frame = s.worn_pelvis_rest(root);                        // the frame, taken before any sink
        const Vec3 rest = frame - Vec3{0, 0, sink};                         // your avatar's own rest, read back
        const Pose live = s.pose_from_live(local, rest + hip_travel, rest);
        return root + s.global_pose(live, &body)[size_t(foot)].pos.z - frame.z;
    };
    const double ground = feet(nullptr, {}, 0);
    const double own = s.global_pose(Pose(s.size()), &body)[size_t(foot)].pos.z;  // the body's own foot height
    CHECK(std::fabs(ground - (def.pelvis_to_foot - s[0].pos.z + own)) < 1e-9);
    for (const Shape* worn : {&neck, &hips, &short_legs}) CHECK(std::fabs(feet(worn, {}, 0) - ground) < 1e-9);
    // An animation that makes SL's height 0.5 m taller lowers your root 0.25 m under the frame: the body stays.
    CHECK(std::fabs(feet(&neck, {}, 0.25) - ground) < 1e-9);
    // Your shape changes while the editor holds you pinned (build 35 test step 3). The frame is the pin, taken on the
    // old shape, and does not move; your root does (standing, the region stands it on the new legs; sitting on the
    // ground, it keeps its place). The ground under the pin is where it was, so the frame's ground must come from the
    // pelvis_to_foot of when the pin was taken: read again from the new shape, it would move the body off the ground.
    for (const Shape* worn : {&neck, &hips, &short_legs}) {
        const double old_root = def.pelvis_to_foot, new_root = sl_body_size(s, nullptr, worn).pelvis_to_foot;
        const Vec3 frame = s.worn_pelvis_rest(old_root);  // the pin's
        for (const double root_now : {old_root, new_root}) {  // sitting on the ground, standing
            const Vec3 rest = frame + Vec3{0, 0, root_now - old_root};
            const Pose live = s.pose_from_live(local, rest, rest);
            const double at = old_root + s.global_pose(live, &body)[size_t(foot)].pos.z - frame.z;
            CHECK(std::fabs(at - ground) < 1e-9);
            const double refreshed = old_root + s.global_pose(live, &body)[size_t(foot)].pos.z -
                                     s.worn_pelvis_rest(new_root).z;
            if (worn != &neck) CHECK(std::fabs(refreshed - ground) > 0.05);  // what reading it again would do
        }
    }
    // A sit that lowers your hip 0.4 m lowers the body 0.4 m, on any worn shape.
    for (const Shape* worn : {(const Shape*)nullptr, &neck, &hips, &short_legs})
        CHECK(std::fabs(feet(worn, {0, 0, -0.4}, 0) - (ground - 0.4)) < 1e-9);
}
