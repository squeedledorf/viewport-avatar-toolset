// The orthographic view's maths (spec 04 VP-67): Camera::projection and the Projector the viewport picks with.
#include <random>

#include "check.h"
#include "view_math.h"

using namespace vats;

namespace {

Projector projector_for(const Camera& cam, double w = 800, double h = 500) {
    Projector p;
    p.view_proj = cam.projection(w / h) * cam.view();
    p.x0 = 40, p.y0 = 30, p.w = w, p.h = h;
    return p;
}

double distance_to_ray(const Vec3& p, const Vec3& o, const Vec3& d) { return (p - o - d * (p - o).dot(d)).length(); }

Camera ortho_camera() {
    Camera c;
    c.ortho = true;
    c.yaw = 0.9, c.pitch = 0.4, c.distance = 2.5, c.target = {0.2, -0.1, 1.1};
    return c;
}

}  // namespace

// A point's screen position, unprojected, gives a ray through the point; in ortho even behind the eye (up to
// kOrthoBack), since a close zoom puts the eye inside the body and the projection still draws that.
TEST(ortho_project_unproject_round_trip) {
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> u(-1.5, 1.5);
    for (bool ortho : {false, true}) {
        Camera cam = ortho_camera();
        cam.ortho = ortho;
        Projector p = projector_for(cam);
        for (int i = 0; i < 200; ++i) {
            Vec3 w = cam.target + Vec3{u(rng), u(rng), u(rng)};
            if (!ortho && (w - cam.eye()).dot(cam.forward()) < 0.1) continue;
            double sx = 0, sy = 0;
            CHECK(p.to_screen(w, sx, sy));
            Vec3 o, d;
            p.ray(cam, sx, sy, o, d);
            CHECK_NEAR(d.length(), 1, 1e-9);
            CHECK_NEAR(distance_to_ray(w, o, d), 0, 2e-4);
            CHECK((w - o).dot(d) > 0);  // in front of where the ray starts
        }
    }
    // Behind the eye, within the ortho's reach: on screen and inside the depth range.
    Camera cam = ortho_camera();
    Vec3 behind = cam.eye() - cam.forward() * (Camera::kOrthoBack * 0.9);
    double c[4];
    (cam.projection(1.6) * cam.view()).apply(behind, c);
    CHECK(c[2] / c[3] > -1 && c[2] / c[3] < 1);
}

// Parallel rays: the same direction (the view's) from every pixel, and origins that move with the pixel.
TEST(ortho_pick_ray_direction_is_constant) {
    Camera cam = ortho_camera();
    Projector p = projector_for(cam);
    Vec3 o0, d0;
    p.ray(cam, p.x0, p.y0, o0, d0);
    for (double fx : {0.0, 0.3, 1.0})
        for (double fy : {0.0, 0.6, 1.0}) {
            Vec3 o, d;
            p.ray(cam, p.x0 + fx * p.w, p.y0 + fy * p.h, o, d);
            CHECK_NEAR((d - cam.forward()).length(), 0, 1e-12);
            CHECK_NEAR((o - o0).dot(cam.forward()), 0, 1e-9);  // all start on one plane
        }
    Vec3 oa, da, ob, db;
    p.ray(cam, p.x0, p.y0 + p.h / 2, oa, da);
    p.ray(cam, p.x0 + p.w, p.y0 + p.h / 2, ob, db);
    const double width = 2 * cam.ortho_half_height() * p.w / p.h;
    CHECK_NEAR((ob - oa).length(), width, 1e-9);
    // Perspective rays fan out, for contrast.
    cam.ortho = false;
    p.ray(cam, p.x0, p.y0, oa, da);
    p.ray(cam, p.x0 + p.w, p.y0 + p.h, ob, db);
    CHECK(da.dot(db) < 0.95);
}

// The ortho view is as tall at the target as the perspective one: turning ortho on keeps the framing, zoom
// (distance) scales it, and Frame All's distance fits the same height. Gizmos keep their pixel size.
TEST(ortho_framing_matches_perspective) {
    for (double dist : {Camera::kDefaultDistance, 1.2, 0.3, 12.0}) {
        Camera persp = ortho_camera();
        persp.ortho = false;
        persp.distance = dist;
        Camera ortho = persp;
        ortho.ortho = true;
        Projector pp = projector_for(persp), po = projector_for(ortho);
        for (Vec3 off : {Vec3{0, 0, 0}, persp.up() * 0.3, persp.right() * -0.2 + persp.up() * 0.1}) {
            double px = 0, py = 0, ox = 1, oy = 1;
            CHECK(pp.to_screen(persp.target + off * dist, px, py));
            CHECK(po.to_screen(ortho.target + off * dist, ox, oy));
            CHECK_NEAR(px, ox, 1e-3);
            CHECK_NEAR(py, oy, 1e-3);
        }
        CHECK_NEAR(po.world_per_pixel(ortho, ortho.target), pp.world_per_pixel(persp, persp.target), 1e-12);
        // Constant at every depth in ortho.
        CHECK_NEAR(po.world_per_pixel(ortho, ortho.eye()), po.world_per_pixel(ortho, ortho.target), 1e-12);
        CHECK_NEAR(ortho.ortho_half_height(), dist * std::tan(Camera::kFov / 2), 1e-12);
    }
    // Zoom scales the ortho size.
    Camera c = ortho_camera();
    double h0 = c.ortho_half_height();
    c.zoom(0.5);
    CHECK_NEAR(c.ortho_half_height(), h0 * 0.5, 1e-12);
}

// A true plan from above: heights do not move a point on screen.
TEST(ortho_top_view_is_a_true_plan) {
    Camera cam = ortho_camera();
    cam.pitch = kPi / 2 - 1e-4;  // the view cube's Top in ortho
    Projector p = projector_for(cam);
    double ax = 0, ay = 0, bx = 1, by = 1;
    CHECK(p.to_screen({0.3, 0.2, 0.0}, ax, ay));
    CHECK(p.to_screen({0.3, 0.2, 1.8}, bx, by));
    CHECK_NEAR(ax, bx, 0.1);
    CHECK_NEAR(ay, by, 0.1);
}
