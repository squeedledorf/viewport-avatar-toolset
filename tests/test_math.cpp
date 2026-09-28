#include <random>

#include "check.h"
#include "vats/math.h"
#include "viewer_ref/viewer_ref.h"

using namespace vats;

namespace {

double quat_diff(const Quat& a, const Quat& b) { return 1.0 - std::fabs(a.normalized().dot(b.normalized())); }
Quat from_ref(const viewer_ref::LLQuaternion& q) { return {q.mQ[3], q.mQ[0], q.mQ[1], q.mQ[2]}; }

}  // namespace

TEST(euler_round_trip) {
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> d(-179, 179), dy(-89, 89);
    for (int i = 0; i < 2000; ++i) {
        Vec3 e{d(rng), dy(rng), d(rng)};
        Vec3 back = quat_to_euler(euler_to_quat(e));
        CHECK_NEAR((back - e).length(), 0, 1e-6);
    }
}

TEST(euler_gimbal_lock) {
    Quat q = euler_to_quat({30, 90, 10});
    Vec3 e = quat_to_euler(q);
    CHECK_NEAR(e.x, 0, 1e-9);
    CHECK_NEAR(e.y, 90, 1e-3);
    CHECK_NEAR(quat_diff(euler_to_quat(e), q), 0, 1e-9);
}

TEST(nearest_euler_stays_continuous) {
    // A spin past 180 degrees keeps counting instead of wrapping.
    Vec3 prev{0, 0, 170};
    Vec3 e = nearest_euler(euler_to_quat({0, 0, 200}), prev);
    CHECK_NEAR(e.z, 200, 1e-6);
    // The twin solution is picked when it is closer.
    Vec3 f = nearest_euler(euler_to_quat({10, 100, 20}), {10, 100, 20});
    CHECK_NEAR((f - Vec3{10, 100, 20}).length(), 0, 1e-6);
}

// SK-19: the Linden quaternion conventions, against the viewer's own code.
TEST(viewer_quaternion_conventions) {
    using namespace viewer_ref;
    // LL a*b == Hamilton b*a.
    Quat a = euler_to_quat({10, 20, 30}), b = euler_to_quat({-40, 5, 60});
    LLQuaternion la((F32)a.x, (F32)a.y, (F32)a.z, (F32)a.w), lb((F32)b.x, (F32)b.y, (F32)b.z, (F32)b.w);
    CHECK_NEAR(quat_diff(from_ref(la * lb), b * a), 0, 1e-6);
    // mayaQ(x, y, z, XYZ) == our Euler convention.
    for (Vec3 e : {Vec3{10, 20, 30}, Vec3{0, 90, 90}, Vec3{-45, 12, 170}})
        CHECK_NEAR(quat_diff(from_ref(mayaQ((F32)e.x, (F32)e.y, (F32)e.z, XYZ)), euler_to_quat(e)), 0, 1e-6);
    // setQuat(roll, pitch, yaw) == Hamilton qx * qy * qz (attachment points).
    LLQuaternion s;
    s.setQuat(0, 90 * DEG_TO_RAD, 90 * DEG_TO_RAD);
    CHECK_NEAR(quat_diff(from_ref(s), Quat{0.5, 0.5, 0.5, 0.5}), 0, 1e-6);
    // Hard-coded values that tell the two orders apart: X 90 and Z 90 (Quat is w, x, y, z).
    const Quat zx{0.5, 0.5, 0.5, 0.5}, xz{0.5, 0.5, -0.5, 0.5};  // Hamilton qz*qx and qx*qz
    CHECK_NEAR(quat_diff(from_ref(mayaQ(90, 0, 90, XYZ)), zx), 0, 1e-6);  // mayaQ = Hamilton qz*qy*qx
    CHECK_NEAR(quat_diff(euler_to_quat({90, 0, 90}), zx), 0, 1e-9);
    LLQuaternion lx((F32)0.70710678, 0, 0, (F32)0.70710678), lz(0, 0, (F32)0.70710678, (F32)0.70710678);
    CHECK_NEAR(quat_diff(from_ref(lx * lz), zx), 0, 1e-6);  // LL x*z = Hamilton z*x
    s.setQuat(90 * DEG_TO_RAD, 0, 90 * DEG_TO_RAD);
    CHECK_NEAR(quat_diff(from_ref(s), xz), 0, 1e-6);  // setQuat = Hamilton qx*qy*qz
}
