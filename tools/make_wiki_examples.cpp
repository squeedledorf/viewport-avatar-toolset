// Viewport Avatar Toolset - builds the help wiki's example projects (docs/wiki/examples) from code, so they can
// be rebuilt after a format change instead of edited by hand.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
//
// Usage: vats_make_wiki_examples <docs/wiki/examples dir>
//        vats_make_wiki_examples - --dynamics-variants <dir>   (the Dynamics comparison GIFs' projects, not shipped)
// Writes one project per page that has a worked example; each function below names its page.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "vats/clip.h"
#include "vats/clips.h"
#include "vats/curve_ops.h"
#include "vats/dynamics.h"
#include "vats/dae.h"
#include "vats/edit.h"
#include "vats/fbx.h"
#include "vats/footlock.h"
#include "vats/key_tags.h"
#include "vats/lint.h"
#include "vats/loop_assist.h"
#include "vats/lip_sync.h"
#include "vats/loop_tools.h"
#include "vats/idle.h"
#include "vats/overlap.h"
#include "vats/jump_arc.h"
#include "vats/pose_ops.h"
#include "vats/pose_presets.h"
#include "vats/pose_tools.h"
#include "vats/project.h"
#include "vats/prop.h"
#include "vats/ragdoll.h"
#include "vats/retarget.h"
#include "vats/rig.h"
#include "vats/shape.h"
#include "vats/simplify.h"
#include "vats/skeleton.h"
#include "vats/time_edit.h"
#include "vats/tween.h"

using namespace vats;

namespace {

// The skeleton from data/character (VATS_DATA_DIR), for the examples that pose through the rig.
const Skeleton& skel() {
    static Skeleton s = [] {
        Skeleton k;
        std::string err;
        if (!k.load_dir(VATS_DATA_DIR, err)) {
            std::fprintf(stderr, "cannot load skeleton: %s\n", err.c_str());
            std::exit(2);
        }
        return k;
    }();
    return s;
}

// Keys a starter pose (Pose library page) at a frame; hand poses go on the left hand, or the right when mirrored.
void starter(Clip& c, const char* slug, double frame, bool mirrored = false) {
    for (const LibraryItem& it : builtin_poses(skel()))
        if (it.id == std::string("builtin:") + slug) return apply_pose(c, skel(), it, frame, mirrored);
    std::fprintf(stderr, "no starter pose %s\n", slug);
    std::exit(2);
}

struct K {
    double frame, x, y, z;
};

// Keys all three rotation channels of a bone (Euler degrees).
void key_rot(Clip& c, const std::string& bone, const std::vector<K>& keys) {
    for (const K& k : keys) {
        const double v[3] = {k.x, k.y, k.z};
        for (int i = 0; i < 3; ++i) c.curves[bone][kRotChannels[i]].set_key(k.frame, v[i]);
    }
    for (auto& [name, curve] : c.curves[bone]) curve.recompute_handles();
}

// The rotation a drag on one ring of the Rotate gizmo (Local axes, the default) gives a bone that starts at `from`
// (Euler degrees): `deg` about the bone's own X, Y or Z (axis 0, 1, 2) in its gizmo frame (Skeleton::bone_axes). The
// beginner tutorials' targets are built this way, so a reader dragging that one ring can reach the target exactly
// (the status bar's distance goes to 0), which a change of one Euler channel alone would not allow.
Vec3 ring(const std::string& bone, const Vec3& from, int axis, double deg) {
    const Quat a = skel().bone_axes(skel().find(bone), nullptr);
    const Vec3 dir = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    const Quat q = euler_to_quat(from) * a * Quat::axis_angle(dir, deg * kDegToRad) * a.conj();
    const Vec3 e = nearest_euler(q, from);
    auto r = [](double v) { return std::round(v * 10) / 10 + 0.0; };  // as Properties shows; + 0.0: never -0
    return {r(e.x), r(e.y), r(e.z)};
}

// What Mirror Bone to Other Side keys on the partner of a left-side bone at `rot` (Euler degrees), rounded as ring().
Vec3 mirrored(const std::string& left, const Vec3& rot) {
    const std::string right = left.substr(0, left.size() - 4) + "Right";
    return ring(right, quat_to_euler(mirror_rotation(skel(), skel().find(left), skel().find(right), euler_to_quat(rot))), 0, 0);
}

// Keys all three position channels of a bone (metres).
void key_pos(Clip& c, const std::string& bone, const std::vector<K>& keys) {
    for (const K& k : keys) {
        const double v[3] = {k.x, k.y, k.z};
        for (int i = 0; i < 3; ++i) c.curves[bone][kPosChannels[i]].set_key(k.frame, v[i]);
    }
    for (auto& [name, curve] : c.curves[bone]) curve.recompute_handles();
}

std::string read_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A starter prop as the Inventory adds it (double-click): its file, name, suggested point and grip offset from
// app/assets/props/props.json, with the installed layout's path (lib_id finds this installation's copy). A slug
// that is not in the list yet gets the Right Hand and no offset, and a warning.
Prop starter_prop(const std::string& slug) {
    Prop p;
    p.name = slug, p.point = "Right Hand", p.lib_id = "starter-" + slug, p.path = "../../assets/props/" + slug + ".dae";
    Json doc;
    std::vector<Prop> list;
    std::string err;
    if (!parse_json(read_text(std::string(VATS_DATA_DIR) + "/../../app/assets/props/props.json"), doc, err) ||
        !props_from_json(doc, list, err)) {
        std::fprintf(stderr, "props.json: %s\n", err.c_str());
        std::exit(2);
    }
    for (Prop& q : list) {
        const Json* s = q.extra.find("slug");
        if (!s || !s->is_string() || s->str != slug) continue;
        const Json* file = q.extra.find("file");
        const Json* point = q.extra.find("suggested_point");
        p.name = q.name, p.pos = q.pos, p.rot = q.rot;
        if (file && file->is_string()) p.path = "../../assets/props/" + file->str;
        if (point && point->is_string()) p.point = point->str;
        return p;
    }
    std::fprintf(stderr, "warning: no starter prop %s yet; the example has it on the Right Hand without a grip offset\n",
                 slug.c_str());
    return p;
}

// The point of a starter prop's model (its own axes, as the mesh file has them) that sits at in_point, metres in
// the attachment point's frame, at the props.json grip: the inverse of the shared placement (vats::prop_frame,
// prop_local). With in_point the fist's hole it is where the hand holds the model; with the prop's support_grip
// (support = true, in_point unused), where the other hand does.
Vec3 starter_model_point(const std::string& slug, Vec3 in_point, bool support = false) {
    const Prop p = starter_prop(slug);
    const std::string dir = std::string(VATS_DATA_DIR) + "/../../app/assets/props/";
    DaeModel m;
    DaeReport rep;
    std::string err;
    Json doc;
    std::vector<Prop> list;
    if (!load_mesh_file(dir + p.path.substr(p.path.rfind('/') + 1), skel(), m, rep, err) ||
        !parse_json(read_text(dir + "props.json"), doc, err) || !props_from_json(doc, list, err)) {
        std::fprintf(stderr, "%s: %s\n", slug.c_str(), err.c_str());
        std::exit(2);
    }
    for (const Prop& q : list)
        if (const Json* g = q.extra.find("support_grip"); support && g && g->is_array() && g->arr.size() == 3 &&
                                                          q.extra.find("slug")->str == slug)
            in_point = {g->arr[0].num, g->arr[1].num, g->arr[2].num};
    const Vec3 l = euler_to_quat(p.rot).conj().rotate(in_point - p.pos);  // prop_local's frame
    return (m.bounds_min + m.bounds_max) * 0.5 + Vec3{l.x / p.scale.x, l.y / p.scale.y, l.z / p.scale.z};
}

bool write(const std::string& path, const Project& p) {
    std::ofstream f(path, std::ios::binary);
    f << save_project(p);
    if (!f) std::fprintf(stderr, "could not write %s\n", path.c_str());
    else std::printf("wrote %s\n", path.c_str());
    return bool(f);
}

// Graph editor: the right arm rises, waves three times from the elbow and comes down.
Project graph_basics() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 72, c.loop = false, c.loop_out = 72, c.priority = 3;
    // Up by frame 15 (the Waving starter pose's arm), down from frame 57.
    key_rot(c, "mCollarRight", {{0, 0, 0, 0}, {15, -5, 0, 0}, {57, -5, 0, 0}, {72, 0, 0, 0}});
    key_rot(c, "mShoulderRight", {{0, 0, 0, 0}, {15, 49, -74, -30}, {57, 49, -74, -30}, {72, 0, 0, 0}});
    // Three waves: the forearm swings about the elbow's Z axis.
    key_rot(c, "mElbowRight", {{0, 0, 0, 0}, {15, 0, 9, 91}, {22, 0, 9, 60}, {29, 0, 9, 105}, {36, 0, 9, 60},
                               {43, 0, 9, 105}, {50, 0, 9, 60}, {57, 0, 9, 91}, {72, 0, 0, 0}});
    key_rot(c, "mWristRight", {{0, 0, 0, 0}, {15, -8, 11, 6}, {57, -8, 11, 6}, {72, 0, 0, 0}});
    return p;
}

// first-wave.vat, what the First steps tutorial builds: the Waving starter pose keyed at frame 0, then the right
// forearm swung out (frame 10) and in (frame 20) with the rotate gizmo's blue ring, the hand carried a little further
// each way than the forearm (a loose hand flops past where the forearm stops), and frame 0's pose pasted at frame 30
// so the loop closes. Everything else (30 fps, frames 0-30, priority 3, ease 0.3 s) is the new document's default.
// It is also the page's target ghost: at each frame the ghost shows the arm to match.
Project first_wave() {
    Project p;
    p.clip = new_project_clip();
    Clip& c = p.clip;
    c.loop = true;
    // The Waving starter pose (core/src/pose_presets.cpp), as applying it at frame 0 keys it, and as Paste Pose keys
    // it again at frame 30.
    for (double f : {0.0, 30.0}) {
        key_rot(c, "mCollarLeft", {{f, -5, 0, 0}});
        key_rot(c, "mShoulderLeft", {{f, -78, 0, 0}});
        key_rot(c, "mElbowLeft", {{f, 0, 0, -12}});
        key_rot(c, "mCollarRight", {{f, -5, 0, 0}});
        key_rot(c, "mShoulderRight", {{f, 49, -74, -30}});
        key_rot(c, "mHead", {{f, 3, 0, -5}});
    }
    // The tutorial's own keys, each one blue-ring drag from the Waving pose: the forearm 30 degrees out (away from the
    // head) at 10 and 24 in at 20; the hand 20 further out and 18 further in.
    const Vec3 elbow{0, 9, 91}, wrist{-8, 11, 6};
    const Vec3 e10 = ring("mElbowRight", elbow, 2, -30), e20 = ring("mElbowRight", elbow, 2, 24);
    const Vec3 w10 = ring("mWristRight", wrist, 2, -20), w20 = ring("mWristRight", wrist, 2, 18);
    key_rot(c, "mElbowRight", {{0, elbow.x, elbow.y, elbow.z}, {10, e10.x, e10.y, e10.z}, {20, e20.x, e20.y, e20.z},
                               {30, elbow.x, elbow.y, elbow.z}});
    key_rot(c, "mWristRight", {{0, wrist.x, wrist.y, wrist.z}, {10, w10.x, w10.y, w10.z}, {20, w20.x, w20.y, w20.z},
                               {30, wrist.x, wrist.y, wrist.z}});
    if (std::getenv("VATS_EXAMPLES_LINT"))
        std::printf("first-wave: elbow %.1f %.1f %.1f / %.1f %.1f %.1f, wrist %.1f %.1f %.1f / %.1f %.1f %.1f\n", e10.x,
                    e10.y, e10.z, e20.x, e20.y, e20.z, w10.x, w10.y, w10.z, w20.x, w20.y, w20.z);
    return p;
}

// The right arm comes forward from rest by frame 15 and holds, as for a handshake.
Clip handshake_clip() {
    Clip c;
    c.fps = 30, c.end_frame = 30, c.loop = false, c.loop_out = 30, c.priority = 3;
    key_rot(c, "mShoulderRight", {{0, 0, 0, 0}, {15, 25, 0, 55}});
    key_rot(c, "mElbowRight", {{0, 0, 0, 0}, {15, 0, 0, 50}});
    key_rot(c, "mWristRight", {{0, 0, 0, 0}, {15, 0, -80, 0}});
    return c;
}

Project couple_handshake() {
    Project p;
    p.clip = handshake_clip();
    Actor lead;
    lead.name = "Lead";
    Actor partner;  // as Add Partner places it: 0.6 m in front, turned to face the first actor
    partner.name = "Partner";
    partner.colour = {0.45f, 0.66f, 0.88f};
    partner.body = "sl-default";  // Ruth
    partner.pos = {0.6, 0, 0};
    partner.rot_z = 180;
    partner.clip = handshake_clip();
    p.actors = {lead, partner};
    p.active = 0;
    sync_actor_timing(p);
    return p;
}

Project prop_in_hand() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 30, c.loop = false, c.loop_out = 30, c.priority = 3;
    // The arm lowered and bent to hold the mug upright in front of the stomach (the sip tutorial's hold), the fingers
    // closed round the handle in Grip (Cylinder), one key at frame 0.
    key_rot(c, "mShoulderRight", {{0, 72, 0, 20}});
    key_rot(c, "mElbowRight", {{0, 0, 0, 100}});
    starter(c, "hand-grip", 0, true);
    c.props.push_back(starter_prop("mug"));  // at its grip
    return p;
}

// Coordinate descent: moves each angle (degrees) up or down while that lowers the cost, halving the step from 8
// to 0.02 degrees.
void descend(std::vector<double>& a, const std::function<double(const std::vector<double>&)>& cost) {
    double best = cost(a);
    for (double st = 8; st > 0.02; st *= 0.5)
        for (bool moved = true; moved;) {
            moved = false;
            for (double& v : a)
                for (double sgn : {1.0, -1.0}) {
                    v += sgn * st;
                    const double e = cost(a);
                    if (e < best) best = e, moved = true;
                    else v -= sgn * st;
                }
        }
}

// How far the given joints of a pose go past what a body can do (the ragdoll's limits, which the Animation Check's
// joint_limits rule uses), degrees, summed.
double past_limits(const Pose& pose, std::initializer_list<int> joints) {
    double deg = 0;
    for (const LimitExcess& e : ragdoll_limit_excesses(skel(), pose, 0))
        for (int j : joints)
            if (e.node == j) deg += e.deg;
    return deg;
}

// The angle between two rotations, degrees.
double angle_between(const Quat& a, const Quat& b) {
    const Quat dq = a.conj() * b;
    return 2 * std::acos(std::min(1.0, std::fabs(dq.w))) * kRadToDeg;
}

// The angle between two directions, degrees.
double angle_between(const Vec3& a, const Vec3& b) {
    return std::acos(std::clamp(a.normalized().dot(b.normalized()), -1.0, 1.0)) * kRadToDeg;
}

// A leg's FK keys (hip, knee, ankle) that put its ankle at a world target, found by searching the angles directly
// on the skeleton's FK: the knee only bends (Rotate Y), and the hip's twist (Rotate Z) is held near twist, so the
// knee points where the foot does. Rounded to 0.1 degree. Returns how far the ankle ends from the target, in metres.
double key_leg(Clip& c, const std::string& side, const Xform& target, double frame, const Shape* shape, double twist) {
    const std::string names[3] = {"mHip" + side, "mKnee" + side, "mAnkle" + side};
    int n[3];
    for (int i = 0; i < 3; ++i) n[i] = skel().find(names[i]);
    Rig rig(skel());
    Pose pose = evaluate(rig, c, frame, shape).pose;
    std::vector<double> a = {0, 0, twist, 0, 0, 0, 0};  // hip x y z, knee y, ankle x y z
    descend(a, [&](const std::vector<double>& v) {
        pose.rot[n[0]] = euler_to_quat({v[0], v[1], v[2]});
        pose.rot[n[1]] = euler_to_quat({0, v[3], 0});
        pose.rot[n[2]] = euler_to_quat({v[4], v[5], v[6]});
        const Xform g = skel().global_pose(pose, shape)[n[2]];
        const double ang = angle_between(g.rot, target.rot), d = (g.pos - target.pos).length();
        return d * d / (0.002 * 0.002) + ang * ang / 4 + (v[2] - twist) * (v[2] - twist) / 25 + v[0] * v[0] / 400 +
               (v[3] < 0 ? v[3] * v[3] * 100 : 0);
    });
    for (double& v : a) v = std::round(v * 10) / 10;
    key_euler(c, names[0], frame, {a[0], a[1], a[2]});
    key_euler(c, names[1], frame, {0, a[3], 0});
    key_euler(c, names[2], frame, {a[4], a[5], a[6]});
    return (evaluate(rig, c, frame, shape).globals[n[2]].pos - target.pos).length();
}

// Where an arm's hand should be: the centre of the fist's hole (grip_hole) in the world, and optionally which way the held blade points (the hand's +X, the way the Sword
// sits in the Right Hand) and which way its cutting edge faces (the hand's -Y, the knuckles' side).
struct HandGoal {
    Vec3 fist;
    Vec3 blade, edge;  // zero: free
};

// An arm's FK keys (shoulder, elbow, wrist; the elbow bends about Z only) for a hand goal, found by searching the
// angles from start on the skeleton's FK, within the joints' natural ranges. Rounded to whole degrees: what the
// tutorial's reader types. Prints the values and the misses when VATS_PRINT is set. start: shoulder x y z, elbow z,
// wrist x y z, elbow y (the forearm's turn); the search stays near it, so a key solved from the key before it turns
// the short way. Returns the angles keyed.
// wrist false: the wrist stays unkeyed (straight) and only the shoulder and elbow are keyed.
std::vector<double> key_arm(Clip& c, const std::string& side, const HandGoal& goal, double frame, std::vector<double> start,
             bool wrist = true, const Shape* shape = nullptr) {
    const std::string names[3] = {"mShoulder" + side, "mElbow" + side, "mWrist" + side};
    int n[3];
    for (int i = 0; i < 3; ++i) n[i] = skel().find(names[i]);
    const int hand = skel().find(side + " Hand");
    const double flex = side == "Right" ? 1 : -1;  // the right elbow bends to +Z, the left to -Z
    Rig rig(skel());
    Pose pose = evaluate(rig, c, frame, shape).pose;
    auto hand_at = [&](const std::vector<double>& v) {
        pose.rot[n[0]] = euler_to_quat({v[0], v[1], v[2]});
        pose.rot[n[1]] = euler_to_quat({0, v[7], v[3]});
        pose.rot[n[2]] = euler_to_quat({v[4], v[5], v[6]});
        return skel().global_pose(pose, shape)[hand];
    };
    auto misses = [&](const std::vector<double>& v, double& d, double& blade, double& edge) {
        const Xform h = hand_at(v);
        d = (h.apply(grip_hole(side == "Left")) - goal.fist).length();
        const Vec3 b = h.rot.rotate({1, 0, 0}), e = h.rot.rotate({0, -1, 0});
        blade = goal.blade.length() > 0 ? angle_between(b, goal.blade) : 0;
        // The edge's miss about the blade: both directions taken square to the blade first.
        edge = goal.edge.length() > 0 ? angle_between(e - b * e.dot(b), goal.edge - b * goal.edge.dot(b)) : 0;
    };
    auto over = [](double v, double lo, double hi) { return v < lo ? lo - v : v > hi ? v - hi : 0; };
    const std::vector<double> guess = start;
    auto cost = [&](const std::vector<double>& v) {
        double d, blade, edge;
        misses(v, d, blade, edge);
        // The elbow bends (Z) and turns the forearm (Y: from palm down at rest, up to 170 degrees towards palm up and
        // 20 the other way); the wrist bends (X), turns a little more
        // (Y) and tilts to the thumb or the little finger (Z).
        double limits = over(flex * v[3], 5, 150) + over(flex * v[7], -170, 20);
        if (wrist)
            limits += over(v[4], -70, 70) + over(v[5], -25, 25) + over(flex * v[6], -35, 20);
        else
            limits += (std::fabs(v[4]) + std::fabs(v[5]) + std::fabs(v[6]) + std::fabs(v[7])) * 10;
        // The shoulder's angles in their plain range (Y within 85 degrees, X and Z within 180), and near the start
        // given, so neighbouring keys interpolate the short way and the values read as a pose.
        limits += over(v[1], -85, 85) + over(v[0], -180, 180) + over(v[2], -180, 180);
        double near = 0;
        for (int i = 0; i < 8; ++i) near += (v[i] - guess[i]) * (v[i] - guess[i]);
        limits += past_limits(pose, {n[0], n[1], n[2]});
        return d * d / (0.005 * 0.005) + blade * blade / 9 + edge * edge / 100 + limits * limits * 10 + near / 400;
    };
    // From the start given and from 40 others spread over the joints' ranges, the best (the same every run: a
    // fixed seed).
    std::vector<double> best = start;
    descend(best, cost);
    unsigned seed = 12345;
    auto rnd = [&](double lo, double hi) {
        seed = seed * 1103515245 + 12345;
        return lo + (hi - lo) * ((seed >> 8) % 10000) / 10000.0;
    };
    for (int k = 0; k < 40; ++k) {
        std::vector<double> t = {rnd(-150, 150), rnd(-90, 90), rnd(-150, 150), flex * rnd(10, 140), 0, 0, 0, 0};
        if (wrist) t[4] = rnd(-60, 60), t[5] = rnd(-20, 20), t[6] = rnd(-30, 15), t[7] = flex * rnd(-160, 15);
        descend(t, cost);
        if (cost(t) < cost(best)) best = t;
    }
    start = best;
    for (double& v : start) v = std::round(v);
    if (!wrist) start[4] = start[5] = start[6] = start[7] = 0;
    key_euler(c, names[0], frame, {start[0], start[1], start[2]});
    key_euler(c, names[1], frame, {0, start[7], start[3]});
    if (wrist) key_euler(c, names[2], frame, {start[4], start[5], start[6]});
    if (std::getenv("VATS_PRINT")) {
        double d, blade, edge;
        misses(start, d, blade, edge);
        std::printf("  %g: %s %g %g %g, elbow 0 %g %g, wrist %g %g %g; misses %.3f m, blade %.0f, edge %.0f deg\n",
                    frame, names[0].c_str(), start[0], start[1], start[2], start[7], start[3], start[4], start[5], start[6],
                    d, blade, edge);
    }
    return start;
}

// Loop tools: one second of walking at Second Life's walking speed, 3.2 m/s, that does not quite end where it
// starts. SL moves a walking avatar much faster than a person walks (about 1.4 m/s), so a walk that keeps its feet
// on the ground there needs a long, quick stride: two strides of 1.6 m in the second (steps of 0.8 m, 0.5 s a
// stride; left heel strikes at 0, 15 and 30, right at 7.5 and 22.5). Built from the feet: each foot's path is laid
// out in the world the way a walking foot moves (published gait data), and the legs are solved to it and keyed as
// FK on every frame:
// - stance, 60% of the stride: the heel strikes with the toes up 12 degrees, the foot rolls flat by 10% and stays
//   exactly where it landed, the heel rises from 33% and the foot pushes off over its toes at 60%;
// - swing, 40%: the foot swings forward, lifted most early in the swing, and levels out for the next heel strike.
// The hips travel at a steady 3.2 m/s, lowest just after each heel strike and highest over the planted foot, sway
// over the planted foot and turn with the swinging leg; the chest turns against them and the arms swing against
// the legs, the elbows a little behind the shoulders. shape: the body the feet are placed for (the SL default
// female, the app's default body).
Project loop_walk(const Shape* shape) {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 30, c.loop = true, c.loop_in = 0, c.loop_out = 30, c.priority = 3;
    const double kCycle = 15, kSpeed = 3.2 / 30, kTwoPi = 2 * 3.14159265358979, kPi = kTwoPi / 2;
    // The hips' height (lowest just after heel strike), where the heel lands ahead of them, how far the foot pitches
    // at push-off and early in the swing, how high the swinging foot lifts and when the heel starts to rise.
    const double kHipDrop = -0.045, kBob = 0.018, kReach = 0.35, kPushOff = 45, kSwingPitch = 50, kLift = 0.07,
                 kRise = 0.28;
    auto wave = [&](double f, double peak_frame) { return std::cos(kTwoPi * (f - peak_frame) / kCycle); };
    // Upper body first, so the legs are solved under the hips as they finally move.
    for (double f = 0; f <= 30; ++f) {
        key_offset(c, "mPelvis", f, {kSpeed * f, 0.02 * wave(f, 3.75), kHipDrop - kBob * std::cos(2 * kTwoPi * (f - 1) / kCycle)});
        if (std::fmod(f, 3) != 0) continue;
        key_euler(c, "mPelvis", f, {3 * wave(f, 2), 3, -7 * wave(f, 0)});
        key_euler(c, "mTorso", f, {-2.5 * wave(f, 2), 3, 6 * wave(f, 0)});
        key_euler(c, "mChest", f, {0, 0, 6 * wave(f, 0.5)});
        key_euler(c, "mHead", f, {0, -5, -5 * wave(f, 0.5)});
        // Arms: hanging, swinging against the legs (Rotate Y, + is back), a little further forward than back;
        // the elbows bend more as the arm comes forward, a frame behind the shoulder.
        key_euler(c, "mShoulderLeft", f, {-82, -2 + 16 * wave(f, 1), 0});
        key_euler(c, "mShoulderRight", f, {82, -2 - 16 * wave(f, 1), 0});
        key_euler(c, "mElbowLeft", f, {0, 0, -(25 - 12 * wave(f, 2))});
        key_euler(c, "mElbowRight", f, {0, 0, 25 + 12 * wave(f, 2)});
    }
    // The feet.
    Rig rig(skel());
    const Evaluation rest = evaluate(rig, Clip{}, 0, shape);
    struct Leg {
        const char* side_name;
        const char* ankle;
        double strike, side, yaw;  // heel strike frame, the foot's line (Y), toe-out (degrees about Z)
    };
    const Leg legs[2] = {{"Left", "mAnkleLeft", 0, 0.07, 5}, {"Right", "mAnkleRight", kCycle / 2, -0.07, -5}};
    // Foot pivots relative to the ankle at rest: the heel's back edge and the tips of the toes, on the ground.
    const double ankle_h = 0.073;
    const Vec3 heel{-0.045, 0, -ankle_h}, toe{0.19, 0, -ankle_h + 0.004};
    for (const Leg& leg : legs) {
        const int ankle = skel().find(leg.ankle);
        const double ground = rest.globals[ankle].pos.z - ankle_h;
        const Quat yaw = Quat::axis_angle({0, 0, 1}, leg.yaw * kDegToRad);
        // The ankle when the foot is pitched (+ toes down) about a pivot on it that stays at world point q.
        auto pitched = [&](double pitch) { return yaw * Quat::axis_angle({0, 1, 0}, pitch * kDegToRad); };
        auto on_pivot = [&](const Vec3& pivot, const Vec3& q, double pitch) {
            return Xform{pitched(pitch) * rest.globals[ankle].rot, q - pitched(pitch).rotate(pivot)};
        };
        // Where the heel lands on the stride that starts at heel strike frame s.
        auto heel_at = [&](double s) { return Vec3{kSpeed * s + kReach, leg.side, ground}; };
        auto foot = [&](double f) {
            const double s = std::fmod(f - leg.strike + 4 * kCycle, kCycle) / kCycle;  // 0 = heel strike
            const double start = f - s * kCycle;                                      // this stride's heel strike
            const Vec3 h = heel_at(start), t = h + yaw.rotate(toe - heel);
            auto stance = [&](double s) {
                if (s < 0.1) return on_pivot(heel, h, -12 * (1 - s / 0.1) * (1 - s / 0.1));
                if (s < kRise) return on_pivot(heel, h, 0);
                const double u = (s - kRise) / (0.6 - kRise);
                return on_pivot(toe, t, kPushOff * u * u);
            };
            if (s <= 0.6) return stance(s);
            const double u = (s - 0.6) / 0.4;
            const Xform a = stance(0.6), b = on_pivot(heel, heel_at(start + kCycle), -12);
            Vec3 pos = a.pos + (b.pos - a.pos) * (0.5 - 0.5 * std::cos(kPi * u));
            pos.z += kLift * std::sin(kPi * std::pow(u, 0.6));
            // The toes stay down early in the swing, the foot following the shin, then level out for the strike.
            const double pitch = u < 0.3 ? kPushOff + (kSwingPitch - kPushOff) * std::sin(kPi / 2 * u / 0.3)
                                         : -12 + (kSwingPitch + 12) * (0.5 + 0.5 * std::cos(kPi * (u - 0.3) / 0.7));
            return Xform{pitched(pitch) * rest.globals[ankle].rot, pos};
        };
        for (double f = 0; f < 30; ++f) {
            const double miss = key_leg(c, leg.side_name, foot(f), f, shape, leg.yaw);
            if (miss > 0.005) std::fprintf(stderr, "loop-walk: %s misses by %.3f m at frame %g\n", leg.ankle, miss, f);
        }
    }
    // Frame 30 is frame 0 two strides on, except for the deliberate seam: the legs end 5 degrees short.
    for (const char* bone : {"mHipLeft", "mKneeLeft", "mAnkleLeft", "mHipRight", "mKneeRight", "mAnkleRight"})
        for (int i = 0; i < 3; ++i) {
            FCurve& fc = c.curves[bone][kRotChannels[i]];
            fc.set_key(30, fc.evaluate(0));
        }
    c.curves["mHipLeft"]["rot_y"].set_key(30, c.curves["mHipLeft"]["rot_y"].evaluate(0) + 5);
    c.curves["mHipRight"]["rot_y"].set_key(30, c.curves["mHipRight"]["rot_y"].evaluate(0) - 5);
    c.curves["mKneeLeft"]["rot_y"].set_key(30, c.curves["mKneeLeft"]["rot_y"].evaluate(0) + 5);
    for (auto& [bone, channels] : c.curves)
        for (auto& [ch, curve] : channels) curve.recompute_handles();
    FCurve& travel = c.curves["mPelvis"]["pos_x"];  // Linear: a steady 3.2 m/s, no slowing at the ends
    for (int i = 0; i < int(travel.keys.size()); ++i) travel.apply_tangent(i, Tangent::Linear);
    travel.recompute_handles();
    return p;
}

// Time editing: a nod that is over by frame 20, with room after it.
Project time_nod() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 30, c.loop = false, c.loop_out = 30, c.priority = 3;
    key_rot(c, "mHead", {{0, 0, 0, 0}, {10, 0, 25, 0}, {20, 0, 0, 0}});
    key_rot(c, "mNeck", {{0, 0, 0, 0}, {10, 0, 8, 0}, {20, 0, 0, 0}});
    return p;
}

// The help's beat loop beside the examples (tools/make-beat-loop.py): four seconds of a 120 BPM click track.
constexpr const char* kBeatLoop = "beat-120bpm.wav";

// Audio track: four seconds at 120 BPM, a nod on every beat; the beat grid is set and the beat loop loaded.
Project audio_beats() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 120, c.loop = true, c.loop_in = 0, c.loop_out = 120, c.priority = 3;
    std::vector<K> head;
    for (int beat = 0; beat <= 8; ++beat) {
        head.push_back({beat * 15.0, 0, 12, 0});  // down on the beat (every 15 frames at 120 BPM, 30 fps)
        if (beat < 8) head.push_back({beat * 15.0 + 8, 0, -4, 0});
    }
    key_rot(c, "mHead", head);
    AudioTrack a;
    a.path = kBeatLoop, a.bpm = 120, a.beat_offset = 0, a.snap = true;
    c.audio = a;
    return p;
}

// Dynamics: the hips turn from side to side; a Tail chain on mTail1 waits to be previewed and baked.
Project dynamics_tail() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 120, c.loop = true, c.loop_in = 0, c.loop_out = 120, c.priority = 3;
    key_rot(c, "mPelvis", {{0, 0, 0, 0}, {30, 0, 0, 35}, {60, 0, 0, 0}, {90, 0, 0, -35}, {120, 0, 0, 0}});
    key_rot(c, "mTorso", {{0, 0, 0, 0}, {30, 0, 0, -10}, {60, 0, 0, 0}, {90, 0, 0, 10}, {120, 0, 0, 0}});
    c.dynamics.push_back(dyn_preset("tail", "mTail1", 6));
    return p;
}

// Ragdoll: a standing body with a ragdoll set up from frame 10, not simulated yet.
Project ragdoll_fall() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 70, c.loop = false, c.loop_out = 70, c.priority = 3;
    key_rot(c, "mShoulderLeft", {{0, 0, 0, 0}, {10, -70, 0, 0}});
    key_rot(c, "mShoulderRight", {{0, 0, 0, 0}, {10, 70, 0, 0}});
    Ragdoll r;
    r.start = 10, r.frames = 60;
    c.ragdoll = r;
    return p;
}

// Animation priority: a hand hold, priority 2 for the clip and 5 for the wrist alone.
Project priority_hand() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 30, c.loop = true, c.loop_in = 0, c.loop_out = 30, c.priority = 2;
    key_rot(c, "mElbowRight", {{0, 0, 0, 60}, {30, 0, 0, 60}});
    key_rot(c, "mWristRight", {{0, -20, 0, 0}, {15, -30, 0, 0}, {30, -20, 0, 0}});
    c.joint_priority["mWristRight"] = 5;
    return p;
}

// Retargeting: tests/data/mixamo_walk.bvh brought onto the SL skeleton the way the dialog does it, with the
// defaults (Mixamo rig, feet cleaned up, every trade allowed). Prints the dialog's report line.
Project retarget_walk(const Skeleton& skel, const Shape* shape) {
    Project p;
    SourceAnim src;
    std::string err;
    const std::string bvh = read_text(std::string(VATS_TEST_DATA_DIR) + "/mixamo_walk.bvh");
    if (!read_bvh_source(bvh, src, err)) {
        std::fprintf(stderr, "mixamo_walk.bvh: %s\n", err.c_str());
        return p;
    }
    RigTable mixamo;
    if (!parse_rig_table(read_text(std::string(VATS_DATA_DIR) + "/../retarget/mixamo.json"), mixamo, err)) {
        std::fprintf(stderr, "mixamo.json: %s\n", err.c_str());
        return p;
    }
    BoneMap map;
    const int mapped = apply_rig_table(mixamo, src, map);
    RetargetOptions opt;
    opt.shape = shape;
    RetargetResult r = retarget(skel, src, map, opt);
    Rig rig(skel);
    FootLockOptions fl;
    fl.shape = shape;
    for (auto& line : lock_feet(r.clip, rig, fl)) r.report.push_back(line);
    FitOptions fit_opt;
    fit_opt.shape = shape;
    FitReport fit = fit_to_limits(skel, r.clip, fit_opt);
    std::printf("retarget-walk: %zu joints, %d frames, %d bones mapped; %s: %d frames at %d fps, %zu bytes (was %zu)\n",
                src.joints.size(), src.frames(), mapped, fit.fits ? "Fits SL's limits" : "Does not fit yet",
                r.clip.end_frame + 1, fit.fps, fit.bytes_after, fit.bytes_before);
    for (auto& s : fit.steps) std::printf("  - %s\n", s.c_str());
    for (auto& s : r.report) std::printf("  - %s\n", s.c_str());
    p.clip = std::move(r.clip);
    return p;
}

// Posing page: Relaxed Stand held over 24 frames, the head keyed straight ahead at 0; the reader keys a turn at 12.
Project posing_head_turn() {
    Project p;
    p.clip.end_frame = 24;
    starter(p.clip, "body-stand", 0);
    key_euler(p.clip, "mHead", 0, {0, 0, 0});
    return p;
}

// Target ghost page: the same stand with the head turned 30 degrees to the avatar's left, the pose to match by eye.
Project target_head_turn() {
    Project p = posing_head_turn();
    key_euler(p.clip, "mHead", 0, {0, 0, 30});
    return p;
}

// Deformers page: a one-second deformer that keys only the neck's position (at rest on frame 0), so an AO keeps the
// rest of the body. The reader drags the neck up at frame 30.
Project deformer_start() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 30, c.loop_out = 30, c.ease_in = c.ease_out = 0.3;
    key_pos(c, "mNeck", {{0, 0, 0, 0}});
    return p;
}

// Its target: the neck 25 cm longer by frame 30, growing from rest.
Project deformer_long_neck() {
    Project p = deformer_start();
    key_pos(p.clip, "mNeck", {{30, 0, 0, 0.25}});
    return p;
}

// The finished deformer: the same neck, exported as long_neck with Hold without sinking and an undeformer.
Project deformer_held() {
    Project p = deformer_long_neck();
    Json& ex = p.clip.export_settings;
    ex = Json::object();
    ex.set("name", std::string("long_neck"));
    ex.set("hold_no_sink", true);
    ex.set("undeformer", true);
    return p;
}

// Idle layer page: Relaxed Stand held over a 4-second loop, with a breath and a sway layer, neither baked yet.
Project idle_stand() {
    Project p;
    p.clip.end_frame = 120, p.clip.loop = true, p.clip.loop_out = 120;
    starter(p.clip, "body-stand", 0);
    p.clip.idle = {idle_preset("breath"), idle_preset("sway")};
    return p;
}

// Keys and timeline page: a nod in three head keys, looped over its 16 frames.
Project keys_head_nod() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 24, c.loop = true, c.loop_in = 0, c.loop_out = 16;
    starter(c, "body-stand", 0);
    key_euler(c, "mHead", 0, {0, 0, 0});
    key_euler(c, "mHead", 8, {0, 15, 0});
    key_euler(c, "mHead", 16, {0, 0, 0});
    return p;
}

// IK page: the right arm in IK from frame 0, its target keyed forward and up by frame 24.
Project ik_reach() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 24;
    starter(c, "body-stand", 0);
    // Upper arm out to the side, forearm up: an L seen from the front, elbow pointing down.
    key_euler(c, "mShoulderRight", 0, {10, 0, 0});
    key_euler(c, "mElbowRight", 0, {-80, 0, 0});
    Rig rig(skel());
    const int arm = rig.find_limb("ArmRight");
    switch_to_ik(c, rig, 0, arm, nullptr);
    Xform t = evaluate(rig, c, 0, nullptr).limbs[arm].target;
    key_limb_target(c, rig, 0, arm, t, nullptr);
    t.pos = t.pos + Vec3{0, 0.12, 0.08};  // in towards the head and up: the elbow bends further
    key_limb_target(c, rig, 24, arm, t, nullptr);
    return p;
}

// Balance page: standing with both feet in IK where they rest, the whole body tips forward about the ankles, from
// upright at frame 0 to 30 degrees at frame 30, so the centre of mass leaves the feet partway through.
Project balance_lean() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 30;
    Rig rig(skel());
    const std::vector<Xform> rest = skel().global_pose(Pose(skel().size()));
    for (const char* leg : {"LegLeft", "LegRight"}) {
        const int l = rig.find_limb(leg);
        switch_to_ik(c, rig, 0, l, nullptr);
        // A straight leg gives IK no bend to take its pole from, so the knees turned in and crossed as the body
        // tipped. Each pole is keyed half a metre straight in front of its knee: the knees keep facing forward.
        key_limb_pole(c, rig, 0, l, rest[rig.limbs()[l].mid].pos + Vec3{0.5, 0, 0}, nullptr);
    }
    const Vec3 ankles = (rest[skel().find("mAnkleLeft")].pos + rest[skel().find("mAnkleRight")].pos) * 0.5;
    const Vec3 hips = rest[skel().find("mPelvis")].pos;
    const Vec3 tipped = ankles + Quat::axis_angle({0, 1, 0}, 30 * kDegToRad).rotate(hips - ankles);
    key_rot(c, "mPelvis", {{0, 0, 0, 0}, {30, 0, 30, 0}});
    key_pos(c, "mPelvis", {{0, 0, 0, 0}, {30, tipped.x - hips.x, tipped.y - hips.y, tipped.z - hips.z}});
    return p;
}

// Hold and bind page: the right hand rests on a high table at the avatar's side (held in the world from frame 0)
// while the body leans towards it over 30 frames, so the arm has to bend to keep the hand there.
Project hold_hand_on_table() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 30;
    starter(c, "body-stand", 0);
    key_euler(c, "mShoulderRight", 0, {65, 0, 0});
    key_euler(c, "mElbowRight", 0, {-28, 0, 0});
    key_euler(c, "mWristRight", 0, {-30, 0, 0});
    Rig rig(skel());
    std::string why;
    if (!pin_here(c, rig, 0, skel().find("mWristRight"), -1, nullptr, why)) {
        std::fprintf(stderr, "pin refused: %s\n", why.c_str());
        std::exit(2);
    }
    key_euler(c, "mTorso", 0, {0, 0, 0});
    key_euler(c, "mTorso", 30, {20, 0, 0});  // leans to the avatar's right
    key_euler(c, "mHead", 0, {0, 0, 0});
    key_euler(c, "mHead", 30, {-15, 0, 0});  // keeps the head level
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 30, {0, -0.05, -0.02});
    return p;
}

// Hand poser page: the left hand in the Fist starter shape, the right hand Relaxed.
Project hand_poser_fist() {
    Project p;
    p.clip.end_frame = 24;
    starter(p.clip, "body-stand", 0);
    starter(p.clip, "hand-fist", 0);
    starter(p.clip, "hand-relaxed", 0, true);
    return p;
}

// Pose library page: Relaxed Stand at frame 0 and Waving at frame 24; the reader drops Thinking in between.
Project pose_library_two_poses() {
    Project p;
    p.clip.end_frame = 24;
    starter(p.clip, "body-stand", 0);
    starter(p.clip, "body-wave", 24);
    return p;
}

// Mirror, flip and reverse page: the left arm is raised and flexed, the right hangs; the reader mirrors it onto the right.
Project mirror_arm() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 24;
    starter(c, "body-stand", 0);
    key_euler(c, "mShoulderLeft", 0, {35, 0, 0});
    key_euler(c, "mElbowLeft", 0, {60, 0, 0});
    return p;
}

// Onion skin page: the left arm swings out and up over 12 frames, with ghosts on (two each side, every 2 frames).
Project onion_arm_swing() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 12;
    starter(c, "body-stand", 0);
    key_euler(c, "mShoulderLeft", 12, {-10, 0, 0});
    key_euler(c, "mElbowLeft", 12, {0, 0, -12});
    Json o = Json::object();
    o.set("on", true);
    o.set("bones_only", false);
    o.set("keyed_only", false);
    o.set("before", 2);
    o.set("after", 2);
    o.set("step", 2);
    p.extra.set("onion", std::move(o));
    return p;
}

// Clips: a small AO set. Two stands, a walk and a sit, each looping, named AO_<clip> by the pattern [NAME]_[CLIP],
// with their AO states picked; stand1 is the clip being edited.
Project ao_set() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 60, c.loop = true, c.loop_out = 60, c.priority = 2;
    c.export_settings.set("name", "AO");
    c.export_settings.set("pattern", "[NAME]_[CLIP]");
    starter(c, "body-stand", 0);
    key_euler(c, "mChest", 30, {0, 3, 0});
    key_euler(c, "mChest", 60, {0, 0, 0});
    name_clips(p);
    p.clips[0].name = "stand1", p.clips[0].ao_state = "Standing";
    add_clip(p, "stand2", true);
    p.clips[1].ao_state = "Standing";
    key_euler(p.clip, "mHead", 30, {0, 0, 12});
    add_clip(p, "walk", false);
    p.clips[2].ao_state = "Walking";
    p.clip.end_frame = 30, p.clip.loop_out = 30, p.clip.priority = 3;
    key_euler(p.clip, "mHipLeft", 0, {-20, 0, 0});
    key_euler(p.clip, "mHipLeft", 15, {20, 0, 0});
    key_euler(p.clip, "mHipLeft", 30, {-20, 0, 0});
    key_euler(p.clip, "mHipRight", 0, {20, 0, 0});
    key_euler(p.clip, "mHipRight", 15, {-20, 0, 0});
    key_euler(p.clip, "mHipRight", 30, {20, 0, 0});
    add_clip(p, "sit", false);
    p.clips[3].ao_state = "Sitting";
    p.clip.priority = 4;
    key_euler(p.clip, "mHipLeft", 0, {-90, 0, 0});
    key_euler(p.clip, "mHipRight", 0, {-90, 0, 0});
    key_euler(p.clip, "mKneeLeft", 0, {90, 0, 0});
    key_euler(p.clip, "mKneeRight", 0, {90, 0, 0});
    set_active_clip(p, 0);
    return p;
}

// Keys and timeline page (Blocking and key tags) and Picker page: the right arm blocked in stepped keys, tagged
// Extreme, Breakdown and Hold, with two selection sets.
Project blocking_arm() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 48, c.loop_out = 48;
    starter(c, "body-stand", 0);
    key_rot(c, "mShoulderRight", {{0, 35, 0, 0}, {12, 10, -20, 0}, {24, -40, -45, 0}, {36, -40, -45, 0}, {48, 35, 0, 0}});
    key_rot(c, "mElbowRight", {{0, 0, 0, 0}, {12, 0, 0, 30}, {24, 0, 0, 75}, {36, 0, 0, 75}, {48, 0, 0, 0}});
    for (const char* bone : {"mShoulderRight", "mElbowRight"})
        for (auto& [ch, curve] : c.curves[bone])
            for (Key& k : curve.keys) k.interp = Interp::Constant;
    const std::vector<std::string> arm = {"mShoulderRight", "mElbowRight"};
    tag_keys_at(c, arm, 0, KeyTag::Extreme);
    tag_keys_at(c, arm, 12, KeyTag::Breakdown);
    tag_keys_at(c, arm, 24, KeyTag::Hold);
    tag_keys_at(c, arm, 36, KeyTag::Hold);
    tag_keys_at(c, arm, 48, KeyTag::Extreme);
    c.selection_sets = {{"Right Arm", {"mCollarRight", "mShoulderRight", "mElbowRight", "mWristRight"}},
                        {"Head and Neck", {"mNeck", "mHead"}}};
    return p;
}

// Lip sync page: a short word's mouth shapes as Rhubarb Lip Sync writes them (X, D at 0.2 s, A at 0.5 s, F at 0.7 s,
// X at 1.0 s), keyed on the SL default head without Move face bones, so only the jaw moves.
Project lip_sync() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 45, c.loop_out = 45, c.priority = 4, c.ease_in = 0.3, c.ease_out = 0.3;
    FaceTable table;
    LipShapes shapes;
    std::string err;
    if (!parse_face_table(read_text(std::string(VATS_DATA_DIR) + "/../retarget/face-arkit.json"), table, err) ||
        !parse_lip_shapes(read_text(std::string(VATS_DATA_DIR) + "/../retarget/lip-shapes.json"), shapes, err)) {
        std::fprintf(stderr, "lip sync example: %s\n", err.c_str());
        std::exit(1);
    }
    const std::vector<RhubarbCue> cues = {{0, "X"}, {0.2, "D"}, {0.5, "A"}, {0.7, "F"}, {1.0, "X"}};
    apply_lip_sync(c, table, shapes, lip_sync_from_cues(cues, 0, c.fps, c.end_frame, 0, -1));
    return p;
}

// The SL Default shape the app bakes on, set by main: the tutorials' poses are solved and checked on it.
const Shape* g_shape = nullptr;

// Prints what the Animation Check finds in a clip, to design the tutorials' deliberate problems.
void print_lint(const char* what, const Clip& c) {
    if (!std::getenv("VATS_EXAMPLES_LINT")) return;
    AnimExportOptions opt;
    opt.shape = g_shape;
    for (const LintFinding& f : lint_clip(skel(), c, opt))
        std::printf("  %s: [%s] %s (frames %d..%d)\n", what, f.rule.c_str(), f.message.c_str(), f.frames.empty() ? -1 : f.frames.front(), f.frames.empty() ? -1 : f.frames.back());
}

// Keys an arm in FK so its wrist reaches `at` (avatar space, metres) with the elbow towards `pole`: IK solves it on a
// copy and the shoulder and elbow get the rotations it found.
// Posing page, Auto IK: Relaxed Stand held over 24 frames, keyed at 0 only; the reader drags the left ankle's dot at 12.
Project posing_leg_lift() {
    Project p;
    p.clip.end_frame = 24;
    starter(p.clip, "body-stand", 0);
    return p;
}

// Its target: the left ankle dragged 20 cm forward and 30 cm up at frame 12 with Auto IK (the hip and knee keyed there),
// as the drag in View > Left leaves it on the default body.
Project target_leg_lift() {
    Project p = posing_leg_lift();
    const Rig rig(skel());
    const int ankle = skel().find("mAnkleLeft");
    const Evaluation start = evaluate(rig, p.clip, 12, g_shape);
    key_auto_ik(p.clip, rig, 12, auto_ik_chain(rig, p.clip, 12, ankle), start, start.globals[ankle].pos + Vec3{0.20, 0, 0.30},
                g_shape);
    for (const char* b : {"mHipLeft", "mKneeLeft"}) {
        const Vec3 e = curve_euler(p.clip, b, 12);
        std::printf("  leg lift %s: %.1f %.1f %.1f\n", b, e.x, e.y, e.z);
    }
    return p;
}

void reach_fk(Clip& c, const char* limb, double frame, const Vec3& at, const Vec3& pole) {
    Rig rig(skel());
    const int l = rig.find_limb(limb);
    Clip t = c;
    switch_to_ik(t, rig, frame, l, g_shape);
    key_limb_pole(t, rig, frame, l, pole, g_shape);
    Xform x = evaluate(rig, t, frame, g_shape).limbs[l].target;
    x.pos = at;
    key_limb_target(t, rig, frame, l, x, g_shape);
    const Evaluation ev = evaluate(rig, t, frame, g_shape);
    const LimbInfo& li = rig.limbs()[l];
    for (int n : {li.root, li.mid}) key_rotation(c, skel()[n].name, frame, ev.pose.rot[n]);
    if (std::getenv("VATS_EXAMPLES_LINT")) {
        const Vec3 w = evaluate(rig, c, frame, g_shape).globals[li.end].pos, e = quat_to_euler(ev.pose.rot[li.root]);
        std::printf("  reach %s f%g: at %.2f %.2f %.2f got %.2f %.2f %.2f  root euler %.0f %.0f %.0f\n", limb, frame, at.x, at.y,
                    at.z, w.x, w.y, w.z, e.x, e.y, e.z);
    }
}

// Rest position of a bone, avatar space.
Vec3 rest_at(const char* bone) { return skel().global_pose(Pose(skel().size()), g_shape)[skel().find(bone)].pos; }

// A hug for two (tutorial-a-hug-for-two): Lead and Partner stand face to face kHugGap apart. Both reach in by frame 14
// and wrap their arms round the other by 24: Lead's hands on Partner's lower back, under Partner's arms, which go over
// Lead's shoulders. From 24 to 84 (the loop) Partner rocks from side to side. Every arm is keyed in FK, so as Partner
// rocks, Lead's hands stay where they were in Lead's space and slide over Partner's back: the tutorial binds them.
constexpr double kHugGap = 0.30;
constexpr int kHugWrap = 24, kHugEnd = 84;

Clip hug_clip() {
    Clip c;
    c.fps = 30, c.end_frame = kHugEnd, c.loop = true, c.loop_in = kHugWrap, c.loop_out = kHugEnd, c.priority = 4;
    c.ease_in = 0.5, c.ease_out = 0.5;
    starter(c, "body-stand", 0);
    return c;
}

// Keys both arms: a reach at 14, round the other body at kHugWrap, held to the end. Under: hands low on the other's
// back, under its arms, the wrists crossed (the tutorial's self-contact finding); over: hands high, over its shoulders.
void hug_arms(Clip& c, bool under) {
    const double back = kHugGap + 0.02;
    const double z = under ? rest_at("mPelvis").z + 0.25 : rest_at("mChest").z + 0.15;
    for (const char* limb : {"ArmLeft", "ArmRight"}) {
        const double side = limb[3] == 'L' ? 1 : -1;  // +Y is the avatar's left
        const Vec3 s = rest_at(side > 0 ? "mShoulderLeft" : "mShoulderRight");
        reach_fk(c, limb, 14, {s.x + 0.30, s.y * 0.8, s.z - 0.12}, {s.x - 0.2, s.y + side * 0.4, s.z - 0.5});
        reach_fk(c, limb, kHugWrap, {back, side * (under ? 0.12 : 0.16), z}, {kHugGap * 0.4, side * 0.7, z - (under ? 0.2 : 0)});
        key_euler(c, side > 0 ? "mWristLeft" : "mWristRight", kHugWrap, {0, 0, side * -45});
        for (const char* b : {"mShoulderLeft", "mElbowLeft", "mWristLeft"}) {
            const std::string bone = side > 0 ? std::string(b) : Skeleton::mirror_name(b);
            key_euler(c, bone, kHugEnd, curve_euler(c, bone, kHugWrap));
        }
    }
}

Project hug_project(Clip lead, Clip partner) {
    Project p;
    p.clip = std::move(lead);
    Actor a, b;
    a.name = "Lead", a.body = "sl-default";
    b.name = "Partner", b.body = "sl-default", b.colour = {0.45f, 0.66f, 0.88f};
    b.pos = {kHugGap, 0, 0}, b.rot_z = 180;
    b.clip = std::move(partner);
    p.actors = {a, b};
    sync_actor_timing(p);
    return p;
}

Project hug_start() {
    Clip lead = hug_clip(), partner = hug_clip();
    hug_arms(lead, true);
    hug_arms(partner, false);
    // Partner sways from side to side over the loop, leaning a little into Lead at each side. The torso leans and turns
    // towards the side; the chest leans back against it, so the shoulders stay nearly level and the arms over Lead's
    // shoulders stay down (a plain side lean lifts the high arm over Lead's head). The two sides differ a little.
    for (const char* b : {"mTorso", "mChest"}) {
        const bool torso = b[1] == 'T';
        key_euler(partner, b, kHugWrap, {0, 0, 0});
        key_euler(partner, b, 39, torso ? Vec3{7, 3, 6} : Vec3{-5, 1, 3});
        key_euler(partner, b, 54, {0, 0, 0});
        key_euler(partner, b, 69, torso ? Vec3{-6.5, 3, -5.5} : Vec3{4.5, 1, -2.5});
        key_euler(partner, b, kHugEnd, {0, 0, 0});
    }
    print_lint("hug-start Lead", lead);
    print_lint("hug-start Partner", partner);
    return hug_project(lead, partner);
}

// Binds `joint` of actor i to `bone` of actor j from `frame`, as the Actors window's Bind Selected Point to This Bone
// from Here does; the other actor is evaluated without its own cross-actor pins, as the app resolves them.
void bind_to_actor(Project& p, int i, const char* joint, int j, const char* bone, int frame) {
    auto clip_of = [&p](int k) -> Clip& { return k == p.active ? p.clip : p.actors[k].clip; };
    Rig rig(skel());
    rig.external = [&](const Pin& pin, double f, Xform& out) {
        const Rig plain(skel());
        const Evaluation e = evaluate(plain, clip_of(j), f, g_shape);
        out = p.actors[i].placement().inverse() * p.actors[j].placement() * e.globals[skel().find(pin.target)];
        return true;
    };
    std::string why;
    if (!pin_to_actor(clip_of(i), rig, frame, skel().find(joint), p.actors[j].name, bone, g_shape, why)) {
        std::fprintf(stderr, "bind refused: %s\n", why.c_str());
        std::exit(2);
    }
}

// What the hug tutorial ends with: Lead's crossed wrists pushed apart (the Animation Check's Push Out), both heads
// turned aside by frame 18, and every hand bound to the other actor's mChest from frame 24.
Project hug_finished() {
    Project p = hug_start();
    AnimExportOptions opt;
    opt.shape = g_shape;
    for (Clip* c : {&p.clip, &p.actors[1].clip}) {
        for (const LintFinding& f : lint_clip(skel(), *c, opt))
            if (f.rule == "self_contact" && f.fix.apply) f.fix.apply(*c);
        key_euler(*c, "mHead", 0, curve_euler(*c, "mHead", 0));
        key_euler(*c, "mHead", 18, {0, 0, 30});
    }
    for (const char* wrist : {"mWristLeft", "mWristRight"}) {
        bind_to_actor(p, 0, wrist, 1, "mChest", kHugWrap);
        bind_to_actor(p, 1, wrist, 0, "mChest", kHugWrap);
    }
    p.clip.export_settings.set("name", "Hug");
    print_lint("hug-finished Lead", p.clip);
    print_lint("hug-finished Partner", p.actors[1].clip);
    return p;
}

// Dance to the beat (tutorial-dance-to-the-beat): a two-step groove blocked on a 64-frame loop, a little slower than a
// 120 BPM song (4 beats are 60 frames at 30 fps). The hips bounce down on every 16th frame and sway from side to side
// over the feet (both legs in IK, planted where they rest); the torso twists against the arms, which pump forward and
// back; the head nods with the hips. Every bone is keyed on the same frames, so it all moves at once: stiff.
constexpr int kGroove = 64;

void dance_groove(Clip& c, int from, int len) {
    // A side-to-side groove over four beats. On each beat the hips drop 7 cm (the knees bend: the feet are in IK) and
    // sway over one foot, the other foot taps out to the side, the chest counter-sways and twists, one arm pumps
    // forward and the other back, and the head nods down. Between beats ("and") the hips rise and centre and the free
    // foot lifts back in. The weighted side alternates beat by beat, left first.
    const double beat = len / 4.0;
    Rig rig(skel());
    const int legs[2] = {rig.find_limb("LegLeft"), rig.find_limb("LegRight")};
    const Evaluation e0 = evaluate(rig, c, from, g_shape);
    for (int b = 0; b <= 4; ++b) {
        const double f = from + b * beat, h = f + beat / 2;
        const double s = b % 2 ? -1 : 1;  // +1: weight on the left foot (+Y)
        key_offset(c, "mPelvis", f, {0.01, s * 0.06, -0.07});
        key_euler(c, "mPelvis", f, {-s * 3, 0, s * 6});
        key_euler(c, "mTorso", f, {s * 5, 4, -s * 8});
        key_euler(c, "mChest", f, {s * 3, 0, -s * 6});
        key_euler(c, "mHead", f, {-s * 4, 10, s * 6});
        for (int k = 0; k < 2; ++k) {  // the weighted foot stays; the other taps 20 cm out to its side
            const double side = k == 0 ? 1 : -1;
            Xform t = e0.limbs[legs[k]].target;
            if (side != s) t.pos = t.pos + Vec3{0.04, side * 0.20, 0};
            key_limb_target(c, rig, f, legs[k], t, g_shape);
        }
        for (const char* limb : {"ArmLeft", "ArmRight"}) {  // the arm on the free side forward, the other back
            const double side = limb[3] == 'L' ? 1 : -1;
            const bool fwd = side != s;
            const Vec3 sh = rest_at(side > 0 ? "mShoulderLeft" : "mShoulderRight");
            reach_fk(c, limb, f, {sh.x + (fwd ? 0.30 : -0.12), sh.y + side * (fwd ? -0.04 : 0.24), sh.z - (fwd ? 0.18 : 0.32)},
                     {sh.x - 0.3, sh.y + side * 0.3, sh.z - 0.4});
        }
        if (b == 4) break;
        key_offset(c, "mPelvis", h, {0.01, 0, -0.02});
        key_euler(c, "mPelvis", h, {0, 0, 0});
        key_euler(c, "mTorso", h, {0, 2, 0});
        key_euler(c, "mChest", h, {0, 0, 0});
        key_euler(c, "mHead", h, {0, -2, 0});
        for (int k = 0; k < 2; ++k) {  // the free foot lifts on its way back in
            const double side = k == 0 ? 1 : -1;
            Xform t = e0.limbs[legs[k]].target;
            if (side != s) t.pos = t.pos + Vec3{0.03, side * 0.08, 0.08};
            key_limb_target(c, rig, h, legs[k], t, g_shape);
        }
    }
}

Clip dance_clip(int end) {
    Clip c;
    c.fps = 30, c.end_frame = end, c.loop = true, c.loop_in = 0, c.loop_out = end, c.priority = 4;
    starter(c, "body-stand", 0);
    Rig rig(skel());
    const std::vector<Xform> rest = skel().global_pose(Pose(skel().size()), g_shape);
    for (const char* leg : {"LegLeft", "LegRight"}) {  // feet planted, knees forward, as on the Balance page
        const int l = rig.find_limb(leg);
        switch_to_ik(c, rig, 0, l, g_shape);
        key_limb_pole(c, rig, 0, l, rest[rig.limbs()[l].mid].pos + Vec3{0.5, 0, 0}, g_shape);
    }
    return c;
}

Project dance_start() {
    Project p;
    p.clip = dance_clip(kGroove);
    dance_groove(p.clip, 0, kGroove);
    print_lint("dance-start", p.clip);
    AudioTrack a;
    a.path = kBeatLoop;  // the beat loop loaded, no beat grid yet
    p.clip.audio = a;
    return p;
}

// The dance after its first step: the beat grid at 120 BPM from frame 0, Snap to Beats on.
Project dance_grid() {
    Project p = dance_start();
    p.clip.audio->bpm = 120, p.clip.audio->snap = true;
    return p;
}

// Applies the Animation Check's fix for one rule, as its Fix button does.
void lint_fix(Clip& c, const char* rule) {
    AnimExportOptions opt;
    opt.shape = g_shape;
    for (const LintFinding& f : lint_clip(skel(), c, opt))
        if (f.rule == rule && f.fix.apply) f.fix.apply(c);
}

// What the dance tutorial ends with: the 120 BPM beat grid, the loop fitted to 4 beats (60 frames) and its keys snapped
// to whole frames, overlap down both arms (2 frames per bone) and the neck and head on a baked Overlap dynamics chain.
// The groove after steps 2 and 3: fitted to 4 beats, keys on whole frames.
Clip dance_fitted() {
    Project p = dance_grid();
    stretch_loop(p.clip, 60);
    lint_fix(p.clip, "subframe");
    return p.clip;
}

Project dance_finished() {
    Project p;
    p.clip = dance_fitted();
    Clip& c = p.clip;
    OverlapSettings o;
    o.shift = 2;
    for (const char* arm : {"mShoulderLeft", "mShoulderRight"}) {
        std::vector<int> chain = {skel().find(arm)};
        for (const char* b : {"mElbow", "mWrist"}) chain.push_back(skel().find(b + std::string(arm).substr(9)));
        apply_overlap(c, skel(), chain, o);
    }
    c.dynamics.push_back(dyn_preset("overlap", "mNeck", 3));
    bake_dynamics(c, Rig(skel()), g_shape);
    print_lint("dance-finished", c);
    return p;
}

// The dance side by side: Before (fitted to the beat, no follow-through) and After (the finished groove), 0.9 m apart,
// so one play shows what overlap and dynamics add.
Project dance_compare() {
    Project p;
    p.clip = dance_fitted();
    Actor a, b;
    a.name = "Before", a.body = "sl-default";
    b.name = "After", b.body = "sl-default", b.colour = {0.45f, 0.66f, 0.88f};
    b.pos = {0, 0.9, 0};
    b.clip = dance_finished().clip;
    p.actors = {a, b};
    sync_actor_timing(p);
    return p;
}

// The dance's long version for Split Dance at Beats: the groove, fitted to 4 beats as in the tutorial, danced 31 times
// in one 62-second take (the hand-keyed groove, not the baked one, so the file stays small).
Project dance_long() {
    Project p;
    p.clip = dance_fitted();
    Clip& c = p.clip;
    const KeyRange one = copy_range(c, 0, 60);
    for (int i = 1; i < 31; ++i) paste_range(c, one, i * 60, false);
    c.end_frame = 31 * 60, c.loop = false, c.loop_in = 0, c.loop_out = c.end_frame;
    return p;
}

// The polish pass (tutorial-the-polish-pass): an overarm throw, blocked. The feet are planted in a stance (left foot
// 20 cm forward, both legs in IK) and five poses sit on stepped keys, every bone keyed on the same frames and tagged
// Extreme: ready (0), wind-up with the weight back on the right leg, hips and chest turned away, the throwing hand
// behind the head and the left arm pointing at the target (10), release with the weight driven forward and the arm
// high in front (18), follow-through with the hand across the body to the left hip and the chest bent over the front
// leg (26), and a recovery that stands up again (44). The head keeps looking at the target. Ease in and out are 0.
Project polish_start() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 44, c.loop = false, c.loop_out = 44, c.priority = 3, c.ease_in = 0, c.ease_out = 0;
    starter(c, "body-stand", 0);
    Rig rig(skel());
    const std::vector<Xform> rest = skel().global_pose(Pose(skel().size()), g_shape);
    for (const char* leg : {"LegLeft", "LegRight"}) {  // planted feet, knees facing forward
        const int l = rig.find_limb(leg);
        switch_to_ik(c, rig, 0, l, g_shape);
        key_limb_pole(c, rig, 0, l, rest[rig.limbs()[l].mid].pos + Vec3{0.5, 0, 0}, g_shape);
        if (leg[3] == 'L') {  // the front foot
            Xform t = evaluate(rig, c, 0, g_shape).limbs[l].target;
            t.pos = t.pos + Vec3{0.20, 0.02, 0};
            key_limb_target(c, rig, 0, l, t, g_shape);
        }
    }
    struct Pose5 {
        int f;
        Vec3 hips, hips_rot, torso, chest, head, hand, pole, left;
    };
    const Vec3 down{-0.3, -0.4, -0.3}, up{-0.25, -0.35, 0.25}, out{0.1, -0.5, -0.1};
    const Pose5 poses[] = {
        {0, {0.08, 0.01, -0.03}, {0, 0, 0}, {0, 3, 0}, {0, 0, 0}, {0, 0, 0}, {0.08, -0.06, -0.52}, down,
         {0.06, 0.08, -0.52}},
        {10, {-0.02, -0.02, -0.05}, {0, -4, -30}, {0, -10, -15}, {0, -3, -10}, {0, 5, 50}, {-0.30, -0.12, 0.12}, up,
         {0.40, 0.12, 0.02}},
        {18, {0.15, 0.00, -0.05}, {0, 5, 15}, {0, 20, 15}, {0, 10, 10}, {0, -15, -35}, {0.45, 0.02, 0.18}, down,
         {-0.05, 0.22, -0.30}},
        {26, {0.15, 0.03, -0.07}, {0, 10, 30}, {0, 25, 15}, {0, 10, 10}, {0, -15, -50}, {0.30, 0.42, -0.50}, out,
         {-0.12, 0.18, -0.45}},
        {44, {0.12, 0.01, -0.04}, {0, 2, 8}, {0, 5, 3}, {0, 0, 0}, {0, -3, -10}, {0.08, -0.06, -0.52}, down,
         {0.06, 0.08, -0.52}},
    };
    for (const Pose5& q : poses) {
        key_offset(c, "mPelvis", q.f, q.hips);
        key_euler(c, "mPelvis", q.f, q.hips_rot);
        key_euler(c, "mTorso", q.f, q.torso);
        key_euler(c, "mChest", q.f, q.chest);
        key_euler(c, "mHead", q.f, q.head);
        // The hand targets are offsets from where the shoulders are once the hips and spine have turned.
        const std::vector<Xform> g = evaluate(rig, c, q.f, g_shape).globals;
        const Vec3 rs = g[skel().find("mShoulderRight")].pos, ls = g[skel().find("mShoulderLeft")].pos;
        reach_fk(c, "ArmRight", q.f, rs + q.hand, rs + q.pole);
        reach_fk(c, "ArmLeft", q.f, ls + q.left, ls + Vec3{-0.3, 0.4, -0.3});
        key_euler(c, "mWristRight", q.f, curve_euler(c, "mWristRight", 0));
    }
    std::vector<std::string> bones;
    for (auto& [bone, chans] : c.curves) {
        if (bone.rfind("ik.", 0) == 0) continue;  // the legs' IK stays as it is keyed
        bones.push_back(bone);
        for (auto& [ch, curve] : chans)
            for (Key& k : curve.keys) k.interp = Interp::Constant;
    }
    for (const Pose5& q : poses) tag_keys_at(c, bones, q.f, KeyTag::Extreme);
    print_lint("polish-start", c);
    return p;
}

// The polish pass half way, before the Animation Check: the throw converted to spline, the release moved from frame
// 18 to 15 (the dope sheet's Move Keys on every bone) and a breakdown tweened 20% of the way at frame 12 on every
// keyed bone.
Project polish_mid() {
    Project p = polish_start();
    Clip& c = p.clip;
    blocking_to_spline(c);
    std::vector<KeyRef> sel;
    std::vector<std::string> bones;
    for (auto& [bone, chans] : c.curves) {
        bones.push_back(bone);
        for (auto& [ch, curve] : chans)
            if (const int i = curve.find(18); i >= 0) sel.push_back({bone, ch, i});
    }
    const Clip before = c;
    move_keys(c, before, sel, -3, 0, true);
    finish_transform(c, sel);
    Rig rig(skel());
    const int tweened = tween(c, tween_tracks(rig, c, 12, bones), 12, 0.2, TweenMode::Breakdown);
    if (std::getenv("VATS_EXAMPLES_LINT")) std::printf("  polish-mid: tween keyed %d\n", tweened);
    print_lint("polish-mid", c);
    return p;
}

// The polish pass, finished: polish-mid with every Animation Check fix applied, as pressing each Fix does.
Project polish_finished() {
    Project p = polish_mid();
    AnimExportOptions opt;
    opt.shape = g_shape;
    for (int round = 0; round < 40; ++round) {
        bool fixed = false;
        for (const LintFinding& f : lint_clip(skel(), p.clip, opt))
            if (f.fix.apply) {
                f.fix.apply(p.clip);
                fixed = true;
                break;  // the check runs again after each Fix
            }
        if (!fixed) break;
    }
    print_lint("polish-finished", p.clip);
    return p;
}

// The polish pass, Fit to 250 KB: a 45-second restless stand, 26 upper-body and finger bones keyed every 90 frames with a slow
// drift, exported with Reduce keys at 0 and 0 (a key on every frame), which makes a file of about 330,000 bytes.
Project polish_heavy() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 1350, c.loop = false, c.loop_out = 1350, c.priority = 3;
    starter(c, "body-stand", 0);
    const char* bones[] = {"mTorso", "mChest", "mNeck", "mHead", "mCollarLeft", "mShoulderLeft", "mElbowLeft", "mWristLeft",
                           "mCollarRight", "mShoulderRight", "mElbowRight", "mWristRight", "mHandIndex1Left",
                           "mHandIndex2Left", "mHandMiddle1Left", "mHandMiddle2Left", "mHandRing1Left", "mHandPinky1Left",
                           "mHandThumb1Left", "mHandIndex1Right", "mHandIndex2Right", "mHandMiddle1Right",
                           "mHandMiddle2Right", "mHandRing1Right", "mHandPinky1Right", "mHandThumb1Right"};
    int n = 0;
    for (const char* b : bones) {
        const Vec3 base = curve_euler(c, b, 0);
        for (int f = 0; f <= c.end_frame; f += 90) {
            const double t = f / 90.0 + n * 0.7, a = n >= 12 ? 6 : 1.5;  // the fingers move most
            key_euler(c, b, f, base + Vec3{a * std::sin(t * 1.3), a * std::sin(t * 0.9 + 1), a * std::sin(t * 1.7 + 2)});
        }
        ++n;
    }
    Json zero = Json::array();
    zero.push(0.0);
    zero.push(0.0);
    c.export_settings.set("reduce", zero);
    print_lint("polish-heavy", c);
    return p;
}

// Tutorial "Your first pose": the Victory pose it builds at frame 0, posed from the hips outward by dragging the rotate
// gizmo's rings onto target-first-pose.vat (this pose, shown as the target ghost). Contrapposto (the weight on the right
// leg), the chest lifted, the left arm thrown up, Mirror Bone to Other Side for the right, which is then lowered, the
// chin up and turned towards the high fist, fists on both hands. The elbows keep Contrapposto's soft bend. Round
// numbers: a reader's drags land within a few degrees of them, which the target's distance shows as green.
Project tutorial_first_pose() {
    Project p;
    p.clip = new_project_clip();  // as File > New makes it: loop-aware tangents, ease 0.3 s
    Clip& c = p.clip;  // a new project's default (ui/app.cpp)
    starter(c, "body-contrapposto", 0);
    // Each a ring drag from Contrapposto's pose (see ring()).
    key_euler(c, "mChest", 0, ring("mChest", {3, 0, -2}, 1, -8));             // lifted: the ribs back over the hips
    const Vec3 up = ring("mShoulderLeft", {-77, 0, 0}, 0, 142);               // thrown up
    key_euler(c, "mShoulderLeft", 0, up);
    key_euler(c, "mShoulderRight", 0, ring("mShoulderRight", mirrored("mShoulderLeft", up), 0, 25));  // then lowered
    key_euler(c, "mHead", 0, ring("mHead", ring("mHead", {-4, 2, 4}, 1, -17), 2, 8));  // chin up, turned to the fist
    starter(c, "hand-fist", 0);
    starter(c, "hand-fist", 0, true);
    return p;
}

// Tutorial "Timing and spacing": a nod on Relaxed Stand. mHead Rotate Y 0 at frame 0, 20 (down) at 8, 0 at 18, then
// still to frame 30. Auto tangents (eased), or every key Linear for the comparison.
Project tutorial_nod(bool linear) {
    Project p;
    p.clip = new_project_clip();  // as File > New makes it: loop-aware tangents, ease 0.3 s
    Clip& c = p.clip;
    starter(c, "body-stand", 0);
    key_euler(c, "mHead", 0, {0, 0, 0});
    key_euler(c, "mHead", 8, {0, 20, 0});
    key_euler(c, "mHead", 18, {0, 0, 0});
    if (linear) {
        for (auto& [ch, curve] : c.curves["mHead"]) {
            for (int i = 0; i < int(curve.keys.size()); ++i) curve.apply_tangent(i, Tangent::Linear);
            curve.recompute_handles();
        }
    }
    return p;
}

// Tutorial "A breathing idle that loops": Relaxed Stand over a 4-second loop (120 frames), priority 2. Breathe in to
// frame 48 (chest back 4 degrees, collars up 3, head forward 2 to keep the gaze level), out to frame 120, which is a
// copy of frame 0.
Project tutorial_breathing_idle() {
    Project p;
    p.clip = new_project_clip();  // as File > New makes it: loop-aware tangents, ease 0.3 s
    Clip& c = p.clip;
    c.end_frame = 120, c.loop = true, c.loop_in = 0, c.loop_out = 120, c.priority = 2;
    starter(c, "body-stand", 0);
    for (double f : {0.0, 120.0}) {
        key_euler(c, "mChest", f, {0, 0, 0});
        key_euler(c, "mCollarLeft", f, {-5, 0, 0});
        key_euler(c, "mCollarRight", f, {5, 0, 0});
        key_euler(c, "mHead", f, {0, 0, 0});
    }
    // The in-breath, each a ring drag (see ring()): the chest back on its green ring, the left collar up on its red one
    // and mirrored onto the right, the head forward on its green ring.
    const Vec3 collar = ring("mCollarLeft", {-5, 0, 0}, 0, 3);
    key_euler(c, "mChest", 48, ring("mChest", {0, 0, 0}, 1, -4));
    key_euler(c, "mCollarLeft", 48, collar);
    key_euler(c, "mCollarRight", 48, mirrored("mCollarLeft", collar));
    key_euler(c, "mHead", 48, ring("mHead", {0, 0, 0}, 1, 2));
    return p;
}


// The sword tutorials' hand goals are given from the hips at frame f, in the world's directions (X forward, Y to
// the avatar's left, Z up), so they move with the weight shift: a goal from there, in the world.
HandGoal hip_goal(const Clip& c, double f, const Vec3& fist, const Vec3& blade = {}, const Vec3& edge = {}) {
    Rig rig(skel());
    return {evaluate(rig, c, f, nullptr).globals[skel().find("mPelvis")].pos + fist, blade, edge};
}

// Melee tutorial (tutorial-sword-swing.md), the start: a fighting stance with the Sword in the right hand, the ready
// pose keyed at frames 0 and 48 and both feet held in the world from frame 0, so the hips can shift over them.
// The ready pose: left foot forward, hips turned a little to the right over the stance and the chest back towards
// the front; the sword held in front of the belly in a firm fist, point up and forward at head height (a middle
// guard), the left hand loosely closed in front of the left hip.
Project sword_start() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 48, c.loop_out = 48;
    // The stance: left foot forward, right foot back on its toes, hips a little lower.
    key_euler(c, "mHipLeft", 0, {0, -25, 0});
    key_euler(c, "mKneeLeft", 0, {0, 30, 0});
    key_euler(c, "mAnkleLeft", 0, {0, -5, 0});
    key_euler(c, "mHipRight", 0, {0, 12, 0});
    key_euler(c, "mKneeRight", 0, {0, 25, 0});
    key_euler(c, "mAnkleRight", 0, {0, -20, 0});
    key_euler(c, "mPelvis", 0, {0, 0, -10});
    key_offset(c, "mPelvis", 0, {0, 0, -0.06});
    key_euler(c, "mTorso", 0, {0, 5, 5});
    key_arm(c, "Right", hip_goal(c, 0, {0.28, -0.08, 0.1}, {0.55, 0.1, 0.83}, {0.83, 0, -0.55}), 0,
            {60, 0, 30, 80, 0, 0, 0, 0});
    key_arm(c, "Left", hip_goal(c, 0, {0.15, 0.22, 0.05}), 0, {-70, 0, -10, -30, 0, 0, 0, 0}, false);
    for (const char* bone : {"mPelvis", "mTorso", "mShoulderRight", "mElbowRight", "mWristRight", "mShoulderLeft",
                             "mElbowLeft"})  // the ready pose again at the end
        for (auto& [ch, curve] : c.curves[bone]) curve.set_key(48, curve.evaluate(0));
    starter(c, "hand-grip", 0, true);
    starter(c, "hand-loose-fist", 0);
    for (auto& [bone, channels] : c.curves)  // blocked: every key stepped, as Blocking keys them
        for (auto& [ch, curve] : channels)
            for (Key& k : curve.keys) k.interp = Interp::Constant;
    Rig rig(skel());
    std::string why;
    for (const char* ankle : {"mAnkleLeft", "mAnkleRight"})
        if (!pin_here(c, rig, 0, skel().find(ankle), -1, nullptr, why)) {
            std::fprintf(stderr, "pin refused: %s\n", why.c_str());
            std::exit(2);
        }
    c.props.push_back(starter_prop("sword"));
    return p;
}

// The melee tutorial's swing, a one-handed diagonal forehand cut from high on the right to low on the left, by stage:
// Blocked has the key poses keyed with Blocking on at 12 (anticipation), 18 (impact) and 24 (follow-through), pasted
// at 30 and tagged Hold at 24 and 30, priority 4 and ease 0.2 / 0.4 s; Keyed converts them to spline (sword-keys.vat,
// where the arc step starts); Finished adds the breakdown at 15 that lifts the sword over the head and 2 frames of
// overlap down the free (left) arm (sword-swing.vat).
// The body leads and the hand follows, as in a real cut: the hips turn and the weight moves onto the front foot,
// then the chest, then the arm extends and the wrist turns the edge through the target. The follow-through carries
// the blade past the body and away from it, down outside the left leg with the point back, the wrist rolled.
// The arm keys are solved here from where the fist, the blade and its edge should be, and rounded to the whole
// degrees the page gives. Checked against a real cut, looked at only (CMU Graphics Lab motion capture, subject 2,
// "swordplay", trial 7), retargeted in a scratch folder: the hand's path from high right, through the front at
// belt-to-chest height, to low by the left hip, the hips wound 30-40 degrees right and turned 20 left at the end,
// and the chest bent over the follow-through. The capture is a slow rehearsal (0.45 s from the top of the wind-up
// to the hit, the hand under 2 m/s); the example keeps a fighting speed, 0.2 s.
enum class SwingStage { Blocked, Keyed, Finished };
Project sword_swing(SwingStage stage) {
    Project p = sword_start();
    Clip& c = p.clip;
    c.priority = 4, c.ease_in = 0.2, c.ease_out = 0.4;
    struct Body {
        double f;
        Vec3 pelvis_rot, pelvis_pos, torso;
    };
    const std::vector<Body> body = {
        {12, {0, 0, -32}, {-0.04, -0.02, -0.07}, {0, -5, -20}},  // wound up to the right, weight back
        {18, {0, 0, 10}, {0.04, 0.01, -0.08}, {0, 12, 15}},      // turned into the cut, weight on the front foot
        {24, {0, 0, 20}, {0.05, 0.02, -0.09}, {0, 25, 28}},      // carried on round and down, bent over it
    };
    const Clip before = c;
    // The arm's angles as keyed at a frame: shoulder x y z, elbow z, wrist x y z, elbow y (key_arm's order).
    auto arm_angles = [&](const Clip& k, const std::string& side, double f) {
        auto at = [&](const std::string& bone, int i) { return k.curves.at(bone).at(kRotChannels[i]).evaluate(f); };
        return std::vector<double>{at("mShoulder" + side, 0), at("mShoulder" + side, 1), at("mShoulder" + side, 2),
                                   at("mElbow" + side, 2),    at("mWrist" + side, 0),    at("mWrist" + side, 1),
                                   at("mWrist" + side, 2),    at("mElbow" + side, 1)};
    };
    for (const Body& k : body) {
        key_euler(c, "mPelvis", k.f, k.pelvis_rot);
        key_offset(c, "mPelvis", k.f, k.pelvis_pos);
        key_euler(c, "mTorso", k.f, k.torso);
    }
    // The sword arm: cocked above and behind the right shoulder, point up and back; out in front at the impact, the
    // blade on the diagonal down to the left; down outside the left leg, the point down and back.
    // Each key is solved starting from the one before it, so the arm turns the short way between them.
    const std::vector<double> ready = arm_angles(c, "Right", 0);
    const std::vector<double> cocked =
        key_arm(c, "Right", hip_goal(c, 12, {-0.05, -0.3, 0.62}, {-0.75, -0.15, 0.65}, {0.65, 0, 0.75}), 12, ready);
    const std::vector<double> impact =
        key_arm(c, "Right", hip_goal(c, 18, {0.54, 0, 0.36}, {0.5, 0.7, -0.5}, {0, 0.6, -0.8}), 18, cocked);
    key_arm(c, "Right", hip_goal(c, 24, {0.16, 0.36, 0.05}, {-0.5, 0.5, -0.7}, {-0.7, 0, -0.7}), 24, impact);
    // The free arm: forward as a guard in the wind-up, pulled back to the hip as the body turns into the cut.
    key_arm(c, "Left", hip_goal(c, 12, {0.35, 0.1, 0.3}), 12, {-45, 0, -50, -40, 0, 0, 0, 0}, false);
    key_arm(c, "Left", hip_goal(c, 18, {-0.15, 0.3, -0.02}), 18, {-70, 0, 30, -15, 0, 0, 0, 0}, false);
    key_arm(c, "Left", hip_goal(c, 24, {-0.18, 0.3, 0}), 24, {-70, 0, 40, -30, 0, 0, 0, 0}, false);
    const std::vector<std::string> keyed = {"mPelvis", "mTorso", "mShoulderRight", "mElbowRight", "mWristRight",
                                            "mShoulderLeft", "mElbowLeft"};
    for (const std::string& bone : keyed)  // 30 is 24 pasted: the two are tagged Hold, so the pose drifts between them
        for (auto& [ch, curve] : c.curves[bone]) curve.set_key(30, curve.evaluate(24));
    step_new_keys(c, before);  // Blocking is on
    tag_keys_at(c, keyed, 24, KeyTag::Hold);
    tag_keys_at(c, keyed, 30, KeyTag::Hold);
    if (stage == SwingStage::Blocked) return p;
    blocking_to_spline(c);
    if (stage == SwingStage::Keyed) return p;
    // The breakdown: the fist over the head, the blade pointing up and back, the edge to the front.
    key_arm(c, "Right", hip_goal(c, 15, {0.12, -0.18, 0.72}, {-0.35, 0.15, 0.92}, {0.93, 0, 0.35}), 15, cocked);
    OverlapSettings o;
    o.shift = 2, o.settle = true;
    apply_overlap(c, skel(), {skel().find("mShoulderLeft"), skel().find("mElbowLeft"), skel().find("mWristLeft")}, o);
    return p;
}


// Pistol hold tutorial (tutorial-pistol-hold.md): the starter Pistol on the Right Hand, the hand in Grip (Cylinder),
// and the right arm straight out on the aim line, the forearm turned thumb up. Only the right
// arm and hand are keyed, so an AO walk keeps the rest. Priority 4, looping. With breathing, a second clip,
// "breathing", holds the same pose for 4 s with a breath layer on mCollarRight, baked; it is the clip that opens.
Project pistol_hold(bool breathing) {
    Project p;
    Clip& c = p.clip;
    c.loop = true, c.priority = 4, c.ease_in = 0.3, c.ease_out = 0.3;
    starter(c, "hand-grip", 0, true);
    // The arm straight out, raised and turned in so the sights come up in front of the right eye: the fist 40 cm in
    // front of the eye and 10 cm below it (the sights sit 10 cm above the grip's middle), the grip upright (the
    // hand's +X up) and the barrel (the fingers, -Y) pointing forward.
    Rig rig(skel());
    const Vec3 eye = evaluate(rig, c, 0, nullptr).globals[skel().find("mEyeRight")].pos;
    key_arm(c, "Right", {eye + Vec3{0.4, 0, -0.1}, {0, 0, 1}, {1, 0, 0}}, 0, {0, 0, 90, 5, 0, 0, 0, -90});
    c.props.push_back(starter_prop("pistol"));
    if (!breathing) return p;
    name_clips(p);
    p.clips[0].name = "hold";
    add_clip(p, "breathing", true);
    p.clip.end_frame = 120, p.clip.loop_out = 120;
    IdleLayer breath = idle_preset("breath");
    breath.bones = {"mCollarRight"};
    p.clip.idle = {breath};
    bake_idle(p.clip, skel());
    return p;
}

// What Second Life plays when a weapon hold at priority 4 runs over an AO walk at 3: per bone, the hold's keys
// where it has them, the walk's everywhere else (the loop-walk example made to walk in place with Remove Hip
// Travel and Make Loop Seamless, as the Loop tools page does). A preview for the weapon tutorials' first GIF, not
// a file anyone would upload.
Project over_walk(const Project& hold, const Shape* shape) {
    Project p = loop_walk(shape);
    Clip& c = p.clip;
    remove_travel(c);
    make_loop_seamless(c, 0);
    for (const auto& [bone, track] : hold.clip.curves) c.curves[bone] = track;
    c.pins = hold.clip.pins;
    c.props = hold.clip.props;
    return p;
}

// The neck and head keys that put the right eye at a world point, looking along a direction (the head's +X): a
// search on the skeleton's FK within the neck's and head's natural ranges, rounded to whole degrees. Prints the
// values and the misses when VATS_PRINT is set.
void key_head(Clip& c, const Vec3& eye, const Vec3& look, double frame) {
    const int neck = skel().find("mNeck"), head = skel().find("mHead"), e = skel().find("mEyeRight");
    Rig rig(skel());
    Pose pose = evaluate(rig, c, frame, nullptr).pose;
    auto over = [](double v, double lo, double hi) { return v < lo ? lo - v : v > hi ? v - hi : 0; };
    auto misses = [&](const std::vector<double>& v, double& d, double& ang) {
        pose.rot[neck] = euler_to_quat({v[0], v[1], v[2]});
        pose.rot[head] = euler_to_quat({v[3], v[4], v[5]});
        const std::vector<Xform> g = skel().global_pose(pose, nullptr);
        d = (g[e].pos - eye).length();
        ang = angle_between(g[head].rot.rotate({1, 0, 0}), look);
    };
    std::vector<double> a(6, 0);
    descend(a, [&](const std::vector<double>& v) {
        double d, ang;
        misses(v, d, ang);
        double lim = 0;
        for (int k : {0, 3}) lim += over(v[k], -15, 15);      // tilt to the side
        for (int k : {1, 4}) lim += over(v[k], -20, 30);      // nod down (+) or up
        for (int k : {2, 5}) lim += over(v[k], -40, 40);      // turn
        double spread = 0;  // the neck and the head share the work
        for (int k = 0; k < 3; ++k) spread += (v[k] - v[k + 3]) * (v[k] - v[k + 3]);
        lim += past_limits(pose, {neck, head});
        return d * d / (0.01 * 0.01) + ang * ang / 4 + lim * lim * 10 + spread / 400;
    });
    for (double& v : a) v = std::round(v);
    key_euler(c, "mNeck", frame, {a[0], a[1], a[2]});
    key_euler(c, "mHead", frame, {a[3], a[4], a[5]});
    if (std::getenv("VATS_PRINT")) {
        double d, ang;
        misses(a, d, ang);
        std::printf("  head: mNeck %g %g %g, mHead %g %g %g; eye misses %.3f m, look %.0f deg\n", a[0], a[1], a[2], a[3],
                    a[4], a[5], d, ang);
    }
}

// Rifle hold tutorial (tutorial-rifle-hold.md), two clips. "aim": a standard rifle aim: the chest turned 30 degrees
// to the right (bladed) and leaning into the rifle; the rifle level and pointing forward with the butt in the pocket
// of the right shoulder, the right fist on the pistol grip with the elbow down; the neck and head lowered onto the
// stock (the cheek weld) so the right eye looks along the sights; the left fist under the handguard, palm up,
// (support_grip in props.json) and bound to mWristRight from frame 0. "ready": a duplicate with the right arm
// re-keyed so the muzzle points 40 degrees down about the shoulder and the head up again; the bound left hand
// follows. The values are solved here and keyed as whole-degree FK keys, the values the page gives. start = the aim
// clip before the left arm is posed and bound (rifle-start.vat).
Project rifle_hold(bool start) {
    Project p;
    Clip& c = p.clip;
    c.loop = true, c.priority = 4, c.ease_in = 0.3, c.ease_out = 0.3;
    Rig rig(skel());
    const bool print = std::getenv("VATS_PRINT");
    const int wr = skel().find("mWristRight");
    key_euler(c, "mChest", 0, {0, 12, -33});
    // The rifle in the world: the model's axes as the world's (X forward along the barrel, Z up), its butt in the
    // shoulder pocket, in front of the shoulder joint and in towards the chest.
    Evaluation e = evaluate(rig, c, 0, nullptr);
    const Quat chest = e.globals[skel().find("mChest")].rot;
    const Vec3 pocket = e.globals[skel().find("mShoulderRight")].pos + chest.rotate({0.1, 0.08, 0.02});
    const Vec3 origin = pocket - Vec3{-0.265, 0, 0.04};  // the model's butt, centre
    // In the model's axes: the pistol grip and the handguard where props.json puts the right fist and the support
    // hand (the same placement the app draws), and the rear sight.
    const Vec3 pistol_grip = starter_model_point("rifle", grip_hole(false)),
               handguard = starter_model_point("rifle", {}, true), rear_sight{-0.02, 0, 0.13};
    if (print)
        std::printf("rifle model: pistol grip (%.3f %.3f %.3f), handguard (%.3f %.3f %.3f)\n", pistol_grip.x, pistol_grip.y,
                    pistol_grip.z, handguard.x, handguard.y, handguard.z);
    // The right fist on the pistol grip: the hole's axis (the hand's +X) up the grip, the fingers (-Y) pointing along
    // the barrel.
    if (print) std::printf("rifle aim, right arm\n");
    const std::vector<double> aim =
        key_arm(c, "Right", {origin + pistol_grip, {0, 0, 1}, {1, 0, 0}}, 0, {100, -20, 20, 120, 0, 0, 0, 0});
    starter(c, "hand-grip", 0, true);
    c.props.push_back(starter_prop("rifle"));
    // The cheek on the stock: the right eye 8 cm behind the rear sight, level with it, looking along the barrel.
    key_head(c, origin + rear_sight + Vec3{-0.08, 0, 0.005}, {1, 0, 0}, 0);
    if (start) return p;
    starter(c, "hand-grip", 0);
    // The left fist under the handguard, palm up: its hole's axis (+X) along the barrel, the fingers (+Y) curling
    // round the far side.
    if (print) std::printf("rifle aim, left arm\n");
    key_arm(c, "Left", {origin + handguard, {0.9, 0, -0.45}, {}}, 0, {-10, 0, -60, -60, 0, 0, 0, 0});
    std::string err;
    if (!pin_here(c, rig, 0, skel().find("mWristLeft"), wr, nullptr, err)) {
        std::fprintf(stderr, "pin refused: %s\n", err.c_str());
        std::exit(2);
    }
    name_clips(p);
    p.clips[0].name = "aim";
    add_clip(p, "ready", true);
    // Ready: the rifle turned 40 degrees muzzle down about the butt, which stays in the shoulder.
    const Quat down = Quat::axis_angle({0, 1, 0}, 40 * kDegToRad);
    auto turned = [&](const Vec3& m) { return pocket + Vec3{0.07, 0, -0.03} + down.rotate(origin + m - pocket); };
    if (print) std::printf("rifle ready, right arm\n");
    key_arm(p.clip, "Right", {turned(pistol_grip), down.rotate({0, 0, 1}), down.rotate({1, 0, 0})}, 0, aim);
    key_euler(p.clip, "mNeck", 0, {0, 0, 15});  // head up, looking forward over the muzzle
    key_euler(p.clip, "mHead", 0, {0, 0, 15});
    set_active_clip(p, 0);
    return p;
}

// ---- Dynamics tutorials (tutorial-dynamics*.md) ----------------------------------------------------------------------
// The base motions are planned as whole poses, one per frame: the upper body by hand-shaped curves, the legs solved
// through their IK onto planned foot paths (knees forward). They are then reduced to linear keys, as a Dynamics bake
// writes them, so the reader gets clean motion to hang the dynamics on. SL axes: +X forward, +Y left, +Z up.

Quat qx(double deg) { return Quat::axis_angle({1, 0, 0}, deg * kDegToRad); }
Quat qy(double deg) { return Quat::axis_angle({0, 1, 0}, deg * kDegToRad); }  // + tips +Z towards +X
Quat qz(double deg) { return Quat::axis_angle({0, 0, 1}, deg * kDegToRad); }  // + turns to the avatar's left
double ease(double s) { return 0.5 - 0.5 * std::cos(kPi * std::clamp(s, 0.0, 1.0)); }

// One frame of a planned body: local rotations by bone, the hips' offset, both ankles in the world (left, right)
// relative to where they are at rest, and each foot's pitch (+ = toes up).
struct BodyFrame {
    std::map<std::string, Quat> rot;
    Vec3 hips;
    Vec3 ankle[2];
    double pitch[2] = {0, 0};  // mFoot (the ball of the foot) bends back to keep the toes level while the heel is up
};

// Keys frames 0..n-1 of the plan onto c as linear keys within tol degrees (0.3 mm on the hips).
void key_body_frames(Clip& c, const std::vector<BodyFrame>& frames, double tol = 0.1) {
    Rig rig(skel());
    const Evaluation rest = evaluate(rig, Clip{}, 0, nullptr);
    Clip ik = c;
    std::set<std::string> bones = {"mPelvis",   "mHipLeft",   "mKneeLeft",   "mAnkleLeft", "mFootLeft",
                                   "mHipRight", "mKneeRight", "mAnkleRight", "mFootRight"};
    for (const BodyFrame& b : frames)
        for (auto& [n, q] : b.rot) bones.insert(n);
    for (int f = 0; f < int(frames.size()); ++f) {
        for (auto& [n, q] : frames[f].rot) key_rotation(ik, n, f, q);
        for (int s = 0; s < 2; ++s) {
            const double p = frames[f].pitch[s];
            key_rotation(ik, s == 0 ? "mFootLeft" : "mFootRight", f, qy(std::min(p, 0.0)));
        }
        key_offset(ik, "mPelvis", f, frames[f].hips);
    }
    const int legs[2] = {rig.find_limb("LegLeft"), rig.find_limb("LegRight")};
    for (int s = 0; s < 2; ++s) switch_to_ik(ik, rig, 0, legs[s], nullptr);
    std::vector<Pose> poses;
    const bool print = std::getenv("VATS_PRINT");
    for (int f = 0; f < int(frames.size()); ++f) {
        const BodyFrame& b = frames[f];
        for (int s = 0; s < 2; ++s) {
            const LimbInfo& l = rig.limbs()[legs[s]];
            const Xform& a0 = rest.globals[l.end];
            const Xform t{qy(-b.pitch[s]) * a0.rot, a0.pos + b.ankle[s]};
            key_limb_target(ik, rig, f, legs[s], t, nullptr);
            const Evaluation e = evaluate(rig, ik, f, nullptr);
            const Vec3 hip = e.globals[l.root].pos;
            key_limb_pole(ik, rig, f, legs[s], (hip + t.pos) * 0.5 + Vec3{0.6, 0, 0}, nullptr);
        }
        const Evaluation e = evaluate(rig, ik, f, nullptr);
        for (int s = 0; print && s < 2; ++s) {
            const LimbInfo& l = rig.limbs()[legs[s]];
            const Vec3 miss = e.globals[l.end].pos - (rest.globals[l.end].pos + b.ankle[s]);
            if (miss.length() > 0.002) std::printf("  frame %d %s misses by %.1f cm\n", f, l.name.c_str(), miss.length() * 100);
        }
        poses.push_back(e.pose);
    }
    std::vector<int> nodes;
    for (const std::string& n : bones) nodes.push_back(skel().find(n));
    bake_samples(c, skel(), nodes, poses, tol, 0.0003, {skel().find("mPelvis")});
    SimplifyOptions o;  // back to a few editable keys, as Edit > Simplify Curves leaves them
    o.tol_deg = 2 * tol, o.tol_mm = 0.5;
    simplify_curves(c, std::vector<std::string>(bones.begin(), bones.end()), o);
}

// Foot geometry relative to the ankle: the heel and the ball of the foot on the sole.
const Vec3 kHeel{-0.04, 0, -0.07}, kBall{0.11, 0, -0.07};

// The ankle, relative to rest, for a foot whose heel's ground point is heel_x ahead of rest, pitched about the heel
// (toes up) or the ball (heel up) as a foot rolls.
Vec3 rolled_ankle(double heel_x, double pitch) {
    const Vec3& piv = pitch >= 0 ? kHeel : kBall;
    return Vec3{heel_x + piv.x, 0, piv.z} - qy(-pitch).rotate(piv);
}

double hermite(double x0, double m0, double x1, double m1, double s) {
    const double s2 = s * s, s3 = s2 * s;
    return (2 * s3 - 3 * s2 + 1) * x0 + (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * x1 + (s3 - s2) * m1;
}

// The walk under the tail (tutorial-dynamics.md): the Loop tools page's walk (Second Life's 3.2 m/s, two strides in
// one second, built from the feet) made to walk in place with Remove Hip Travel and Make Loop Seamless, as an AO walk
// is, at priority 4 with short eases for its one-second loop.
Project dynamics_walk(const Shape* shape) {
    Project p = loop_walk(shape);
    Clip& c = p.clip;
    remove_travel(c);
    make_loop_seamless(c, 0);
    c.loop_tangents = true, c.priority = 4, c.ease_in = 0.3, c.ease_out = 0.3;
    return p;
}

// The arms hanging as in the walk, each swung forward (negative) or back by swing degrees about the shoulder and bent
// by elbow degrees.
void hang_arms(BodyFrame& b, double swing_left, double swing_right, double elbow_left = 14, double elbow_right = 14) {
    for (int s = 0; s < 2; ++s) {
        const double side = s == 0 ? 1 : -1;
        const std::string n = s == 0 ? "Left" : "Right";
        b.rot["mCollar" + n] = qx(side * -4);
        b.rot["mShoulder" + n] = qy(s == 0 ? swing_left : swing_right) * qz(side * -3) * qx(side * -75);
        b.rot["mElbow" + n] = qz(side * -(s == 0 ? elbow_left : elbow_right));
        b.rot["mWrist" + n] = qz(side * -6) * qx(side * 8);
    }
}

// A value that holds at a until frame f0, eases to b by f1 and holds there.
double ramp(double f, double f0, double f1, double a, double b) { return a + (b - a) * ease((f - f0) / (f1 - f0)); }

// The tail's shape before any dynamics: down and back from the hips, curving up to level at the tip, keyed at frame 0.
void key_tail_shape(Clip& c) {
    key_euler(c, "mTail1", 0, {0, -30, 0});
    key_euler(c, "mTail2", 0, {0, 8, 0});
    key_euler(c, "mTail3", 0, {0, 8, 0});
    key_euler(c, "mTail4", 0, {0, 7, 0});
    key_euler(c, "mTail5", 0, {0, 5, 0});
    key_euler(c, "mTail6", 0, {0, 4, 0});
}

DynChain chain(const std::string& root, int length, double stiffness, double damping, double drag, double gravity,
               double radius) {
    DynChain d;
    d.root = root, d.length = length, d.stiffness = stiffness, d.damping = damping, d.drag = drag, d.gravity = gravity,
    d.radius = radius;
    return d;
}

// Bakes every chain as the Dynamics window's Bake All does, against the app's default Bake shape (SL Default).
void bake(Project& p) {
    static const BodyShape body = [] {
        AvatarParams params;
        std::string err;
        parse_avatar_params(read_text(std::string(VATS_DATA_DIR) + "/avatar_lad.xml"), params, err);
        return sl_default_shape(skel(), params, false);
    }();
    Rig rig(skel());
    bake_dynamics(p.clip, rig, &body.shape);
}

// The settings the tail tutorial arrives at for the walk: a medium tail, softer and heavier than the Tail preset, damped
// enough that the tip does not whip round on this brisk walk.
DynChain walk_tail() { return chain("mTail1", 6, 0.05, 0.2, 0.03, 0.5, 0.03); }

// Tail on a walk, the start: the walk with the tail's shape keyed, no chain yet.
Project dynamics_walk_start(const Shape* shape) {
    Project p = dynamics_walk(shape);
    key_tail_shape(p.clip);
    return p;
}

// Tail on a walk, finished: a chain on mTail1 with the walk's settings, baked.
Project dynamics_walk_tail(const Shape* shape) {
    Project p = dynamics_walk_start(shape);
    p.clip.dynamics.push_back(walk_tail());
    bake(p);
    return p;
}

// Editing and export (tutorial-dynamics-keys-and-export.md): the tail wagged by hand under the dynamics. mTail1 keyed
// swinging 12 degrees each way once a cycle (frames 0, 15, 30), the chain baked on top, so the rest of the tail
// follows the wag with overlap.
Project dynamics_walk_wag(const Shape* shape) {
    Project p = dynamics_walk_start(shape);
    key_euler(p.clip, "mTail1", 0, {0, -30, 12});
    key_euler(p.clip, "mTail1", 15, {0, -30, -12});
    key_euler(p.clip, "mTail1", 30, {0, -30, 12});
    p.clip.dynamics.push_back(walk_tail());
    bake(p);
    return p;
}

// The test motion for comparing settings (tutorial-dynamics.md, tutorial-dynamics-wings.md): the hips swing 25 degrees
// to one side in 8 frames and stop, hold, swing back and stop, over a two-second loop. The feet stay planted; the chest
// and head turn back against the hips so the eyes stay forward. A sharp start and a long hold show a chain's lag,
// overshoot and settle.
Project dynamics_swish() {
    Project p;
    Clip& c = p.clip;
    c.loop_tangents = true;
    c.fps = 30, c.end_frame = 60, c.loop = true, c.loop_in = 0, c.loop_out = 60, c.priority = 4;
    std::vector<BodyFrame> frames;
    for (int f = 0; f <= 60; ++f) {
        const double yaw = f < 30 ? ramp(f, 2, 10, -25, 25) : ramp(f, 32, 40, 25, -25);
        BodyFrame b;
        b.hips = {0, yaw * 0.0012, -0.03};
        b.rot["mPelvis"] = qz(yaw);
        b.rot["mTorso"] = qz(-yaw * 0.45);
        b.rot["mChest"] = qz(-yaw * 0.25);
        b.rot["mNeck"] = qz(-yaw * 0.15);
        b.rot["mHead"] = qz(-yaw * 0.15);
        hang_arms(b, -3, -3);
        frames.push_back(b);
    }
    key_body_frames(c, frames);
    starter(c, "hand-relaxed", 0);
    starter(c, "hand-relaxed", 0, true);
    key_tail_shape(c);
    return p;
}

// The swish with the Tail preset's chain on mTail1, not baked: the page's place to try settings.
Project dynamics_swish_tail() {
    Project p = dynamics_swish();
    p.clip.dynamics.push_back(dyn_preset("tail", "mTail1", 6));
    return p;
}

// Tail recipes: the swish with a heavy tail and a light, whippy one, baked.
Project dynamics_tail_recipe(bool heavy) {
    Project p = dynamics_swish();
    p.clip.dynamics.push_back(heavy ? chain("mTail1", 6, 0.03, 0.12, 0.05, 1.5, 0.04) : chain("mTail1", 6, 0.15, 0.25, 0.02, 0.1, 0.02));
    bake(p);
    return p;
}

// Ears on a head turn: standing, the head looks left, holds, looks right, holds and comes back, over three seconds. Each
// turn takes 7 frames and dips 6 degrees in the middle (an arc, not a slide); the neck takes a third of the turn and
// the chest a little, a frame or two behind the head.
Project dynamics_head_turn() {
    Project p;
    Clip& c = p.clip;
    c.loop_tangents = true;
    c.fps = 30, c.end_frame = 90, c.loop = true, c.loop_in = 0, c.loop_out = 90, c.priority = 4;
    auto turn = [](double f) {  // head yaw, degrees
        return f < 30 ? ramp(f, 6, 13, 0, 45) : f < 60 ? ramp(f, 36, 43, 45, -45) : ramp(f, 66, 74, -45, 0);
    };
    auto dip = [](double f) {  // pitch down mid-turn
        double d = 0;
        for (auto [a, b] : {std::pair{6.0, 13.0}, {36.0, 43.0}, {66.0, 74.0}})
            if (f > a && f < b) d = 6 * std::sin(kPi * (f - a) / (b - a));
        return d;
    };
    std::vector<BodyFrame> frames;
    for (int f = 0; f <= 90; ++f) {
        BodyFrame b;
        b.hips = {0, 0, -0.015};
        b.rot["mHead"] = qz(turn(f) * 0.6) * qy(dip(f));
        b.rot["mNeck"] = qz(turn(f - 1) * 0.3) * qy(dip(f - 1) * 0.5);
        b.rot["mChest"] = qz(turn(f - 2) * 0.1);
        hang_arms(b, -3, -3);
        frames.push_back(b);
    }
    key_body_frames(c, frames);
    starter(c, "hand-relaxed", 0);
    starter(c, "hand-relaxed", 0, true);
    return p;
}

Project dynamics_ears() {
    Project p = dynamics_head_turn();
    for (const char* ear : {"mFaceEar1Left", "mFaceEar1Right"}) p.clip.dynamics.push_back(chain(ear, 2, 0.1, 0.15, 0.02, 0.2, 0.01));
    bake(p);
    return p;
}

// Wings: standing, both wings beat once a second: up at frame 0, a fast downstroke to frame 12 (the power stroke),
// a slower upstroke to 30. Only mWing1Left and mWing1Right are keyed, about the axis across each wing's root, so the
// whole wing turns as one board; the chest lifts a little on the downstroke. mWing2 and on are the chain's to move.
constexpr int kFlapFrames = 30;
double flap_angle(double f) {
    f = std::fmod(f + kFlapFrames, kFlapFrames);
    return f < 12 ? ramp(f, 0, 12, 30, -35) : ramp(f, 12, kFlapFrames, -35, 30);
}
Project dynamics_flap() {
    Project p;
    Clip& c = p.clip;
    c.loop_tangents = true;
    c.fps = 30, c.end_frame = kFlapFrames, c.loop = true, c.loop_in = 0, c.loop_out = kFlapFrames, c.priority = 4;
    c.ease_in = 0.3, c.ease_out = 0.3;
    std::vector<BodyFrame> frames;
    for (int f = 0; f <= kFlapFrames; ++f) {
        BodyFrame b;
        const double a = flap_angle(f), lift = (30 - flap_angle(f - 3)) / 65;  // 0 up .. 1 down, 3 frames later
        b.hips = {0, 0, -0.03 + 0.012 * lift};
        b.rot["mChest"] = qy(-3 * lift);
        b.rot["mHead"] = qy(3 * lift);
        b.rot["mWing1Left"] = Quat::axis_angle({1, 1, 0}, a * kDegToRad);
        b.rot["mWing1Right"] = Quat::axis_angle({-1, 1, 0}, a * kDegToRad);
        hang_arms(b, -3, -3);
        frames.push_back(b);
    }
    key_body_frames(c, frames);
    starter(c, "hand-relaxed", 0);
    starter(c, "hand-relaxed", 0, true);
    return p;
}

// The wing chains: mWing2 and the two bones after it, on both sides.
Project dynamics_wings(const char* recipe) {
    Project p = dynamics_flap();
    for (const char* root : {"mWing2Left", "mWing2Right"}) {
        const std::string r = recipe;
        p.clip.dynamics.push_back(r == "big"     ? chain(root, 3, 0.04, 0.1, 0.08, 0.6, 0.03)
                                  : r == "small" ? chain(root, 3, 0.2, 0.08, 0.02, 0.1, 0.02)
                                                 : chain(root, 3, 0.08, 0.12, 0.05, 0.3, 0.03));
    }
    bake(p);
    return p;
}

// Jiggle: a hop in place. Stand (0-6), crouch with the arms back (to 16), push off (to 20), fly (20-30, a ballistic
// arc 13 cm high), land toes first (30), absorb down to a deep bend (38) and rise back to the stand (56). The sharp
// stop at the landing is what the soft parts react to.
Project dynamics_jump() {
    Project p;
    Clip& c = p.clip;
    c.loop_tangents = true;
    c.fps = 30, c.end_frame = 72, c.loop = false, c.loop_out = 72, c.priority = 4, c.ease_in = 0.3, c.ease_out = 0.4;
    const double z_up = 0.04, fly = 10.0 / 30, v0 = 9.81 * fly / 2;  // takeoff height and speed
    std::vector<BodyFrame> frames;
    for (int f = 0; f <= 72; ++f) {
        BodyFrame b;
        double z, lean, arms, elbow = 14, heel = 0;  // arms: shoulder swing (negative = forward)
        if (f <= 6) z = -0.01, lean = 3, arms = -3;
        else if (f <= 16) {
            const double s = ease((f - 6) / 10.0);
            z = -0.01 - 0.13 * s, lean = 3 + 22 * s, arms = -3 + 38 * s;
        } else if (f <= 20) {
            const double s = (f - 16) / 4.0;
            z = hermite(-0.14, 0, z_up, v0 * 4 / 30, s), lean = 25 - 22 * ease(s), arms = 35 - 90 * ease(s);
            elbow = 14 + 16 * ease(s), heel = -40 * s * s;
        } else if (f <= 30) {
            const double t = (f - 20) / 30.0;
            z = z_up + v0 * t - 9.81 * t * t / 2, lean = 3, arms = -55 + 5 * ease((f - 20) / 10.0), elbow = 30, heel = -40;
        } else {
            const double s = std::min(1.0, (f - 30) / 8.0), r = std::clamp((f - 38) / 18.0, 0.0, 1.0);
            z = f <= 38 ? hermite(z_up, -v0 * 8 / 30, -0.16, 0, s) : -0.16 + 0.15 * ease(r);
            lean = f <= 38 ? 3 + 17 * ease(s) : 20 - 17 * ease(r);
            arms = f <= 38 ? -50 + 20 * ease(s) : -30 + 27 * ease(r);
            elbow = f <= 38 ? 30 : 30 - 16 * ease(r);
            heel = f <= 33 ? -40 * (1 - ease((f - 30) / 3.0)) : 0;
        }
        b.hips = {-0.02 * lean / 25, 0, z};
        for (int s = 0; s < 2; ++s) {
            b.pitch[s] = heel;
            b.ankle[s] = rolled_ankle(0, heel);
            if (f > 20 && f < 30)  // the legs in the air, knees drawn up a little at the top
                b.ankle[s].z = std::max(b.ankle[s].z, z - z_up + 0.054 + 0.05 * std::sin(kPi * (f - 20) / 10));
        }
        b.rot["mTorso"] = qy(lean * 0.6);
        b.rot["mChest"] = qy(lean * 0.4);
        b.rot["mNeck"] = qy(-lean * 0.3);
        b.rot["mHead"] = qy(-lean * 0.4);
        hang_arms(b, arms, arms, elbow, elbow);
        frames.push_back(b);
    }
    key_body_frames(c, frames);
    starter(c, "hand-relaxed", 0);
    starter(c, "hand-relaxed", 0, true);
    return p;
}

// The jiggle chains: the belly, both chest volumes and the buttocks, each a point spring, baked.
Project dynamics_jiggle() {
    Project p = dynamics_jump();
    for (const char* v : {"BELLY", "LEFT_PEC", "RIGHT_PEC", "BUTT"}) p.clip.dynamics.push_back(dyn_preset("jiggle", v, 1));
    bake(p);
    return p;
}

// The Dynamics tutorials' comparison GIFs (tools/record-tutorial-media/tutorial-dynamics.sh): the tail on the swish and
// the wings on the flap, baked with one setting at a low, a middle and a high value and the others at the middle
// recipe; gravity on a softer tail (stiffness 0.03), where it shows. Radius has no comparison: in these motions the
// chains never come near a collision volume. Written to a scratch folder as <part>-<setting>-<n>.vat, n = 0, 1, 2;
// not shipped.
bool write_dynamics_variants(const std::string& dir) {
    struct Setting {
        const char* name;
        double DynChain::*field;
        double values[3];
    };
    const Setting settings[] = {{"stiffness", &DynChain::stiffness, {0.02, 0.08, 0.3}},
                                {"damping", &DynChain::damping, {0.02, 0.12, 0.5}},
                                {"drag", &DynChain::drag, {0, 0.03, 0.2}},
                                {"gravity", &DynChain::gravity, {0, 0.3, 2}}};
    bool ok = true;
    for (const char* part : {"tail", "wings"})
        for (const Setting& s : settings)
            for (int i = 0; i < 3; ++i) {
                const bool tail = part == std::string("tail");
                Project p = tail ? dynamics_swish() : dynamics_flap();
                const std::vector<std::string> roots = tail ? std::vector<std::string>{"mTail1"}
                                                            : std::vector<std::string>{"mWing2Left", "mWing2Right"};
                for (const std::string& r : roots) {
                    DynChain d = tail ? chain(r, 6, 0.08, 0.12, 0.03, 0.3, 0.03) : chain(r, 3, 0.08, 0.12, 0.05, 0.3, 0.03);
                    if (s.field == &DynChain::gravity) d.stiffness = 0.03;  // gravity only shows on a soft chain
                    d.*s.field = s.values[i];
                    p.clip.dynamics.push_back(d);
                }
                bake(p);
                ok &= write(dir + "/" + part + "-" + s.name + "-" + std::to_string(i) + ".vat", p);
            }
    Project strong = dynamics_jump();  // the exaggerated jiggle beside the preset's
    for (const char* v : {"BELLY", "LEFT_PEC", "RIGHT_PEC", "BUTT"}) strong.clip.dynamics.push_back(chain(v, 1, 0.04, 0.03, 0, 0, 0));
    bake(strong);
    ok &= write(dir + "/jiggle-strong.vat", strong);
    return ok;
}

void pin(Clip& c, const char* bone, const char* to, double frame) {
    Rig rig(skel());
    std::string why;
    if (!pin_here(c, rig, frame, skel().find(bone), to ? skel().find(to) : -1, g_shape, why)) {
        std::fprintf(stderr, "pin %s refused: %s\n", bone, why.c_str());
        std::exit(2);
    }
}

// Tutorial: a sit pose for furniture (sit-chair.vat), as the page builds it: the starter Chair; Sit on This (the Sitting
// pose, the hips dropped until the feet reach the floor, the ankles held there, the hips raised until the thighs rest
// on the seat: core sit_on_seat with the seat the app finds, 46 cm up); the wrists bound to the thighs; the back
// leaned into the chair with the head countering it and turned a little; both hands in Resting on Surface. A static
// pose: keys at frame 0 only, looping, priority 4. sit-start.vat is step 1: the chair, nothing else.
// The starter Chair's seat height (m), where Sit on This finds it: ui/props.cpp casts rays down onto the mesh under the
// thighs.
constexpr double kChairSeat = 0.46;

Project sit_start() {
    Project p;
    p.clip = new_project_clip();
    p.clip.props.push_back(starter_prop("chair"));
    return p;
}

Project sit_chair() {
    Project p = sit_start();
    Clip& c = p.clip;
    c.loop = true, c.loop_out = 30, c.priority = 4, c.ease_in = 0.5, c.ease_out = 0.5;
    starter(c, "body-sit", 0);
    {
        Rig rig(skel());
        std::string report;
        if (!sit_on_seat(c, rig, 0, kChairSeat, g_shape, report)) {
            std::fprintf(stderr, "sit refused: %s\n", report.c_str());
            std::exit(2);
        }
    }
    pin(c, "mWristLeft", "mHipLeft", 0);
    pin(c, "mWristRight", "mHipRight", 0);
    key_euler(c, "mTorso", 0, ring("mTorso", {0, 0, 0}, 1, -12));  // leaned back until the shoulders meet the chair back
    key_euler(c, "mHead", 0, ring("mHead", ring("mHead", {0, 0, 0}, 1, 10), 2, 8));  // eyes level, turned a little left
    starter(c, "hand-surface", 0);
    starter(c, "hand-surface", 0, true);
    c.export_settings.set("name", "Sit");
    Json heights = Json::array();  // Also export for heights, as ticking it sets them
    for (double h : {1.75, 1.95, 2.15}) heights.push(h);
    c.export_settings.set("heights", std::move(heights));
    return p;
}

// Tutorial: sip from a mug. sip-start.vat is the set-up: Relaxed Stand, the starter Mug in the right hand at its
// grip, the right hand in Grip (Cylinder) and the arm bent to hold the mug in front of the body, 72 frames at
// priority 4. sip-mug.vat adds the sip: a dip before the lift (anticipation), the mug at the lips, the wrist bound
// to the head while the head tips back to drink, then down past the hold and back (settle).
Project sip_start() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 72, c.loop_out = 72, c.priority = 4, c.ease_in = 0.3, c.ease_out = 0.3;
    starter(c, "body-stand", 0);
    c.props.push_back(starter_prop("mug"));
    starter(c, "hand-grip", 0, true);
    key_euler(c, "mShoulderRight", 0, {72, 0, 20});
    key_euler(c, "mElbowRight", 0, {0, 0, 100});
    key_euler(c, "mWristRight", 0, {0, 0, 0});
    key_euler(c, "mHead", 0, {0, 0, 0});
    c.export_settings.set("name", "Sip");
    return p;
}

Project sip_mug() {
    Project p = sip_start();
    Clip& c = p.clip;
    // Anticipation: the mug dips and the eyes go to it.
    key_euler(c, "mShoulderRight", 8, {74, 0, 18});
    key_euler(c, "mElbowRight", 8, {0, 0, 92});
    key_euler(c, "mHead", 8, {0, 4, 0});
    // At the lips: the mug tipped 35 degrees towards the face, the near side of its rim on the lower lip, the
    // handle to the right. The arm is solved for where that puts the fist, from the mug's grip (props.json).
    key_euler(c, "mHead", 20, {0, 0, 0});
    {
        Rig rig(skel());
        const Vec3 lip = evaluate(rig, c, 20, nullptr).globals[skel().find("mFaceLipLowerCenter")].pos;
        const double tilt = 35 * kDegToRad;
        const Vec3 up{-std::sin(tilt), 0, std::cos(tilt)};           // the mug's +Z, its top leaning back
        const Vec3 body = Vec3{-0.35, 1, 0}.normalized();            // +Y, handle to body: to the left, a little back
        const Vec3 side = body.cross(up).normalized();              // +X
        const Vec3 y = up.cross(side);                              // +Y square to the other two
        auto world = [&](const Vec3& v) { return side * v.x + y * v.y + up * v.z; };
        const Vec3 rim{0, 0.016, 0.1};  // the centre of the rim in the model
        // The rim's centre sits 10 cm in front of the lip and 1 cm above it: its near side at the lips on both default bodies.
        const Vec3 fwd = Vec3{1, 0, 0} - up * up.x;
        const Vec3 centre = lip + fwd.normalized() * 0.1 + Vec3{0, 0, 0.01};
        const Vec3 grip = starter_model_point("mug", grip_hole(false));
        key_arm(c, "Right", {centre + world(grip - rim), up, side}, 20, {56, -67, 40, 113, 43, 1, -59, 0});
    }
    // The drink: the wrist rides the head, which tips back and returns.
    pin(c, "mWristRight", "mHead", 20);
    key_euler(c, "mHead", 34, {0, -14, 0});
    key_euler(c, "mHead", 40, {0, -14, 0});
    key_euler(c, "mHead", 46, {0, 0, 0});
    Rig rig(skel());
    std::string why;
    if (!unpin_here(c, rig, 48, skel().find("mWristRight"), g_shape, why)) std::fprintf(stderr, "release: %s\n", why.c_str());
    // Down past the hold, then settle back on it.
    key_euler(c, "mShoulderRight", 60, {72, 0, 20});
    key_euler(c, "mElbowRight", 60, {0, 0, 92});
    key_euler(c, "mWristRight", 60, {0, 0, 0});
    key_euler(c, "mShoulderRight", 68, {72, 0, 20});
    key_euler(c, "mElbowRight", 68, {0, 0, 100});
    return p;
}

// Tutorial: a walk cycle for your AO. The first step's four poses, left foot forward: contact (0), down (2), passing
// (4) and up (6), 16 frames looping, walking in place at Second Life's walking speed. walk-first-step.vat is this;
// walk-cycle.vat goes on.
Project walk_first_step() {
    Project p;
    Clip& c = p.clip;
    c.end_frame = 16, c.loop = true, c.loop_out = 16, c.priority = 3, c.ease_in = 0.25, c.ease_out = 0.25;
    // The hips drop just after contact, rise over the planted leg, and sway towards it (Y is the avatar's left).
    key_pos(c, "mPelvis", {{0, 0, 0, -0.025}, {2, 0, 0.012, -0.032}, {4, 0, 0.018, 0.012}, {6, 0, 0.01, 0.031}});
    // The hips turn with the forward leg and the chest turns back against them (Z turns to the left).
    key_rot(c, "mPelvis", {{0, 0, 0, -4}, {4, 0, 0, 0}});
    key_rot(c, "mTorso", {{0, 0, 0, 8}, {4, 0, 0, 0}});
    // The legs (Rotate Y: - swings forward, + back; knees bend +) were fitted by a search, then rounded: the planted
    // foot stays within 6 mm of the floor from heel strike to toe-off and slides back at an even 3.0-3.3 m/s, SL's
    // walking speed (3.20 m/s), while the swinging foot clears the floor. Ranges are those of published gait data:
    // hip 18 flexion to 22 extension, knee 4 to 63, ankle -11 to 35.
    key_rot(c, "mHipLeft", {{0, 0, -18, 0}, {2, 0, -17, 0}, {4, 0, 3, 0}, {6, 0, 9, 0}});
    key_rot(c, "mKneeLeft", {{0, 0, 4, 0}, {2, 0, 23, 0}, {4, 0, 8, 0}, {6, 0, 17, 0}});
    key_rot(c, "mAnkleLeft", {{0, 0, -5, 0}, {2, 0, -11, 0}, {4, 0, -4, 0}, {6, 0, 2, 0}});
    key_rot(c, "mHipRight", {{0, 0, 11, 0}, {2, 0, 5, 0}, {4, 0, -14, 0}, {6, 0, -22, 0}});
    key_rot(c, "mKneeRight", {{0, 0, 35, 0}, {2, 0, 48, 0}, {4, 0, 63, 0}, {6, 0, 22, 0}});
    key_rot(c, "mAnkleRight", {{0, 0, 4, 0}, {2, 0, 35, 0}, {4, 0, 4, 0}, {6, 0, -8, 0}});
    // Arms hang (X) and swing against the legs (Y): with the left leg forward, the left arm is back (+Y) and the
    // right arm forward (-Y on the right side, as mirroring keeps Y).
    key_rot(c, "mShoulderLeft", {{0, -75, 20, 0}, {4, -75, 0, 0}});
    key_rot(c, "mShoulderRight", {{0, 75, -20, 0}, {4, 75, 0, 0}});
    key_rot(c, "mElbowLeft", {{0, 0, 0, -10}});
    key_rot(c, "mElbowRight", {{0, 0, 0, 10}});
    return p;
}

// The finished walk: the first step pasted mirrored half a cycle later, the loop made seamless, as an AO set with a
// stand beside it.
Project walk_cycle() {
    Project p = walk_first_step();
    Clip& c = p.clip;
    const KeyRange step = copy_range(c, 0, 7);
    paste_range(c, step, 8, false, &skel());
    make_loop_seamless(c, 0);
    c.export_settings.set("name", "AO");
    c.export_settings.set("pattern", "[NAME]_[CLIP]");
    name_clips(p);
    p.clips[0].name = "walk", p.clips[0].ao_state = "Walking";
    add_clip(p, "stand", false);
    p.clips[1].ao_state = "Standing";
    starter(p.clip, "body-stand", 0);
    p.clip.priority = 2;
    set_active_clip(p, 0);
    return p;
}

// Run cycle tutorial: eight key poses, one every `step` frames, in place. Pose p of the right leg: 0 contact, 1 down,
// 2 push-off, 3 flight, 4 left contact (right knee tucked), 5 passing, 6 reach, 7 flight (right reaching for contact).
// The left leg plays the same poses four later; the arms swing opposite the legs. Poses 0-2 are solved so the planted
// point (ankle, then toe) moves back evenly at Second Life's run speed once the cycle is 16 frames (2 frames a pose);
// the others are keyed directly. Hip heights: stance from the table, flight above both neighbours.
struct RunLeg {
    double hip, knee, ankle;
};
constexpr RunLeg kRunSwing[8] = {{}, {}, {}, {30, 70, 25}, {5, 105, 10}, {-38, 110, 5}, {-62, 62, 0}, {-40, 25, -12}};
struct RunStance {
    double x;          // the planted point, metres in front of the hips (ankle; toe on push-off)
    double foot;       // the foot's pitch, degrees, toes down
    double hips;       // the hips' height from rest, metres
    bool toe;
};
constexpr double kRunStep = 5.5 * 2 / 30;  // the ground under the foot between two poses at 16 frames
constexpr double kRunReach = 0.36;  // the heel lands this far in front of the hips
constexpr RunStance kRunStance[3] = {{kRunReach, -18, -0.04, false},
                                     {kRunReach - kRunStep, 0, -0.08, false},
                                     {kRunReach - kRunStep + 0.221 - kRunStep, 25, -0.025, true}};

// Keys hip, knee and ankle (about Y) at frame f so the leg's planted point is at the stance target.
void run_stance(Clip& c, double f, const char* side, const RunStance& s, double roll = 0) {
    const double rx = side[0] == 'R' ? roll : -roll;  // the foot in towards the middle
    const std::string hip = std::string("mHip") + side, knee = std::string("mKnee") + side,
                      ankle = std::string("mAnkle") + side;
    const Skeleton& sk = skel();
    const std::vector<Xform> rest = sk.global_pose(Pose(sk.size()));
    const int ih = sk.find(hip), ia = sk.find(ankle), it = sk.find(std::string("mToe") + side);
    const Vec3 thigh = rest[sk.find(knee)].pos - rest[ih].pos, shin = rest[ia].pos - rest[sk.find(knee)].pos;
    const double l1 = std::hypot(thigh.x, thigh.z), l2 = std::hypot(shin.x, shin.z);
    const double bt = std::atan2(-thigh.x, -thigh.z), bs = std::atan2(-shin.x, -shin.z);  // back from straight down
    // The toe's offset from the ankle at rest, turned by the foot's pitch (a turn about +Y takes +X towards -Z).
    const Vec3 toe = rest[it].pos - rest[ia].pos;
    const double fp = s.foot * kDegToRad;
    const Vec3 toe_off{toe.x * std::cos(fp) + toe.z * std::sin(fp), 0, -toe.x * std::sin(fp) + toe.z * std::cos(fp)};
    Vec3 want = s.toe ? Vec3{s.x, 0, rest[it].pos.z} - toe_off : Vec3{s.x, 0, rest[ia].pos.z};
    Rig rig(sk);
    Vec3 aim = want;
    for (int iter = 0; iter < 6; ++iter) {
        const Evaluation e0 = evaluate(rig, c, f, nullptr);
        const Vec3 h = e0.globals[ih].pos, d = aim - h;
        const double dist = std::min(std::hypot(d.x, d.z), l1 + l2 - 1e-4);
        const double bend = std::acos(std::clamp((dist * dist - l1 * l1 - l2 * l2) / (2 * l1 * l2), -1.0, 1.0));
        const double th = std::atan2(-d.x, -d.z) - std::atan2(l2 * std::sin(bend), l1 + l2 * std::cos(bend));
        const double hy = (th - bt) * kRadToDeg, ky = (th + bend - bs) * kRadToDeg - hy;
        key_euler(c, hip, f, {rx, hy, 0});
        key_euler(c, knee, f, {0, ky, 0});
        key_euler(c, ankle, f, {-rx, s.foot - hy - ky, 0});
        const Evaluation e1 = evaluate(rig, c, f, nullptr);
        const Vec3 got = e1.globals[ia].pos;
        aim = aim + Vec3{want.x - got.x, 0, want.z - got.z};
    }
}

void run_keys(Clip& c, int step) {
    const int n = 8 * step;
    c.fps = 30, c.end_frame = n, c.loop = true, c.loop_in = 0, c.loop_out = n, c.priority = 3;
    c.loop_tangents = true;  // as in a new project
    for (int p = 0; p <= 8; ++p) {
        const double f = p * step;
        const int q = p % 8;
        if (q >= 3) {
            const RunLeg r = kRunSwing[q];
            key_euler(c, "mHipRight", f, {0, r.hip, 0});
            key_euler(c, "mKneeRight", f, {0, r.knee, 0});
            key_euler(c, "mAnkleRight", f, {0, r.ankle, 0});
        }
        if ((q + 4) % 8 >= 3) {
            const RunLeg l = kRunSwing[(q + 4) % 8];
            key_euler(c, "mHipLeft", f, {0, l.hip, 0});
            key_euler(c, "mKneeLeft", f, {0, l.knee, 0});
            key_euler(c, "mAnkleLeft", f, {0, l.ankle, 0});
        }
        // The right leg forward (p 0) swings the left arm forward and turns the hips left, the chest right.
        const double s = std::cos(p * kPi / 4);   // 1 at right contact, -1 at left contact
        key_euler(c, "mShoulderLeft", f, {-72, -35 * s, 0});
        key_euler(c, "mShoulderRight", f, {72, 35 * s, 0});
        key_euler(c, "mElbowLeft", f, {0, 0, -85 - 10 * s});
        key_euler(c, "mElbowRight", f, {0, 0, 85 - 10 * s});
        key_euler(c, "mPelvis", f, {0, 0, 6 * s});
        key_euler(c, "mTorso", f, {0, 10, -8 * s});
        key_euler(c, "mChest", f, {0, 4, -6 * s});
        key_euler(c, "mHead", f, {0, -10, 4 * s});
    }
    starter(c, "hand-loose-fist", 0);
    starter(c, "hand-loose-fist", 0, true);
    for (int p = 0; p <= 8; ++p) {
        const int q = p % 8;
        const double z = q % 4 == 3 ? std::max(kRunStance[2].hips, kRunStance[0].hips) + 0.03 : kRunStance[q % 4].hips;
        const double sway = q % 4 == 3 ? 0 : q < 4 ? -0.03 : 0.03;  // over the planted foot
        key_offset(c, "mPelvis", p * step, {0, sway, z});
        if (q % 4 < 3) run_stance(c, p * step, q < 4 ? "Right" : "Left", kRunStance[q % 4], 4);
    }
}

// run-blocked.vat: the key poses stepped, on a walk's timing (24 frames), tagged Extreme (contacts) and Breakdown.
Project run_blocked() {
    Project p;
    Clip& c = p.clip;
    run_keys(c, 3);
    for (auto& [name, track] : c.curves)
        for (auto& [ch, curve] : track)
            for (Key& k : curve.keys) k.interp = Interp::Constant;
    std::vector<std::string> all;
    for (auto& [name, track] : c.curves) all.push_back(name);
    for (int f = 0; f <= 24; f += 3) tag_keys_at(c, all, f, f % 12 == 0 ? KeyTag::Extreme : KeyTag::Breakdown);
    return p;
}

// How far the lowest ankle or toe goes below its height at rest, over the clip (metres, positive = below), and where.
double feet_below(const Clip& c, int& at) {
    Rig rig(skel());
    const double ground = sole_floor(skel(), nullptr);  // the Animation Check's measure
    double w = 0;
    for (int f = 0; f <= c.end_frame; ++f)
        if (const double d = ground - sole_height(skel(), evaluate(rig, c, f, nullptr).globals); d > w) w = d, at = f;
    return w;
}

// run-timed.vat: the blocked run as the tutorial has it after Convert Blocking to Spline and Stretch Time at SL Run
// speed (16 frames), before its clean-up. run-cycle.vat: the finished run, as the rest of the tutorial makes it.
Project run_timed() {
    Project p = run_blocked();
    Clip& c = p.clip;
    blocking_to_spline(c);
    const Gait g = measure_gait(Rig(skel()), c);
    const int n = match_speed_by_time(c, g, kSlSpeeds[1].mps);
    apply_loop_tangents(c);
    const Gait h = measure_gait(Rig(skel()), c);
    int at = 0;
    const double below = feet_below(c, at);
    std::printf("run-timed: spline %.2f m, %.2f s, %.2f m/s; stretched to %d frames: %.2f m, %.2f s, %.2f m/s; feet %.1f cm "
                "below at %d\n", g.stride, g.cycle, g.speed, n, h.stride, h.cycle, h.speed, below * 100, at);
    return p;
}

Project run_cycle() {
    Project p = run_timed();
    Clip& c = p.clip;
    std::printf("run-cycle: seamless on %d channels\n", make_loop_seamless(c, 0));
    // Overlap, 1 frame per bone: chest, neck and head, and each arm from the shoulder.
    for (const std::vector<const char*>& names : {std::vector<const char*>{"mChest", "mNeck", "mHead"},
                                                  {"mShoulderLeft", "mElbowLeft", "mWristLeft"},
                                                  {"mShoulderRight", "mElbowRight", "mWristRight"}}) {
        std::vector<int> chain;
        for (const char* b : names) chain.push_back(skel().find(b));
        apply_overlap(c, skel(), chain, {});
    }
    const SimplifyResult sr = simplify_curves(c, {}, {});
    std::printf("run-cycle: simplified %d keys to %d\n", sr.before, sr.after);
    c.ease_in = c.ease_out = 0.25;
    apply_loop_tangents(c);
    int at = 0;
    const double below = feet_below(c, at);
    const Gait h = measure_gait(Rig(skel()), c);
    std::printf("run-cycle: %.2f m, %.2f s, %.2f m/s; feet %.1f cm below at %d\n", h.stride, h.cycle, h.speed, below * 100, at);
    name_clips(p);
    p.clips[0].ao_state = "Running";  // the AO's own run: the check's priority rule leaves it alone
    return p;
}

// Turns tutorial: small steps in place for an AO's Turning Left, a 24-frame loop. The right foot lifts by frame 6 and
// is down again at 12, the left at 18 and 24; the hips sway over the standing foot. The tutorial adds the turn itself.
Project turn_steps() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 24, c.loop = true, c.loop_in = 0, c.loop_out = 24, c.priority = 3;
    c.loop_tangents = true;
    c.ease_in = c.ease_out = 0.3;
    starter(c, "body-stand", 0, false);
    starter(c, "body-stand", 0, true);
    for (const char* side : {"Right", "Left"}) {
        const bool r = side[0] == 'R';
        const double up = r ? 6 : 18, down0 = r ? 0 : 12, down1 = r ? 12 : 24;
        for (const char* b : {"mHip", "mKnee", "mAnkle"})
            for (double f : {down0, down1}) key_euler(c, std::string(b) + side, f, {0, 0, 0});
        key_euler(c, std::string("mHip") + side, up, {0, -22, 0});
        key_euler(c, std::string("mKnee") + side, up, {0, 44, 0});
        key_euler(c, std::string("mAnkle") + side, up, {0, -10, 0});
    }
    // Weight over the standing foot: left (+Y) while the right foot is up, then right; down a little as each foot lands.
    key_pos(c, "mPelvis", {{0, 0, 0, -0.01}, {6, 0, 0.03, 0}, {12, 0, 0, -0.01}, {18, 0, -0.03, 0}, {24, 0, 0, -0.01}});
    return p;
}

// turn-left.vat: the turn as the tutorial finishes it: the head leads by 20 degrees, the chest by 8 and the hips by 4,
// named turn_left (Turning Left), with turn_right, its copy, exported mirrored (Turning Right).
Project turn_left() {
    Project p = turn_steps();
    Clip& c = p.clip;
    key_euler(c, "mHead", 0, {0, 0, 20});
    key_euler(c, "mChest", 0, {0, 0, 8});
    key_euler(c, "mPelvis", 0, {0, 0, 4});
    c.export_settings.set("name", "AO");
    c.export_settings.set("pattern", "[NAME]_[CLIP]");
    name_clips(p);
    p.clips[0].name = "turn_left", p.clips[0].ao_state = "Turning Left";
    add_clip(p, "turn_right", true);
    p.clips[1].ao_state = "Turning Right";
    p.clip.mirror_export = true;
    set_active_clip(p, 0);
    return p;
}

// Jump tutorial: a standing jump in place, 48 frames. Stand (0), crouch (10), takeoff on the toes (14), tucked in the
// air (20), landing on the toes (26), absorb (30), rise (38) and stand (48). The feet are solved onto the floor on the
// ground frames; the hips in the air are only three keys, which the tutorial replaces with Jump Arc.
Project jump_poses() {
    Project p;
    Clip& c = p.clip;
    c.fps = 30, c.end_frame = 48, c.loop = false, c.loop_out = 48, c.priority = 4;
    c.ease_in = c.ease_out = 0.3;
    starter(c, "hand-relaxed", 0);
    starter(c, "hand-relaxed", 0, true);
    struct Pose1 {
        double f, hips, lean, head, arm, elbow;  // hips m; lean: torso pitch; arm: shoulder swing (+ back)
    };
    const Pose1 poses[] = {{0, 0, 0, 0, 0, 12},     {10, -0.26, 28, -18, 45, 20}, {14, 0.05, -4, 5, -80, 10},
                           {20, 0.42, 12, -6, -85, 30}, {26, 0.03, 6, 0, -80, 20}, {30, -0.22, 26, -14, -30, 40},
                           {38, -0.02, 4, -2, 8, 15},   {48, 0, 0, 0, 0, 12}};
    const double rest_ankle_x = skel().global_pose(Pose(skel().size()))[skel().find("mAnkleRight")].pos.x;
    for (const Pose1& q : poses) {
        key_offset(c, "mPelvis", q.f, {0, 0, q.hips});
        key_euler(c, "mTorso", q.f, {0, q.lean * 0.6, 0});
        key_euler(c, "mChest", q.f, {0, q.lean * 0.4, 0});
        key_euler(c, "mHead", q.f, {0, q.head, 0});
        key_euler(c, "mShoulderLeft", q.f, {-78, q.arm, 0});
        key_euler(c, "mShoulderRight", q.f, {78, q.arm, 0});
        key_euler(c, "mElbowLeft", q.f, {0, 0, -q.elbow});
        key_euler(c, "mElbowRight", q.f, {0, 0, q.elbow});
        const bool air = q.f == 20, toes = q.f == 14 || q.f == 26;
        for (const char* side : {"Left", "Right"}) {
            if (air) {  // tucked
                key_euler(c, std::string("mHip") + side, q.f, {0, -55, 0});
                key_euler(c, std::string("mKnee") + side, q.f, {0, 80, 0});
                key_euler(c, std::string("mAnkle") + side, q.f, {0, 20, 0});
                continue;
            }
            // On the ground: the foot flat under the hips, or on its toes at takeoff and landing.
            // Deep bends (the crouch and the absorb) lift the heels a little, as the ankle cannot bend far enough.
            const bool deep = q.hips < -0.15;
            const double pitch = toes ? 35.0 : deep ? 14.0 : 0.0;
            const RunStance s{toes || deep ? rest_ankle_x + 0.221 : rest_ankle_x, pitch, q.hips, toes || deep};
            run_stance(c, q.f, side, s);
        }
    }
    // Breakdowns on the way down into the crouch and up from the landing keep the feet on the floor between poses.
    // The push (11 to 13) and the catch (27, 28) roll over the toes.
    struct Breakdown {
        double f, hips, pitch;
    };
    for (const Breakdown& b : {Breakdown{5, -0.1, 0}, Breakdown{11, -0.17, 20}, Breakdown{12, -0.1, 25},
                               Breakdown{13, -0.03, 30}, Breakdown{27, -0.04, 28}, Breakdown{28, -0.13, 22},
                               Breakdown{34, -0.12, 0}}) {
        key_offset(c, "mPelvis", b.f, {0, 0, b.hips});
        const bool toe = b.pitch > 0;
        for (const char* side : {"Left", "Right"})
            run_stance(c, b.f, side, {toe ? rest_ankle_x + 0.221 : rest_ankle_x, b.pitch, b.hips, toe});
    }
    return p;
}

// jump-arc.vat: the key poses after Jump Arc from 14 to 26. jump-finished.vat: then 2 frames of overlap down each arm,
// settling at the end, as the tutorial ends it.
Project jump_arced() {
    Project p = jump_poses();
    JumpArcOptions opt;
    opt.takeoff = 14, opt.landing = 26;
    std::string msg;
    jump_arc(p.clip, opt, msg);
    std::printf("jump-arc: %s; the hips at 20: %.3f m\n", msg.c_str(), curve_offset(p.clip, "mPelvis", 20).z);
    return p;
}

Project jump_finished() {
    Project p = jump_arced();
    Clip& c = p.clip;
    for (const char* side : {"Left", "Right"}) {
        std::vector<int> chain;
        for (const char* b : {"mShoulder", "mElbow", "mWrist"}) chain.push_back(skel().find(std::string(b) + side));
        OverlapSettings s;
        s.shift = 2, s.settle = true;
        apply_overlap(c, skel(), chain, s);
    }
    return p;
}


}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[2]) == "--dynamics-variants") return write_dynamics_variants(argv[3]) ? 0 : 1;
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <docs/wiki/examples dir> [--dynamics-variants <dir>]\n", argv[0]);
        return 2;
    }
    Skeleton skel;
    AvatarParams params;
    std::string err;
    if (!skel.load_dir(VATS_DATA_DIR, err) || !parse_avatar_params(read_text(std::string(VATS_DATA_DIR) + "/avatar_lad.xml"), params, err)) {
        std::fprintf(stderr, "cannot load the skeleton: %s\n", err.c_str());
        return 1;
    }
    const BodyShape body = sl_default_shape(skel, params, false);  // the app's default Bake shape
    g_shape = &body.shape;
    const std::string dir = std::string(argv[1]) + "/";
    bool ok = true;
    ok &= write(dir + "graph-basics.vat", graph_basics());
    ok &= write(dir + "loop-walk.vat", loop_walk(&body.shape));
    ok &= write(dir + "time-nod.vat", time_nod());
    ok &= write(dir + "audio-beats.vat", audio_beats());
    ok &= write(dir + "dynamics-tail.vat", dynamics_tail());
    ok &= write(dir + "dynamics-walk-start.vat", dynamics_walk_start(&body.shape));
    ok &= write(dir + "dynamics-walk-tail.vat", dynamics_walk_tail(&body.shape));
    ok &= write(dir + "dynamics-walk-wag.vat", dynamics_walk_wag(&body.shape));
    ok &= write(dir + "dynamics-swish.vat", dynamics_swish_tail());
    ok &= write(dir + "dynamics-tail-heavy.vat", dynamics_tail_recipe(true));
    ok &= write(dir + "dynamics-tail-whippy.vat", dynamics_tail_recipe(false));
    ok &= write(dir + "dynamics-ears-start.vat", dynamics_head_turn());
    ok &= write(dir + "dynamics-ears.vat", dynamics_ears());
    ok &= write(dir + "dynamics-wings-start.vat", dynamics_flap());
    ok &= write(dir + "dynamics-wings.vat", dynamics_wings(""));
    ok &= write(dir + "dynamics-wings-big.vat", dynamics_wings("big"));
    ok &= write(dir + "dynamics-wings-small.vat", dynamics_wings("small"));
    ok &= write(dir + "dynamics-jump-start.vat", dynamics_jump());
    ok &= write(dir + "dynamics-jiggle.vat", dynamics_jiggle());
    ok &= write(dir + "ragdoll-fall.vat", ragdoll_fall());
    ok &= write(dir + "priority-hand.vat", priority_hand());
    ok &= write(dir + "retarget-walk.vat", retarget_walk(skel, &body.shape));
    ok &= write(dir + "first-wave.vat", first_wave());
    ok &= write(dir + "couple-handshake.vat", couple_handshake());
    ok &= write(dir + "prop-in-hand.vat", prop_in_hand());
    ok &= write(dir + "posing-head-turn.vat", posing_head_turn());
    ok &= write(dir + "target-head-turn.vat", target_head_turn());
    ok &= write(dir + "posing-leg-lift.vat", posing_leg_lift());
    ok &= write(dir + "target-leg-lift.vat", target_leg_lift());
    ok &= write(dir + "keys-head-nod.vat", keys_head_nod());
    ok &= write(dir + "deformer-start.vat", deformer_start());
    ok &= write(dir + "deformer-long-neck.vat", deformer_long_neck());
    ok &= write(dir + "deformer-held.vat", deformer_held());
    ok &= write(dir + "ik-reach.vat", ik_reach());
    ok &= write(dir + "hold-hand-on-table.vat", hold_hand_on_table());
    ok &= write(dir + "hand-poser-fist.vat", hand_poser_fist());
    ok &= write(dir + "pose-library-two-poses.vat", pose_library_two_poses());
    ok &= write(dir + "mirror-arm.vat", mirror_arm());
    ok &= write(dir + "onion-arm-swing.vat", onion_arm_swing());
    ok &= write(dir + "balance-lean.vat", balance_lean());
    ok &= write(dir + "ao-set.vat", ao_set());
    ok &= write(dir + "blocking-arm.vat", blocking_arm());
    ok &= write(dir + "lip-sync.vat", lip_sync());
    ok &= write(dir + "idle-stand.vat", idle_stand());
    ok &= write(dir + "sit-start.vat", sit_start());
    ok &= write(dir + "sit-chair.vat", sit_chair());
    ok &= write(dir + "walk-first-step.vat", walk_first_step());
    ok &= write(dir + "walk-cycle.vat", walk_cycle());
    ok &= write(dir + "sip-start.vat", sip_start());
    ok &= write(dir + "sip-mug.vat", sip_mug());
    ok &= write(dir + "hug-start.vat", hug_start());
    ok &= write(dir + "hug-finished.vat", hug_finished());
    ok &= write(dir + "dance-start.vat", dance_start());
    ok &= write(dir + "dance-grid.vat", dance_grid());
    ok &= write(dir + "dance-finished.vat", dance_finished());
    ok &= write(dir + "dance-long.vat", dance_long());
    ok &= write(dir + "dance-compare.vat", dance_compare());
    ok &= write(dir + "polish-start.vat", polish_start());
    ok &= write(dir + "polish-mid.vat", polish_mid());
    ok &= write(dir + "polish-finished.vat", polish_finished());
    ok &= write(dir + "polish-heavy.vat", polish_heavy());
    ok &= write(dir + "tutorial-first-pose.vat", tutorial_first_pose());
    ok &= write(dir + "tutorial-nod.vat", tutorial_nod(false));
    ok &= write(dir + "tutorial-nod-linear.vat", tutorial_nod(true));
    ok &= write(dir + "tutorial-breathing-idle.vat", tutorial_breathing_idle());
    ok &= write(dir + "sword-start.vat", sword_start());
    ok &= write(dir + "pistol-hold.vat", pistol_hold(false));
    ok &= write(dir + "pistol-hold-breathing.vat", pistol_hold(true));
    ok &= write(dir + "rifle-start.vat", rifle_hold(true));
    ok &= write(dir + "rifle-hold.vat", rifle_hold(false));
    ok &= write(dir + "pistol-over-walk.vat", over_walk(pistol_hold(false), &body.shape));
    ok &= write(dir + "rifle-over-walk.vat", over_walk(rifle_hold(false), &body.shape));
    ok &= write(dir + "sword-keys.vat", sword_swing(SwingStage::Keyed));
    ok &= write(dir + "sword-swing.vat", sword_swing(SwingStage::Finished));
    ok &= write(dir + "run-blocked.vat", run_blocked());
    ok &= write(dir + "run-timed.vat", run_timed());
    ok &= write(dir + "run-cycle.vat", run_cycle());
    ok &= write(dir + "turn-steps.vat", turn_steps());
    ok &= write(dir + "turn-left.vat", turn_left());
    ok &= write(dir + "jump-poses.vat", jump_poses());
    ok &= write(dir + "jump-arc.vat", jump_arced());
    ok &= write(dir + "jump-finished.vat", jump_finished());
    return ok ? 0 : 1;
}
