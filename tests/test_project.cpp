#include <string>

#include "check.h"
#include "vats/json.h"
#include "vats/project.h"

using namespace vats;

namespace {

bool parses(std::string_view text) {
    Json j;
    std::string err;
    return parse_json(text, j, err);
}

Json parse(std::string_view text) {
    Json j;
    std::string err;
    CHECK(parse_json(text, j, err));
    return j;
}

}  // namespace

TEST(json_strings) {
    Json j = parse(R"("q\" b\\ s\/ \b\f\n\r\t")");
    CHECK_EQ(j.str, std::string("q\" b\\ s/ \b\f\n\r\t"));
    CHECK_EQ(parse("\"\\u00e9\\u20AC\"").str, std::string("\xC3\xA9\xE2\x82\xAC"));
    CHECK_EQ(parse("\"\\ud83d\\ude00\"").str, std::string("\xF0\x9F\x98\x80"));
    CHECK_EQ(parse("\"caf\xC3\xA9 \xF0\x9F\x98\x80\"").str, std::string("caf\xC3\xA9 \xF0\x9F\x98\x80"));
    CHECK_EQ(parse("\"\\u0000\"").str, std::string(1, '\0'));
    CHECK(!parses("\"\\ud83d\""));
    CHECK(!parses("\"\\ud83d\\u0041\""));
    CHECK(!parses("\"\\ude00\""));
    CHECK(!parses("\"\\x\""));
    CHECK(!parses("\"\\u12G4\""));
    CHECK(!parses("\"tab\there\""));
    CHECK(!parses("\"open"));
    // The writer escapes quotes, backslashes and control characters, and passes UTF-8 through.
    Json s("a\"b\\c\n\x01\xC3\xA9");
    CHECK_EQ(write_json(s), std::string("\"a\\\"b\\\\c\\n\\u0001\xC3\xA9\"\n"));
    CHECK_EQ(parse(write_json(s)), s);
}

TEST(json_numbers) {
    CHECK_EQ(parse("30").num, 30.0);
    CHECK_EQ(parse("30.0").num, 30.0);
    CHECK_EQ(parse("-0.5e-3").num, -0.0005);
    CHECK_EQ(parse("1E2").num, 100.0);
    CHECK_EQ(parse("1e+2").num, 100.0);
    CHECK_EQ(parse("1e-400").num, 0.0);  // underflow reads as zero, overflow is an error
    CHECK_EQ(parse("0.5E-999").num, 0.0);
    CHECK(!parses("0.5e999"));
    for (const char* bad : {"01", "1.", ".5", "+1", "-", "1e", "1e+", "NaN", "Infinity", "0x10", "1e400", "- 1"})
        CHECK(!parses(bad));
    CHECK_EQ(write_json(Json(30.0)), std::string("30\n"));
    CHECK_EQ(write_json(Json(-2)), std::string("-2\n"));
    CHECK_EQ(write_json(Json(0.1)), std::string("0.1\n"));
    CHECK_EQ(write_json(Json(1e21)), std::string("1e+21\n"));
    for (double d : {1.0 / 3.0, 0.1 + 0.2, 2.2250738585072014e-308, 1.7976931348623157e308, -123456.789, 5e-4}) {
        Json back = parse(write_json(Json(d)));
        CHECK_EQ(back.num, d);
    }
}

TEST(json_structure) {
    Json j = parse(" {\"z\": 1, \"a\": [true, false, null], \"m\": {}} ");
    CHECK(j.is_object());
    CHECK_EQ(j.obj.size(), size_t(3));
    CHECK_EQ(j.obj[0].first, std::string("z"));  // insertion order kept
    CHECK_EQ(j.obj[1].first, std::string("a"));
    CHECK(j.find("a")->arr[2].is_null());
    CHECK(!j.find("missing"));
    CHECK_EQ(write_json(j), std::string("{\n\t\"z\": 1,\n\t\"a\": [true, false, null],\n\t\"m\": {}\n}\n"));
    CHECK_EQ(write_json(parse("[[1, 2], [], {\"k\": \"v\"}]")),
             std::string("[\n\t[1, 2],\n\t[],\n\t{\n\t\t\"k\": \"v\"\n\t}\n]\n"));
    CHECK(parses("\xEF\xBB\xBF{}"));

    for (const char* bad : {"", " ", "{} {}", "1 x", "[1,]", "{\"a\": 1,}", "{\"a\" 1}", "{a: 1}", "[1 2]",
                            "{\"a\": 1, \"a\": 2}", "tru", "nul", "[", "{\"a\":"})
        CHECK(!parses(bad));

    Json j2;
    std::string err;
    size_t at = 0;
    CHECK(!parse_json("[1, x]", j2, err, &at));
    CHECK_EQ(at, size_t(4));
    CHECK(err.find("at byte 4") != std::string::npos);

    std::string deep(kJsonMaxDepth, '[');
    deep += std::string(kJsonMaxDepth, ']');
    CHECK(parses(deep));
    CHECK(!parses("[" + deep + "]"));
    std::string deep_obj;
    for (int i = 0; i <= kJsonMaxDepth; ++i) deep_obj += "{\"a\":";
    deep_obj += "1" + std::string(kJsonMaxDepth + 1, '}');
    CHECK(!parses(deep_obj));
}

namespace {

Project sample_project() {
    Project p;
    Clip& c = p.clip;
    c.fps = 24;
    c.end_frame = 48;
    c.loop = true;
    c.loop_in = 6;
    c.loop_out = 40;
    c.priority = 4;
    c.ease_in = 0.25;
    c.ease_out = 1.0 / 3.0;
    c.hand_pose = 7;
    c.emote = "express_smile";
    c.curves["mShoulderLeft"]["rot_x"].set_key(0, 10);
    c.curves["mShoulderLeft"]["rot_x"].set_key(12.5, -33.3333);
    c.curves["mShoulderLeft"]["rot_x"].set_key(48, 0.1, Interp::Linear);
    FCurve& free = c.curves["mPelvis"]["pos_z"];
    free.set_key(0, 0);
    free.set_key(24, -0.1);
    free.apply_tangent(1, Tangent::Break);
    free.keys[1].lx = 20.5;
    free.keys[1].ly = -0.3;
    c.curves["mPelvis"]["pos_x"];            // empty channel: not written
    c.curves["mHead"]["rot_y"];              // empty track: not written
    c.joint_priority["mHead"] = 5;
    c.joint_priority["mNeck"] = -1;
    AnimConstraint con{};
    for (size_t i = 0; i < con.size(); ++i) con[i] = static_cast<std::uint8_t>(i * 3 + 1);
    c.constraints.push_back(con);
    OrphanJoint o;
    o.name = "mCustomTail";
    o.priority = 2;
    o.rot = {{0.0, Quat{0.5, 0.5, -0.5, 0.5}}, {1.25, Quat{}}};
    o.pos = {{0.5, Vec3{0.1, -0.2, 0.3}}};
    c.orphans.push_back(o);
    p.clip.mirror_export = true;
    p.clip.export_settings = parse(R"({"name": "wave", "number": 3, "side": "Left", "shape": "sl-default"})");
    {
        std::string err;
        CHECK(props_from_json(parse(R"([{"path": "props/cup.dae", "bone": "mWristRight", "scale": 2}])"), p.clip.props, err));
    }
    Pin pin;
    pin.joint = "Right Hand";
    pin.via = "mWristRight";
    pin.to = 20;
    pin.pos = {0.1, 0, 1};
    pin.rot = Quat{0.5, -0.5, 0.5, 0.5};
    pin.release_key = 21;
    c.pins.push_back(pin);
    pin.joint = pin.via = "mAnkleLeft";
    pin.target = "mPelvis";
    pin.from = 5;
    pin.to = -1;
    pin.start_key = 5;
    pin.release_key = -1;
    c.pins.push_back(pin);
    p.meta.set("note", "caf\xC3\xA9 \"quoted\"\n");
    p.extra.set("future_field", parse(R"({"x": [1, 2.5]})"));
    return p;
}

}  // namespace

TEST(project_round_trip) {
    Project p = sample_project();
    std::string s1 = save_project(p);
    Project q;
    std::string err;
    CHECK(load_project(s1, q, err));
    CHECK(!q.read_only);
    CHECK(!q.migrated);
    CHECK_EQ(save_project(q), s1);

    CHECK(s1.rfind("{\n\t\"format\": \"vats-project\",\n\t\"version\": 1,\n"
                   "\t\"euler_order\": \"xyz-extrinsic\",\n",
                   0) == 0);
    CHECK(s1.find("pos_x") == std::string::npos);
    CHECK(s1.find("mHead\": {") == std::string::npos);

    const Clip &a = p.clip, &b = q.clip;
    CHECK_EQ(b.fps, a.fps);
    CHECK_EQ(b.end_frame, a.end_frame);
    CHECK_EQ(b.loop, a.loop);
    CHECK_EQ(b.loop_in, a.loop_in);
    CHECK_EQ(b.loop_out, a.loop_out);
    CHECK_EQ(b.priority, a.priority);
    CHECK_EQ(b.ease_in, a.ease_in);
    CHECK_EQ(b.ease_out, a.ease_out);
    CHECK_EQ(b.hand_pose, a.hand_pose);
    CHECK_EQ(b.emote, a.emote);
    CHECK_EQ(b.joint_priority, a.joint_priority);
    CHECK(b.constraints == a.constraints);
    CHECK_EQ(b.orphans.size(), size_t(1));
    CHECK_EQ(b.orphans[0].name, a.orphans[0].name);
    CHECK_EQ(b.orphans[0].priority, a.orphans[0].priority);
    CHECK(b.orphans[0].rot == a.orphans[0].rot);
    CHECK(b.orphans[0].pos == a.orphans[0].pos);
    CHECK_EQ(b.curves.size(), size_t(2));
    for (auto& [track, channels] : b.curves)
        for (auto& [channel, curve] : channels) {
            auto& ka = a.curves.at(track).at(channel).keys;
            CHECK_EQ(curve.keys.size(), ka.size());
            for (size_t i = 0; i < ka.size() && i < curve.keys.size(); ++i) {
                auto &x = ka[i], &y = curve.keys[i];
                CHECK(x.frame == y.frame && x.value == y.value && x.interp == y.interp && x.left == y.left &&
                      x.right == y.right && x.lx == y.lx && x.ly == y.ly && x.rx == y.rx && x.ry == y.ry);
            }
        }
    CHECK_EQ(q.clip.mirror_export, true);
    CHECK_EQ(q.clip.export_settings, p.clip.export_settings);
    CHECK(q.clip.props == p.clip.props);
    CHECK(b.pins == a.pins);
    CHECK_EQ(q.meta, p.meta);
    CHECK_EQ(q.extra, p.extra);

    std::string err2;
    Project d;
    CHECK(load_project(save_project(Project{}), d, err2));
    CHECK_EQ(save_project(d), save_project(Project{}));
}

TEST(project_ik_solve) {
    Project p;
    std::string s = save_project(p);
    CHECK(s.find("ik_solve") == std::string::npos);  // written only when not the default
    p.clip.ik_solve = IkSolve::Literal;
    s = save_project(p);
    CHECK(s.find("\t\"ik_solve\": \"literal\",\n") != std::string::npos);
    Project q;
    std::string err;
    CHECK(load_project(s, q, err));
    CHECK(q.clip == p.clip);
    CHECK(q.extra.obj.empty());
    CHECK(load_project(R"({"format": "vats-project", "ik_solve": "vats"})", q, err));
    CHECK(q.clip.ik_solve == IkSolve::VATs);
    CHECK(!load_project(R"({"format": "vats-project", "ik_solve": "other"})", q, err));
    CHECK(!load_project(R"({"format": "vats-project", "ik_solve": 1})", q, err));
    CHECK(load_project(R"({"format": "vats-project", "version": 4, "ik_solve": "other"})", q, err));
    CHECK(q.read_only);
}

TEST(project_rejects_malformed) {
    Project p = sample_project();
    std::string before = save_project(p);
    for (const char* bad : {
             R"({"format": "vats-project", "curves": {"mHead": {"rot_x": [[0, 1, 2, 0, 0, 0, 0, 0]]}}})",
             R"({"format": "vats-project", "curves": {"mHead": {"rot_x": [[0, 1, 2, 0, 0, 0, 0, 0, "x"]]}}})",
             R"({"format": "vats-project", "curves": {"mHead": {"rot_x": [5]}}})",
             R"({"format": "vats-project", "curves": {"mHead": {"rot_x": {}}}})",
             R"({"format": "vats-project", "curves": {"mHead": []}})",
             R"({"format": "vats-project", "curves": []})",
             R"({"format": "vats-project", "fps": "30"})",
             R"({"format": "vats-project", "anchors": [1]})",
             R"({"format": "vats-project", "anchors": {}})",
             R"({"format": "vats-project", "anchors": [{"pos": [1, 2]}]})",
             R"({"format": "vats-project", "anchors": [{"rot": [0, 0, 0, "1"]}]})",
             R"({"format": "vats-project", "anchors": [{"from": "3"}]})",
             R"({"format": "vats-project", "constraints": ["00"]})",
             R"({"format": "vats-project", "orphans": [{"name": "x", "rot": [[0, 1, 2]]}]})",
             R"({"format": "vats-project", "euler_order": "zyx"})",
             R"({"format": "something-else"})",
             R"({"version": 1})",
             R"([1, 2])",
             "{\"format\": \"vats-project\",",
         }) {
        std::string err;
        CHECK(!load_project(bad, p, err));
        CHECK(!err.empty());
    }
    CHECK_EQ(save_project(p), before);  // untouched on failure

    std::string err;
    CHECK(!load_project(R"({"format": "vats-project", "curves": {"mHead": {"rot_x": [[0, 1]]}}})", p, err));
    CHECK(err.find("curves.mHead.rot_x[0]") != std::string::npos);

    // Enum ints are clamped; out-of-range clip fields too.
    Project c;
    CHECK(load_project(R"({"format": "vats-project", "fps": 0, "hand_pose": 99, "priority": 9.7,
                          "curves": {"mHead": {"rot_x": [[0, 1, 9, -3, 7.2, 0, 0, 0, 0]]}}})",
                       c, err));
    CHECK_EQ(c.clip.fps, 1);
    CHECK_EQ(c.clip.hand_pose, 13);
    CHECK_EQ(c.clip.priority, 6);
    auto& k = c.clip.curves.at("mHead").at("rot_x").keys[0];
    CHECK(k.interp == Interp::Bezier);
    CHECK(k.left == Handle::AutoClamped);
    CHECK(k.right == Handle::Plateau);
}

TEST(project_newer_version_read_only) {
    Project p;
    std::string err;
    CHECK(load_project(R"({"format": "vats-project", "version": 4, "euler_order": "zyx", "fps": 60, "new": 1})",
                       p, err));
    CHECK(p.read_only);
    CHECK_EQ(p.clip.fps, 60);
    CHECK(p.extra.find("new"));
    CHECK(load_project(R"({"format": "vats-project", "version": 4.0})", p, err));
    CHECK(p.read_only);
    CHECK(load_project(R"({"format": "vats-project", "version": 1})", p, err));
    CHECK(!p.read_only);
    CHECK(!load_project(R"({"format": "vats-project", "version": 0})", p, err));
}

TEST(prop_parse_sl_vector) {
    Vec3 v;
    CHECK(parse_sl_vector("<1, -2.5, 3e-2>", v));
    CHECK(v == (Vec3{1, -2.5, 0.03}));
    CHECK(parse_sl_vector("  < +0.5 ,1,  -1E1 >\n", v));
    CHECK(v == (Vec3{0.5, 1, -10}));
    for (const char* bad : {"", "<1, 2>", "<1, 2, 3, 4>", "1, 2, 3", "<1, x, 3>", "<1,, 3>", "<1, 2, 3> x", "<nan, 1, 2>"})
        CHECK(!parse_sl_vector(bad, v));
}

TEST(loop_out_follows_last_frame) {
    Clip c = new_project_clip();  // 0 to 30, Loop out 30
    set_last_frame(c, 60);
    CHECK_EQ(c.loop_out, 60);  // it was at the old last frame
    set_loop(c, true);
    CHECK(c.loop && c.loop_in == 0 && c.loop_out == 60);
    c.loop_out = 40;  // set by hand: stays when the clip grows, comes in when it shrinks
    set_last_frame(c, 90);
    CHECK_EQ(c.loop_out, 40);
    c.loop_in = 35;
    set_last_frame(c, 20);
    CHECK(c.end_frame == 20 && c.loop_out == 20 && c.loop_in == 20);
    Clip old;  // a project lengthened before Loop out followed: 0 to 30 in 60 frames, loop off
    old.end_frame = 60;
    set_loop(old, true);
    CHECK_EQ(old.loop_out, 60);
    Clip chosen;  // loop points someone set stay
    chosen.end_frame = 60, chosen.loop_in = 10, chosen.loop_out = 30;
    set_loop(chosen, true);
    CHECK(chosen.loop_in == 10 && chosen.loop_out == 30);
}
