// Weight painting (spec 08 RG-15): add, subtract and smooth keep every vertex summing to 1 with at most four
// influences, a mirrored dab mirrors, and a stroke is one undo step.
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/bone_heat.h"
#include "vats/weight_paint.h"

using namespace vats;

namespace {

// A capped tube along +Z (rings of 16), centred at (0, y, 0), its vertices weighted by bone heat to joints a (below
// z = 0.5) and b (above).
void add_tube(DaeModel& m, double y, int a, int b, int root) {
    const std::uint32_t base = std::uint32_t(m.positions.size() / 3);
    const int rings = 21, sides = 16;
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < sides; ++s) {
            const double t = 2 * kPi * s / sides;
            m.positions.insert(m.positions.end(), {float(0.08 * std::cos(t)), float(y + 0.08 * std::sin(t)), float(r * 0.05)});
        }
    const std::uint32_t lo = std::uint32_t(m.positions.size() / 3);
    m.positions.insert(m.positions.end(), {0.f, float(y), 0.f, 0.f, float(y), 1.f});
    for (int r = 0; r + 1 < rings; ++r)
        for (int s = 0; s < sides; ++s) {
            const std::uint32_t p = base + std::uint32_t(r * sides + s), q = base + std::uint32_t(r * sides + (s + 1) % sides);
            m.indices.insert(m.indices.end(), {p, q, q + sides, p, q + sides, p + sides});
        }
    for (int s = 0; s < sides; ++s) {
        const std::uint32_t p = base + std::uint32_t(s), q = base + std::uint32_t((s + 1) % sides);
        m.indices.insert(m.indices.end(), {lo, q, p, lo + 1, p + std::uint32_t((rings - 1) * sides), q + std::uint32_t((rings - 1) * sides)});
    }
    std::vector<std::uint32_t> piece;
    for (std::uint32_t v = base; v < m.positions.size() / 3; ++v) piece.push_back(v);
    bone_heat(m.positions, m.indices, piece, {{a, {0, y, 0}, {0, y, 0.5}}, {b, {0, y, 0.5}, {0, y, 1}}}, root, {}, m.joints, m.weights);
}

DaeModel tubes() {
    const Skeleton& s = skel();
    DaeModel m;
    add_tube(m, 0.4, s.find("mShoulderLeft"), s.find("mElbowLeft"), dae_root(s));
    add_tube(m, -0.4, s.find("mShoulderRight"), s.find("mElbowRight"), dae_root(s));
    m.rigged = true;
    m.bounds_min = {-0.1, -0.5, 0}, m.bounds_max = {0.1, 0.5, 1};
    return m;
}

double weight_on(const DaeModel& m, int v, int node) {
    double w = 0;
    for (int k = 0; k < 4; ++k)
        if (m.joints[size_t(v) * 4 + size_t(k)] == node) w += m.weights[size_t(v) * 4 + size_t(k)];
    return w;
}

void check_sums(const DaeModel& m) {
    for (int v = 0; v < m.vertex_count(); ++v) {
        double sum = 0;
        int used = 0;
        for (int k = 0; k < 4; ++k) sum += m.weights[size_t(v) * 4 + size_t(k)], used += m.weights[size_t(v) * 4 + size_t(k)] > 0;
        CHECK_NEAR(sum, 1, 1e-5);
        CHECK(used >= 1 && used <= 4);
    }
}

int vertex_at(const DaeModel& m, const Vec3& p) {
    int best = 0;
    double d = 1e9;
    for (int v = 0; v < m.vertex_count(); ++v) {
        const double e = (Vec3{m.positions[size_t(v) * 3], m.positions[size_t(v) * 3 + 1], m.positions[size_t(v) * 3 + 2]} - p).length();
        if (e < d) d = e, best = v;
    }
    return best;
}

}  // namespace

TEST(weight_paint_add_subtract_and_smooth_keep_weights_whole) {
    const Skeleton& s = skel();
    const int shoulder = s.find("mShoulderLeft"), elbow = s.find("mElbowLeft");
    DaeModel m = tubes();
    const PaintMesh pm = paint_mesh(m);
    check_sums(m);
    const int v = vertex_at(m, {0.08, 0.4, 0.8});  // well up the elbow's half
    const double before = weight_on(m, v, shoulder);
    PaintBrush add{PaintOp::Add, 0.1, 0.5, PaintFalloff::Smooth};
    CHECK(paint_dab(s, m, pm, shoulder, {0.08, 0.4, 0.8}, add) > 0);
    CHECK(weight_on(m, v, shoulder) > before + 0.3);
    check_sums(m);
    // Subtracting takes it away again; the elbow takes up the rest.
    PaintBrush sub{PaintOp::Subtract, 0.1, 1.0, PaintFalloff::Constant};
    CHECK(paint_dab(s, m, pm, shoulder, {0.08, 0.4, 0.8}, sub) > 0);
    CHECK_NEAR(weight_on(m, v, shoulder), 0, 1e-6);
    CHECK_NEAR(weight_on(m, v, elbow), 1, 1e-5);
    check_sums(m);
    // A vertex on the elbow alone, its elbow weight taken: it goes to the joint around it (the shoulder below).
    const int low = vertex_at(m, {0.08, 0.4, 0.55});
    PaintBrush all{PaintOp::Subtract, 0.02, 1.0, PaintFalloff::Constant};
    for (int i = 0; i < 3; ++i) paint_dab(s, m, pm, elbow, {0.08, 0.4, 0.55}, all);
    CHECK_NEAR(weight_on(m, low, elbow), 0, 1e-6);
    check_sums(m);
    // Smoothing a sharp step evens it out.
    PaintBrush hard{PaintOp::Add, 0.06, 1.0, PaintFalloff::Constant};
    paint_dab(s, m, pm, shoulder, {0.08, 0.4, 0.9}, hard);
    const int ring = vertex_at(m, {0.08, 0.4, 0.9}), above = vertex_at(m, {0.08, 0.4, 1.0});
    const double step = weight_on(m, ring, shoulder) - weight_on(m, above, shoulder);
    PaintBrush smooth{PaintOp::Smooth, 0.15, 1.0, PaintFalloff::Constant};
    for (int i = 0; i < 4; ++i) paint_dab(s, m, pm, shoulder, {0.08, 0.4, 0.95}, smooth);
    CHECK(weight_on(m, ring, shoulder) - weight_on(m, above, shoulder) < step * 0.6);
    check_sums(m);
    // The brush keeps to the surface it is on: a dab wide enough to reach the other tube leaves it alone, unless asked.
    const int other = vertex_at(m, {0.08, -0.4, 0.5});
    const double other_was = weight_on(m, other, shoulder);
    PaintBrush wide{PaintOp::Add, 1.0, 1.0, PaintFalloff::Constant};
    paint_dab(s, m, pm, shoulder, {0.08, 0.4, 0.5}, wide);
    CHECK_NEAR(weight_on(m, other, shoulder), other_was, 1e-9);
    wide.connected = false;
    paint_dab(s, m, pm, shoulder, {0.08, 0.4, 0.5}, wide);
    CHECK_NEAR(weight_on(m, other, shoulder), 1, 1e-6);
    check_sums(m);
    // Never more than four influences: five joints painted onto one spot.
    for (const char* j : {"mWristLeft", "mHandIndex1Left", "mHandMiddle1Left", "mHandRing1Left", "mHandPinky1Left"})
        paint_dab(s, m, pm, s.find(j), {0.08, 0.4, 0.3}, PaintBrush{PaintOp::Add, 0.05, 0.3, PaintFalloff::Linear});
    check_sums(m);
}

TEST(weight_paint_mirrors_and_undoes_a_stroke_at_once) {
    const Skeleton& s = skel();
    const int shoulder = s.find("mShoulderLeft");
    DaeModel m = tubes();
    const PaintMesh pm = paint_mesh(m);
    const DaeModel start = m;
    // One stroke: several dabs, each mirrored onto the right tube with the right shoulder.
    std::vector<int> jb;
    const std::vector<float> wb = begin_stroke(m, jb);
    for (int i = 0; i < 5; ++i) {
        const Vec3 at{0.08, 0.4, 0.6 + 0.04 * i};
        PaintBrush b{PaintOp::Add, 0.12, 0.4, PaintFalloff::Smooth};
        paint_dab(s, m, pm, shoulder, at, b);
        paint_dab(s, m, pm, mirror_joint(s, shoulder), mirror_point(at), b);
    }
    CHECK_EQ(mirror_joint(s, shoulder), s.find("mShoulderRight"));
    CHECK_EQ(mirror_joint(s, dae_volume(s, s.find_volume("L_UPPER_ARM"))), dae_volume(s, s.find_volume("R_UPPER_ARM")));
    // Mirror images weigh alike on the mirrored joints.
    for (const Vec3& p : {Vec3{0.08, 0.4, 0.7}, Vec3{0.0566, 0.4566, 0.7}, Vec3{0.08, 0.4, 0.75}}) {
        const int l = vertex_at(m, p), r = vertex_at(m, mirror_point(p));
        CHECK(weight_on(m, l, shoulder) > weight_on(start, l, shoulder) + 0.1);
        CHECK_NEAR(weight_on(m, l, shoulder), weight_on(m, r, s.find("mShoulderRight")), 1e-4);
    }
    // The stroke is one undo step: undone, every weight is back; redone, as it was painted.
    const PaintStroke stroke = end_stroke(m, jb, wb);
    CHECK(!stroke.empty());
    const DaeModel painted = m;
    undo_stroke(stroke, m);
    CHECK(m.joints == start.joints && m.weights == start.weights);
    undo_stroke(stroke, m, true);
    CHECK(m.joints == painted.joints && m.weights == painted.weights);
}

// A click on the body with no bone picked picks the joint that carries the vertex most; SK-40 indices map back to
// nodes, a collision volume's to its node and mRoot to none.
TEST(paint_pick_finds_the_dominant_joint_and_its_node) {
    DaeModel m;
    const int chest = skel().find("mChest"), neck = skel().find("mNeck");
    m.joints = {chest, neck, dae_root(skel()), dae_root(skel())};
    m.weights = {0.3f, 0.7f, 0, 0};
    CHECK_EQ(dominant_joint(m, 0), neck);
    CHECK_EQ(dominant_joint(m, 1), -1);
    CHECK_EQ(node_of_sk40(skel(), neck), neck);
    CHECK_EQ(node_of_sk40(skel(), dae_root(skel())), -1);
    const auto& v0 = skel().volumes().front();
    CHECK_EQ(node_of_sk40(skel(), dae_volume(skel(), 0)), v0.node);
    CHECK_EQ(sk40_of_node(skel(), v0.node), dae_volume(skel(), 0));
}
