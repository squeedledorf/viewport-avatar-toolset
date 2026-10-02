// Bone heat and the robust Laplacian (spec 08 RG-13): the solver, the Laplacian's properties on broken meshes, and
// weights on a two-bone cylinder.
#include <algorithm>
#include <cmath>
#include <map>

#include "check.h"
#include "vats/bone_heat.h"

using namespace vats;

namespace {

// A closed cylinder along +Z: rings of `sides` vertices every `step` from z0 to z1, capped by a centre vertex at each
// end. Seams: the first and last vertex of each ring are separate vertices at one position when seam is set (as a
// UV-split export has them), so welding must join them.
struct Tube {
    std::vector<float> pos;
    std::vector<std::uint32_t> idx;
    std::vector<double> z;  // per ring
    int sides = 0, rings = 0, per_ring = 0;
};
Tube tube(double radius, double z0, double z1, double step, int sides, bool seam) {
    Tube t;
    t.sides = sides;
    t.rings = int(std::lround((z1 - z0) / step)) + 1;
    t.per_ring = sides + (seam ? 1 : 0);
    for (int r = 0; r < t.rings; ++r) {
        const double z = z0 + (z1 - z0) * r / (t.rings - 1);
        t.z.push_back(z);
        for (int s = 0; s < t.per_ring; ++s) {
            const double a = 2 * kPi * (s % sides) / sides;
            t.pos.insert(t.pos.end(), {float(radius * std::cos(a)), float(radius * std::sin(a)), float(z)});
        }
    }
    auto at = [&](int r, int s) { return std::uint32_t(r * t.per_ring + s); };
    for (int r = 0; r + 1 < t.rings; ++r)
        for (int s = 0; s < sides; ++s) {
            const int s1 = seam ? s + 1 : (s + 1) % sides;
            t.idx.insert(t.idx.end(), {at(r, s), at(r, s1), at(r + 1, s1), at(r, s), at(r + 1, s1), at(r + 1, s)});
        }
    const std::uint32_t bottom = std::uint32_t(t.pos.size() / 3);
    t.pos.insert(t.pos.end(), {0.f, 0.f, float(z0)});
    const std::uint32_t top = bottom + 1;
    t.pos.insert(t.pos.end(), {0.f, 0.f, float(z1)});
    for (int s = 0; s < sides; ++s) {
        const int s1 = seam ? s + 1 : (s + 1) % sides;
        t.idx.insert(t.idx.end(), {bottom, at(0, s1), at(0, s)});
        t.idx.insert(t.idx.end(), {top, at(t.rings - 1, s), at(t.rings - 1, s1)});
    }
    return t;
}

double weight_of(const std::vector<int>& j, const std::vector<float>& w, size_t v, int node) {
    double x = 0;
    for (int k = 0; k < 4; ++k)
        if (j[v * 4 + size_t(k)] == node) x += w[v * 4 + size_t(k)];
    return x;
}

std::vector<std::uint32_t> range(std::uint32_t first, std::uint32_t count) {
    std::vector<std::uint32_t> r(count);
    for (std::uint32_t i = 0; i < count; ++i) r[i] = first + i;
    return r;
}

}  // namespace

TEST(sparse_cholesky_solves_a_laplacian_system) {
    // A grid's cotangent Laplacian plus a positive diagonal: solve, then check the residual.
    std::vector<Vec3> p;
    std::vector<std::array<int, 3>> t;
    const int n = 30;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) p.push_back({x * 0.1 + 0.013 * ((x * 7 + y * 3) % 5), y * 0.1, 0.01 * ((x * y) % 3)});
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x) {
            const int a = y * n + x;
            t.push_back({a, a + 1, a + n + 1}), t.push_back({a, a + n + 1, a + n});
        }
    const MeshLaplacian lap = mesh_laplacian(p, t, LaplacianKind::Robust);
    std::vector<double> extra(p.size());
    for (size_t i = 0; i < p.size(); ++i) extra[i] = 0.01 * (1 + double(i % 7));
    SparseCholesky chol;
    CHECK(chol.factor(lap, extra, p));
    std::vector<double> b(p.size()), x;
    for (size_t i = 0; i < b.size(); ++i) b[i] = std::sin(double(i));
    x = b;
    chol.solve(x);
    std::vector<double> ax(p.size());
    for (size_t i = 0; i < p.size(); ++i) ax[i] = (lap.diagonal[i] + extra[i]) * x[i];
    for (size_t e = 0; e < lap.edges.size(); ++e) {
        const int i = lap.edges[e][0], j = lap.edges[e][1];
        ax[size_t(i)] -= lap.weight[e] * x[size_t(j)], ax[size_t(j)] -= lap.weight[e] * x[size_t(i)];
    }
    double worst = 0;
    for (size_t i = 0; i < p.size(); ++i) worst = std::max(worst, std::fabs(ax[i] - b[i]));
    CHECK(worst < 1e-9);
    // A singular system (no diagonal added) is refused rather than solved.
    SparseCholesky bad;
    CHECK(!bad.factor(lap, {}, p));
}

TEST(robust_laplacian_has_no_negative_weights_where_the_cotangent_one_does) {
    // A fan with very obtuse triangles (a sliver row) and a non-manifold fin and a degenerate triangle.
    std::vector<Vec3> p = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0.5, 0.02, 0}, {1.5, 0.02, 0}, {1, 1, 0}, {1, -1, 0},
                           {1, 0, 1}, {1.5, 0, 0}};
    std::vector<std::array<int, 3>> t = {{0, 1, 3}, {1, 2, 4}, {3, 1, 5}, {1, 4, 5}, {0, 6, 1}, {1, 6, 2},
                                         {0, 1, 7},   // a third fin on edge 0-1: non-manifold
                                         {1, 8, 2}};  // zero area, its corners on a line
    const MeshLaplacian cot = mesh_laplacian(p, t, LaplacianKind::Cotangent);
    const MeshLaplacian rob = mesh_laplacian(p, t, LaplacianKind::Robust);
    CHECK(cot.negative > 0);
    CHECK_EQ(rob.negative, 0);
    CHECK(rob.flips > 0);
    for (double w : rob.weight) CHECK(std::isfinite(w) && w >= -1e-12);
    // Rows sum to zero by construction; the masses sum to the mesh's area (each triangle once).
    double area = 0;
    for (const auto& x : t) area += 0.5 * (p[size_t(x[1])] - p[size_t(x[0])]).cross(p[size_t(x[2])] - p[size_t(x[0])]).length();
    double mass = 0;
    for (double m : rob.mass) mass += m;
    CHECK_NEAR(mass, area, 1e-3 * area);  // the mollified lengths add a hair
    for (double m : rob.mass) CHECK(m >= 0);
    // On a clean mesh that is Delaunay already (a regular tetrahedron's faces), it is the cotangent Laplacian exactly:
    // the tufted cover of a closed manifold is two copies of it, halved.
    const std::vector<Vec3> q = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
    const std::vector<std::array<int, 3>> s = {{0, 1, 2}, {0, 3, 1}, {0, 2, 3}, {1, 3, 2}};
    const MeshLaplacian a = mesh_laplacian(q, s, LaplacianKind::Cotangent), b = mesh_laplacian(q, s, LaplacianKind::Robust);
    CHECK_EQ(a.edges.size(), b.edges.size());
    CHECK_EQ(b.flips, 0);
    for (size_t e = 0; e < a.edges.size() && e < b.edges.size(); ++e) {
        CHECK(a.edges[e] == b.edges[e]);
        CHECK_NEAR(a.weight[e], b.weight[e], 1e-4 * std::fabs(a.weight[e]));
    }
    for (size_t v = 0; v < q.size(); ++v) CHECK_NEAR(a.mass[v], b.mass[v], 1e-4 * a.mass[v]);
}

TEST(bone_heat_on_a_two_bone_cylinder_goes_smoothly_from_one_to_zero) {
    // Bones A (0..1 m) and B (1..2 m) up the middle of a capped cylinder with a UV seam.
    const Tube t = tube(0.15, 0, 2, 0.05, 24, true);
    const std::vector<HeatBone> bones = {{7, {0, 0, 0.0}, {0, 0, 1}}, {9, {0, 0, 1}, {0, 0, 2}}};
    std::vector<int> j;
    std::vector<float> w;
    BoneHeatStats st;
    const std::uint32_t nv = std::uint32_t(t.pos.size() / 3);
    CHECK(bone_heat(t.pos, t.idx, range(0, nv), bones, 99, {}, j, w, &st));
    CHECK_EQ(j.size(), size_t(nv) * 4);
    CHECK_EQ(st.unreached, 0);
    CHECK(st.max_weight <= 1 + 1e-6 && st.min_weight >= -1e-6);
    // Every vertex: at most 4 influences, summing to 1, sorted.
    for (std::uint32_t v = 0; v < nv; ++v) {
        double sum = 0;
        int used = 0;
        for (int k = 0; k < 4; ++k) sum += w[v * 4 + size_t(k)], used += w[v * 4 + size_t(k)] > 0;
        CHECK_NEAR(sum, 1, 1e-5);
        CHECK(used >= 1 && used <= 4);
        CHECK(w[v * 4] >= w[v * 4 + 1]);
    }
    // Along the cylinder: A's weight 1 at the bottom, 0 at the top, a half at the joint, never rising with height, and
    // with no jump between rings. Seam vertices weigh as their welded twins.
    double last = 2;
    for (int r = 0; r < t.rings; ++r) {
        double ring = 0;
        for (int s = 0; s < t.per_ring; ++s) ring += weight_of(j, w, size_t(r * t.per_ring + s), 7);
        ring /= t.per_ring;
        for (int s = 0; s < t.per_ring; ++s) CHECK_NEAR(weight_of(j, w, size_t(r * t.per_ring + s), 7), ring, 1e-3);
        CHECK(ring <= last + 1e-6);
        if (last <= 1) CHECK(last - ring < 0.2);
        last = ring;
        if (t.z[size_t(r)] < 0.6) CHECK(ring > 0.95);
        if (t.z[size_t(r)] > 1.4) CHECK(ring < 0.05);
        if (std::fabs(t.z[size_t(r)] - 1) < 1e-6) CHECK_NEAR(ring, 0.5, 0.05);
    }
    // Both of the middle ring's neighbours sit either side of a half.
    CHECK(weight_of(j, w, size_t((t.rings / 2 - 2) * t.per_ring), 7) > 0.5);
    CHECK(weight_of(j, w, size_t((t.rings / 2 + 2) * t.per_ring), 7) < 0.5);
}

TEST(bone_heat_sees_a_bone_past_other_pieces_but_not_its_own_far_side) {
    // An open tube round its bone: every vertex sees it.
    const Tube a = tube(0.1, 0, 1, 0.1, 12, false);
    std::vector<int> j;
    std::vector<float> w;
    BoneHeatStats st;
    CHECK(bone_heat(a.pos, a.idx, range(0, std::uint32_t(a.pos.size() / 3)), {{3, {0, 0, 0}, {0, 0, 1}}}, 99, {}, j, w, &st));
    CHECK_EQ(st.unreached, 0);
    CHECK_EQ(st.hidden, 0);
    // A closed box with the tube shut inside it and the bone outside: the box's far corners are hidden behind its near
    // wall but warmed through the box's surface; the tube is not hidden by the box (another piece: eyeballs in a head,
    // a strap under a vest), only its own far side is by its near side.
    std::vector<float> pos = {-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 2, 1, -1, 2, 1, 1, 2, -1, 1, 2};
    std::vector<std::uint32_t> idx = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                      1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6, 3, 0, 4, 3, 4, 7};
    const std::uint32_t off = std::uint32_t(pos.size() / 3);
    pos.insert(pos.end(), a.pos.begin(), a.pos.end());
    for (std::uint32_t i : a.idx) idx.push_back(i + off);
    CHECK(bone_heat(pos, idx, range(0, std::uint32_t(pos.size() / 3)), {{3, {3, 0, 0}, {3, 0, 1}}}, 99, {}, j, w, &st));
    CHECK(st.hidden > 0);
    CHECK_EQ(st.unreached, 0);
    CHECK_EQ(st.pieces, 2);
    for (size_t v = 0; v < pos.size() / 3; ++v) CHECK_NEAR(weight_of(j, w, v, 3), 1, 1e-5);
}

TEST(bone_heat_leaves_a_streamer_out_of_reach_on_the_bone_it_hangs_from) {
    // A flat ribbon (a scarf's streamer) hanging out sideways from the top of bone A (the neck), its far end over bone
    // B (a tail) half a metre below. Within reach, only A heats it; its far end must not take B.
    std::vector<float> pos;
    std::vector<std::uint32_t> idx;
    const int n = 40;
    for (int i = 0; i <= n; ++i)
        for (const float w : {-0.03f, 0.03f}) pos.insert(pos.end(), {-0.05f - 1.0f * float(i) / n, w, 1.0f});
    for (std::uint32_t i = 0; i < std::uint32_t(n); ++i) idx.insert(idx.end(), {2 * i, 2 * i + 2, 2 * i + 3, 2 * i, 2 * i + 3, 2 * i + 1});
    const std::vector<HeatBone> bones = {{7, {0, 0, 0.6}, {0, 0, 1.0}}, {9, {-0.6, 0, 0.5}, {-1.0, 0, 0.5}}};
    std::vector<int> j;
    std::vector<float> w;
    BoneHeatStats st;
    const std::uint32_t nv = std::uint32_t(pos.size() / 3);
    CHECK(bone_heat(pos, idx, range(0, nv), bones, 99, {}, j, w, &st));
    CHECK(weight_of(j, w, nv - 1, 9) > 0.5);  // no reach: the far end goes to the tail
    BoneHeatOptions o;
    o.reach = 0.25;
    CHECK(bone_heat(pos, idx, range(0, nv), bones, 99, o, j, w, &st));
    CHECK(st.far > 0);
    for (std::uint32_t v = 0; v < nv; ++v) CHECK_NEAR(weight_of(j, w, v, 7), 1, 1e-4);
}

TEST(weight_transfer_takes_the_nearest_surface_points_weights) {
    // A body tube weighted by heat, and a sleeve just outside it: the sleeve's weights follow the body's under it.
    Tube body = tube(0.15, 0, 2, 0.05, 24, false), sleeve = tube(0.17, 0.5, 1.5, 0.05, 24, false);
    std::vector<float> pos = body.pos;
    std::vector<std::uint32_t> idx = body.idx;
    const std::uint32_t off = std::uint32_t(pos.size() / 3);
    pos.insert(pos.end(), sleeve.pos.begin(), sleeve.pos.end());
    for (std::uint32_t i : sleeve.idx) idx.push_back(i + off);
    const DaePart b{"Body", 0, off, 0, std::uint32_t(body.idx.size())};
    const DaePart s{"Sleeve", off, std::uint32_t(sleeve.pos.size() / 3), std::uint32_t(body.idx.size()), std::uint32_t(sleeve.idx.size())};
    std::vector<int> j(pos.size() / 3 * 4, 99);
    std::vector<float> w(pos.size() / 3 * 4, 0.f);
    const std::vector<HeatBone> bones = {{7, {0, 0, 0}, {0, 0, 1}}, {9, {0, 0, 1}, {0, 0, 2}}};
    CHECK(bone_heat(pos, idx, range(b.first_vertex, b.vertex_count), bones, 99, {}, j, w));
    CHECK(transfer_weights(pos, idx, b, s, 99, j, w));
    for (std::uint32_t v = s.first_vertex; v < s.first_vertex + s.vertex_count; ++v) {
        // The body vertex at the same height and angle.
        const int ring = int(std::lround((pos[v * 3 + 2] - 0) / 0.05));
        const int side = int((v - s.first_vertex) % std::uint32_t(sleeve.per_ring));
        if (v >= s.first_vertex + std::uint32_t(sleeve.rings * sleeve.per_ring)) continue;  // its caps
        const size_t under = size_t(ring * body.per_ring + side);
        CHECK_NEAR(weight_of(j, w, v, 7), weight_of(j, w, under, 7), 0.02);
        double sum = 0;
        for (int k = 0; k < 4; ++k) sum += w[v * 4 + size_t(k)];
        CHECK_NEAR(sum, 1, 1e-5);
    }
}
