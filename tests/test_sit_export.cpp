#include <cstdio>

#include "check.h"
#include "vats/sit_export.h"

using namespace vats;

// Spec 08 GR-2: sit-system lines. The fixtures were worked out by hand from the formats' documentation
// (AVsitter2 AVpos notecard, nPose V4 XANIM), not by running this code:
//
// The furniture root holds the sit target at <1, 2, 0.5>, tipped 90 degrees about X (Rx90).
// Lead stands 0.3 m left of the sit target (+Y), turned 60 degrees; Partner 0.12345 m in front, turned 12.3456;
// Third 0.5 m behind, turned -90.
//   position = root + Rx90 * offset: Rx90 (x, y, z) = (x, -z, y), so Lead (0, 0.3, 0) -> (1, 2, 0.8),
//   Partner (0.12345, 0, 0) -> (1.12345, 2, 0.5), Third (-0.5, 0, 0) -> (0.5, 2, 0.5).
//   rotation = Rx90 * Rz(t) = [[c, -s, 0], [0, 0, -1], [s, c, 0]]. Decomposed as llEuler2Rot builds it,
//   Rz(z) Ry(y) Rx(x): y = asin(-s) = -t, x = atan2(c, 0) = 90, z = atan2(0, c) = 0, so <90, -t, 0>.
//   Summing the Euler angles instead would give <90, 0, t>. Third (t = -90, c = 0) is the gimbal case:
//   [[0, 1, 0], [0, 0, -1], [-1, 0, 0]] = Rz(-90) Ry(90), so <0, 90, -90>.
namespace {

const SitRoot kRoot{{1, 2, 0.5}, {90, 0, 0}};

Sitter sitter(const char* name, const char* anim, Vec3 pos, double rot_z) {
    return {name, anim, {Quat::axis_angle({0, 0, 1}, rot_z * kDegToRad), pos}};
}

// The rotation a line's "<x, y, z>" degrees stand for, as llEuler2Rot(v * DEG_TO_RAD) reads it.
Quat read_rot(const std::string& line, int nth_vector) {
    size_t at = 0;
    for (int k = 0; k <= nth_vector; ++k) at = line.find('<', at) + 1;
    Vec3 v;
    std::sscanf(line.c_str() + at, "%lf , %lf , %lf", &v.x, &v.y, &v.z);
    return euler_to_quat(v);
}

bool same_rot(const Quat& a, const Quat& b) { return std::fabs(std::fabs(a.dot(b)) - 1) < 1e-6; }

}  // namespace

TEST(sit_export_avsitter) {
    const std::vector<Sitter> s{sitter("Lead", "Hug_01_Lead", {0, 0.3, 0}, 60),
                                sitter("Partner", "Hug_01_Partner", {0.12345, 0, 0}, 12.3456)};
    const std::string out = avsitter_lines("Hug_01", s, kRoot);
    // AVpos: SITTER sections from 0, SYNC <menu>|<anim>, {menu}<pos 3 decimals><Euler degrees 1 decimal>, no spaces.
    CHECK_EQ(out, std::string("SITTER 0|Lead\n"
                              "SYNC Hug_01|Hug_01_Lead\n"
                              "{Hug_01}<1,2,0.8><90,-60,0>\n"
                              "\n"
                              "SITTER 1|Partner\n"
                              "SYNC Hug_01|Hug_01_Partner\n"
                              "{Hug_01}<1.123,2,0.5><90,-12.3,0>\n"));
    // Degrees, not radians or a quaternion: read back as AVsitter does, it is the sitter's rotation in the root.
    const std::string line = out.substr(out.find("{Hug_01}<1,"), out.find('\n', out.find("{Hug_01}<1,")) - out.find("{Hug_01}<1,"));
    CHECK(same_rot(read_rot(line, 1), kRoot.xform().rot * s[0].place.rot));
    CHECK(same_rot(euler_to_quat({90, -60, 0}), Quat::axis_angle({1, 0, 0}, kPi / 2) * Quat::axis_angle({0, 0, 1}, kPi / 3)));
    // Without a root offset the placement is written as it is; long menu names are cut to AVsitter's 23 characters.
    const std::string plain = avsitter_lines("A_very_long_pose_name_for_menu", {sitter("A", "a", {0.6, 0, 0}, 180)}, {});
    CHECK_EQ(plain, std::string("SITTER 0|A\nSYNC A_very_long_pose_name_f|a\n{A_very_long_pose_name_f}<0.6,0,0><0,0,180>\n"));
}

TEST(sit_export_npose) {
    const std::vector<Sitter> s{sitter("Lead", "Hug_01_Lead", {0, 0.3, 0}, 60),
                                sitter("Partner", "Hug_01_Partner", {0.12345, 0, 0}, 12.3456),
                                sitter("Third", "Hug_01_Third", {-0.5, 0, 0}, -90)};
    const std::string out = npose_lines(s, kRoot);
    // XANIM|seat# from 1|anim|<pos, 3 decimals>|<Euler degrees, 2 decimals>, ", " between components.
    CHECK_EQ(out, std::string("XANIM|1|Hug_01_Lead|<1, 2, 0.8>|<90, -60, 0>\n"
                              "XANIM|2|Hug_01_Partner|<1.123, 2, 0.5>|<90, -12.35, 0>\n"
                              "XANIM|3|Hug_01_Third|<0.5, 2, 0.5>|<0, 90, -90>\n"));
    // Degrees read back as nPose Core does (llEuler2Rot(v * DEG_TO_RAD)) give each sitter's rotation in the root.
    size_t at = 0;
    for (const Sitter& one : s) {
        const std::string line = out.substr(at, out.find('\n', at) - at);
        CHECK(same_rot(read_rot(line, 1), kRoot.xform().rot * one.place.rot));
        at = out.find('\n', at) + 1;
    }
}
