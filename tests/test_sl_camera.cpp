// The Second Life preset's camera (view_math.h, slcam): the viewer's Alt camera, LLAgentCamera in llagentcamera.cpp.
#include <cmath>

#include "check.h"
#include "view_math.h"

using namespace vats;

namespace {

// A default-sized avatar standing at the origin facing +X, as the viewport's edited actor is.
slcam::Focus avatar() {
    slcam::Focus f;
    f.size = {slcam::kAgentDepth, slcam::kAgentWidth, 1.69};
    f.avatar = {Quat{}, {0, 0, 1.69 / 2}};
    return f;
}

const Vec3 kNose{0.12, 0, 1.62};  // the tip of the nose, in front of the face

// The camera 3 m in front of the face, Alt+clicked on the nose: the eye stays, the focus is the nose.
Camera focused_on_nose() {
    Camera cam;
    cam.target = {0, 0, 1.6}, cam.yaw = 0, cam.pitch = 0.05, cam.distance = 3;
    cam.min_eye_z = slcam::kMinOffGround;
    cam.focus_on(kNose);
    return cam;
}

// Inside the head: a box round the skull, 9 cm deep behind the nose tip, 16 cm wide, 1.45..1.75 m high.
bool inside_head(const Vec3& p) { return p.x > -0.12 && p.x < 0.12 && std::fabs(p.y) < 0.08 && p.z > 1.45 && p.z < 1.75; }

}  // namespace

// Alt+click: the camera stays where it was and looks at the point hit (setFocusGlobal keeps the camera position).
TEST(sl_focus_keeps_eye_and_targets_hit) {
    Camera cam;
    cam.target = {0, 0, 1}, cam.yaw = 0.4, cam.pitch = 0.2, cam.distance = 3;
    const Vec3 eye = cam.eye();
    cam.focus_on(kNose);
    CHECK_NEAR((cam.eye() - eye).length(), 0, 1e-9);
    CHECK_NEAR((cam.target - kNose).length(), 0, 1e-12);
    CHECK_NEAR(cam.distance, (eye - kNose).length(), 1e-9);
    // The focus is what the middle of the view shows: the ray through the centre passes through it.
    Projector p;
    p.view_proj = cam.projection(1.5) * cam.view();
    p.w = 900, p.h = 600;
    double sx = 0, sy = 0;
    CHECK(p.to_screen(kNose, sx, sy));
    CHECK_NEAR(sx, 450, 1e-3);
    CHECK_NEAR(sy, 300, 1e-3);
}

// Orbiting keeps the focus where it is and the distance to it, as cameraOrbitAround / cameraOrbitOver turn the
// offset about the focus; at a distance the avatar box allows, nothing is clamped.
TEST(sl_orbit_keeps_focus) {
    Camera cam = focused_on_nose();
    const slcam::Focus f = avatar();
    const Vec3 focus = cam.target;
    const double d = cam.distance;
    for (int i = 0; i < 40; ++i) slcam::orbit_around(cam, f, 0.2);
    CHECK_NEAR((cam.target - focus).length(), 0, 1e-12);
    CHECK_NEAR(cam.distance, d, 1e-9);
    CHECK_NEAR(std::remainder(cam.yaw - 8.0, 2 * kPi), std::remainder(focused_on_nose().yaw, 2 * kPi), 1e-9);
    slcam::orbit_over(cam, f, 0.3);
    CHECK_NEAR((cam.target - focus).length(), 0, 1e-12);
    CHECK_NEAR(cam.distance, d, 1e-9);
    // Straight up is as far as it goes: 1 degree from the vertical.
    slcam::orbit_over(cam, f, 10);
    CHECK_NEAR(cam.pitch, kPi / 2 - kDegToRad, 1e-9);
    slcam::orbit_over(cam, f, -10);
    CHECK_NEAR(cam.pitch, -kPi / 2 + kDegToRad, 1e-9);
}

// Zooming in hard on the nose stops in front of it at calcCameraMinDistance: the box's half depth less the focus's
// offset, plus the near plane and 0.1 m. Then orbiting all the way round never takes the eye into the head.
TEST(sl_zoom_stops_at_the_face_and_orbit_stays_outside) {
    Camera cam = focused_on_nose();
    const slcam::Focus f = avatar();
    for (int i = 0; i < 200; ++i) slcam::zoom_in(cam, f, std::pow(0.99, 20));  // Alt+drag up, far past the face
    // From the front: extents x = 0.45 x 0.55 - 2 x 0.12, half of it, + 0.01 + 0.1.
    const double want = (0.45 * 0.55 - 2 * 0.12) / 2 + slcam::kNear + 0.1;
    double got = 0;
    CHECK(slcam::min_distance(cam, f, got));
    CHECK_NEAR(got, want, 0.01);
    CHECK_NEAR(cam.distance, want, 0.01);
    CHECK(cam.eye().x > kNose.x + 0.1);  // in front of the nose, never behind it
    CHECK(!inside_head(cam.eye()));
    for (int i = 0; i < 360; ++i) {  // Alt+drag sideways, a degree at a time, and up and down on the way
        slcam::orbit_around(cam, f, kDegToRad);
        if (i % 90 == 45) slcam::orbit_over(cam, f, i < 180 ? 0.4 : -0.6);
        slcam::zoom_in(cam, f, 0.5);  // and keep pulling in
        CHECK(!inside_head(cam.eye()));
        CHECK_NEAR((cam.target - kNose).length(), 0, 1e-12);
    }
}

// The wheel and Alt+Up go through cameraOrbitIn: never nearer an avatar than AVATAR_MIN_ZOOM (0.5 m), an object
// than 2 cm, the land than 15 cm; and never out past 240 m, nor more than 4 times the distance in one step.
TEST(sl_orbit_in_limits) {
    Camera cam = focused_on_nose();
    slcam::Focus f = avatar();
    for (int i = 0; i < 50; ++i) slcam::orbit_in(cam, f, cam.distance * (1 - std::pow(std::sqrt(std::sqrt(2.0)), -1)));
    CHECK_NEAR(cam.distance, slcam::kAvatarMinZoom, 1e-9);
    // One wheel click is the fourth root of two.
    cam.distance = 2;
    slcam::orbit_in(cam, f, cam.distance * (1 - std::pow(std::sqrt(std::sqrt(2.0)), 1)));
    CHECK_NEAR(cam.distance, 2 * std::pow(2.0, 0.25), 1e-9);
    f.kind = slcam::Focus::Object;
    for (int i = 0; i < 50; ++i) slcam::orbit_in(cam, f, cam.distance * 0.5);
    CHECK_NEAR(cam.distance, slcam::kObjectMinZoom, 1e-9);
    f.kind = slcam::Focus::Land;
    for (int i = 0; i < 50; ++i) slcam::orbit_in(cam, f, cam.distance * 0.5);
    CHECK_NEAR(cam.distance, slcam::kLandMinZoom, 1e-9);
    cam.distance = 10;
    slcam::zoom_in(cam, f, 100);
    CHECK_NEAR(cam.distance, 40, 1e-9);  // four times, MAINT-3154
    for (int i = 0; i < 10; ++i) slcam::zoom_in(cam, f, 100);
    CHECK_NEAR(cam.distance, slcam::kMaxZoom, 1e-9);
    for (int i = 0; i < 10; ++i) slcam::orbit_in(cam, f, -1000);
    CHECK_NEAR(cam.distance, slcam::kMaxZoom, 1e-9);
}

// Pan: the focus and the camera move together along the camera's left and up axes.
TEST(sl_pan_moves_focus_and_eye_together) {
    Camera cam = focused_on_nose();
    cam.distance = 2;
    const slcam::Focus f = avatar();
    const Vec3 eye = cam.eye(), focus = cam.target, left = -cam.right(), up = cam.up();
    slcam::pan(cam, f, 0.3, -0.2);
    const Vec3 moved = left * 0.3 + up * -0.2;
    CHECK_NEAR((cam.target - (focus + moved)).length(), 0, 1e-9);
    CHECK_NEAR((cam.eye() - (eye + moved)).length(), 0, 1e-9);
}

// The ground: the eye never goes below getCameraMinOffGround (0.5 m); the orbit itself is left alone, so coming back
// up retraces it, and the view still looks at the focus.
TEST(sl_camera_stays_above_ground) {
    Camera cam = focused_on_nose();
    cam.distance = 3;
    const slcam::Focus f = avatar();
    slcam::orbit_over(cam, f, -1.2);  // well under the floor
    CHECK(cam.target.z - std::sin(-cam.pitch) * cam.distance < 0);  // where the orbit alone would put it
    CHECK_NEAR(cam.eye().z, slcam::kMinOffGround, 1e-12);
    CHECK_NEAR((cam.target - cam.eye()).normalized().dot(cam.forward()), 1, 1e-12);
    // Picking still works from the lifted eye: the centre ray hits the focus.
    Projector p;
    p.view_proj = cam.projection(1.5) * cam.view();
    p.w = 900, p.h = 600;
    Vec3 o, d;
    p.ray(cam, 450, 300, o, d);
    CHECK_NEAR(((cam.target - o) - d * (cam.target - o).dot(d)).length(), 0, 1e-9);
    slcam::orbit_over(cam, f, 1.2);
    CHECK_NEAR(cam.eye().z, focused_on_nose().eye().z + 0, 0.5);  // back above the floor, unclamped
    CHECK(cam.eye().z > slcam::kMinOffGround);
    // Off (other presets): the eye goes where the orbit puts it.
    cam.min_eye_z = -1e30;
    slcam::orbit_over(cam, f, -1.2);
    CHECK(cam.eye().z < 0);
}

// A focus glide interrupted by an orbit: input updates the target while easing carries on.
// The camera ends at the combined target, with no per-frame step larger than allowed by easing.
TEST(camera_glide_interrupted_by_orbit_blends_smoothly) {
    Camera cam;
    cam.target = {0, 0, 1.0};
    cam.yaw = 0.2;
    cam.pitch = 0.1;
    cam.distance = 3.0;

    CameraGlide glide;
    const Vec3 new_focus{1.0, 2.0, 1.5};
    glide.focus_on(cam, new_focus);
    CHECK(glide.active);

    const double dt = 1.0 / 60.0;
    // Advance 6 frames (~0.1 s into the glide)
    for (int i = 0; i < 6; ++i) {
        glide.update(cam, dt);
    }
    CHECK(glide.active);
    CHECK((cam.target - new_focus).length() > 0.1);  // still in flight

    // Interrupt the glide with an orbit around the focus
    const double orbit_angle = 0.35;
    const slcam::Focus f = avatar();
    glide.apply_input(cam, [&](Camera& c) {
        slcam::orbit_around(c, f, orbit_angle);
    });

    const Camera target_state = glide.target_cam;
    // Step until the glide finishes, verifying no sudden per-frame jump occurred
    double max_yaw_step = 0;
    double max_target_step = 0;
    int steps = 0;
    while (glide.active && steps < 600) {
        Camera prev = cam;
        glide.update(cam, dt);
        const double dyaw = std::abs(std::remainder(cam.yaw - prev.yaw, 2 * kPi));
        const double dpos = (cam.target - prev.target).length();
        if (dyaw > max_yaw_step) max_yaw_step = dyaw;
        if (dpos > max_target_step) max_target_step = dpos;
        steps++;
    }

    CHECK(!glide.active);
    // Ends at the combined target
    CHECK_NEAR((cam.target - new_focus).length(), 0, 1e-4);
    CHECK_NEAR(std::remainder(cam.yaw - target_state.yaw, 2 * kPi), 0, 1e-4);
    CHECK_NEAR(cam.distance, target_state.distance, 1e-4);
    CHECK_NEAR(cam.pitch, target_state.pitch, 1e-4);

    // With dt = 1/60 and half_life = 0.06, the maximum easing factor per frame is ~0.175.
    // An immediate snap would produce a yaw step of >= 0.35 or a pos step of >= 0.5.
    // The per-frame step is smoothly bounded by easing.
    CHECK(max_yaw_step < 0.15);
    CHECK(max_target_step < 0.3);
}

TEST(camera_glide_reduce_motion_is_instant) {
    Camera cam;
    cam.target = {0, 0, 1.0};
    cam.yaw = 0.2;
    cam.pitch = 0.1;
    cam.distance = 3.0;

    CameraGlide glide;
    const Vec3 new_focus{1.0, 2.0, 1.5};
    glide.focus_on(cam, new_focus);
    CHECK(glide.active);

    glide.update(cam, 1.0 / 60.0, /*reduce_motion=*/true);
    CHECK(!glide.active);
    CHECK_NEAR((cam.target - new_focus).length(), 0, 1e-12);
}

// SL Alt-click: during the focus glide the eye stays where it was; only the look-at point travels.
TEST(camera_focus_glide_keeps_the_eye_fixed) {
    Camera cam;
    cam.target = {0, 0, 1.0};
    cam.yaw = 0.2, cam.pitch = 0.1, cam.distance = 3.0;
    const Vec3 eye = cam.eye();
    CameraGlide glide;
    glide.focus_on(cam, {1.0, 2.0, 1.5});
    for (int i = 0; i < 120 && glide.active; ++i) {
        glide.update(cam, 1.0 / 60.0);
        CHECK((cam.eye() - eye).length() < 1e-6);
    }
    CHECK(!glide.active);
    CHECK((cam.target - Vec3{1.0, 2.0, 1.5}).length() < 1e-6);
}

// An Alt-click while a view turn (or an orbit) is still gliding keeps the eye where it is on screen, not where that
// glide was heading.
TEST(camera_focus_mid_glide_keeps_the_shown_eye) {
    for (int orbit = 0; orbit < 2; ++orbit) {
        Camera cam;
        cam.target = {0, 0, 1.0};
        cam.yaw = 0, cam.pitch = 0.1, cam.distance = 3.0;
        CameraGlide glide;
        if (orbit) {
            glide.focus_on(cam, {0.3, 0.2, 1.1});
            glide.update(cam, 1.0 / 60.0);
            glide.apply_input(cam, [](Camera& c) { c.yaw += 0.8; });
        } else {
            glide.look_from(cam, {0, 1, 0});  // a quarter turn
        }
        for (int i = 0; i < 3; ++i) glide.update(cam, 1.0 / 60.0);
        const Vec3 eye = cam.eye();
        glide.focus_on(cam, {0.2, 0, 1.2});
        for (int i = 0; i < 120 && glide.active; ++i) {
            glide.update(cam, 1.0 / 60.0);
            CHECK((cam.eye() - eye).length() < 1e-6);
        }
        CHECK(!glide.active);
    }
}

// The camera commands (Frame Selected, Zoom, Camera Views) go through apply_input: given mid-glide they land.
TEST(camera_input_mid_glide_is_kept) {
    Camera cam;
    cam.target = {0, 0, 1.0};
    cam.distance = 3.0;
    CameraGlide glide;
    glide.look_from(cam, {0, 1, 0});
    glide.update(cam, 1.0 / 60.0);
    glide.apply_input(cam, [](Camera& c) { c.zoom(0.85), c.target = {0.5, 0, 1.2}; });
    for (int i = 0; i < 600 && glide.active; ++i) glide.update(cam, 1.0 / 60.0);
    CHECK_NEAR(cam.distance, 3.0 * 0.85, 1e-3);
    CHECK((cam.target - Vec3{0.5, 0, 1.2}).length() < 1e-3);
}
