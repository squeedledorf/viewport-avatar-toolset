// A Bento face pose fitted to a shape key (spec 08 SK-5). The face is made here: patches of vertices around the jaw
// and the upper left lid, weighted to them, a ring shared with the head, and the head's own; its shape key is what a
// known bone pose does to it, so the fit has a right answer.
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/dae.h"
#include "vats/face_fit.h"

using namespace vats;

namespace {

int node(const char* name) { return skel().find(name); }

// The synthetic face: 3 x 3 x 2 vertices 1.5 cm apart in front of each bone, weighted wholly to it; a ring between the
// lid and the head, half each; and a patch on the head's crown, all head.
DaeModel synthetic_face() {
    const Skeleton& s = skel();
    std::vector<Xform> rest = s.global_pose(Pose(size_t(s.size())));
    DaeModel m;
    m.rigged = true;
    m.binds = rest;
    m.binds.push_back({});
    for (auto& v : s.volumes()) m.binds.push_back(rest[v.joint] * Xform{v.rot, v.pos});
    m.bound.assign(m.binds.size(), false);
    const int root = dae_root(s);
    auto patch = [&](int a, int b, float wa, const Vec3& from) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 2; ++k) {
                    const Vec3 p = rest[a].pos + from + Vec3{0.008 * k, 0.015 * (i - 1), 0.015 * (j - 1)};
                    m.positions.insert(m.positions.end(), {float(p.x), float(p.y), float(p.z)});
                    m.normals.insert(m.normals.end(), {1, 0, 0});
                    m.joints.insert(m.joints.end(), {a, b < 0 ? root : b, root, root});
                    m.weights.insert(m.weights.end(), {wa, 1 - wa, 0, 0});
                }
    };
    const int jaw = node("mFaceJaw"), lid = node("mFaceEyeLidUpperLeft"), head = node("mHead");
    patch(jaw, -1, 1, {0.03, 0, -0.01});
    patch(lid, -1, 1, {0.01, 0, 0.004});
    patch(lid, head, 0.5f, {0.012, 0, 0.03});
    patch(head, -1, 1, {0.02, 0, 0.12});
    m.parts.push_back({"Face", 0, std::uint32_t(m.vertex_count()), 0, 0});
    m.bounds_min = m.bounds_max = rest[head].pos;
    return m;
}

// The key a pose makes: every vertex's move from rest under it.
DaeShapeKey key_of(const DaeModel& m, const Pose& pose, const std::string& name) {
    const Skeleton& s = skel();
    std::vector<float> pos, nrm;
    skin_prop(m, s, s.global_pose(pose), nullptr, pos, nrm);
    DaeShapeKey k;
    k.name = name;
    for (std::uint32_t v = 0; v < std::uint32_t(m.vertex_count()); ++v) {
        const Vec3 d{pos[v * 3] - m.positions[v * 3], pos[v * 3 + 1] - m.positions[v * 3 + 1], pos[v * 3 + 2] - m.positions[v * 3 + 2]};
        if (d.length() < 1e-9) continue;
        k.vertices.push_back(v);
        for (int c = 0; c < 3; ++c) k.dpos.push_back(float(d[c]));
    }
    return k;
}

double degrees(const Quat& a, const Quat& b) { return (a.conj() * b).normalized().angle() * kRadToDeg; }

}  // namespace

TEST(face_fit_recovers_the_pose_that_made_the_key) {
    const Skeleton& s = skel();
    DaeModel m = synthetic_face();
    const int jaw = node("mFaceJaw"), lid = node("mFaceEyeLidUpperLeft");
    Pose made(size_t(s.size()));
    made.rot[size_t(jaw)] = Quat::axis_angle({0, 1, 0}, 14 * kDegToRad);   // the jaw drops open
    made.rot[size_t(lid)] = Quat::axis_angle({0, 1, 0}, 28 * kDegToRad);   // the lid closes
    m.shape_keys.push_back(key_of(m, made, "Open and blink"));

    FaceFitOptions turns;
    turns.positions = false;
    const FaceFit fit = fit_face_pose(s, nullptr, m, {}, "Open and blink", turns);
    CHECK_EQ(fit.why, std::string());
    CHECK(fit.bones.size() == 2);  // the jaw and the lid; the head is never posed
    CHECK(degrees(fit.pose.rot[size_t(jaw)], made.rot[size_t(jaw)]) < 0.05);
    CHECK(degrees(fit.pose.rot[size_t(lid)], made.rot[size_t(lid)]) < 0.05);
    CHECK(fit.explained > 0.9999);
    CHECK(fit.residual_rms < 1e-5);
    CHECK_NEAR(fit.reachable, 1.0, 1e-9);  // every moved vertex has a face bone's weight (the ring half the lid's)
    CHECK(fit.target_rms > 0.002);

    // As a face pose: the two bones, in Euler degrees, no offsets.
    const LibraryItem it = face_fit_pose(s, fit, "Open and blink");
    CHECK(it.kind == "face" && it.bones.size() == 2 && it.offsets.empty());
    CHECK(degrees(euler_to_quat(it.bones.at("mFaceJaw")), made.rot[size_t(jaw)]) < 0.05);

    // With positions the bones may move too: still exact.
    const FaceFit moved = fit_face_pose(s, nullptr, m, {}, "Open and blink");
    CHECK(moved.residual_rms < 1e-5);
}

// A key on vertices only the head carries: no face bone can move them, and the fit says so.
TEST(face_fit_refuses_a_key_no_face_bone_carries) {
    const Skeleton& s = skel();
    DaeModel m = synthetic_face();
    DaeShapeKey k;
    k.name = "Crown";
    for (std::uint32_t v = 54; v < 72; ++v) {  // the head's patch
        k.vertices.push_back(v);
        k.dpos.insert(k.dpos.end(), {0, 0, 0.01f});
    }
    m.shape_keys.push_back(k);
    const FaceFit fit = fit_face_pose(s, nullptr, m, {}, "Crown");
    CHECK(!fit.why.empty());
    CHECK(fit.bones.empty());
    CHECK_NEAR(fit.reachable, 0.0, 1e-12);
    CHECK_EQ(fit.vertices, 18);
    CHECK(!fit_face_pose(s, nullptr, m, {}, "No such key").why.empty());
}

// The share that decides whether a key offers a face pose: a face key's motion is all on face-weighted vertices, a key
// on the head's own patch none, and a key on both by how far each part moves.
TEST(shape_key_face_share_tells_face_keys_from_body_keys) {
    const Skeleton& s = skel();
    DaeModel m = synthetic_face();
    Pose made(size_t(s.size()));
    made.rot[size_t(node("mFaceJaw"))] = Quat::axis_angle({0, 1, 0}, 14 * kDegToRad);
    m.shape_keys.push_back(key_of(m, made, "Jaw open"));
    DaeShapeKey crown, both;
    crown.name = "Crown", both.name = "Both";
    for (std::uint32_t v = 54; v < 72; ++v) crown.vertices.push_back(v), crown.dpos.insert(crown.dpos.end(), {0, 0, 0.01f});
    both.vertices = {0, 54, 55, 56};  // one jaw vertex and three of the crown's, each 1 cm
    for (int i = 0; i < 4; ++i) both.dpos.insert(both.dpos.end(), {0, 0, 0.01f});
    m.shape_keys.push_back(crown), m.shape_keys.push_back(both);
    CHECK_NEAR(shape_key_face_share(s, m, "Jaw open"), 1.0, 1e-9);
    CHECK_NEAR(shape_key_face_share(s, m, "Crown"), 0.0, 1e-12);
    CHECK_NEAR(shape_key_face_share(s, m, "Both"), 0.25, 1e-9);
    CHECK_NEAR(shape_key_face_share(s, m, "No such key"), 0.0, 1e-12);
    m.rigged = false;
    CHECK_NEAR(shape_key_face_share(s, m, "Jaw open"), 0.0, 1e-12);
}

// A key no pose can make (one vertex of the jaw's patch pulled away from the rest): the best fit, and an honest number.
TEST(face_fit_reports_what_bones_cannot_do) {
    const Skeleton& s = skel();
    DaeModel m = synthetic_face();
    DaeShapeKey k;
    k.name = "Dimple";
    k.vertices = {8};  // the middle of the jaw's patch, front layer
    k.dpos = {-0.01f, 0, 0};
    m.shape_keys.push_back(k);
    const FaceFit fit = fit_face_pose(s, nullptr, m, {}, "Dimple");
    CHECK_EQ(fit.why, std::string());
    CHECK(fit.explained < 0.2);  // one vertex of a rigid patch cannot move alone: the patch follows it by 1/18
    CHECK(fit.residual_rms > 0.008);
}
