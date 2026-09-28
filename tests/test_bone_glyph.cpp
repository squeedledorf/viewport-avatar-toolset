// The viewport's bone glyphs: closed, outward-wound solids, and the folded Bento spine drawn as rings.
#include <map>
#include <tuple>

#include "check.h"
#include "fixtures.h"
#include "vats/bone_glyph.h"

using namespace vats;

namespace {

using Key = std::tuple<long long, long long, long long>;
Key key(const Vec3& p) { return {std::llround(p.x * 1e7), std::llround(p.y * 1e7), std::llround(p.z * 1e7)}; }

// Every edge in exactly two triangles, once each way (so the winding agrees across it), and a positive volume
// (so the winding faces out). Returns the volume.
double check_closed(const std::vector<Vec3>& tris) {
    CHECK(!tris.empty() && tris.size() % 3 == 0);
    std::map<std::pair<Key, Key>, int> edges;
    double volume = 0;
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const Vec3 &a = tris[t], &b = tris[t + 1], &c = tris[t + 2];
        CHECK((b - a).cross(c - a).length() > 1e-12);  // no degenerate face
        for (int e = 0; e < 3; ++e) ++edges[{key(tris[t + e]), key(tris[t + (e + 1) % 3])}];
        volume += a.dot(b.cross(c)) / 6;
    }
    for (const auto& [e, n] : edges) {
        CHECK_EQ(n, 1);
        auto back = edges.find({e.second, e.first});
        CHECK(back != edges.end() && back->second == 1);
    }
    CHECK(volume > 0);
    return volume;
}

}  // namespace

TEST(bone_glyph_is_a_closed_outward_solid) {
    const Quat frames[] = {Quat{}, Quat::axis_angle({0.3, -1, 0.2}, 1.1), Quat::axis_angle({0, 0, 1}, kPi / 2)};
    const std::pair<Vec3, Vec3> bones[] = {{{0, 0, 1}, {0, 0, 1.2}}, {{0.1, 0.2, 1}, {0.4, -0.1, 0.8}}, {{0, 0, 0}, {1, 0, 0}}};
    for (const Quat& f : frames)
        for (auto [h, t] : bones) {
            std::vector<Vec3> tris;
            bone_glyph(tris, h, t, f);
            CHECK_EQ(tris.size(), size_t(24));
            check_closed(tris);
            // Convex: each face's normal points away from the middle of the glyph.
            const Vec3 centre = h + (t - h) * 0.3;
            for (size_t k = 0; k < tris.size(); k += 3) {
                const Vec3 n = (tris[k + 1] - tris[k]).cross(tris[k + 2] - tris[k]);
                CHECK(n.dot((tris[k] + tris[k + 1] + tris[k + 2]) * (1.0 / 3) - centre) > 0);
            }
        }
}

TEST(joint_ring_is_a_closed_outward_solid) {
    std::vector<Vec3> tris;
    joint_ring(tris, {0.1, 0, 1.2}, {0.2, 0.1, 1}, 0.02);
    const double r = 0.02, tube = 0.004;  // a torus: 2 pi^2 R r^2, less for its 20 x 6 facets
    CHECK_NEAR(check_closed(tris), 2 * kPi * kPi * r * tube * tube, 0.25 * 2 * kPi * kPi * r * tube * tube);
}

TEST(folded_spine_bones_get_rings) {
    const Skeleton& s = skel();
    const auto g = s.global_pose(Pose(size_t(s.size())));
    const auto kind = glyph_kinds(s, g, nullptr);
    auto k = [&](const char* n) { return kind[size_t(s.find(n))]; };
    for (const char* spike : {"mPelvis", "mTorso", "mChest", "mNeck", "mHead", "mShoulderLeft", "mHipRight"})
        CHECK_EQ(k(spike), -1);
    for (const char* ring : {"mSpine1", "mSpine2", "mSpine3", "mSpine4"}) CHECK(k(ring) >= 0);
    // mSpine1 and mSpine4 share mTorso's joint: nested rings, not one on the other.
    CHECK_NEAR((g[size_t(s.find("mSpine1"))].pos - g[size_t(s.find("mSpine4"))].pos).length(), 0, 1e-6);
    CHECK(k("mSpine1") != k("mSpine4"));
    // The eyes' twins too, and nothing else at rest.
    CHECK(k("mFaceEyeAltLeft") >= 0 && k("mFaceEyeAltRight") >= 0);
    CHECK_EQ(k("mEyeLeft"), -1);
    int rings = 0;
    for (int v : kind) rings += v >= 0;
    CHECK_EQ(rings, 6);
    // Bent, a fold opens and the bone is a spike again.
    Pose p(size_t(s.size()));
    p.rot[size_t(s.find("mSpine1"))] = Quat::axis_angle({0, 1, 0}, 0.5);
    const auto bent = glyph_kinds(s, s.global_pose(p), nullptr);
    CHECK_EQ(bent[size_t(s.find("mSpine1"))], -1);
}
