// Box selection in the viewport (ui/box_select.h): which joints fall in the box, and the modifiers per preset.
#include "box_select.h"
#include "check.h"

using namespace vats;

namespace {

Projector projector_for(const Camera& cam) {
    Projector p;
    p.view_proj = cam.projection(800.0 / 500) * cam.view();
    p.x0 = 40, p.y0 = 30, p.w = 800, p.h = 500;
    return p;
}

std::vector<Xform> points(std::initializer_list<Vec3> at) {
    std::vector<Xform> out;
    for (const Vec3& p : at) out.push_back(Xform{Quat{}, p});
    return out;
}

}  // namespace

// A box round a joint's projection takes it, in perspective and orthographic views, drawn from either corner; joints
// outside, filtered out (hidden) or behind the camera do not count.
TEST(box_select_takes_joints_projected_inside) {
    for (bool ortho : {false, true}) {
        Camera cam;
        cam.ortho = ortho;
        cam.yaw = 0.3, cam.pitch = 0.2, cam.distance = 3, cam.target = {0, 0, 1};
        const Projector pr = projector_for(cam);
        auto pts = points({{0, 0, 1}, {0, 0.1, 1.1}, {0, -1.5, 0.2}, cam.eye() - cam.forward() * 2.0 + cam.right() * 0.8});  // 3: behind the eye
        const auto all = [](int) { return true; };
        // A box round the first two joints only.
        double ax = 0, ay = 0, bx = 0, by = 0;
        CHECK(pr.to_screen(pts[0].pos, ax, ay) && pr.to_screen(pts[1].pos, bx, by));
        const double l = std::min(ax, bx) - 5, r = std::max(ax, bx) + 5, t = std::min(ay, by) - 5, b = std::max(ay, by) + 5;
        CHECK(points_in_rect(pr, pts, l, t, r, b, all) == (std::vector<int>{0, 1}));
        CHECK(points_in_rect(pr, pts, r, b, l, t, all) == (std::vector<int>{0, 1}));  // dragged up and left
        CHECK(points_in_rect(pr, pts, l, t, r, b, [](int i) { return i != 1; }) == std::vector<int>{0});  // hidden
        // The whole view: every joint in front of the camera, never the one behind it (perspective).
        const auto whole = points_in_rect(pr, pts, -1e5, -1e5, 1e5, 1e5, all);
        CHECK(whole.size() >= 3 && whole[0] == 0 && whole[1] == 1 && whole[2] == 2);
        if (!ortho) CHECK(std::find(whole.begin(), whole.end(), 3) == whole.end());
        CHECK(points_in_rect(pr, pts, 0, 0, 1, 1, all).empty());
    }
}

TEST(box_select_modifiers_per_preset) {
    using M = BoxMode;
    // Industry (Maya): Shift toggles, Ctrl removes, Ctrl+Shift adds.
    CHECK(box_mode(Preset::Industry, false, false, false) == M::Replace);
    CHECK(box_mode(Preset::Industry, true, false, false) == M::Toggle);
    CHECK(box_mode(Preset::Industry, false, true, false) == M::Remove);
    CHECK(box_mode(Preset::Industry, true, true, false) == M::Add);
    // Blender: Shift extends, Ctrl subtracts; B's box extends.
    CHECK(box_mode(Preset::Blender, false, false, false) == M::Replace);
    CHECK(box_mode(Preset::Blender, true, false, false) == M::Add);
    CHECK(box_mode(Preset::Blender, false, true, false) == M::Remove);
    CHECK(box_mode(Preset::Blender, false, false, true) == M::Add);
    CHECK(box_mode(Preset::Blender, false, true, true) == M::Remove);
    // QAvimator: Ctrl starts the box, so Ctrl alone replaces and Shift adds.
    CHECK(box_mode(Preset::QAvimator, false, true, false) == M::Replace);
    CHECK(box_mode(Preset::QAvimator, true, true, false) == M::Add);
    // Second Life: Shift adds, Ctrl removes.
    CHECK(box_mode(Preset::SecondLife, false, false, false) == M::Replace);
    CHECK(box_mode(Preset::SecondLife, true, false, false) == M::Add);
    CHECK(box_mode(Preset::SecondLife, false, true, false) == M::Remove);
}

TEST(box_select_apply_keeps_order_and_primary_last) {
    std::vector<int> sel{3, 7};
    apply_box(sel, {1, 7}, BoxMode::Add);
    CHECK(sel == (std::vector<int>{3, 7, 1}));
    apply_box(sel, {1, 9}, BoxMode::Toggle);
    CHECK(sel == (std::vector<int>{3, 7, 9}));
    apply_box(sel, {3, 4}, BoxMode::Remove);
    CHECK(sel == (std::vector<int>{7, 9}));
    apply_box(sel, {2, 5}, BoxMode::Replace);
    CHECK(sel == (std::vector<int>{2, 5}));
    apply_box(sel, {}, BoxMode::Replace);  // an empty box clears, as a click on empty space
    CHECK(sel.empty());
}
