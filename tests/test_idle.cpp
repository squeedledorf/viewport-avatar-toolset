#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/idle.h"
#include "vats/loop_tools.h"
#include "vats/project.h"

using namespace vats;

namespace {

double deg(const Quat& q) { return q.angle() * kRadToDeg; }

// Loops 6..42 inside a 48-frame clip; the chest already has a seamless nod of its own.
Clip loop_clip() {
    Clip c;
    c.end_frame = 48;
    c.loop = true;
    c.loop_in = 6, c.loop_out = 42;
    key_euler(c, "mChest", 6, {0, 5, 0});
    key_euler(c, "mChest", 24, {0, -5, 0});
    key_euler(c, "mChest", 42, {0, 5, 0});
    return c;
}

}  // namespace

TEST(idle_period_snaps_to_loop) {
    Clip c = loop_clip();  // loop of 36 frames at 30 fps
    IdleLayer l = idle_preset("breath");
    l.period = 1.7;  // 51 frames: one breath per loop
    CHECK(std::fabs(idle_period_frames(c, l) - 36) < 1e-9);
    l.period = 0.4;  // 12 frames: three per loop
    CHECK(std::fabs(idle_period_frames(c, l) - 12) < 1e-9);
    l.period = 0.5;  // 15 frames: 2.4 per loop, snapped to 2
    CHECK(std::fabs(idle_period_frames(c, l) - 18) < 1e-9);
}

TEST(idle_no_seam_jump) {
    for (const char* kind : {"breath", "sway"})
        for (double period : {0.9, 2.3, 7.0}) {
            Clip c = loop_clip();
            IdleLayer l = idle_preset(kind);
            l.period = period, l.amplitude = 3, l.seed = 11;
            // The layer itself: loop-in and loop-out are the same point of the motion.
            Pose a(skel().size()), b(skel().size());
            apply_idle(skel(), c, l, c.loop_in, a);
            apply_idle(skel(), c, l, c.loop_out, b);
            for (int n = 0; n < int(skel().size()); ++n) {
                CHECK(deg(a.rot[n].conj() * b.rot[n]) < 1e-6);
                CHECK((a.offset[n] - b.offset[n]).length() < 1e-9);
            }
            // Baked, the curves stay within the bake's tolerance at the seam.
            c.idle.push_back(l);
            bake_idle(c, skel());
            CHECK(loop_seam_jumps(c, 0.05, 0.0001).empty());
        }
}

TEST(idle_amplitude_bounded) {
    Clip c;
    c.end_frame = c.loop_out = 900;
    for (const char* kind : {"breath", "sway"}) {
        IdleLayer l = idle_preset(kind);
        l.amplitude = 2, l.period = 1.0, l.seed = 3;
        double peak = 0, rise = 0;
        for (int f = 0; f <= c.end_frame; ++f) {
            Pose p(skel().size());
            apply_idle(skel(), c, l, f, p);
            for (int n : idle_nodes(skel(), l)) {
                peak = std::max(peak, deg(p.rot[n]));
                rise = std::max(rise, p.offset[n].length());
            }
        }
        CHECK(peak <= 2 + 1e-9);
        CHECK(peak > 1);                   // and it really moves
        CHECK(rise <= 0.002 + 1e-12);      // a breath's mTorso rise: amplitude millimetres
    }
}

TEST(idle_same_seed_same_keys) {
    Clip c = loop_clip();
    c.idle.push_back(idle_preset("sway"));
    c.idle.push_back(idle_preset("breath"));
    Clip a = c, b = c;
    bake_idle(a, skel());
    bake_idle(b, skel());
    CHECK(a == b);
    Clip d = c;
    d.idle[0].seed = 2;
    bake_idle(d, skel());
    CHECK(d.curves != a.curves);
    // Re-baking starts from the pre-bake tracks; unbaking puts them back.
    Clip again = a;
    bake_idle(again, skel());
    CHECK(again == a);
    unbake_idle(again, skel(), 1);
    unbake_idle(again, skel(), 0);
    CHECK(again.curves == c.curves);
}

TEST(idle_layers_sharing_bones_unbake_alone) {
    Clip c = loop_clip();
    c.idle.push_back(idle_preset("sway"));
    c.idle.push_back(idle_preset("breath"));  // both move mChest
    Clip only_breath = c;
    bake_idle(only_breath, skel(), 1);
    Clip both = c;
    bake_idle(both, skel(), 1);
    bake_idle(both, skel(), 0);
    unbake_idle(both, skel(), 0);
    CHECK(both == only_breath);
    unbake_idle(both, skel(), 1);
    CHECK(both == c);
}

TEST(idle_skips_face_bones) {
    IdleLayer l = idle_preset("sway");
    l.bones = {"mHead", "mFaceJaw", "mEyeLeft", "mNope"};
    auto nodes = idle_nodes(skel(), l);
    CHECK_EQ(int(nodes.size()), 1);
    CHECK(skel()[nodes[0]].name == "mHead");
}

TEST(idle_breath_keeps_torso_rotation) {
    Clip c = loop_clip();
    key_euler(c, "mTorso", 10, {0, 0, 12});
    const Track torso = c.curves["mTorso"];
    c.idle.push_back(idle_preset("breath"));
    bake_idle(c, skel());
    for (const char* ch : kRotChannels) CHECK(c.curves["mTorso"][ch] == torso.at(ch));
    CHECK(c.has_channels("mTorso", kPosChannels));
}

TEST(idle_project_round_trip) {
    Project p;
    p.clip = loop_clip();
    p.clip.idle.push_back(idle_preset("breath"));
    p.clip.idle.push_back(idle_preset("sway"));
    p.clip.idle[1].extra.set("future", 7);
    bake_idle(p.clip, skel(), 0);
    Project q;
    std::string err;
    CHECK(load_project(save_project(p), q, err, ""));
    CHECK(q.clip.idle == p.clip.idle);
}
