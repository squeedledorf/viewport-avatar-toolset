// Centre of mass and Auto-Balance (08 CM-1, CM-2).
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "../tools/mech_rig.h"
#include "vats/avatar_mesh.h"
#include "vats/balance.h"
#include "vats/dae.h"
#include "vats/edit.h"

using namespace vats;

namespace {

Vec3 ankle(const std::vector<Xform>& g, const char* side) { return g[skel().find(std::string("mAnkle") + side)].pos; }

int limb(const Rig& rig, const char* name) { return rig.find_limb(name); }

// Ten frames standing with both feet held by leg IK where they rest, the whole body leaning forward by deg
// about the point between the ankles.
Clip leaning(const Rig& rig, double deg) {
    Clip c;
    c.end_frame = 10;
    for (const char* leg : {"LegLeft", "LegRight"}) switch_to_ik(c, rig, 0, limb(rig, leg), nullptr);
    const std::vector<Xform> rest = skel().global_pose(Pose(skel().size()));
    const Vec3 a = (ankle(rest, "Left") + ankle(rest, "Right")) * 0.5, p = rest[skel().find("mPelvis")].pos;
    const Vec3 moved = a + Quat::axis_angle({0, 1, 0}, deg * kDegToRad).rotate(p - a);
    for (int f : {0, 10}) {
        key_euler(c, "mPelvis", f, {0, deg, 0});
        key_offset(c, "mPelvis", f, moved - p);
    }
    return c;
}

}  // namespace

TEST(balance_rest_pose_is_over_the_feet) {
    Rig rig(skel());
    Clip c;
    const std::vector<Xform> g = evaluate(rig, c, 0, nullptr).globals;
    Balance b = balance_of(skel(), g, nullptr);
    CHECK(b.contact);
    CHECK(b.inside());
    CHECK_EQ(b.support.size(), size_t(4));
    const Vec3 mid = (ankle(g, "Left") + ankle(g, "Right")) * 0.5;
    CHECK(std::hypot(b.ground.x - mid.x, b.ground.y - mid.y) < 0.03);
    CHECK(b.com.z > 0.9 && b.com.z < 1.2);  // about the hips' height
}

TEST(balance_forward_lean_falls_outside) {
    Rig rig(skel());
    Clip c = leaning(rig, 30);
    Balance b = balance_of(skel(), evaluate(rig, c, 5, nullptr).globals, nullptr);
    CHECK(b.contact);
    CHECK(!b.inside());
    CHECK(b.ground.x > b.support[0].x);  // in front
}

TEST(balance_no_contact_shows_nothing) {
    Rig rig(skel());
    Clip c;
    key_offset(c, "mPelvis", 0, {0, 0, 0.5});  // half a metre off the ground
    Balance b = balance_of(skel(), evaluate(rig, c, 0, nullptr).globals, nullptr);
    CHECK(!b.contact);
    CHECK(b.support.empty());
}

TEST(auto_balance_brings_the_lean_back_inside) {
    Rig rig(skel());
    Clip c = leaning(rig, 30);
    std::vector<std::vector<Xform>> before;
    for (int f = 0; f <= 10; ++f) before.push_back(evaluate(rig, c, f, nullptr).globals);
    AutoBalanceOptions opt;
    const std::string report = auto_balance(c, rig, opt);
    CHECK(report.find("still off balance") == std::string::npos);
    for (int f = 0; f <= 10; ++f) {
        const std::vector<Xform> g = evaluate(rig, c, f, nullptr).globals;
        Balance b = balance_of(skel(), g, nullptr);
        CHECK(b.inside());
        CHECK(b.margin > opt.margin - 0.005);
        for (const char* side : {"Left", "Right"}) CHECK((ankle(g, side) - ankle(before[f], side)).length() < 0.001);
        CHECK_NEAR(g[0].pos.z, before[f][0].pos.z, 1e-6);  // X and Y only
    }
    // The pelvis rotation (the lean itself) is untouched.
    CHECK_NEAR(curve_euler(c, "mPelvis", 5).y, 30, 1e-6);
}

TEST(auto_balance_counter_lean_turns_the_torso_against_the_hips) {
    Rig rig(skel());
    Clip c = leaning(rig, 30), plain = c;
    const Vec3 start = curve_offset(c, "mPelvis", 5);
    AutoBalanceOptions opt;
    auto_balance(plain, rig, opt);
    opt.counter_lean = true;
    auto_balance(c, rig, opt);
    CHECK(balance_of(skel(), evaluate(rig, c, 5, nullptr).globals, nullptr).inside());
    CHECK(curve_euler(c, "mTorso", 5).y < -1);  // leans back, towards the feet
    CHECK((curve_offset(c, "mPelvis", 5) - start).length() < (curve_offset(plain, "mPelvis", 5) - start).length() - 0.01);
}

TEST(auto_balance_keeps_frames_outside_the_range) {
    Rig rig(skel());
    Clip c = leaning(rig, 30);
    c.end_frame = 24;
    const Vec3 lean = curve_offset(c, "mPelvis", 10);
    key_offset(c, "mPelvis", 0, lean + Vec3{-0.05, 0.02, 0});  // the hips sway, so the curves have shape
    key_offset(c, "mPelvis", 16, lean + Vec3{0.03, -0.02, 0});
    key_offset(c, "mPelvis", 24, lean);
    const Clip before = c;
    AutoBalanceOptions opt;
    opt.from = 6, opt.to = 11;
    auto_balance(c, rig, opt);
    for (int f : {0, 1, 2, 3, 4, 5, 12, 13, 15, 16, 20, 24})
        CHECK((curve_offset(c, "mPelvis", f) - curve_offset(before, "mPelvis", f)).length() < 1e-9);
    for (double f : {2.5, 4.5, 12.5, 14.5}) CHECK((curve_offset(c, "mPelvis", f) - curve_offset(before, "mPelvis", f)).length() < 1e-9);
    CHECK(balance_of(skel(), evaluate(rig, c, 8, nullptr).globals, nullptr).inside());
}

// The mech's hind soles rest 12 cm above its front ones (tools/mech_rig.h builds the hind legs for the rig axes
// tests, not to stand), so at rest it stands on its front feet. With its hind legs lowered to the floor, the mesh
// contact spans all four feet: hind (behind X = 0, both sides) and front.
TEST(balance_mech_spans_four_feet) {
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_dae(mech::dae(skel(), mech::build(skel())), "", skel(), m, r, err));
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);

    Rig rig(skel());
    auto support_of = [&](const Clip& c) {
        const std::vector<Xform> g = evaluate(rig, c, 0, &s).globals;
        std::vector<float> pos, nrm;
        skin_prop(m, skel(), g, &s, pos, nrm);
        return balance_of(skel(), g, &s, nullptr, pos);
    };
    const Balance rest = support_of(Clip{});
    CHECK(rest.contact);
    CHECK(!rest.support.empty());
    for (const Vec3& pt : rest.support) CHECK(pt.x > 0.0);  // the front feet only

    Clip c;
    for (const char* hip : {"mHindLimb1Left", "mHindLimb1Right"}) key_offset(c, hip, 0, {0, 0, -0.11});
    const Balance b = support_of(c);
    CHECK(b.contact);
    double min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9;
    for (const Vec3& pt : b.support) {
        min_x = std::min(min_x, pt.x), max_x = std::max(max_x, pt.x);
        min_y = std::min(min_y, pt.y), max_y = std::max(max_y, pt.y);
        CHECK_NEAR(pt.z, b.ground.z, 1e-6);
    }
    CHECK(min_x < -0.2);  // the hind feet, behind
    CHECK(max_x > 0.1);   // the front feet
    CHECK(min_y < -0.25 && max_y > 0.25);  // both hind feet, splayed out
}

TEST(balance_hind_feet_only_ground_height) {
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_dae(mech::dae(skel(), mech::build(skel())), "", skel(), m, r, err));
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);

    Rig rig(skel());
    Clip c;
    const std::vector<Xform> g = evaluate(rig, c, 0, &s).globals;

    // A body weighted ONLY to hind feet (like a creature whose biped feet are unused)
    auto hind_only = [&](int node) {
        std::string_view name = skel()[node].name;
        return name.find("HindLimb4") != std::string_view::npos;
    };
    std::function<bool(int)> hf = hind_only;
    Balance b = balance_of(skel(), g, &s, &hf);
    CHECK(b.contact);
    CHECK(b.ground.z < 0.2);
    CHECK(b.ground.z > -0.05);
}

TEST(balance_mech_contact_height_matches_lowest_vertex) {
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_dae(mech::dae(skel(), mech::build(skel())), "", skel(), m, r, err));
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);

    Rig rig(skel());
    Clip c;
    const std::vector<Xform> g = evaluate(rig, c, 0, &s).globals;

    std::vector<float> pos, nrm;
    skin_prop(m, skel(), g, &s, pos, nrm);

    double lowest_z = 1e9;
    for (size_t i = 0; i + 2 < pos.size(); i += 3) lowest_z = std::min(lowest_z, double(pos[i + 2]));

    Balance b = balance_of(skel(), g, &s, nullptr, pos);
    CHECK(b.contact);
    // Core test from brief §1: on the mech, the contact height equals the lowest skinned vertex
    // within 1 mm, and the drop line's end lies on that plane.
    CHECK(std::fabs(b.ground.z - lowest_z) < 1e-3);
    CHECK(!b.support.empty());
    for (const Vec3& pt : b.support) {
        CHECK_NEAR(pt.z, b.ground.z, 1e-6);
    }
}

TEST(balance_mech_leg_raised_excludes_raised_foot) {
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_dae(mech::dae(skel(), mech::build(skel())), "", skel(), m, r, err));
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);

    Rig rig(skel());
    Clip c;
    // Lift right leg up (mHipRight pitch up by 45 degrees)
    key_rotation(c, "mHipRight", 0, Quat::axis_angle(Vec3{0, 1, 0}, -kPi / 4));
    const std::vector<Xform> g = evaluate(rig, c, 0, &s).globals;

    std::vector<float> pos, nrm;
    skin_prop(m, skel(), g, &s, pos, nrm);

    double lowest_z = 1e9;
    for (size_t i = 0; i + 2 < pos.size(); i += 3) lowest_z = std::min(lowest_z, double(pos[i + 2]));

    Balance b = balance_of(skel(), g, &s, nullptr, pos);
    CHECK(b.contact);
    CHECK(std::fabs(b.ground.z - lowest_z) < 1e-3);
    CHECK(!b.support.empty());
    for (const Vec3& pt : b.support) {
        CHECK_NEAR(pt.z, b.ground.z, 1e-6);
        // Raised right foot (y < 0) vertices are well above lowest_z + 2 cm,
        // so support polygon only touches the remaining grounded left foot (y > 0)
        CHECK(pt.y > 0.0);
    }
}

TEST(balance_sl_avatar_contact_height_matches_lowest_vertex) {
    AvatarMesh mesh;
    std::string err;
    CHECK(mesh.load(skel(), VATS_DATA_DIR, err));
    mesh.build(Body::Female);
    const Skeleton& s = skel();
    Rig rig(s);
    Clip c;
    const std::vector<Xform> g = evaluate(rig, c, 0, nullptr).globals;
    std::vector<float> pos, nrm;
    mesh.skin(g, nullptr, pos, nrm);

    double lowest_z = 1e9;
    for (size_t i = 0; i + 2 < pos.size(); i += 3) lowest_z = std::min(lowest_z, double(pos[i + 2]));

    Balance b = balance_of(s, g, nullptr, nullptr, pos);
    CHECK(b.contact);
    CHECK(std::fabs(b.ground.z - lowest_z) < 1e-3);
    CHECK(!b.support.empty());
    for (const Vec3& pt : b.support) {
        CHECK_NEAR(pt.z, b.ground.z, 1e-6);
    }
}

TEST(balance_mesh_contact_floor_caching) {
    AvatarMesh mesh;
    std::string err;
    CHECK(mesh.load(skel(), VATS_DATA_DIR, err));
    mesh.build(Body::Female);
    const Skeleton& s = skel();
    Rig rig(s);
    Clip c;
    const std::vector<Xform> g = evaluate(rig, c, 0, nullptr).globals;
    std::vector<float> pos, nrm;
    mesh.skin(g, nullptr, pos, nrm);

    MeshContactFloor floor = compute_mesh_contact_floor(pos);
    CHECK(floor.has_mesh);
    CHECK(!floor.contact_points.empty());

    Balance uncached = balance_of(s, g, nullptr, nullptr, pos);
    Balance cached = balance_of(s, g, nullptr, nullptr, std::vector<std::span<const float>>{}, &floor);

    CHECK(uncached.contact);
    CHECK(cached.contact);
    CHECK_NEAR(cached.ground.z, uncached.ground.z, 1e-6);
    CHECK_NEAR(cached.com.x, uncached.com.x, 1e-6);
    CHECK_NEAR(cached.com.y, uncached.com.y, 1e-6);
    CHECK_NEAR(cached.com.z, uncached.com.z, 1e-6);
    CHECK_EQ(cached.support.size(), uncached.support.size());
    for (size_t i = 0; i < cached.support.size(); ++i) {
        CHECK_NEAR(cached.support[i].x, uncached.support[i].x, 1e-6);
        CHECK_NEAR(cached.support[i].y, uncached.support[i].y, 1e-6);
        CHECK_NEAR(cached.support[i].z, uncached.support[i].z, 1e-6);
    }
}


// Feet whose soles aren't level (a creature's front feet 4 cm above its hind ones) are all planted; a raised foot
// (20 cm up) is not.
TEST(mesh_contact_floor_takes_every_planted_foot) {
    auto foot = [](std::vector<float>& v, float x, float y, float z) {
        for (float dx : {0.0f, 0.05f})
            for (float dy : {0.0f, 0.05f}) v.insert(v.end(), {x + dx, y + dy, z});
        v.insert(v.end(), {x, y, z + 0.3f});  // the leg above it
    };
    std::vector<float> m;
    foot(m, -0.5f, -0.3f, 0.0f), foot(m, -0.5f, 0.3f, 0.0f);   // hind feet
    foot(m, 0.5f, -0.3f, 0.04f), foot(m, 0.5f, 0.3f, 0.04f);   // front feet, a little higher
    foot(m, 0.0f, 0.8f, 0.2f);                                   // a raised foot
    const MeshContactFloor f = compute_mesh_contact_floor({std::span<const float>(m)});
    CHECK(f.has_mesh);
    CHECK_NEAR(f.lowest_z, 0.0, 1e-6);
    bool front = false, raised = false;
    for (const Vec3& p : f.contact_points) {
        front = front || p.x > 0.4;
        raised = raised || p.y > 0.7;
        CHECK_NEAR(p.z, 0.0, 1e-6);
    }
    CHECK(front);
    CHECK(!raised);
}

// Auto-Balance balances on the floor the view shows (CM-1): on the mesh's, the mech, whose box feet touch the floor
// at their toes only, is off balance at rest (its bones say it stands fine), and Auto-Balance brings it inside.
TEST(auto_balance_uses_the_mesh_floor) {
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_dae(mech::dae(skel(), mech::build(skel())), "", skel(), m, r, err));
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);
    Rig rig(skel());
    auto floor = [&](const std::vector<Xform>& g) {
        std::vector<float> pos, nrm;
        skin_prop(m, skel(), g, &s, pos, nrm);
        return compute_mesh_contact_floor(pos);
    };
    auto on_mesh = [&](const Clip& c, int f) {
        const std::vector<Xform> g = evaluate(rig, c, f, &s).globals;
        const MeshContactFloor fl = floor(g);
        return balance_of(skel(), g, &s, nullptr, std::vector<std::span<const float>>{}, &fl);
    };
    Clip c;
    c.end_frame = 4;
    CHECK(on_mesh(c, 2).contact);
    CHECK(!on_mesh(c, 2).inside());
    CHECK(balance_of(skel(), evaluate(rig, c, 2, &s).globals, &s).inside());  // the bones' floor: nothing to do
    AutoBalanceOptions opt;
    opt.shape = &s;
    opt.mesh_floor = floor;
    auto_balance(c, rig, opt);
    for (int f = 0; f <= 4; ++f) CHECK(on_mesh(c, f).inside());
}
