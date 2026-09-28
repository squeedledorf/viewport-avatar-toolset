#include "check.h"
#include "vats/fcurve.h"

using namespace vats;

namespace {

FCurve three(double a, double b, double c) {
    FCurve f;
    f.set_key(0, a);
    f.set_key(10, b);
    f.set_key(20, c);
    return f;
}

}  // namespace

TEST(fcurve_evaluate_basics) {
    FCurve f;
    CHECK_NEAR(f.evaluate(3), 0, 0);
    f.set_key(10, 5);
    CHECK_NEAR(f.evaluate(0), 5, 0);  // holds before the first key
    f.set_key(20, 15);
    CHECK_NEAR(f.evaluate(30), 15, 0);  // and after the last
    f.keys[0].interp = Interp::Linear;
    CHECK_NEAR(f.evaluate(15), 10, 1e-12);
    f.keys[0].interp = Interp::Constant;
    CHECK_NEAR(f.evaluate(19.9), 5, 0);
}

TEST(fcurve_auto_clamped_flattens_extremum) {
    FCurve f = three(0, 10, 0);  // key 1 is a peak
    const Key& k = f.keys[1];
    CHECK_NEAR(k.ly, 10, 1e-12);
    CHECK_NEAR(k.ry, 10, 1e-12);
    CHECK_NEAR(k.rx, 10 + 10.0 / 3, 1e-12);
    // Ends with one neighbour are flat for AutoClamped.
    CHECK_NEAR(f.keys[0].ry, 0, 1e-12);
    // A peak never overshoots.
    for (double t = 0; t <= 20; t += 0.25) CHECK(f.evaluate(t) <= 10 + 1e-9);
}

TEST(fcurve_auto_uses_neighbour_slope) {
    FCurve f = three(0, 10, 20);  // monotone
    for (auto& k : f.keys) k.left = k.right = Handle::Auto;
    f.recompute_handles();
    CHECK_NEAR(f.keys[1].ry - f.keys[1].value, 1.0 * 10 / 3, 1e-12);  // slope 1 per frame
    CHECK_NEAR(f.keys[0].ry, 10.0 / 3, 1e-12);                          // end key: slope to its neighbour
    CHECK_NEAR(f.evaluate(5), 5, 1e-4);                                  // a straight line stays straight
}

TEST(fcurve_vector_flat_plateau) {
    FCurve f = three(0, 10, 4);
    f.apply_tangent(1, Tangent::Linear);
    CHECK_NEAR(f.keys[1].lx, 10 - 10.0 / 3, 1e-12);
    CHECK_NEAR(f.keys[1].ly, 10 - 10.0 / 3, 1e-12);
    CHECK_NEAR(f.keys[1].ry, 10 - 6.0 / 3, 1e-12);
    f.apply_tangent(1, Tangent::Flat);
    CHECK_NEAR(f.keys[1].ly, 10, 0);
    CHECK_NEAR(f.keys[1].ry, 10, 0);

    FCurve p = three(0, 9, 10);
    for (auto& k : p.keys) k.left = k.right = Handle::Plateau;
    p.recompute_handles();
    CHECK(p.keys[1].ry <= 10 + 1e-12);  // clamped to the next key's value
    CHECK(p.keys[1].ly >= 0 - 1e-12);
}

TEST(fcurve_set_key_moves_handles_and_keeps_interp) {
    FCurve f;
    f.set_key(0, 0, Interp::Linear);
    f.set_key(10, 10);
    CHECK(f.keys[1].interp == Interp::Linear);  // copied from the key before
    int i = f.set_key(10, 20);
    CHECK_EQ(i, 1);
    CHECK_EQ(f.keys.size(), size_t(2));
    CHECK_NEAR(f.keys[1].value, 20, 0);
    CHECK(f.remove_key(0));
    CHECK(!f.remove_key(3));
}

TEST(fcurve_stepped_and_unify) {
    FCurve f = three(0, 10, 0);
    f.apply_tangent(0, Tangent::Stepped);
    CHECK_NEAR(f.evaluate(9.99), 0, 0);
    f.apply_tangent(1, Tangent::Break);
    f.keys[1].rx = 12;
    f.keys[1].ry = 14;
    f.apply_tangent(1, Tangent::Unify);
    const Key& k = f.keys[1];
    double cross = (k.rx - k.frame) * (k.ly - k.value) - (k.ry - k.value) * (k.lx - k.frame);
    CHECK_NEAR(cross, 0, 1e-9);  // collinear
    CHECK(k.left == Handle::Aligned);
}

TEST(fcurve_set_key_returns_index_after_growth) {
    FCurve f;
    for (int i = 0; i < 100; ++i) CHECK_EQ(f.set_key(i * 2.0, i), i);  // appends force reallocations
    CHECK_EQ(f.set_key(1.0, 5), 1);
}
