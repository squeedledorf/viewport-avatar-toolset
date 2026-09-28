// Key tags, blocking mode and Breakdown timing (08 KT-1..KT-4).
#include <string>

#include "check.h"
#include "vats/curve_ops.h"
#include "vats/edit.h"
#include "vats/json.h"
#include "vats/key_tags.h"
#include "vats/project.h"
#include "vats/tween.h"

using namespace vats;

namespace {

const FCurve& curve(const Clip& c, const char* track, const char* channel) { return c.curves.at(track).at(channel); }

// Keys rot_z of mElbowLeft at the given frames with the given values.
Clip elbow(std::initializer_list<std::pair<double, double>> keys) {
    Clip c;
    for (auto [f, v] : keys) c.curves["mElbowLeft"]["rot_z"].set_key(f, v);
    return c;
}

}  // namespace

TEST(key_tags_round_trip_through_project) {
    Project p;
    key_euler(p.clip, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(p.clip, "mShoulderLeft", 10, {0, 0, 45});
    key_euler(p.clip, "mShoulderLeft", 20, {0, 0, 90});
    FCurve& z = p.clip.curves["mShoulderLeft"]["rot_z"];
    z.keys[0].tag = KeyTag::Extreme;
    z.keys[1].tag = KeyTag::Breakdown;
    z.keys[2].tag = KeyTag::Hold;
    const std::string text = save_project(p);
    // One byte (two hex digits) per key, only for curves that have a tag; the curves themselves are unchanged.
    Json doc;
    std::string err;
    CHECK(parse_json(text, doc, err));
    const Json* tags = doc.find("key_tags");
    CHECK(tags && tags->find("mShoulderLeft") && tags->find("mShoulderLeft")->find("rot_z"));
    CHECK_EQ(tags->find("mShoulderLeft")->find("rot_z")->str, std::string("010203"));
    CHECK(!tags->find("mShoulderLeft")->find("rot_x"));
    CHECK(doc.find("curves")->find("mShoulderLeft")->find("rot_z")->arr[0].arr.size() == 9);

    Project back;
    CHECK(load_project(text, back, err));
    CHECK(back.clip == p.clip);
    CHECK(save_project(back) == text);

    // No tags: no field, as before.
    Project plain;
    key_euler(plain.clip, "mNeck", 0, {1, 2, 3});
    CHECK(save_project(plain).find("key_tags") == std::string::npos);
}

TEST(key_tags_old_and_mismatched_projects) {
    // A project from before tags: every key loads untagged.
    const char* old = R"({"format": "vats-project", "version": 1, "fps": 30, "end_frame": 20,
        "curves": {"mNeck": {"rot_x": [[0, 0, 2, 0, 0, -1, 0, 1, 0], [10, 5, 2, 0, 0, 9, 5, 11, 5]]}}})";
    Project p;
    std::string err;
    CHECK(load_project(old, p, err));
    for (const Key& k : curve(p.clip, "mNeck", "rot_x").keys) CHECK(k.tag == KeyTag::None);

    // Written by a build that kept the field but changed the keys: a count that no longer matches, or an unknown
    // tag, leaves that curve untagged and the project still loads.
    std::string text = old;
    text.insert(text.rfind('}'), R"(, "key_tags": {"mNeck": {"rot_x": "010203"}, "mHead": {"rot_x": "01"}})");
    CHECK(load_project(text, p, err));
    for (const Key& k : curve(p.clip, "mNeck", "rot_x").keys) CHECK(k.tag == KeyTag::None);
    text = old;
    text.insert(text.rfind('}'), R"(, "key_tags": {"mNeck": {"rot_x": "0109"}})");
    CHECK(load_project(text, p, err));
    CHECK(curve(p.clip, "mNeck", "rot_x").keys[0].tag == KeyTag::None);
    text = old;
    text.insert(text.rfind('}'), R"(, "key_tags": {"mNeck": {"rot_x": "0302"}})");
    CHECK(load_project(text, p, err));
    CHECK(curve(p.clip, "mNeck", "rot_x").keys[0].tag == KeyTag::Hold);
    CHECK(curve(p.clip, "mNeck", "rot_x").keys[1].tag == KeyTag::Breakdown);
    // The wrong type is an error, like any other malformed field.
    text = old;
    text.insert(text.rfind('}'), R"(, "key_tags": {"mNeck": {"rot_x": 3}})");
    CHECK(!load_project(text, p, err));
}

TEST(key_tags_tag_commands) {
    Clip c;
    key_euler(c, "mNeck", 0, {0, 0, 0});
    key_euler(c, "mNeck", 10, {10, 0, 0});
    CHECK(tag_keys_at(c, {"mNeck", "mHead"}, 10, KeyTag::Extreme) == 3);
    CHECK(tag_keys_at(c, {"mNeck"}, 10, KeyTag::Extreme) == 0);
    for (const char* ch : kRotChannels) CHECK(curve(c, "mNeck", ch).keys[1].tag == KeyTag::Extreme);
    CHECK(curve(c, "mNeck", "rot_x").keys[0].tag == KeyTag::None);
    CHECK(tag_keys(c, {{"mNeck", "rot_x", 0}, {"mNeck", "rot_x", 7}}, KeyTag::Hold) == 1);
    CHECK(curve(c, "mNeck", "rot_x").keys[0].tag == KeyTag::Hold);
    // A re-key keeps the tag.
    key_euler(c, "mNeck", 10, {20, 0, 0});
    CHECK(curve(c, "mNeck", "rot_x").keys[1].tag == KeyTag::Extreme);
}

TEST(key_tags_blocking_steps_new_keys_only) {
    Clip before = elbow({{0, 0}, {10, 30}});
    Clip c = before;
    c.curves["mElbowLeft"]["rot_z"].set_key(5, 12);   // new
    c.curves["mElbowLeft"]["rot_z"].set_key(10, 40);  // re-keyed
    key_euler(c, "mNeck", 3, {1, 2, 3});              // a new curve
    CHECK(step_new_keys(c, before) == 4);
    const FCurve& z = curve(c, "mElbowLeft", "rot_z");
    CHECK(z.keys[0].interp == Interp::Bezier);
    CHECK(z.keys[1].interp == Interp::Constant);
    CHECK(z.keys[2].interp == Interp::Bezier);
    for (const char* ch : kRotChannels) CHECK(curve(c, "mNeck", ch).keys[0].interp == Interp::Constant);
    CHECK(step_new_keys(c, c) == 0);
    CHECK_NEAR(z.evaluate(8), 12, 1e-12);  // stepped: holds until the next key
    // Keys that only moved are not new.
    Clip moved = before;
    std::vector<KeyRef> s = {{"mElbowLeft", "rot_z", 1}};
    move_keys(moved, before, s, 5, 0, true);
    finish_transform(moved, s);
    CHECK(step_new_keys(moved, before) == 0);
}

TEST(key_tags_blocking_to_spline) {
    // A blocked shot: stepped keys, a Hold pair at 10-20 (a copied pose), then a move on at 30.
    Clip c = elbow({{0, 0}, {10, 50}, {20, 50}, {30, 90}});
    for (Key& k : c.curves["mElbowLeft"]["rot_z"].keys) k.interp = Interp::Constant;
    FCurve& z = c.curves["mElbowLeft"]["rot_z"];
    z.keys[1].tag = z.keys[2].tag = KeyTag::Hold;
    // The IK switch and a held position stay as they are.
    FCurve& blend = c.curves["ik.ArmLeft"]["blend"];
    blend.set_key(0, 1, Interp::Constant);
    blend.set_key(20, 0, Interp::Constant);
    FCurve& px = c.curves["mPelvis"]["pos_x"];
    px.set_key(10, 0.5, Interp::Constant);
    px.set_key(20, 0.5, Interp::Constant);
    px.keys[0].tag = px.keys[1].tag = KeyTag::Hold;

    CHECK(blocking_to_spline(c) == 1);
    const FCurve& zz = curve(c, "mElbowLeft", "rot_z");
    for (int i = 0; i < 4; ++i) {
        const Key& k = zz.keys[i];
        CHECK(k.interp == Interp::Bezier);
        CHECK(k.left == (i == 2 ? Handle::Plateau : Handle::AutoClamped));
        CHECK(k.right == (i == 1 ? Handle::Plateau : Handle::AutoClamped));
    }
    CHECK_NEAR(zz.keys[2].value, 51, 1e-12);  // a 1 degree drift towards the next key (90)
    CHECK(zz.keys[1].tag == KeyTag::Hold && zz.keys[2].tag == KeyTag::Hold);
    CHECK(zz.evaluate(15) > 50 && zz.evaluate(15) < 51);  // it moves through the hold
    for (const Key& k : curve(c, "ik.ArmLeft", "blend").keys) CHECK(k.interp == Interp::Constant);
    CHECK_EQ(curve(c, "mPelvis", "pos_x").keys[1].value, 0.5);
    CHECK(curve(c, "mPelvis", "pos_x").keys[1].interp == Interp::Bezier);

    // Direction: towards the next key, never past it; the last pair drifts on from the key before.
    Clip d = elbow({{0, 10}, {10, 10}, {20, 9.5}});
    d.curves["mElbowLeft"]["rot_z"].keys[0].tag = d.curves["mElbowLeft"]["rot_z"].keys[1].tag = KeyTag::Hold;
    blocking_to_spline(d);
    CHECK_NEAR(curve(d, "mElbowLeft", "rot_z").keys[1].value, 9.5, 1e-12);
    Clip e = elbow({{0, 0}, {10, 30}, {20, 30}});
    e.curves["mElbowLeft"]["rot_z"].keys[1].tag = e.curves["mElbowLeft"]["rot_z"].keys[2].tag = KeyTag::Hold;
    blocking_to_spline(e);
    CHECK_NEAR(curve(e, "mElbowLeft", "rot_z").keys[2].value, 31, 1e-12);
    // A hold that already moves by the drift or more is left alone.
    Clip f = elbow({{0, 0}, {10, 30}, {20, 35}, {30, 0}});
    f.curves["mElbowLeft"]["rot_z"].keys[1].tag = f.curves["mElbowLeft"]["rot_z"].keys[2].tag = KeyTag::Hold;
    CHECK(blocking_to_spline(f) == 0);
    CHECK_EQ(curve(f, "mElbowLeft", "rot_z").keys[2].value, 35.0);
}

TEST(key_tags_breakdown_keeps_proportional_time) {
    // Extremes at 0 and 20, a breakdown at 5 (a quarter of the way), and one more key at 40.
    Clip press = elbow({{0, 0}, {5, 10}, {20, 40}, {40, 0}});
    press.curves["mElbowLeft"]["rot_z"].keys[1].tag = KeyTag::Breakdown;
    Clip c = press;
    const std::vector<KeyRef> press_sel = {{"mElbowLeft", "rot_z", 2}};
    std::vector<KeyRef> sel = press_sel;
    // Move the key at 20 to 28, in steps as a drag does: the breakdown stays a quarter of the way, at 7.
    for (double df : {3.0, 6.0, 8.0}) {
        sel = press_sel;
        move_keys(c, press, sel, df, 0, true, &press_sel);
    }
    finish_transform(c, sel);
    const FCurve& z = curve(c, "mElbowLeft", "rot_z");
    CHECK(z.keys.size() == 4);
    CHECK_NEAR(z.keys[1].frame, 7, 1e-12);
    CHECK_NEAR(z.keys[1].value, 10, 1e-12);
    CHECK(z.keys[1].tag == KeyTag::Breakdown);
    CHECK_NEAR(z.keys[2].frame, 28, 1e-12);
    CHECK(sel.size() == 1 && sel[0].index == 2);

    // Moving the key before it: 0 -> 4 puts the breakdown at 4 + (20 - 4) / 4 = 8.
    Clip m = press;
    std::vector<KeyRef> s0 = {{"mElbowLeft", "rot_z", 0}};
    move_keys(m, press, s0, 4, 0, false);
    CHECK_NEAR(curve(m, "mElbowLeft", "rot_z").keys[1].frame, 8, 1e-12);
    // An untagged key does not follow, and a selected breakdown moves with the selection.
    Clip u = elbow({{0, 0}, {5, 10}, {20, 40}});
    std::vector<KeyRef> s1 = {{"mElbowLeft", "rot_z", 2}};
    Clip up = u;
    move_keys(u, up, s1, 8, 0, false);
    CHECK_NEAR(curve(u, "mElbowLeft", "rot_z").keys[1].frame, 5, 1e-12);
    Clip b = press;
    std::vector<KeyRef> s2 = {{"mElbowLeft", "rot_z", 1}, {"mElbowLeft", "rot_z", 2}};
    move_keys(b, press, s2, 8, 0, false);
    CHECK_NEAR(curve(b, "mElbowLeft", "rot_z").keys[1].frame, 13, 1e-12);
    // With no anchor on one side (the last key is a breakdown) it stays where it is.
    Clip t = elbow({{0, 0}, {10, 5}});
    t.curves["mElbowLeft"]["rot_z"].keys[1].tag = KeyTag::Breakdown;
    Clip tp = t;
    std::vector<KeyRef> s3 = {{"mElbowLeft", "rot_z", 0}};
    move_keys(t, tp, s3, 4, 0, false);
    CHECK_NEAR(curve(t, "mElbowLeft", "rot_z").keys[1].frame, 10, 1e-12);
}

TEST(key_tags_tween_makes_breakdowns) {
    Clip c;
    key_euler(c, "mShoulderLeft", 0, {0, 0, 0});
    key_euler(c, "mShoulderLeft", 20, {0, 0, 90});
    tween(c, {"mShoulderLeft"}, 5, 0.5, TweenMode::Breakdown);
    for (const char* ch : kRotChannels) {
        const FCurve& k = curve(c, "mShoulderLeft", ch);
        CHECK(k.keys[1].tag == KeyTag::Breakdown);
        CHECK(k.keys[0].tag == KeyTag::None && k.keys[2].tag == KeyTag::None);
    }
    // Relax changes values only.
    c.curves["mShoulderLeft"]["rot_z"].keys[1].tag = KeyTag::Extreme;
    tween(c, {"mShoulderLeft"}, 5, 0.5, TweenMode::Relax);
    CHECK(curve(c, "mShoulderLeft", "rot_z").keys[1].tag == KeyTag::Extreme);
}
