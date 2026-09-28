#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/dynamics.h"
#include "vats/edit.h"
#include "vats/project.h"

using namespace vats;

namespace {

const Rig& rig() {
    static Rig r(skel());
    return r;
}

// The pelvis turns 45 degrees about Z by frame 10, back by frame 20, then holds until end.
Clip swing_clip(int end = 60) {
    Clip c;
    c.end_frame = end;
    c.loop_out = end;
    key_euler(c, "mPelvis", 0, {0, 0, 0});
    key_euler(c, "mPelvis", 10, {0, 0, 45});
    key_euler(c, "mPelvis", 20, {0, 0, 0});
    return c;
}

double deg(const Quat& q) { return q.angle() * kRadToDeg; }

std::vector<Pose> run(const Clip& c) {
    DynSim sim(skel(), c.dynamics);
    return simulate_frames(rig(), c, nullptr, sim);
}

}  // namespace

TEST(dynamics_chain_nodes) {
    DynChain t = dyn_preset("tail", "mTail1", 6);
    auto nodes = dyn_nodes(skel(), t);
    CHECK_EQ(int(nodes.size()), 6);
    CHECK(skel()[nodes[5]].name == "mTail6");
    CHECK_EQ(int(dyn_nodes(skel(), dyn_preset("jiggle", "BELLY", 3)).size()), 1);
    CHECK(dyn_nodes(skel(), dyn_preset("tail", "Chest", 2)).empty());  // attachment points do not swing
}

TEST(dynamics_deterministic) {
    Clip c = swing_clip();
    c.dynamics.push_back(dyn_preset("tail", "mTail1", 6));
    Clip a = c, b = c;
    bake_dynamics(a, rig(), nullptr);
    bake_dynamics(b, rig(), nullptr);
    CHECK(a == b);
}

TEST(dynamics_still_parent_no_motion) {
    Clip c;
    c.end_frame = 30;
    DynChain t = dyn_preset("tail", "mTail1", 6);
    t.gravity = 0;
    c.dynamics.push_back(t);
    c.dynamics.push_back(dyn_preset("jiggle", "BELLY", 1));
    auto frames = run(c);
    int belly = skel().find("BELLY");
    for (const Pose& p : frames) {
        for (int n : dyn_nodes(skel(), t)) CHECK(deg(p.rot[n]) < 1e-6);
        CHECK(p.offset[belly].length() < 1e-9);
    }
}

TEST(dynamics_swing_lags_then_settles) {
    Clip c = swing_clip(80);
    DynChain t = dyn_preset("tail", "mTail1", 6);
    t.gravity = 0;
    c.dynamics.push_back(t);
    auto frames = run(c);
    int tail1 = skel().find("mTail1");
    CHECK(deg(frames[8].rot[tail1]) > 2);    // trails the turning pelvis
    CHECK(deg(frames[80].rot[tail1]) < 0.5);  // 60 frames after the pelvis stopped
}

TEST(dynamics_loop_ends_where_it_starts) {
    Clip c;
    c.end_frame = c.loop_out = 30;
    c.loop = true;
    key_euler(c, "mPelvis", 0, {0, 0, -20});
    key_euler(c, "mPelvis", 15, {0, 0, 20});
    key_euler(c, "mPelvis", 30, {0, 0, -20});
    c.dynamics.push_back(dyn_preset("tail", "mTail1", 6));
    auto frames = run(c);
    for (int n : dyn_nodes(skel(), c.dynamics[0])) {
        double d = deg(frames[30].rot[n].conj() * frames[0].rot[n]);
        CHECK(d < 1.0);
    }
    CHECK(deg(frames[15].rot[skel().find("mTail3")]) > 1);  // and it does move
}

TEST(dynamics_bake_matches_simulation) {
    Clip c = swing_clip();
    c.dynamics.push_back(dyn_preset("tail", "mTail1", 6));
    c.dynamics.push_back(dyn_preset("jiggle", "BELLY", 1));
    auto frames = run(c);
    Clip baked = c;
    bake_dynamics(baked, rig(), nullptr);
    CHECK(baked.dynamics[0].baked && baked.dynamics[1].baked);
    int belly = skel().find("BELLY");
    for (int f = 0; f <= c.end_frame; ++f) {
        Evaluation e = evaluate(rig(), baked, f, nullptr);
        for (int n : dyn_nodes(skel(), c.dynamics[0])) CHECK(deg(e.pose.rot[n].conj() * frames[f].rot[n]) < 0.3);
        CHECK((e.pose.offset[belly] - frames[f].offset[belly]).length() < 0.001);
    }
    // Re-baking starts from the pre-bake tracks, so it gives the same keys.
    Clip again = baked;
    bake_dynamics(again, rig(), nullptr);
    CHECK(again == baked);
    // Unbaking puts the original tracks back.
    unbake_dynamics(again, skel(), 0);
    unbake_dynamics(again, skel(), 1);
    CHECK(again.curves == c.curves);
}

TEST(dynamics_collision_keeps_chain_outside) {
    Clip c = swing_clip();
    DynChain t = dyn_preset("tail", "mTail1", 6);
    t.gravity = 4;
    t.stiffness = 0.01;
    t.radius = 0.08;
    c.dynamics.push_back(t);
    auto frames = run(c);
    auto nodes = dyn_nodes(skel(), t);
    int touching = 0;
    for (int f = 0; f <= c.end_frame; ++f) {
        auto sim = skel().global_pose(frames[f]);
        auto anim = evaluate(rig(), c, f, nullptr).globals;
        for (size_t i = 0; i + 1 < nodes.size(); ++i) {
            Vec3 tip = sim[nodes[i + 1]].pos, target = anim[nodes[i + 1]].pos;
            for (const CollisionVolume& v : skel().volumes()) {
                Vec3 s = v.scale + Vec3{t.radius, t.radius, t.radius};
                auto unit = [&](const Vec3& w) {
                    Vec3 q = sim[v.node].inverse().apply(w);
                    return Vec3{q.x / s.x, q.y / s.y, q.z / s.z}.length();
                };
                if (unit(target) < 1) continue;  // the rest pose is inside this one: not an obstacle
                // Bone length wins over collision where a tip is wedged against a volume; allow a shallow graze.
                CHECK(unit(tip) > 0.9);
                touching += unit(tip) < 1.02;
            }
        }
    }
    CHECK(touching > 0);  // the drooping tail really reached a volume
}

TEST(dynamics_project_round_trip) {
    Project p;
    p.clip = swing_clip();
    p.clip.dynamics.push_back(dyn_preset("tail", "mTail1", 6));
    p.clip.dynamics.push_back(dyn_preset("jiggle", "BELLY", 1));
    p.clip.dynamics[1].extra.set("future", 7);
    bake_dynamics(p.clip, rig(), nullptr, 0);
    Project q;
    std::string err;
    CHECK(load_project(save_project(p), q, err, ""));
    CHECK(q.clip.dynamics == p.clip.dynamics);
    p.clip.dynamics[0].bend = 25;  // the bend limit saves when set
    CHECK(load_project(save_project(p), q, err, ""));
    CHECK(q.clip.dynamics == p.clip.dynamics);
}

namespace {

// The largest angle a bone of the chain turns from one frame to the next, and from its animated pose (world
// directions of the bone), over the clip.
std::pair<double, double> worst_turns(const Clip& c, const DynChain& chain) {
    const auto frames = run(c);
    const auto nodes = dyn_nodes(skel(), chain);
    double per_frame = 0, off = 0;
    std::vector<Vec3> prev;
    for (int f = 0; f <= c.end_frame; ++f) {
        const auto sim = skel().global_pose(frames[f]), anim = evaluate(rig(), c, f, nullptr).globals;
        std::vector<Vec3> dirs;
        for (int n : nodes) {
            const Vec3 d = sim[n].rot.rotate(skel()[n].end).normalized(), a = anim[n].rot.rotate(skel()[n].end).normalized();
            off = std::max(off, std::acos(std::clamp(d.dot(a), -1.0, 1.0)) * kRadToDeg);
            if (!prev.empty())
                per_frame = std::max(per_frame, std::acos(std::clamp(d.dot(prev[dirs.size()]), -1.0, 1.0)) * kRadToDeg);
            dirs.push_back(d);
        }
        prev = dirs;
    }
    return {per_frame, off};
}

}  // namespace

// Extreme settings stay stable: a spring chain amplifies the swing bone by bone, and with full stiffness or no
// damping the tail's tip flailed past 130 degrees a frame. Every bone now turns at most 720 degrees a second (24 a
// frame at 30 fps) beyond what the animation turns it.
TEST(dynamics_extreme_settings_stay_stable) {
    for (auto [stiffness, damping] : {std::pair{1.0, 0.0}, {1.0, 0.12}, {0.5, 0.12}, {0.08, 0.0}, {0.08, 0.12}}) {
        Clip c = swing_clip();
        DynChain t = dyn_preset("tail", "mTail1", 6);
        t.stiffness = stiffness, t.damping = damping;
        c.dynamics.push_back(t);
        const auto [per_frame, off] = worst_turns(c, t);
        if (per_frame > 35) std::fprintf(stderr, "  stiffness %.2f damping %.2f: %.1f degrees a frame\n", stiffness, damping, per_frame);
        CHECK(per_frame <= 35);  // 24 of its own and the hips' 4.5 a frame, with room for the collisions' push
        (void)off;
    }
}

// Bend limit: no bone of the chain bends further than it from its animated pose (relative to its simulated parent,
// so the tip can still sit further from its animated place in the world).
TEST(dynamics_bend_limit) {
    Clip c = swing_clip();
    DynChain t = dyn_preset("tail", "mTail1", 6);
    t.stiffness = 0.02, t.gravity = 1, t.radius = 0;
    c.dynamics.push_back(t);
    auto free_frames = run(c);
    c.dynamics[0].bend = 10;
    auto frames = run(c);
    const auto nodes = dyn_nodes(skel(), t);
    double free_bend = 0, bend = 0;
    for (int f = 0; f <= c.end_frame; ++f) {
        const Pose anim = evaluate(rig(), c, f, nullptr).pose;
        for (int n : nodes) {
            free_bend = std::max(free_bend, deg(anim.rot[n].conj() * free_frames[f].rot[n]));
            bend = std::max(bend, deg(anim.rot[n].conj() * frames[f].rot[n]));
        }
    }
    CHECK(free_bend > 20);   // a soft, heavy tail bends a lot without it
    CHECK(bend < 10.5);      // and no bone bends more than the limit with it
}

// Radius keeps the chain further out; a large one used to switch collisions off for every volume within that
// distance of the animated tail (the thighs and the butt), so the swinging tail went through them.
TEST(dynamics_radius_does_not_switch_collisions_off) {
    auto deepest = [](double radius) {
        Clip c = swing_clip();
        key_euler(c, "mTail1", 0, {0, -80, 0});  // hanging down behind the thighs
        DynChain t = dyn_preset("tail", "mTail1", 6);
        t.gravity = 1, t.stiffness = 0.02, t.radius = radius;
        c.dynamics.push_back(t);
        const auto frames = run(c);
        const auto nodes = dyn_nodes(skel(), t);
        double depth = 0;  // how far into a volume (its own size, 1 = the surface) a bone's tail gets
        for (int f = 0; f <= c.end_frame; ++f) {
            const auto sim = skel().global_pose(frames[f]), anim = evaluate(rig(), c, f, nullptr).globals;
            for (size_t i = 0; i < nodes.size(); ++i)
                for (const CollisionVolume& v : skel().volumes()) {
                    const Vec3 tip = sim[nodes[i]].apply(skel()[nodes[i]].end), atip = anim[nodes[i]].apply(skel()[nodes[i]].end);
                    auto unit = [&](const std::vector<Xform>& g, const Vec3& w) {
                        const Vec3 q = g[v.node].inverse().apply(w);
                        return Vec3{q.x / v.scale.x, q.y / v.scale.y, q.z / v.scale.z}.length();
                    };
                    if (unit(anim, atip) < 1) continue;  // the animated tail is in it: not an obstacle
                    depth = std::max(depth, 1 - unit(sim, tip));
                }
        }
        return depth;
    };
    const double none = deepest(0), wide = deepest(0.15);
    CHECK(wide <= none + 0.005);  // it went 3% of the thigh's size in
}

// A chain from mWing2 takes the fan bone beside the tip (a branch off mWing3) and bakes it with the rest.
TEST(dynamics_chain_branches_into_the_fan) {
    DynChain w = dyn_preset("tail", "mWing2Left", 3);
    std::vector<std::string> names;
    for (int n : dyn_nodes(skel(), w)) names.push_back(skel()[n].name);
    CHECK((names == std::vector<std::string>{"mWing2Left", "mWing3Left", "mWing4Left", "mWing4FanLeft"}));
    // A head's children do not start at one point: a chain from the neck stays on its first-child path.
    CHECK_EQ(int(dyn_nodes(skel(), dyn_preset("overlap", "mNeck", 3)).size()), 3);
    Clip c;
    c.end_frame = 30, c.loop_out = 30;
    key_euler(c, "mWing1Left", 0, {0, 0, 0});
    key_euler(c, "mWing1Left", 8, {40, 0, 0});
    key_euler(c, "mWing1Left", 16, {0, 0, 0});
    c.dynamics.push_back(w);
    bake_dynamics(c, rig(), nullptr);
    CHECK(c.curves.count("mWing4FanLeft") && !c.curves["mWing4FanLeft"]["rot_x"].empty());
    // The fan swings with its own lag, not rigidly with mWing3.
    double moved = 0;
    for (int f = 0; f <= 30; ++f) moved = std::max(moved, deg(evaluate_curves(skel(), c, f).rot[skel().find("mWing4FanLeft")]));
    CHECK(moved > 1);
}
