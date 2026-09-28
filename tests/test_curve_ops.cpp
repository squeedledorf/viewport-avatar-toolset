#include <algorithm>
#include <cmath>

#include "check.h"
#include "vats/curve_ops.h"

using namespace vats;

namespace {

FCurve curve(std::initializer_list<std::pair<double, double>> keys) {
    FCurve c;
    for (auto& [f, v] : keys) c.set_key(f, v);
    return c;
}

bool near_curves(const FCurve& a, const FCurve& b, double tol) {
    if (a.keys.size() != b.keys.size()) return false;
    for (size_t i = 0; i < a.keys.size(); ++i) {
        const Key &p = a.keys[i], &q = b.keys[i];
        if (p.interp != q.interp || p.left != q.left || p.right != q.right) return false;
        for (auto [x, y] : {std::pair{p.frame, q.frame}, {p.value, q.value}, {p.lx, q.lx}, {p.ly, q.ly}, {p.rx, q.rx},
                            {p.ry, q.ry}})
            if (std::fabs(x - y) > tol) return false;
    }
    return true;
}

// FCurve::evaluate solves time to 1e-5 frames; this solves it to 1e-12 so the shape test measures the split alone.
double exact(const FCurve& c, double f) {
    auto& ks = c.keys;
    if (f <= ks.front().frame) return ks.front().value;
    if (f >= ks.back().frame) return ks.back().value;
    size_t i = 1;
    while (ks[i].frame <= f) ++i;
    const Key &A = ks[i - 1], &B = ks[i];
    if (A.interp != Interp::Bezier) return c.evaluate(f);
    auto bez = [](double p0, double p1, double p2, double p3, double t) {
        double u = 1 - t;
        return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
    };
    double x1 = std::clamp(A.rx, A.frame, B.frame), x2 = std::clamp(B.lx, A.frame, B.frame), lo = 0, hi = 1;
    for (int n = 0; n < 100; ++n) ((bez(A.frame, x1, x2, B.frame, (lo + hi) / 2) < f) ? lo : hi) = (lo + hi) / 2;
    return bez(A.value, A.ry, B.ly, B.value, (lo + hi) / 2);
}

double cross(const Key& k) { return (k.rx - k.frame) * (k.ly - k.value) - (k.ry - k.value) * (k.lx - k.frame); }

}  // namespace

TEST(curve_ops_insert_on_curve_keeps_shape) {
    FCurve c = curve({{0, 0}, {10, 0.4}, {20, -0.15}, {30, 0.1}});
    c.keys[2].left = c.keys[2].right = Handle::Free;  // a broken key with its own handles
    c.keys[2].lx = 17;
    c.keys[2].ly = -0.3;
    c.keys[2].rx = 24;
    c.keys[2].ry = -0.1;
    c.keys[1].left = c.keys[1].right = Handle::Vector;
    c.recompute_handles();
    const FCurve before = c;

    CHECK_EQ(insert_on_curve(c, 4.3), 1);
    CHECK_EQ(insert_on_curve(c, 15), 3);
    CHECK_EQ(insert_on_curve(c, 27.5), 5);
    CHECK_EQ(insert_on_curve(c, 15), 3);  // an existing key is returned unchanged
    CHECK_EQ(c.keys.size(), size_t(7));
    CHECK(c.keys[1].left == Handle::Aligned && c.keys[1].right == Handle::Aligned);
    for (int i = 0; i <= 100; ++i) CHECK_NEAR(exact(c, i * 0.3), exact(before, i * 0.3), 1e-6);
    c.recompute_handles();  // the neighbours are frozen, so a later recompute keeps the shape
    for (int i = 0; i <= 100; ++i) CHECK_NEAR(exact(c, i * 0.3), exact(before, i * 0.3), 1e-6);

    FCurve s = curve({{0, 1}, {10, 5}});
    s.keys[0].interp = Interp::Constant;
    int k = insert_on_curve(s, 4);  // not Bezier: a plain key with the evaluated value
    CHECK_NEAR(s.keys[k].value, 1, 0);
    CHECK(s.keys[k].interp == Interp::Constant);
    k = insert_on_curve(s, 12);  // outside the keyed range
    CHECK_NEAR(s.keys[k].value, 5, 0);
}

TEST(curve_ops_reverse_twice_restores) {
    Clip clip;
    clip.end_frame = 30;
    clip.loop_in = 2;
    clip.loop_out = 25;
    FCurve& a = clip.curves["mElbowLeft"]["rot_x"];
    a = curve({{0, 0}, {7, 40}, {15, -10}, {22, 5}, {30, 12}});
    a.keys[1].left = a.keys[1].right = Handle::Free;
    a.keys[1].lx = 5.5;
    a.keys[1].ly = 30;
    a.keys[1].rx = 9.25;
    a.keys[1].ry = 44;
    a.keys[2].left = a.keys[2].right = Handle::Vector;
    a.keys[3].interp = Interp::Linear;
    a.keys[0].left = a.keys[0].right = Handle::Auto;
    a.recompute_handles();
    // A stepped run held to the end (like a blend curve).
    FCurve& b = clip.curves["ik.ArmLeft"]["blend"];
    b = curve({{0, 0}, {10, 1}, {20, 3}, {30, 3}});
    for (int i = 0; i < 3; ++i) b.keys[i].interp = Interp::Constant;
    clip.pins.push_back({"Left Hand", "Left Hand", "", 5, 12, {}, {}, 5, 13});
    clip.pins.push_back({"Right Hand", "Right Hand", "", 20, -1, {}, {}, 20, -1});
    const Clip orig = clip;

    reverse_clip(clip);
    const FCurve& ra = clip.curves["mElbowLeft"]["rot_x"];
    const FCurve& oa = orig.curves.at("mElbowLeft").at("rot_x");
    for (int t = 0; t <= 30; ++t) CHECK_NEAR(ra.evaluate(t), oa.evaluate(30 - t), 2e-4);
    CHECK_EQ(clip.loop_in, 5);
    CHECK_EQ(clip.loop_out, 28);
    CHECK_EQ(clip.pins[0].from, 18);
    CHECK_EQ(clip.pins[0].to, 25);
    CHECK_EQ(clip.pins[0].start_key, 25);
    CHECK_EQ(clip.pins[0].release_key, 17);
    CHECK_EQ(clip.pins[1].from, 0);
    CHECK_EQ(clip.pins[1].to, 10);
    CHECK_EQ(clip.pins[1].start_key, 10);

    reverse_clip(clip);
    for (auto& [track, channels] : orig.curves)
        for (auto& [ch, c] : channels) CHECK(near_curves(clip.curves[track][ch], c, 1e-9));
    CHECK(clip.pins == orig.pins);
    CHECK_EQ(clip.loop_in, 2);
    CHECK_EQ(clip.loop_out, 25);
}

TEST(curve_ops_reverse_steps_lossless) {
    // Stepped runs whose held value differs from the key that ends them (05 pitfall 8, section 5 item 8).
    Clip clip;
    clip.end_frame = 30;
    FCurve& c = clip.curves["mNeck"]["rot_x"];
    c = curve({{0, 0}, {5, 3}, {12, -2}, {20, 4}, {26, 9}, {29, 1}});
    for (int i : {0, 1, 3}) c.keys[i].interp = Interp::Constant;
    c.keys[4].interp = Interp::Linear;
    c.keys[2].left = c.keys[2].right = Handle::Free;
    c.keys[2].lx = 10;
    c.keys[2].ly = 1;
    c.keys[2].rx = 13.5;
    c.keys[2].ry = -7;
    c.recompute_handles();
    const FCurve orig = c;

    reverse_clip(clip);
    // Whole frames (what export samples) and the held runs between keys all read the mirrored time exactly.
    for (double t = 0; t <= 30; t += 0.25) {
        bool stepped = orig.evaluate(30 - t) == orig.evaluate(30 - t + 0.01);
        CHECK_NEAR(c.evaluate(t), orig.evaluate(30 - t), stepped ? 0 : 2e-4);
    }
    for (auto& k : c.keys) CHECK(k.frame >= 0 && k.frame <= 30);

    reverse_clip(clip);
    CHECK(near_curves(c, orig, 1e-12));

    // The same through a project-style round trip of the reversed keys: they are ordinary keys.
    reverse_clip(clip);
    FCurve copy;
    copy.keys = c.keys;
    copy.recompute_handles();
    CHECK(copy == c);
}

TEST(curve_ops_reverse_keys_beyond_end) {
    Clip clip;
    clip.end_frame = 30;
    FCurve& c = clip.curves["mPelvis"]["rot_y"];
    c = curve({{0, 0}, {20, 1}, {40, 0}, {50, 2}});
    const FCurve old = c;
    reverse_clip(clip);
    const FCurve& r = clip.curves["mPelvis"]["rot_y"];
    for (auto& k : r.keys) CHECK(k.frame >= 0 && k.frame <= 30);
    for (int t = 0; t <= 30; ++t) CHECK_NEAR(r.evaluate(t), old.evaluate(30 - t), 1e-5);

    // A step whose held value shows on the far side is kept, and frame 0 is fixed up (AM-115).
    Clip s;
    s.end_frame = 30;
    FCurve& st = s.curves["ik.ArmLeft"]["blend"];
    st = curve({{0, 0}, {10, 1}});
    st.keys[0].interp = st.keys[1].interp = Interp::Constant;
    reverse_clip(s);
    const FCurve& rs = s.curves["ik.ArmLeft"]["blend"];
    CHECK_NEAR(rs.evaluate(0), 1, 0);
    CHECK_NEAR(rs.evaluate(19), 1, 0);
    CHECK_NEAR(rs.evaluate(21), 0, 0);
}

TEST(curve_ops_move_from_press_does_not_drift) {
    Clip clip;
    clip.curves["mNeck"]["rot_x"] = curve({{0, 0}, {10, 5}, {20, -5}, {30, 2}});
    const Clip press = clip;
    std::vector<KeyRef> sel = {{"mNeck", "rot_x", 1}, {"mNeck", "rot_x", 2}};
    move_keys(clip, press, sel, 3.4, 1, true);
    const FCurve& c = clip.curves["mNeck"]["rot_x"];
    CHECK_NEAR(c.keys[1].frame, 13, 0);
    CHECK_NEAR(c.keys[1].value, 6, 0);
    for (int i = 0; i < 20; ++i) move_keys(clip, press, sel, 7.2 + i * 1e-3, 0, true);
    CHECK_NEAR(c.keys[1].frame, 17, 0);
    CHECK_NEAR(c.keys[1].value, 5, 0);  // from the press state: no drift
    CHECK_NEAR(c.keys[2].frame, 27, 0);

    move_keys(clip, press, sel, -15, 0, true);  // clamped for the whole selection: spacing kept
    CHECK_NEAR(c.keys[0].frame, 0, 0);
    CHECK_NEAR(c.keys[1].frame, 0, 0);
    CHECK_NEAR(c.keys[2].frame, 10, 0);
    CHECK_EQ(c.keys.size(), size_t(4));

    move_keys(clip, press, sel, 10, 0, true);  // 20 lands on the unselected key at 30
    CHECK_EQ(c.keys.size(), size_t(4));
    finish_transform(clip, sel);
    CHECK_EQ(c.keys.size(), size_t(3));
    CHECK_NEAR(c.keys[2].frame, 30, 0);
    CHECK_NEAR(c.keys[2].value, -5, 0);  // the moved key wins
    CHECK_EQ(sel.size(), size_t(2));
    CHECK_EQ(sel[0].index, 1);
    CHECK_EQ(sel[1].index, 2);
}

TEST(curve_ops_scale_and_negative_scale) {
    Clip clip;
    clip.curves["mNeck"]["rot_x"] = curve({{0, 0}, {10, 1}, {20, 7}, {30, 0}});
    FCurve& c = clip.curves["mNeck"]["rot_x"];
    c.keys[1].interp = Interp::Constant;
    c.keys[1].left = Handle::Free;
    c.keys[1].lx = 8;
    c.keys[1].ly = 3;
    const Clip press = clip;
    std::vector<KeyRef> sel = {{"mNeck", "rot_x", 1}, {"mNeck", "rot_x", 2}};
    scale_keys(clip, press, sel, 10, 0, 1.5, 1, true);
    scale_keys(clip, press, sel, 10, 0, 2, 1, true);
    CHECK_NEAR(c.keys[1].frame, 10, 0);
    CHECK_NEAR(c.keys[2].frame, 30, 0);

    scale_keys(clip, press, sel, 15, 0, -1, 1, true);  // 02 section 3.5
    CHECK_EQ(c.keys.size(), size_t(5));
    CHECK_NEAR(c.keys[1].frame, 10, 0);
    CHECK_NEAR(c.keys[3].frame, 20, 0);
    const Key &k20 = c.keys[1], &held = c.keys[2], &k10 = c.keys[3];  // old key 20 now at 10, old key 10 at 20
    CHECK(k20.interp == Interp::Constant);  // takes its predecessor's interp, keeps its own value at its frame...
    CHECK_NEAR(k20.value, 7, 0);
    CHECK(held.frame > 10 && held.frame < 10.01);  // ...and a sliver key right after holds the step's value
    CHECK_NEAR(held.value, 1, 0);
    CHECK(k10.interp == Interp::Bezier);  // the first takes the last's
    CHECK(k10.right == Handle::Free);     // handles swap sides
    CHECK_NEAR(k10.rx, 22, 0);
    CHECK_NEAR(k10.ry, 3, 0);
    CHECK_EQ(sel.size(), size_t(3));
    for (int t = 10; t <= 20; ++t) CHECK_NEAR(c.evaluate(t), press.curves.at("mNeck").at("rot_x").evaluate(30 - t), 0);

    // Negative scale twice is the identity (05 section 5 item 8).
    const Clip once = clip;
    scale_keys(clip, once, sel, 15, 0, -1, 1, true);
    finish_transform(clip, sel);
    CHECK(near_curves(c, press.curves.at("mNeck").at("rot_x"), 1e-12));
}

TEST(curve_ops_flip_time_and_values) {
    Clip clip;
    clip.curves["mNeck"]["rot_x"] = curve({{0, 0}, {10, 4}, {14, 6}, {30, 0}});
    std::vector<KeyRef> sel = {{"mNeck", "rot_x", 1}, {"mNeck", "rot_x", 2}};
    flip_time(clip, sel, true);
    const FCurve& c = clip.curves["mNeck"]["rot_x"];
    CHECK_NEAR(c.keys[1].frame, 10, 0);
    CHECK_NEAR(c.keys[1].value, 6, 0);
    CHECK_NEAR(c.keys[2].value, 4, 0);
    CHECK_EQ(sel.size(), size_t(2));

    // Pitfall 05-8, fixed: the step 10..14 holds 6 and ends on 4. Flipped, frame 10 still reads 4 and the run
    // after it holds 6, and flipping again gives back the original keys.
    clip.curves["mNeck"]["rot_x"].keys[1].interp = Interp::Constant;
    const FCurve stepped = c;
    flip_time(clip, sel, true);
    CHECK_EQ(c.keys.size(), size_t(5));
    CHECK_EQ(sel.size(), size_t(3));
    for (double t = 10; t <= 14; t += 0.25) CHECK_NEAR(c.evaluate(t), stepped.evaluate(24 - t), 0);
    CHECK_NEAR(c.keys[1].value, 4, 0);
    CHECK_NEAR(c.keys[2].value, 6, 0);
    flip_time(clip, sel, true);
    CHECK(near_curves(c, stepped, 0));
    CHECK_EQ(sel.size(), size_t(2));

    flip_values(clip, sel);
    CHECK_NEAR(c.keys[1].value, -6, 0);
    CHECK_NEAR(c.keys[0].value, 0, 0);
}

TEST(curve_ops_drag_handle) {
    FCurve c = curve({{0, 0}, {10, 10}, {20, 0}});
    drag_handle(c, 1, true, 5, 12);  // a right handle cannot pass its key to the left
    CHECK_NEAR(c.keys[1].rx, 10, 0);
    double left_len = std::hypot(c.keys[1].lx - 10, c.keys[1].ly - 10);
    drag_handle(c, 1, true, 13, 12);
    const Key& k = c.keys[1];
    CHECK(k.left == Handle::Aligned && k.right == Handle::Aligned);
    CHECK_NEAR(cross(k), 0, 1e-9);
    CHECK(k.lx < k.frame);
    CHECK_NEAR(std::hypot(k.lx - 10, k.ly - 10), left_len, 1e-9);

    c.apply_tangent(1, Tangent::Break);
    drag_handle(c, 1, false, 12, 3);
    CHECK_NEAR(c.keys[1].lx, 10, 0);  // clamped
    CHECK_NEAR(c.keys[1].ly, 3, 0);
    drag_handle(c, 1, true, 15, 20);
    CHECK_NEAR(c.keys[1].lx, 10, 0);  // broken: the other side stays
    CHECK(c.keys[1].left == Handle::Free);
}

TEST(curve_ops_tangent_commands) {
    Clip clip;
    clip.curves["mNeck"]["rot_x"] = curve({{0, 0}, {10, 10}, {20, 4}});
    FCurve& c = clip.curves["mNeck"]["rot_x"];
    std::vector<KeyRef> sel = {{"mNeck", "rot_x", 1}};
    apply_tangent(clip, sel, Tangent::Linear);
    CHECK(c.keys[1].interp == Interp::Bezier && c.keys[1].left == Handle::Vector && c.keys[1].right == Handle::Vector);
    CHECK_NEAR(c.keys[1].ly, 10 - 10.0 / 3, 1e-12);
    apply_tangent(clip, sel, Tangent::Stepped);
    CHECK(c.keys[1].interp == Interp::Constant && c.keys[1].left == Handle::Vector);
    apply_tangent(clip, sel, Tangent::Spline);
    CHECK(c.keys[1].interp == Interp::Bezier && c.keys[1].left == Handle::Auto);
    apply_tangent(clip, sel, Tangent::Plateau);
    CHECK(c.keys[1].right == Handle::Plateau);
    apply_tangent(clip, sel, Tangent::Flat);
    CHECK_NEAR(c.keys[1].ry, 10, 0);
    apply_tangent(clip, sel, Tangent::Break);
    CHECK(c.keys[1].left == Handle::Free);
    CHECK_NEAR(c.keys[1].ry, 10, 0);  // points unchanged
    c.keys[1].ry = 13;
    apply_tangent(clip, sel, Tangent::Unify);
    CHECK(c.keys[1].left == Handle::Aligned);
    CHECK_NEAR(cross(c.keys[1]), 0, 1e-9);
    apply_tangent(clip, sel, Tangent::Auto);
    CHECK(c.keys[1].left == Handle::AutoClamped);

    delete_keys(clip, {{"mNeck", "rot_x", 0}, {"mNeck", "rot_x", 1}, {"mNeck", "rot_x", 2}});
    CHECK(clip.curves.empty());
}

TEST(curve_ops_euler_filter) {
    Clip clip;
    clip.curves["mNeck"]["rot_z"] = curve({{0, 170}, {10, -170}, {20, -150}});
    clip.curves["mNeck"]["rot_x"] = curve({{5, 10}});
    clip.curves["mHead"]["rot_z"] = curve({{0, 10}, {10, 20}});
    CHECK_EQ(euler_filter(clip, {"mNeck", "mHead"}), 1);
    const FCurve& z = clip.curves["mNeck"]["rot_z"];
    CHECK_EQ(z.keys.size(), size_t(3));  // no keys added
    CHECK_NEAR(z.keys[1].value, 190, 1e-12);
    CHECK_NEAR(z.keys[2].value, 210, 1e-12);
    CHECK_EQ(clip.curves["mNeck"]["rot_x"].keys.size(), size_t(1));
    CHECK_EQ(euler_filter(clip, {"mNeck", "mHead"}), 0);  // already clean
}

TEST(curve_ops_euler_filter_flip) {
    // (x+180, 180-y, z+180) is the same rotation: a converter that switched branches at frame 20.
    Clip clip;
    clip.curves["mHead"]["rot_x"] = curve({{0, 10}, {10, 15}, {20, 200}, {30, 205}});
    clip.curves["mHead"]["rot_y"] = curve({{0, 80}, {10, 85}, {20, 92}, {30, 94}});
    clip.curves["mHead"]["rot_z"] = curve({{0, 20}, {10, 25}, {20, 210}, {30, 215}});
    Key& y20 = clip.curves["mHead"]["rot_y"].keys[2];
    y20.left = y20.right = Handle::Free;
    y20.lx = 18, y20.ly = 95, y20.rx = 22, y20.ry = 90;
    const Clip before = clip;
    CHECK_EQ(euler_filter(clip, {"mHead"}), 1);
    auto& t = clip.curves["mHead"];
    for (const char* ch : kRotChannels) CHECK_EQ(t[ch].keys.size(), size_t(4));  // no keys added
    const double want[4][3] = {{10, 80, 20}, {15, 85, 25}, {20, 88, 30}, {25, 86, 35}};
    for (int i = 0; i < 4; ++i) {
        Vec3 now{t["rot_x"].keys[i].value, t["rot_y"].keys[i].value, t["rot_z"].keys[i].value};
        for (int a = 0; a < 3; ++a) CHECK_NEAR(now[a], want[i][a], 1e-12);
        const auto& b = before.curves.at("mHead");
        Vec3 was{b.at("rot_x").keys[i].value, b.at("rot_y").keys[i].value, b.at("rot_z").keys[i].value};
        CHECK_NEAR(std::fabs(euler_to_quat(now).dot(euler_to_quat(was))), 1, 1e-12);  // same rotations
    }
    // The flipped y key's free handles are mirrored with it: y -> 180 - y.
    const Key& k = t["rot_y"].keys[2];
    CHECK_NEAR(k.ly, 85, 1e-12);
    CHECK_NEAR(k.ry, 90, 1e-12);
    CHECK_EQ(euler_filter(clip, {"mHead"}), 0);  // already clean
}

TEST(curve_ops_retime_clip) {
    Clip clip;
    clip.fps = 30;
    clip.end_frame = 30;
    clip.loop_in = 5;
    clip.loop_out = 25;
    clip.curves["mNeck"]["rot_x"] = curve({{0, 0}, {7, 20}, {13, -5}, {30, 10}});
    clip.curves["mNeck"]["pos_z"] = curve({{1, 0.1}, {29, 0.2}});
    Pin pin;
    pin.joint = pin.via = "mWristLeft";
    pin.from = 10;
    pin.to = 20;
    pin.start_key = 10;
    clip.pins.push_back(pin);
    const Clip orig = clip;

    retime_clip(clip, 24);
    CHECK_EQ(clip.fps, 24);
    CHECK_EQ(clip.end_frame, 24);
    CHECK_EQ(clip.loop_in, 4);
    CHECK_EQ(clip.loop_out, 20);
    CHECK_EQ(clip.pins[0].from, 8);
    CHECK_EQ(clip.pins[0].to, 16);
    CHECK_EQ(clip.pins[0].start_key, 8);
    CHECK_EQ(clip.pins[0].release_key, -1);
    CHECK_NEAR(clip.curves["mNeck"]["rot_x"].keys[1].frame, 5.6, 1e-12);
    // Timing is kept: the same second gives the same value.
    for (double s : {0.1, 0.23, 0.5, 0.77})
        CHECK_NEAR(clip.curves["mNeck"]["rot_x"].evaluate(s * 24), orig.curves.at("mNeck").at("rot_x").evaluate(s * 30),
                   1e-9);

    retime_clip(clip, 30);
    CHECK(clip == orig);  // integer-frame keys come back exactly
}
