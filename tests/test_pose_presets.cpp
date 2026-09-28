#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include "check.h"
#include "fixtures.h"
#include "vats/lint.h"
#include "vats/pose_presets.h"
#include "vats/rig.h"

using namespace vats;

namespace {

const char* const kDigits[] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};

const LibraryItem& preset(const std::string& slug) {
    for (auto& it : builtin_poses(skel()))
        if (it.id == "builtin:" + slug) return it;
    std::fprintf(stderr, "no preset %s\n", slug.c_str());
    std::exit(2);
}

std::vector<Xform> globals(const LibraryItem& it) {
    const Skeleton& s = skel();
    Pose p(s.size());
    for (auto& [name, e] : it.bones) p.rot[s.find(name)] = euler_to_quat(e);
    return s.global_pose(p);
}

// Joints of one left digit (base, two knuckles, tip) in the wrist frame, cm.
std::vector<Vec3> digit(const std::vector<Xform>& g, int d) {
    const Skeleton& s = skel();
    Xform w = g[s.find("mWristLeft")].inverse();
    std::vector<Vec3> v;
    for (int n = 1; n <= 3; ++n) {
        int i = s.find(std::string("mHand") + kDigits[d] + std::to_string(n) + "Left");
        v.push_back(w.apply(g[i].pos) * 100);
        if (n == 3) v.push_back(w.apply(g[i].apply(s[i].end)) * 100);
    }
    return v;
}

Vec3 tip(const LibraryItem& it, int d) { return digit(globals(it), d)[3]; }

// Closest approach of two digits' bone chains, sampled.
double chain_gap(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    double best = 1e9;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int s = 0; s <= 10; ++s)
                for (int t = 0; t <= 10; ++t) {
                    Vec3 p = a[i] + (a[i + 1] - a[i]) * (s / 10.0), q = b[j] + (b[j + 1] - b[j]) * (t / 10.0);
                    best = std::min(best, (p - q).length());
                }
    return best;
}

// Bitmask of digits whose tip is at least 85% as far from the wrist as at rest (bit 0 = thumb).
int extended(const LibraryItem& it) {
    const LibraryItem rest;
    auto g = globals(it), g0 = globals(rest);
    int mask = 0;
    for (int d = 0; d < 5; ++d)
        if (digit(g, d)[3].length() > 0.85 * digit(g0, d)[3].length()) mask |= 1 << d;
    return mask;
}

const Vec3 kPalm{0.5, 5, -1};  // centre of the palm, wrist frame, cm

bool curled(const LibraryItem& it, int d) { return (tip(it, d) - kPalm).length() < 4.5; }

}  // namespace

TEST(pose_presets_items) {
    auto& items = builtin_poses(skel());
    CHECK(&items == &builtin_poses(skel()));
    std::set<std::string> ids;
    int hands = 0;
    for (auto& it : items) {
        CHECK(it.id.rfind("builtin:", 0) == 0);
        CHECK(ids.insert(it.id).second);
        CHECK(!it.name.empty());
        CHECK(!it.clip);
        for (auto& [name, e] : it.bones) CHECK(skel().find(name) >= 0);
        if (it.kind == "hand") {
            ++hands;
            CHECK_EQ(it.side, std::string("Left"));
            CHECK_EQ(it.bones.size(), size_t(15));
        } else {
            CHECK_EQ(it.kind, std::string("pose"));
            CHECK(it.side.empty());
        }
    }
    CHECK(hands >= 30);
    CHECK(items.size() >= size_t(hands + 7));
}

// Flexion of every finger joint, measured from the bone directions: no hyperextension past 10
// degrees, MCP <= 90, PIP <= 110, DIP <= 90, and a DIP never bent further than its PIP.
TEST(pose_presets_joint_limits) {
    const Skeleton& s = skel();
    for (auto& it : builtin_poses(s)) {
        if (it.kind != "hand") continue;
        for (int d = 0; d < 5; ++d) {
            double flex[3];
            for (int n = 1; n <= 3; ++n) {
                std::string bone = std::string("mHand") + kDigits[d] + std::to_string(n) + "Left";
                Vec3 dir = s[s.find(bone)].end.normalized();
                Vec3 palm = d == 0 ? Vec3{-0.7, 0, -1}.normalized() : Vec3{0, 0, -1};
                Vec3 toward = (palm - dir * dir.dot(palm)).normalized();
                Vec3 now = euler_to_quat(it.bones.at(bone)).rotate(dir);
                flex[n - 1] = std::atan2(now.dot(toward), now.dot(dir)) * kRadToDeg;
            }
            const double limit[3] = {d == 0 ? 60.0 : 90.0, 110, 90};
            for (int n = 0; n < 3; ++n) {
                // The thumb's base also abducts, which this reads as extension, so only its upper limit applies.
                bool thumb_base = d == 0 && n == 0;
                if ((!thumb_base && flex[n] < -10.5) || flex[n] > limit[n] + 0.5)
                    check::fail(__FILE__, __LINE__, it.id + " " + kDigits[d] + " joint " + std::to_string(n + 1) +
                                                        " flexed " + std::to_string(flex[n]));
            }
            if (d > 0 && flex[2] > flex[1] + 1)
                check::fail(__FILE__, __LINE__, it.id + " " + kDigits[d] + " DIP bent past PIP");
        }
    }
}

// Neighbouring fingers keep a finger's width apart and the thumb never passes through a finger.
TEST(pose_presets_no_overlap) {
    for (auto& it : builtin_poses(skel())) {
        if (it.kind != "hand") continue;
        auto g = globals(it);
        std::vector<Vec3> j[5];
        for (int d = 0; d < 5; ++d) j[d] = digit(g, d);
        for (int d = 1; d < 4; ++d) {
            double gap = chain_gap(j[d], j[d + 1]);
            if (gap < 1.2) check::fail(__FILE__, __LINE__, it.id + " " + kDigits[d] + "/" + kDigits[d + 1] +
                                                               " gap " + std::to_string(gap));
        }
        for (int d = 1; d < 5; ++d) {
            double gap = chain_gap(j[0], j[d]);
            if (gap < 0.5) check::fail(__FILE__, __LINE__, it.id + " thumb/" + kDigits[d] + " gap " + std::to_string(gap));
        }
    }
}

TEST(pose_presets_hand_shapes) {
    const LibraryItem& fist = preset("hand-fist");
    for (int d = 1; d < 5; ++d) CHECK(curled(fist, d));
    CHECK(tip(fist, 0).z < -3);  // thumb wraps under the curled fingers
    for (int d = 1; d < 5; ++d) CHECK(tip(preset("hand-flat"), d).length() > 15);

    const LibraryItem& point = preset("hand-point");
    CHECK(tip(point, 1).length() > 18);
    for (int d = 2; d < 5; ++d) CHECK(curled(point, d));

    CHECK((tip(preset("hand-pinch"), 0) - tip(preset("hand-pinch"), 1)).length() < 1.5);
    CHECK((tip(preset("hand-ok"), 0) - tip(preset("hand-ok"), 1)).length() < 1.5);
    double loose = (tip(preset("hand-pinch-loose"), 0) - tip(preset("hand-pinch-loose"), 1)).length();
    CHECK(loose > 2 && loose < 4);
    double pen = (tip(preset("hand-pen"), 0) - tip(preset("hand-pen"), 1)).length();
    CHECK(pen > 1 && pen < 3);  // a pen's width between the pads
    double cup = (tip(preset("hand-cup"), 0) - tip(preset("hand-cup"), 1)).length();
    CHECK(cup > 6 && cup < 10);

    const LibraryItem& peace = preset("hand-peace");
    CHECK((tip(peace, 1) - tip(peace, 2)).length() > 7);
    CHECK(curled(peace, 3) && curled(peace, 4));
    CHECK(tip(preset("hand-thumbs-up"), 0).x > 12);

    // Which digits stick out (bit 0 = thumb, 1 = index, ... 4 = pinky).
    CHECK_EQ(extended(fist), 0);
    CHECK_EQ(extended(point), 0b00010);
    CHECK_EQ(extended(preset("hand-point-thumb")), 0b00011);
    CHECK_EQ(extended(peace), 0b00110);
    CHECK_EQ(extended(preset("hand-thumbs-up")), 0b00001);
    CHECK_EQ(extended(preset("hand-horns")), 0b10010);
    CHECK_EQ(extended(preset("hand-call-me")), 0b10001);
    CHECK_EQ(extended(preset("hand-pistol")), 0b00111);
    CHECK_EQ(extended(preset("hand-count-1")), 0b00010);
    CHECK_EQ(extended(preset("hand-count-2")), 0b00110);
    CHECK_EQ(extended(preset("hand-count-3")), 0b01110);
    CHECK_EQ(extended(preset("hand-count-4")), 0b11110);
    CHECK_EQ(extended(preset("hand-count-5")), 0b11111);
    CHECK_EQ(extended(preset("hand-open")), 0b11111);

    // Typing: the four fingertips land on one row of keys.
    const LibraryItem& typing = preset("hand-typing");
    for (int d = 1; d < 5; ++d) CHECK_NEAR(tip(typing, d).z, -7.5, 1.2);
}

TEST(pose_presets_body) {
    const Skeleton& s = skel();
    auto at = [&](const std::string& slug, const char* bone) {
        auto g = globals(preset(slug));
        return g[s.find("mPelvis")].inverse().apply(g[s.find(bone)].pos) * 100;
    };
    // Symmetric poses get a mirrored right side.
    for (auto slug : {"body-stand", "body-hips", "body-sit"}) {
        Vec3 l = at(slug, "mWristLeft"), r = at(slug, "mWristRight");
        CHECK_NEAR(l.x, r.x, 0.1);
        CHECK_NEAR(l.y, -r.y, 0.1);
        CHECK_NEAR(l.z, r.z, 0.1);
    }
    CHECK(at("body-stand", "mWristLeft").z < 5);  // arms hang down
    CHECK(at("body-stand", "mWristLeft").x > at("body-stand", "mElbowLeft").x);  // elbows a little bent
    Vec3 hip = at("body-hips", "mWristLeft");
    CHECK(std::fabs(hip.z - 13) < 5 && std::fabs(hip.y - 18) < 4);
    CHECK(at("body-hips", "mElbowLeft").y > 28);
    CHECK(at("body-arms-crossed", "mWristLeft").y < 0 && at("body-arms-crossed", "mWristRight").y > 0);
    CHECK(at("body-arms-crossed", "mWristRight").x > at("body-arms-crossed", "mWristLeft").x);  // right arm on top
    Vec3 chin = at("body-thinking", "mHead") + Vec3{9, 0, -4};
    CHECK((at("body-thinking", "mHandMiddle1Right") - chin).length() < 7);  // knuckles under the chin
    CHECK(at("body-wave", "mWristRight").z > at("body-wave", "mHead").z);
    Vec3 knee = at("body-sit", "mKneeLeft"), ankle = at("body-sit", "mAnkleLeft");
    CHECK(knee.x > 45 && std::fabs(knee.z) < 10);  // thigh level
    CHECK(std::fabs(ankle.x - knee.x) < 5);         // shin vertical
    CHECK(at("body-contrapposto", "mKneeLeft").x > at("body-contrapposto", "mKneeRight").x + 4);
    CHECK(at("body-contrapposto", "mAnkleRight").z < -99 && at("body-contrapposto", "mAnkleLeft").z < -97);
}

// The library mirrors a left-hand preset onto the right hand: same shape, reflected.
TEST(pose_presets_mirror_to_right) {
    const Skeleton& s = skel();
    const LibraryItem& ok = preset("hand-ok");
    Clip clip;
    apply_pose(clip, s, ok, 0, true);
    CHECK(!clip.curves.count("mHandIndex1Left"));
    auto g = s.global_pose(evaluate_curves(s, clip, 0));
    Xform w = g[s.find("mWristRight")].inverse();
    for (int d = 0; d < 5; ++d) {
        int i = s.find(std::string("mHand") + kDigits[d] + "3Right");
        Vec3 r = w.apply(g[i].apply(s[i].end)) * 100, l = tip(ok, d);
        CHECK((r - Vec3{l.x, -l.y, l.z}).length() < 0.2);
    }
}

namespace {

// A body starter pose keyed at frame 0 of an otherwise empty clip, as clicking it in the Inventory does.
Clip keyed(const LibraryItem& it) {
    Clip c;
    c.fps = 30, c.end_frame = 1;
    apply_pose(c, skel(), it, 0, false);
    return c;
}

}  // namespace

// On both default bodies (female: the skeleton's own proportions, male: its shape), every body pose passes the
// Animation Check's joint-limit rule, and the fitting stances its self-contact and ground rules too (the other poses
// touch on purpose: crossed arms, hands on the thighs). The fitting stances are also symmetric joint for joint
// (fingers included), keep the spine and head straight, and key the whole body.
TEST(pose_presets_body_checks) {
    const Skeleton& s = skel();
    Rig rig(s);
    int stances = 0;
    for (auto& it : builtin_poses(s)) {
        if (it.kind != "pose") continue;
        const bool stance = it.category == "Fitting stances";
        for (const Shape* shape : {static_cast<const Shape*>(nullptr), &s.male_shape()}) {
            AnimExportOptions opt;
            opt.shape = shape;
            for (const LintFinding& f : lint_clip(s, keyed(it), opt, {}, shape))
                if (f.rule == "joint_limits" || (stance && (f.rule == "self_contact" || f.rule == "ground")))
                    check::fail(__FILE__, __LINE__, it.id + (shape ? " male: " : " female: ") + f.message);
        }
        if (!stance) continue;
        ++stances;
        CHECK(it.hip.has_value());
        for (const char* b : {"mPelvis", "mTorso", "mChest", "mNeck", "mHead"}) CHECK(it.bones.at(b).length() < 1e-9);
        for (const char* b : {"mCollarRight", "mShoulderRight", "mElbowRight", "mWristRight", "mHipRight", "mKneeRight",
                              "mAnkleRight", "mHandIndex3Right", "mHandThumb3Right"})
            CHECK(it.bones.count(b));
        // Mirrored across the body's midplane: each bone's world rotation, and its joint to within the few
        // millimetres the skeleton's own legs differ by.
        auto g = evaluate(rig, keyed(it), 0, nullptr).globals;
        for (int n = 0; n < s.joint_count(); ++n) {
            int m = s.find(Skeleton::mirror_name(s[n].name));
            if (m < 0 || m == n || !it.bones.count(s[n].name)) continue;
            const Quat& r = g[m].rot;
            double turn = 2 * std::acos(std::min(1.0, std::fabs(g[n].rot.dot({r.w, -r.x, r.y, -r.z})))) * kRadToDeg;
            Vec3 p = g[m].pos;
            if (turn > 0.1 || (g[n].pos - Vec3{p.x, -p.y, p.z}).length() > 0.01)
                check::fail(__FILE__, __LINE__, it.id + " " + s[n].name + " not mirrored");
        }
    }
    CHECK_EQ(stances, 11);
}

// Where the fitting stances put the arms, palms and feet.
TEST(pose_presets_fitting_stances) {
    const Skeleton& s = skel();
    Rig rig(s);
    auto globals_of = [&](const char* slug) { return evaluate(rig, keyed(preset(slug)), 0, nullptr).globals; };
    auto at = [&](const char* slug, const char* bone) { return globals_of(slug)[s.find(bone)].pos * 100; };  // cm
    auto arm = [&](const char* slug) { return (at(slug, "mWristLeft") - at(slug, "mShoulderLeft")).normalized(); };
    auto elevation = [&](const char* slug) { return std::asin(arm(slug).z) * kRadToDeg; };
    for (auto slug : {"body-t-pose", "body-arms-straight-legs-apart", "body-arms-straight-sitting"}) {
        CHECK_NEAR(elevation(slug), 0, 1);
        CHECK(arm(slug).y > 0.99);  // straight out to the side
    }
    for (auto slug : {"body-arms-down-legs-together", "body-arms-down-sitting"}) CHECK(elevation(slug) < -75);
    for (auto slug : {"body-arms-downward-legs-apart", "body-arms-downward-legs-together"}) CHECK_NEAR(elevation(slug), -45, 2);
    for (auto slug : {"body-arms-upward-legs-apart", "body-arms-upward-legs-together"}) CHECK_NEAR(elevation(slug), 45, 2);
    for (auto slug : {"body-arms-forward-legs-apart", "body-arms-forward-legs-together"}) {
        CHECK_NEAR(elevation(slug), 0, 1);
        CHECK(arm(slug).x > 0.99);  // straight ahead, the arms parallel
    }
    // Palms: down with the arms out, forward or down; towards the thighs with them down; forward with them up.
    auto palm = [&](const char* slug) { return globals_of(slug)[s.find("mWristLeft")].rot.rotate({0, 0, -1}); };
    CHECK(palm("body-t-pose").z < -0.99);
    CHECK(palm("body-arms-forward-legs-together").z < -0.99);
    CHECK(palm("body-arms-down-legs-together").y < -0.95);
    CHECK(palm("body-arms-upward-legs-together").x > 0.95);
    // Feet: together under the hips, apart about shoulder width, flat either way.
    CHECK_NEAR(at("body-t-pose", "mAnkleLeft").y, 8.2, 1);
    const double apart = at("body-arms-upward-legs-apart", "mAnkleLeft").y, shoulder = at("body-t-pose", "mShoulderLeft").y;
    CHECK(apart > shoulder - 3 && apart < shoulder + 3);
    for (auto slug : {"body-t-pose", "body-arms-upward-legs-apart"})
        CHECK_NEAR(at(slug, "mToeLeft").z - at(slug, "mAnkleLeft").z, -6.1, 0.3);
    // Sitting: thighs level, knees at 90 degrees, feet on the ground.
    for (auto slug : {"body-arms-down-sitting", "body-arms-straight-sitting"}) {
        Vec3 hip = at(slug, "mHipLeft"), knee = at(slug, "mKneeLeft"), ankle = at(slug, "mAnkleLeft");
        CHECK_NEAR(knee.z, hip.z, 1);
        CHECK(knee.x - hip.x > 45);
        CHECK(std::fabs(ankle.x - knee.x) < 4 && ankle.z < knee.z - 40);
        CHECK_NEAR(ankle.z, at("body-t-pose", "mAnkleLeft").z, 0.5);
    }
}
