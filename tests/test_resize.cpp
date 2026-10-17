// Resize skeleton (08 RS).
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/bvh.h"
#include "vats/edit.h"
#include "vats/resize.h"
#include "vats/rig.h"
#include "vats/shape.h"
#include "vats/sl_preview.h"

using namespace vats;

namespace {

const AvatarParams& params() {
    static AvatarParams p = [] {
        std::ifstream f(std::string(VATS_DATA_DIR) + "/avatar_lad.xml", std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
        AvatarParams out;
        std::string err;
        if (!parse_avatar_params(text, out, err)) {
            std::fprintf(stderr, "cannot parse avatar_lad.xml: %s\n", err.c_str());
            std::exit(2);
        }
        return out;
    }();
    return p;
}

// Hips that sway and drop, the left arm bent at the elbow so a held wrist stays within reach.
Clip sway() {
    Clip c;
    c.fps = 30;
    c.end_frame = 30;
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 30, {0.06, -0.04, -0.05});
    key_euler(c, "mPelvis", 0, {0, 0, 0});
    key_euler(c, "mPelvis", 30, {3, 5, 12});
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 30, {0, 20, 0});
    key_euler(c, "mElbowLeft", 0, {0, 0, -100});
    return c;
}

const AnimJoint* joint(const AnimFile& f, const std::string& name) {
    for (const AnimJoint& j : f.joints)
        if (j.name == name) return &j;
    return nullptr;
}

}  // namespace

TEST(resize_reads_the_export_setting) {
    Json ex = Json::object();
    CHECK_NEAR(body_resize(ex), 1.0, 0);
    ex.set("size", 2.5);
    CHECK_NEAR(body_resize(ex), 2.5, 0);
    ex.set("size", 40.0);
    CHECK_NEAR(body_resize(ex), kResizeMax, 0);
    ex.set("size", -1.0);
    CHECK_NEAR(body_resize(ex), 1.0, 0);
    ex.set("size", std::string("big"));
    CHECK_NEAR(body_resize(ex), 1.0, 0);
}

TEST(resized_shape_scales_every_node_about_the_ground) {
    const Pose rest(skel().size());
    const BodyShape female = sl_default_shape(skel(), params(), false), male = sl_default_shape(skel(), params(), true);
    for (const Shape* base : {(const Shape*)nullptr, &female.shape, &male.shape})
        for (double s : {0.5, 2.0, 5.0}) {
            const Shape r = resized_shape(skel(), base, s);
            const std::vector<Xform> a = skel().global_pose(rest, base), b = skel().global_pose(rest, &r);
            double worst = 0;
            for (int i = 0; i < skel().size(); ++i) {
                worst = std::max(worst, (b[i].pos - a[i].pos * s).length());
                CHECK((b[i].rot.conj() * a[i].rot).angle() < 1e-9);  // only positions change
            }
            CHECK(worst < 1e-9);
        }
    // About another ground (a worn avatar whose soles are not at 0): that point stays, the rest scales from it.
    {
        const Shape r = resized_shape(skel(), &male.shape, 2.5, -0.1);
        const std::vector<Xform> a = skel().global_pose(rest, &male.shape), b = skel().global_pose(rest, &r);
        const Vec3 g{0, 0, -0.1};
        double worst = 0;
        for (int i = 0; i < skel().size(); ++i) worst = std::max(worst, (b[i].pos - (g + (a[i].pos - g) * 2.5)).length());
        CHECK(worst < 1e-9);
    }
    // The body size SL takes grows by the same factor: the resized body's scales carry what a joint-position body
    // s times SL's carries in its positions.
    for (double s : {0.5, 3.0}) {
        const Shape r = resized_shape(skel(), nullptr, s);
        CHECK_NEAR(sl_body_size(skel(), nullptr, &r).height, s * sl_body_size(skel()).height, 1e-9);
    }
}

TEST(resized_shape_keeps_the_hip_moves_in_metres) {
    // Moves below the hip grow with their parent's scale; the hip's offset meets no parent and stays as keyed.
    Pose p(skel().size());
    const int pelvis = skel().find("mPelvis"), neck = skel().find("mNeck");
    p.offset[size_t(pelvis)] = {0, 0, -0.3};
    p.offset[size_t(neck)] = {0, 0, 0.1};
    const Shape r = resized_shape(skel(), nullptr, 2);
    const std::vector<Xform> g = skel().global_pose(p, &r), rest = skel().global_pose(Pose(skel().size()), &r);
    CHECK_NEAR(g[pelvis].pos.z - rest[pelvis].pos.z, -0.3, 1e-9);
    const Xform l = skel().local_xform(neck, p, &r);
    CHECK_NEAR(l.pos.z, (skel()[neck].pos.z + 0.1) * 2, 1e-9);
}

TEST(scale_position_keys_scales_hip_controllers_and_pins) {
    Clip c = sway();
    key_offset(c, "mNeck", 0, {0, 0, 0.1});  // below the hip: left alone
    c.curves["ik.LeftLeg"]["pos_z"].set_key(0, 0.2);
    c.curves["ik.LeftLeg"]["pole_y"].set_key(0, -0.4);
    c.curves["pin:mWristLeft"]["pos_x"].set_key(0, 0.1);  // a held point's keyed slide
    Pin world, own, other;
    world.joint = "mWristLeft", world.pos = {0.3, 0.2, 1.1};
    own.joint = "mWristRight", own.target = "mChest", own.pos = {0.05, -0.1, 0.02};
    other.joint = "mElbowLeft", other.target = "mHead", other.target_actor = "Partner", other.pos = {0.1, 0, 0};
    c.pins = {world, own, other};
    c.dynamics.push_back({});
    key_offset(c, "mPelvis", 10, {0.01, 0.02, -0.2});
    c.dynamics[0].source = c.curves;  // a baked layer's pre-bake tracks scale alike
    const Clip before = c;

    const ResizedKeys n = scale_position_keys(skel(), c, 2.5);
    CHECK_EQ(n.pins, 2);
    const FCurve& hz = c.curves["mPelvis"]["pos_z"];
    const FCurve& hz0 = before.curves.at("mPelvis").at("pos_z");
    for (size_t k = 0; k < hz.keys.size(); ++k) {
        CHECK_NEAR(hz.keys[k].value, hz0.keys[k].value * 2.5, 1e-12);
        CHECK_NEAR(hz.keys[k].ly, hz0.keys[k].ly * 2.5, 1e-12);
        CHECK_NEAR(hz.keys[k].ry, hz0.keys[k].ry * 2.5, 1e-12);
        CHECK_NEAR(hz.keys[k].lx, hz0.keys[k].lx, 0);  // frames stay
    }
    CHECK_NEAR(hz.evaluate(20), hz0.evaluate(20) * 2.5, 1e-9);
    CHECK_NEAR(c.dynamics[0].source["mPelvis"]["pos_z"].evaluate(20), hz0.evaluate(20) * 2.5, 1e-9);
    CHECK_NEAR(c.curves["ik.LeftLeg"]["pos_z"].evaluate(0), 0.5, 1e-12);
    CHECK_NEAR(c.curves["ik.LeftLeg"]["pole_y"].evaluate(0), -1.0, 1e-12);
    CHECK_NEAR(c.curves["pin:mWristLeft"]["pos_x"].evaluate(0), 0.25, 1e-12);
    CHECK(c.curves["mNeck"] == before.curves.at("mNeck"));
    CHECK(c.curves["mPelvis"]["rot_z"] == before.curves.at("mPelvis").at("rot_z"));
    CHECK_NEAR((c.pins[0].pos - world.pos * 2.5).length(), 0, 1e-12);
    CHECK_NEAR((c.pins[1].pos - own.pos * 2.5).length(), 0, 1e-12);
    CHECK(c.pins[2] == other);
    scale_position_keys(skel(), c, 1 / 2.5);  // back: the size changed back again
    CHECK_NEAR(c.curves["mPelvis"]["pos_z"].evaluate(20), hz0.evaluate(20), 1e-12);
}

TEST(resized_body_with_scaled_keys_holds_a_scaled_pin) {
    // The sway with a world pin on the left wrist, made twice the size with its keys: SL plays the export on that body
    // with the wrist on the scaled target.
    Rig rig(skel());
    Clip c = sway();
    const int wrist = skel().find("mWristLeft");
    std::string why;
    CHECK(pin_here(c, rig, 0, wrist, -1, nullptr, why));
    const Vec3 held = c.pins[0].pos;
    const double s = 2;
    scale_position_keys(skel(), c, s);
    const Shape body = resized_shape(skel(), nullptr, s);
    AnimExportOptions opt{0, 0, 60};
    opt.shape = &body;
    opt.resize = s;
    const AnimFile f = export_anim(skel(), c, opt).file;
    double off = 0;
    for (int fr = 0; fr <= c.end_frame; ++fr) {
        const Pose p = anim_pose(skel(), f, double(fr) / c.fps);
        off = std::max(off, (skel().global_pose(p, &body)[wrist].pos - held * s).length());
    }
    CHECK(off < 1e-3);
}

TEST(resized_export_writes_positions_as_a_joint_position_body_holds_them) {
    Clip c = sway();
    key_offset(c, "mNeck", 0, {0, 0, 0});
    key_offset(c, "mNeck", 30, {0, 0.02, 0.1});
    AnimExportOptions one{0, 0, 60};
    const AnimFile a = export_anim(skel(), c, one).file;
    const Shape body = resized_shape(skel(), nullptr, 3);
    AnimExportOptions three = one;
    three.shape = &body;
    three.resize = 3;
    const AnimFile b = export_anim(skel(), c, three).file;
    const AnimJoint *na = joint(a, "mNeck"), *nb = joint(b, "mNeck"), *ha = joint(a, "mPelvis"), *hb = joint(b, "mPelvis");
    CHECK(na && nb && ha && hb);
    CHECK_EQ(na->pos.size(), nb->pos.size());
    const double step = 10.0 / 65535;  // one quantisation step over -5..5 m
    // The viewer's quantiser floors, encoding twice: up to two steps off each way, three times that for the small file.
    for (size_t k = 0; k < na->pos.size(); ++k)
        CHECK((decode_position(nb->pos[k]) - decode_position(na->pos[k]) * 3).length() < 8 * std::sqrt(3.0) * step);
    CHECK_EQ(ha->pos.size(), hb->pos.size());
    for (size_t k = 0; k < ha->pos.size(); ++k)
        CHECK((decode_position(hb->pos[k]) - decode_position(ha->pos[k])).length() < 2 * step);  // the hip as keyed

    // BVH: the skeleton itself is three times the size; the hip's move is not.
    BvhExportOptions bo;
    const std::string ta = export_bvh(skel(), c, bo).text;
    bo.resize = 3;
    bo.shape = &body;
    const std::string tb = export_bvh(skel(), c, bo).text;
    auto offsets = [](const std::string& t) {
        std::vector<double> z;
        std::istringstream in(t);
        std::string word;
        while (in >> word)
            if (word == "OFFSET") {
                double x, y, w;
                in >> x >> y >> w;
                z.push_back(y);
            }
        return z;
    };
    const std::vector<double> oa = offsets(ta), ob = offsets(tb);
    CHECK_EQ(oa.size(), ob.size());
    for (size_t k = 0; k < oa.size(); ++k) CHECK_NEAR(ob[k], oa[k] * 3, 1e-4);
}
