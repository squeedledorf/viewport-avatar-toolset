// Rig axes from a mesh body's own bone axes, and Auto IK (spec 08 AI), on the CC0 mech of tools/mech_rig.h.
#include <algorithm>
#include <cmath>
#include <string>

#include "../tools/mech_rig.h"
#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/skeleton.h"
#include "vats/dae.h"
#include "vats/edit.h"
#include "vats/rig.h"

using namespace vats;

namespace {

int node(const char* name) { return skel().find(name); }
double deg_between(const Quat& a, const Quat& b) { return (a.conj() * b).angle() * kRadToDeg; }
bool finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

bool load_mech(DaeModel& m, int turn = 0, bool y_up = false) {
    DaeReport r;
    std::string err;
    const bool ok = load_dae(mech::dae(skel(), mech::build(skel()), turn, y_up), "", skel(), m, r, err);
    if (!ok) std::fprintf(stderr, "mech: %s\n", err.c_str());
    return ok;
}

// The mech as a mesh body: its proportions and its rig axes.
Shape mech_shape(const DaeModel& m) {
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);
    return s;
}

}  // namespace

TEST(rig_axes_survive_import) {
    const mech::Body body = mech::build(skel());
    // As written, facing -Y as Blender rigs do, and Y up: the axes come out in SL space all the same.
    for (auto [turn, y_up] : {std::pair{0, false}, std::pair{-1, false}, std::pair{0, true}, std::pair{1, true}}) {
        DaeModel m;
        CHECK(load_mech(m, turn, y_up));
        CHECK(m.rigged);
        CHECK(m.rig_axes.size() == m.binds.size());
        double worst = 0, worst_bind = 0;
        const std::vector<Xform> rest = skel().global_pose(Pose(skel().size()));
        for (const mech::Bone& b : body.bones) {
            worst = std::max(worst, deg_between(m.rig_axes[b.node], b.axes));
            worst_bind = std::max(worst_bind, deg_between(m.binds[b.node].rot, rest[b.node].rot));  // skinning: SL frames
        }
        CHECK(worst < 0.01);
        CHECK(worst_bind < 1e-6);
    }
    // A file already in SL's frames has none.
    DaeModel sl;
    DaeReport r;
    std::string err;
    mech::Body flat = body;
    for (mech::Bone& b : flat.bones) b.axes = Quat{};
    CHECK(load_dae(mech::dae(skel(), flat), "", skel(), sl, r, err));
    CHECK(sl.rig_axes.empty());
    Shape s;
    CHECK(!rig_axes_from_parts(skel(), {&sl}, s));
}

TEST(rig_axes_on_the_body_shape) {
    DaeModel m;
    CHECK(load_mech(m, -1));
    const Shape s = mech_shape(m);
    const mech::Body body = mech::build(skel());
    const std::vector<Xform> g = skel().global_pose(Pose(skel().size()), &s);
    for (const mech::Bone& b : body.bones) {
        CHECK(has_rig_axes(&s, b.node));
        CHECK(deg_between(g[b.node].rot * skel().bone_axes(b.node, &s), b.axes) < 0.01);  // the Local gizmo's axes
        CHECK((g[b.node].pos - b.head).length() < 1e-4);  // the body's own joint positions
    }
    // Tails along the bone axis (+Y): to the child joint, or for an end bone as far as its box reaches.
    const int knee = node("mHindLimb2Left"), foot = node("mHindLimb4Left"), head = node("mHead");
    CHECK((s.tails[knee] - s.axes[knee].rotate({0, 0.42, 0})).length() < 1e-4);
    CHECK((s.tails[foot] - s.axes[foot].rotate({0, 0.16, 0})).length() < 1e-3);
    CHECK(std::fabs(s.tails[head].length() - (body.find(head)->tail - body.find(head)->head).length()) < 1e-3);
    CHECK((bone_tail(skel(), g, &s, foot) - body.find(foot)->tail).length() < 1e-3);
    // Joints the mech does not rig keep SL's frames.
    CHECK(!has_rig_axes(&s, node("mTail1")));
    CHECK(skel().bone_axes(node("mTail1"), &s) == skel().bone_frame(node("mTail1")));
}

TEST(rig_axis_turn_keys_sl_frame) {
    DaeModel m;
    CHECK(load_mech(m));
    const Shape s = mech_shape(m);
    const Rig rig(skel());
    const int knee = node("mHindLimb2Left"), hock = node("mHindLimb3Left");
    const Quat a = s.axes[knee];
    // 30 degrees about the knee's rig X, its hinge: the key is the same turn in SL's joint frame.
    const Quat rig_turn = Quat::axis_angle({1, 0, 0}, 30 * kDegToRad);
    const Quat sl = from_rig_axes(a, rig_turn);
    CHECK(deg_between(to_rig_axes(a, sl), rig_turn) < 1e-9);
    Clip c;
    key_rotation(c, "mHindLimb2Left", 0, sl);
    const std::vector<Xform> before = skel().global_pose(Pose(skel().size()), &s);
    const Evaluation ev = evaluate(rig, c, 0, &s);
    const Vec3 hinge = mech::hind_hinge(true), at = before[knee].pos;
    const Vec3 want = at + Quat::axis_angle(hinge, 30 * kDegToRad).rotate(before[hock].pos - at);
    CHECK((ev.globals[hock].pos - want).length() < 1e-6);
    // The export is an ordinary .anim: the same bytes as the same turn keyed in SL's frame by its Euler angles.
    Clip typed;
    key_euler(typed, "mHindLimb2Left", 0, quat_to_euler(sl));
    c.end_frame = typed.end_frame = 10;
    const AnimExportResult x = export_anim(skel(), c), y = export_anim(skel(), typed);
    CHECK(x.errors.empty());
    CHECK(write_anim(x.file) == write_anim(y.file));
    CHECK(x.file.joints.size() == 1 && x.file.joints[0].name == "mHindLimb2Left");
    CHECK(!x.file.joints.empty() && !x.file.joints[0].rot.empty() && x.file.joints[0].pos.empty());
    if (!x.file.joints.empty() && !x.file.joints[0].rot.empty())
        CHECK(deg_between(decode_rotation(x.file.joints[0].rot[0]), sl) < 0.1);
}

TEST(auto_ik_default_chains) {
    const Skeleton& k = skel();
    CHECK(auto_ik_default_length(k, node("mWristLeft")) == 3);  // to the collar
    CHECK(auto_ik_default_length(k, node("mAnkleRight")) == 2);  // to the hip
    CHECK(auto_ik_default_length(k, node("mFootLeft")) == 3);
    CHECK(auto_ik_default_length(k, node("mHindLimb4Left")) == 3);  // to its first bone
    CHECK(auto_ik_default_length(k, node("mTail6")) == 5);
    CHECK(auto_ik_default_length(k, node("mWing4Left")) == 3);
    CHECK(auto_ik_default_length(k, node("mHandIndex3Left")) == 2);
    CHECK(auto_ik_default_length(k, node("mHead")) == 2);
    CHECK(auto_ik_default_length(k, node("mTorso")) == 0);  // only the pelvis above it
    CHECK(auto_ik_default_length(k, 0) == 0);
    CHECK(auto_ik_default_length(k, node("mFaceJaw")) == 0);
    const Rig rig(k);
    Clip c;
    AutoIkChain ch = auto_ik_chain(rig, c, 0, node("mWristLeft"), 0);
    CHECK(ch.bones == (std::vector<int>{node("mCollarLeft"), node("mShoulderLeft"), node("mElbowLeft")}));
    CHECK(auto_ik_chain(rig, c, 0, node("mWristLeft"), 1).bones == std::vector<int>{node("mElbowLeft")});
    // As long as it goes: up to mTorso, through Bento's mSpine3 and 4 (passed through, never turned or keyed).
    const AutoIkChain all = auto_ik_chain(rig, c, 0, node("mWristLeft"), 99);
    CHECK(all.bones.front() == node("mTorso"));  // never the pelvis, nor mSpine1 and 2 at the root
    CHECK(std::count_if(all.bones.begin(), all.bones.end(), [](int b) { return skel()[b].name.rfind("mSpine", 0) != 0; }) ==
          all.longest);
    CHECK(all.longest == 5);  // mTorso, mChest, collar, shoulder, elbow; mSpine3 and mSpine4 do not count
    Clip w;
    const Evaluation start = evaluate(rig, w, 0, nullptr);
    const std::vector<std::string> keyed =
        key_auto_ik(w, rig, 0, all, start, start.globals[node("mWristLeft")].pos + Vec3{0.1, 0, 0.1}, nullptr);
    CHECK(keyed.size() == 5 && !w.curves.count("mSpine3") && !w.curves.count("mSpine4"));
    key_offset(c, "mWristLeft", 0, {});
    CHECK(auto_ik_chain(rig, c, 0, node("mWristLeft"), 0).bones.empty());  // position keys: Move moves it
}

TEST(auto_ik_reaches_and_keeps_root) {
    const Rig rig(skel());
    for (const char* end : {"mAnkleLeft", "mWristRight", "mTail6", "mWing4Left", "mHindLimb4Right"}) {
        Clip c;
        const int e = node(end);
        const AutoIkChain ch = auto_ik_chain(rig, c, 0, e);
        CHECK(!ch.bones.empty());
        const Evaluation start = evaluate(rig, c, 0, nullptr);
        const int root = ch.bones.front(), up = skel()[root].parent;
        double len = 0;
        for (size_t k = 0; k < ch.bones.size(); ++k)
            len += ((k + 1 < ch.bones.size() ? start.globals[ch.bones[k + 1]].pos : start.globals[e].pos) -
                    start.globals[ch.bones[k]].pos).length();
        // A target about a fifth of the chain's length away, in towards the root and across: within reach.
        const Vec3 in = (start.globals[root].pos - start.globals[e].pos).normalized();
        const Vec3 across = (Vec3{0.1, 0.6, 0.5} - in * in.dot(Vec3{0.1, 0.6, 0.5})).normalized();
        const Vec3 target = start.globals[e].pos + (in + across) * (0.15 * len);
        const std::vector<std::string> keyed = key_auto_ik(c, rig, 0, ch, start, target, nullptr);
        CHECK(keyed.size() == ch.bones.size());
        const Evaluation ev = evaluate(rig, c, 0, nullptr);
        CHECK((ev.globals[e].pos - target).length() < 1e-3);
        CHECK((ev.globals[root].pos - start.globals[root].pos).length() < 1e-9);  // the chain's root stays
        CHECK(deg_between(ev.globals[up].rot, start.globals[up].rot) < 1e-9);
        // Out of reach: as near as it gets, pointing at it, nothing blows up.
        Clip far;
        key_auto_ik(far, rig, 0, ch, start, start.globals[root].pos + Vec3{0, 3, 0}, nullptr);
        const Evaluation fe = evaluate(rig, far, 0, nullptr);
        CHECK(finite(fe.globals[e].pos));
        CHECK((fe.globals[root].pos - start.globals[root].pos).length() < 1e-9);
        CHECK((fe.globals[e].pos - fe.globals[root].pos).normalized().dot(Vec3{0, 1, 0}) > 0.95);
        // Back where it began: the pose it began with.
        Clip back;
        key_auto_ik(back, rig, 0, ch, start, start.globals[e].pos, nullptr);
        for (int b : ch.bones) CHECK(deg_between(evaluate(rig, back, 0, nullptr).pose.rot[b], start.pose.rot[b]) < 1e-6);
    }
    // The knee only bends, about its hinge, and the right way.
    Clip c;
    const int ankle = node("mAnkleLeft"), knee = node("mKneeLeft");
    const Evaluation start = evaluate(rig, c, 0, nullptr);
    key_auto_ik(c, rig, 0, auto_ik_chain(rig, c, 0, ankle), start, start.globals[ankle].pos + Vec3{-0.1, 0, 0.25}, nullptr);
    const Quat bent = evaluate(rig, c, 0, nullptr).pose.rot[knee];
    const Vec3 axis = Vec3{bent.x, bent.y, bent.z}.normalized();
    CHECK(bent.angle() > 10 * kDegToRad);
    CHECK(axis.dot({0, 1, 0}) > 0.9999);  // SL's leg hinge, positive: the shin swings back
}

TEST(auto_ik_bends_about_rig_hinge) {
    DaeModel m;
    CHECK(load_mech(m));
    const Shape s = mech_shape(m);
    const Rig rig(skel());
    const int knee = node("mHindLimb2Left"), hock = node("mHindLimb3Left"), foot = node("mHindLimb4Left");
    const std::vector<Vec3> hinges = auto_ik_hinges(rig, &s);
    CHECK((hinges[knee] - mech::hind_hinge(true)).length() < 1e-4);  // the mech's own hinge, not SL's Y
    CHECK((hinges[hock] - mech::hind_hinge(true)).length() < 1e-4);
    CHECK(std::fabs(hinges[knee].dot({0, 1, 0})) < 0.95);
    CHECK((auto_ik_hinges(rig, nullptr)[knee] - Vec3{0, 1, 0}).length() < 1e-9);  // SL's without the body
    // The same mech rigged in SL's frames (no rig axes) bends its knee in the plane it is rigged bent in.
    {
        mech::Body flat = mech::build(skel());
        for (mech::Bone& b : flat.bones) b.axes = Quat{};
        DaeModel fm;
        DaeReport r;
        std::string err;
        CHECK(load_dae(mech::dae(skel(), flat), "", skel(), fm, r, err));
        Shape fs;
        shape_from_binds(skel(), {&fm}, nullptr, fs);
        CHECK(fs.axes.empty());
        const std::vector<Vec3> bent = auto_ik_hinges(rig, &fs);
        CHECK((bent[knee] - mech::hind_hinge(true)).length() < 1e-4);
        CHECK((bent[hock] - mech::hind_hinge(true)).length() < 1e-4);
        CHECK((bent[node("mKneeLeft")] - Vec3{0, 1, 0}).length() < 1e-9);  // a straight leg keeps SL's hinge
    }
    Clip c;
    const Evaluation start = evaluate(rig, c, 0, &s);
    const AutoIkChain ch = auto_ik_chain(rig, c, 0, foot);
    const Vec3 target = start.globals[foot].pos + Vec3{0.08, 0.02, 0.12};
    key_auto_ik(c, rig, 0, ch, start, target, &s);
    const Evaluation ev = evaluate(rig, c, 0, &s);
    CHECK((ev.globals[foot].pos - target).length() < 1e-3);
    for (int j : {knee, hock}) {  // each turned about the rig hinge only
        const Quat turn = skel()[j].rest * ev.pose.rot[j] * (skel()[j].rest * start.pose.rot[j]).conj();
        const Vec3 axis = Vec3{turn.x, turn.y, turn.z}.normalized();
        CHECK(turn.angle() > 0.5 * kDegToRad);
        CHECK(std::fabs(axis.dot(mech::hind_hinge(true))) > 0.9999);
    }
}

TEST(auto_ik_respects_pins_and_ik) {
    const Rig rig(skel());
    std::string why;
    // A held tail joint stays put: the chain stops below it.
    Clip c;
    CHECK(pin_here(c, rig, 0, node("mTail3"), -1, nullptr, why));
    const int tip = node("mTail6");
    const AutoIkChain ch = auto_ik_chain(rig, c, 0, tip);
    CHECK(ch.bones == (std::vector<int>{node("mTail4"), node("mTail5")}));
    const Evaluation start = evaluate(rig, c, 0, nullptr);
    const Vec3 target = start.globals[tip].pos + Vec3{0.06, 0.05, 0.04};  // in towards the held joint: within reach
    key_auto_ik(c, rig, 0, ch, start, target, nullptr);
    const Evaluation ev = evaluate(rig, c, 0, nullptr);
    CHECK((ev.globals[tip].pos - target).length() < 1e-3);
    const int held = node("mTail3");
    CHECK((ev.globals[held].pos - start.globals[held].pos).length() < 1e-9);
    CHECK(deg_between(ev.globals[held].rot, start.globals[held].rot) < 1e-6);
    CHECK(!c.curves.count("mTail3") && !c.curves.count("mTail2"));
    // A held wrist is moved through its pin, not Auto IK; nor is a bone its pin drives.
    Clip w;
    CHECK(pin_here(w, rig, 0, node("mWristLeft"), -1, nullptr, why));
    CHECK(auto_ik_chain(rig, w, 0, node("mWristLeft")).bones.empty());
    CHECK(!auto_ik_chain(rig, w, 0, node("mWristLeft")).why.empty());
    CHECK(auto_ik_chain(rig, w, 0, node("mElbowLeft")).bones.empty());
    // An arm in IK: its bones belong to the IK target; its fingers still drag.
    Clip k;
    switch_to_ik(k, rig, 0, rig.find_limb("ArmLeft"), nullptr);
    CHECK(auto_ik_chain(rig, k, 0, node("mWristLeft")).bones.empty());
    CHECK(auto_ik_chain(rig, k, 0, node("mHandIndex3Left")).bones.size() == 2);
}

// FP-2: a drag that pressed the body pulls the point pressed. A point on the foot's toe end, pulled forward and out,
// ends where the pointer took it; the ankle turns too, and its head is not what follows.
TEST(auto_ik_pulls_grabbed_point) {
    const Rig rig(skel());
    Clip c;
    const int ankle = node("mAnkleLeft");
    const Evaluation start = evaluate(rig, c, 0, nullptr);
    AutoIkChain ch = auto_ik_chain(rig, c, 0, ankle);
    const Xform& g = start.globals[ankle];
    const Vec3 grab = g.pos + Vec3{0.12, 0.02, -0.06};  // near the toes
    ch.bones.push_back(ankle);
    ++ch.turning;
    ch.grab_on = true;
    ch.grab = g.inverse().apply(grab);
    const Vec3 target = grab + Vec3{0.15, 0.1, 0.1};
    const std::vector<std::string> keyed = key_auto_ik(c, rig, 0, ch, start, target, nullptr);
    CHECK(std::find(keyed.begin(), keyed.end(), "mAnkleLeft") != keyed.end());
    const Evaluation ev = evaluate(rig, c, 0, nullptr);
    CHECK((ev.globals[ankle].apply(ch.grab) - target).length() < 1e-3);
    CHECK((ev.globals[ankle].pos - target).length() > 0.05);
}

// Addendum: pull the mech hind leg, an SL leg, and an arm outward past full extension in small steps.
// The solve must not stick near extension or suddenly buckle into a different pose.
TEST(auto_ik_drag_extension_smooth) {
    DaeModel m;
    CHECK(load_mech(m));
    const Shape mech_s = mech_shape(m);
    const Rig rig(skel());

    struct LimbCase {
        const char* name;
        const char* end_joint;
        const Shape* shape;
        Vec3 initial_bend;  // joint, axis, angle to bend first if straight
        const char* bend_joint;
        double bend_deg;
    };

    const LimbCase cases[] = {
        {"mech_hind_leg", "mHindLimb4Left", &mech_s, {}, nullptr, 0},
        {"sl_leg", "mAnkleLeft", nullptr, {0, 1, 0}, "mKneeLeft", 45},
        {"sl_arm", "mWristLeft", nullptr, {0, 0, 1}, "mElbowLeft", 45},
    };

    for (const auto& tc : cases) {
        Clip c;
        if (tc.bend_joint) {
            key_rotation(c, tc.bend_joint, 0, Quat::axis_angle(tc.initial_bend, tc.bend_deg * kDegToRad));
        }
        const int end = node(tc.end_joint);
        const AutoIkChain ch = auto_ik_chain(rig, c, 0, end);
        CHECK(!ch.bones.empty());
        Evaluation cur = evaluate(rig, c, 0, tc.shape);
        const int root = ch.bones.front();
        const Vec3 root_pos = cur.globals[root].pos;
        const Vec3 start_end = cur.globals[end].pos;

        // Total chain reach
        double chain_len = 0;
        for (size_t k = 0; k < ch.bones.size(); ++k) {
            const Vec3 p0 = cur.globals[ch.bones[k]].pos;
            const Vec3 p1 = (k + 1 < ch.bones.size()) ? cur.globals[ch.bones[k + 1]].pos : cur.globals[end].pos;
            chain_len += (p1 - p0).length();
        }

        const Vec3 dir = (start_end - root_pos).normalized();
        const double start_dist = (start_end - root_pos).length();
        const double max_dist = chain_len * 1.35;
        const int steps = 100;

        double prev_proj = (start_end - root_pos).dot(dir);
        Pose prev_pose = cur.pose;

        for (int i = 1; i <= steps; ++i) {
            const double dist = start_dist + (max_dist - start_dist) * (double(i) / steps);
            const Vec3 target = root_pos + dir * dist;

            // Solve step from previous step's pose (simulating pointer drag)
            key_auto_ik(c, rig, 0, ch, cur, target, tc.shape, &cur.pose);
            cur.globals = skel().global_pose(cur.pose, tc.shape);

            // 1. No bone rotation changes wildly in a single small step (no sudden buckling)
            for (int b : ch.bones) {
                const double d = deg_between(cur.pose.rot[b], prev_pose.rot[b]);
                CHECK(d < 10.0);
            }
            prev_pose = cur.pose;

            // 2. End moves monotonically toward target direction (within small tolerance)
            const double proj = (cur.globals[end].pos - root_pos).dot(dir);
            CHECK(proj >= prev_proj - 1e-3);
            prev_proj = std::max(prev_proj, proj);
        }
    }
}

