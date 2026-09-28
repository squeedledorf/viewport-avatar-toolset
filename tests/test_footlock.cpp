// Foot-contact clean-up (07 RT-9, 08 FC) and clip splitting (07 RT-10.4).
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/edit.h"
#include "vats/footlock.h"
#include "vats/retarget.h"

using namespace vats;

namespace {

// Two seconds of a crouched shuffle: the hips glide forward 10 cm (so a planted foot slides), the
// right foot stays down, the left lifts between frames 24 and 34.
Clip shuffle() {
    Clip c;
    c.fps = 30;
    c.end_frame = 60;
    key_offset(c, "mPelvis", 0, {0, 0, 0});
    key_offset(c, "mPelvis", 60, {0.10, 0, 0});
    for (const char* side : {"Left", "Right"})
        for (int f : {0, 60}) {
            key_euler(c, std::string("mHip") + side, f, {0, -25, 0});
            key_euler(c, std::string("mKnee") + side, f, {0, 50, 0});
            key_euler(c, std::string("mAnkle") + side, f, {0, -25, 0});
        }
    for (int f : {18, 40}) key_euler(c, "mHipLeft", f, {0, -25, 0}), key_euler(c, "mKneeLeft", f, {0, 50, 0});
    for (int f : {24, 34}) key_euler(c, "mHipLeft", f, {0, -70, 0}), key_euler(c, "mKneeLeft", f, {0, 120, 0});
    return c;
}

int leg(const Rig& rig, const char* name) {
    for (int i = 0; i < int(rig.limbs().size()); ++i)
        if (rig.limbs()[i].name == name) return i;
    return -1;
}

std::string read_text(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The floor and each foot point's height above it at rest (as footlock takes them).
struct Soles {
    double floor = 1e9;
    std::vector<Xform> rest;
    explicit Soles(const Skeleton& s) : rest(s.global_pose(Pose(s.size()))) {
        for (const char* n : {"mAnkleLeft", "mFootLeft", "mToeLeft"})
            if (int i = s.find(n); i >= 0) floor = std::min(floor, rest[i].pos.z);
    }
    double sole(const std::vector<Xform>& g, int node) const { return g[node].pos.z - (rest[node].pos.z - floor); }
};

// Two 1 s strides at 30 fps with a heel-toe roll, the lowest sole point 8 cm above the floor all through
// (the pelvis bobs to keep it there) except that the right foot steps 3 cm higher still (as a take whose
// foot markers sit off), and the hips 5% faster than the planted feet carry them, so the feet skate
// forward while down. Each leg: stance half the cycle (heel strike toes-up 15 degrees, flat from 15% of
// it, toes down to 25 degrees over its last 30%), then swing; the knees stay nearly straight.
Clip heel_toe_walk(const Skeleton& s) {
    Clip c;
    c.fps = 30;
    c.end_frame = 60;
    auto t_phase = [](int f) { return f / 30.0 - std::floor(f / 30.0); };
    auto leg = [&](const char* side, int f, double phase) {
        double hip, knee = 8, pitch;
        if (phase < 0.5) {
            const double u = phase / 0.5;
            hip = -20 + 35 * u;
            pitch = u < 0.15 ? -15 * (1 - u / 0.15) : u < 0.7 ? 0 : 25 * (u - 0.7) / 0.3;
        } else {
            const double u = (phase - 0.5) / 0.5;
            hip = 15 - 35 * u, knee += 50 * std::sin(kPi * u), pitch = 25 * (1 - u) - 15 * u;
        }
        key_euler(c, std::string("mHip") + side, f, {0, hip, 0});
        key_euler(c, std::string("mKnee") + side, f, {0, knee, 0});
        key_euler(c, std::string("mAnkle") + side, f, {0, pitch - hip - knee, 0});
    };
    for (int f = 0; f <= 60; ++f) {
        leg("Left", f, t_phase(f));
        leg("Right", f, t_phase(f + 15));
    }
    // The hips' speed: how far the stance sweeps a planted ankle back, over the stance's time.
    Rig rig(s);
    const int la = s.find("mAnkleLeft");
    const double sweep = evaluate(rig, c, 0, nullptr).globals[la].pos.x - evaluate(rig, c, 15, nullptr).globals[la].pos.x;
    const double v = sweep / 0.5 * 1.05;
    const Soles so(s);
    const int pts[] = {la, s.find("mToeLeft"), s.find("mAnkleRight"), s.find("mToeRight")};
    std::vector<Vec3> off;
    for (int f = 0; f <= 60; ++f) {
        const auto g = evaluate(rig, c, f, nullptr).globals;
        double low = 1e9;
        for (int p : pts) low = std::min(low, so.sole(g, p));
        const bool right_down = t_phase(f + 15) < 0.5;
        off.push_back({v * f / 30.0, 0, so.floor + 0.08 + (right_down ? 0.03 : 0) - low});
    }
    for (int f = 0; f <= 60; ++f) key_offset(c, "mPelvis", f, off[f]);
    return c;
}

// 180 minus the knee's bend from rest (its hinge turn): 180 is as straight as the solver makes a leg.
double knee_deg(const Evaluation& e, const Skeleton& s, const char* knee) {
    return 180 - e.pose.rot[s.find(knee)].angle() * kRadToDeg;
}

}  // namespace

TEST(footlock_heel_toe_walk) {
    const Skeleton& s = skel();
    Rig rig(s);
    Clip c = heel_toe_walk(s);
    const Clip before = c;
    CHECK_NEAR(foot_ground(rig, c, {}), 0.08, 1e-4);
    // Before the fix the feet skate.
    const int la = s.find("mAnkleLeft");
    CHECK((evaluate(rig, before, 10, nullptr).globals[la].pos - evaluate(rig, before, 2, nullptr).globals[la].pos).length() > 0.01);

    // Every stance: the heel lands first, then the toe, which leaves last.
    const auto contacts = find_foot_contacts(rig, c, {});
    int stances = 0;
    for (auto& h : contacts) {
        if (h.toe) continue;
        for (auto& t : contacts)
            if (t.toe && t.limb == h.limb && t.from <= h.to + 1 && t.to >= h.from) {
                CHECK(h.from < t.from);
                CHECK(t.to >= h.to);
                ++stances;
            }
    }
    CHECK(stances >= 3);

    FootLockOptions o;
    o.to_ground = true;
    const auto report = lock_feet(c, rig, o);
    bool lowered = false, grounded = false;
    for (auto& line : report) {
        lowered |= line.find("pelvis lowered") != std::string::npos;
        grounded |= line.find("ground: 8.0 cm above the floor; feet put on the ground") != std::string::npos;
    }
    CHECK(lowered);  // the right foot is snapped 3 cm lower than the take has it: out of reach
    CHECK(grounded);
    CHECK_NEAR(foot_ground(rig, c, {}), 0.0, 0.002);  // the feet touch the floor

    const Soles so(s);
    std::vector<Evaluation> ev;
    for (int f = 0; f <= c.end_frame; ++f) ev.push_back(evaluate(rig, c, f, nullptr));
    // Planted points stay put, on the floor, while they hold the foot (the lower of heel and toe when both
    // are down).
    double worst = 0;
    for (auto& k : contacts) {
        const int ankle = rig.limbs()[k.limb].end, toe = s.find(k.limb == rig.find_limb("LegLeft") ? "mToeLeft" : "mToeRight");
        const int p = k.toe ? toe : ankle, other = k.toe ? ankle : toe;
        bool first = true;
        Vec3 at;
        for (int f = k.from; f <= k.to; ++f) {
            const auto& g = ev[f].globals;
            if (so.sole(g, p) > so.sole(g, other) + 0.001) continue;
            if (first) at = g[p].pos, first = false;
            worst = std::max(worst, (g[p].pos - at).length());
            CHECK_NEAR(so.sole(g, p), so.floor, 0.002);
        }
    }
    CHECK(worst < 0.002);
    // Out of reach the hips come down instead: the knee never snaps straight.
    double widest = 0;
    for (auto& e : ev) widest = std::max({widest, knee_deg(e, s, "mKneeLeft"), knee_deg(e, s, "mKneeRight")});
    CHECK(widest <= 178);
}

TEST(footlock_ankle_only_without_toes) {
    // The skeleton without its toe bones: contacts and holds fall back to the ankle.
    std::string xml = read_text(std::string(VATS_DATA_DIR) + "/avatar_skeleton.xml");
    for (const char* name : {"name=\"mToeLeft\"", "name=\"mToeRight\""}) {
        const size_t at = xml.find(name), from = xml.rfind("<bone", at), to = xml.find("/>", at);
        xml.erase(from, to + 2 - from);
    }
    xml.replace(xml.find("num_bones=\"133\""), 15, "num_bones=\"131\"");
    Skeleton s;
    std::string err;
    CHECK(s.load(xml, read_text(std::string(VATS_DATA_DIR) + "/avatar_lad.xml"), err));
    CHECK(s.find("mToeLeft") < 0);
    Rig rig(s);
    Clip c = heel_toe_walk(skel());
    const auto contacts = find_foot_contacts(rig, c, {});
    CHECK(contacts.size() >= 3);
    for (auto& k : contacts) CHECK(!k.toe);
    const auto report = lock_feet(c, rig, {});
    CHECK(report[0].find("foot contacts held still") != std::string::npos);
    for (auto& k : contacts) {
        const int ankle = rig.limbs()[k.limb].end;
        const Vec3 at = evaluate(rig, c, k.from, nullptr).globals[ankle].pos;
        for (int f = k.from; f <= k.to; ++f) CHECK((evaluate(rig, c, f, nullptr).globals[ankle].pos - at).length() < 0.002);
    }
}


TEST(footlock_finds_contacts_not_swing) {
    Rig rig(skel());
    Clip c = shuffle();
    auto contacts = find_foot_contacts(rig, c, {});
    const int L = leg(rig, "LegLeft"), R = leg(rig, "LegRight");
    int left = 0;
    bool right_whole = false;
    for (auto& k : contacts) {
        if (k.toe) continue;
        if (k.limb == L) {
            ++left;
            CHECK(k.to < 24 || k.from > 34);  // never during the swing
        }
        if (k.limb == R && k.from <= 5 && k.to >= 55) right_whole = true;
    }
    CHECK_EQ(left, 2);
    CHECK(right_whole);
}

TEST(footlock_holds_planted_feet) {
    const Skeleton& s = skel();
    Rig rig(s);
    Clip c = shuffle();
    const Clip before = c;
    auto contacts = find_foot_contacts(rig, c, {});
    auto report = lock_feet(c, rig, {});
    CHECK(!report.empty());
    double worst = 0;
    for (auto& k : contacts) {
        if (k.toe) continue;  // the crouched feet stay flat: the heel holds them
        const int ankle = rig.limbs()[k.limb].end;
        const Vec3 planted = evaluate(rig, c, k.from, nullptr).globals[ankle].pos;
        for (int f = k.from; f <= k.to; ++f)
            worst = std::max(worst, (evaluate(rig, c, f, nullptr).globals[ankle].pos - planted).length());
    }
    CHECK(worst < 0.002);
    // Mid-swing the left leg is untouched.
    const int la = s.find("mAnkleLeft");
    for (int f = 27; f <= 31; ++f)
        CHECK_NEAR((evaluate(rig, c, f, nullptr).globals[la].pos - evaluate(rig, before, f, nullptr).globals[la].pos).length(),
                   0.0, 1e-6);
    // Before the fix the right foot slid most of the 10 cm.
    const int ra = s.find("mAnkleRight");
    CHECK((evaluate(rig, before, 55, nullptr).globals[ra].pos - evaluate(rig, before, 5, nullptr).globals[ra].pos).length() > 0.07);
    // Exported, the lock is baked: the .anim holds the foot too.
    AnimExportOptions o;
    AnimImportResult back = import_anim(s, export_anim(s, c, o).file);
    const Vec3 p5 = evaluate(rig, back.clip, 5, nullptr).globals[ra].pos, p55 = evaluate(rig, back.clip, 55, nullptr).globals[ra].pos;
    CHECK((p55 - p5).length() < 0.01);
}

TEST(retarget_slice_keeps_shape) {
    Clip c = shuffle();
    Clip part = slice_clip(c, 20, 45);
    CHECK_EQ(part.end_frame, 25);
    for (int t = 0; t <= 25; ++t) {
        Vec3 a = curve_euler(c, "mKneeLeft", 20 + t), b = curve_euler(part, "mKneeLeft", t);
        CHECK_NEAR((a - b).length(), 0.0, 1e-6);
        CHECK_NEAR((curve_offset(c, "mPelvis", 20 + t) - curve_offset(part, "mPelvis", t)).length(), 0.0, 1e-9);
    }
}

TEST(retarget_split_parts_fit) {
    const Skeleton& s = skel();
    // 150 s at 30 fps, every body joint keyed every frame with noise-like motion: too long and too big.
    Clip c;
    c.fps = 30;
    c.end_frame = 4500;
    int j = 0;
    for (int n = 0; n < s.joint_count() && j < 40; ++n) {
        if (s[n].category != Category::Body) continue;
        ++j;
        for (int f = 0; f <= c.end_frame; f += 2)
            key_euler(c, s[n].name, f, {20 * std::sin(f * 0.37 + n), 15 * std::sin(f * 0.23 + 2 * n), 10 * std::sin(f * 0.51)});
    }
    std::vector<FitReport> reps;
    std::vector<Clip> parts = split_to_fit(s, c, {}, &reps);
    CHECK(parts.size() >= 3);
    CHECK_EQ(reps.size(), parts.size());
    int covered = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        CHECK(parts[i].end_frame / double(parts[i].fps) <= 60);
        CHECK(reps[i].fits);
        AnimExportOptions o;
        o.reduce_rot_deg = reps[i].rot_tol_deg;
        o.reduce_pos_m = reps[i].pos_tol_m;
        CHECK(write_anim(export_anim(s, parts[i], o).file).size() < kAnimMaxUploadBytes);
        covered += int(std::lround(parts[i].end_frame * 30.0 / parts[i].fps));
    }
    CHECK_EQ(covered, c.end_frame);
}
