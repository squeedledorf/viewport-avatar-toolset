// Face animation by hand (08 FA): expression sliders and read-back, face poses, the blink/saccade/look-at layer
// and the look-at tool.
#include <cmath>
#include <fstream>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/edit.h"
#include "vats/face_anim.h"
#include "vats/loop_tools.h"
#include "vats/project.h"

using namespace vats;

namespace {

const FaceTable& table() {
    static FaceTable t = [] {
        std::ifstream f(std::string(VATS_DATA_DIR) + "/../retarget/face-arkit.json");
        std::stringstream ss;
        ss << f.rdbuf();
        FaceTable out;
        std::string err;
        if (!parse_face_table(ss.str(), out, err)) std::fprintf(stderr, "face table: %s\n", err.c_str());
        return out;
    }();
    return t;
}

const Rig& rig() {
    static Rig r(skel());
    return r;
}

Clip seconds(double s, int fps = 30) {
    Clip c;
    c.fps = fps;
    c.end_frame = int(s * fps);
    return c;
}

double angle_deg(const Vec3& a, const Vec3& b) {
    return std::acos(std::clamp(a.normalized().dot(b.normalized()), -1.0, 1.0)) / kDegToRad;
}

// Where node looks at a frame (its +X), and the angle to a target, in degrees.
double miss(const Clip& c, const std::string& bone, double f, const Vec3& target) {
    const int n = skel().find(bone);
    const Evaluation e = evaluate(rig(), c, f, nullptr);
    return angle_deg(e.globals[n].rot.rotate({1, 0, 0}), target - e.globals[n].pos);
}

}  // namespace

// --- Face panel -----------------------------------------------------------------------------------------------

TEST(face_sliders_key_bones_and_read_back) {
    const std::map<std::string, double> w = {{"jawOpen", 0.6}, {"eyeBlinkLeft", 0.5}, {"browInnerUp", 0.7}};
    Clip c;
    key_face_weights(c, table(), w, true, 10);
    CHECK(c.curves.count("mFaceJaw"));
    CHECK(c.curves.count("slider") == 0);  // bone keys only
    CHECK(face_weights_match(c, table(), w, true, 10));
    CHECK(!face_weights_match(c, table(), {{"jawOpen", 0.3}}, true, 10));
    const auto back = read_face_weights(c, table(), 10, true);
    CHECK(face_weights_match(c, table(), back, true, 10));  // whatever it finds keys the same bones
    CHECK_NEAR(back.at("jawOpen"), 0.6, 0.02);
    CHECK_NEAR(back.at("browInnerUp"), 0.7, 0.02);
    // Move face bones off: no position keys, and a shape that only moves bones has nothing to read back.
    Clip r;
    key_face_weights(r, table(), {{"browInnerUp", 1.0}, {"jawOpen", 0.5}}, false, 0);
    for (auto& [track, channels] : r.curves) CHECK(!channels.count("pos_x"));
    CHECK(!face_shape_keys(table(), "browInnerUp", false));
    CHECK(face_shape_keys(table(), "browInnerUp", true));
    CHECK_NEAR(read_face_weights(r, table(), 0, false).at("browInnerUp"), 0.0, 1e-9);
    CHECK_NEAR(read_face_weights(r, table(), 0, false).at("jawOpen"), 0.5, 0.02);
    // A VRM preset expands into its shapes and reads back as them.
    Clip j;
    key_face_weights(j, table(), {{"joy", 1.0}}, true, 0);
    CHECK_NEAR(read_face_weights(j, table(), 0, true).at("mouthSmileLeft"), 1.0, 0.02);
    // A slider change keys only the bones it moves, by its difference: a hand-set eye keeps its keys, and a jaw
    // turned by hand opens from where it is.
    Clip e;
    key_euler(e, "mEyeLeft", 0, {0, 0, 7});
    key_euler(e, "mFaceJaw", 0, {0, 3, 0});
    const std::map<std::string, double> before, after = {{"jawOpen", 0.5}};
    key_face_weights(e, table(), after, false, 10, &before);
    CHECK_NEAR(curve_euler(e, "mFaceJaw", 10).y, 13, 1e-9);
    CHECK(!has_key_at(e, "mEyeLeft", 10));
}

TEST(face_pose_saves_and_applies_with_offsets) {
    Clip c;
    key_face_weights(c, table(), {{"mouthSmileLeft", 1.0}, {"browDownLeft", 0.8}}, true, 5);
    LibraryItem it = make_face_pose(c, table(), 5, true);
    CHECK(it.kind == "face" && !it.clip && !it.offsets.empty());
    it.name = "smirk";
    Library lib, back;
    lib.items.push_back(it);
    std::string err;
    CHECK(load_library(save_library(lib), back, err));
    CHECK(back.items.size() == 1 && back.items[0].offsets == it.offsets);
    Clip d;
    apply_pose(d, skel(), back.items[0], 20, false);
    CHECK(face_weights_match(d, table(), {{"mouthSmileLeft", 1.0}, {"browDownLeft", 0.8}}, true, 20));
    // Mirrored, the left brow's move lands on the right brow.
    Clip m;
    apply_pose(m, skel(), back.items[0], 0, true);
    CHECK(face_weights_match(m, table(), {{"mouthSmileRight", 1.0}, {"browDownRight", 0.8}}, true, 0));
}

// --- Layer ----------------------------------------------------------------------------------------------------

TEST(face_layer_blink_count_within_range) {
    Clip c = seconds(60);
    FaceLayer L;
    L.seed = 7;
    L.saccades = false;
    const FaceEvents ev = face_layer_events(c, L);
    CHECK(ev.blinks.size() >= 9 && ev.blinks.size() <= 31);  // one every 2-6 s over 60 s
    for (size_t i = 1; i < ev.blinks.size(); ++i) {
        const double gap = ev.blinks[i] - ev.blinks[i - 1];
        CHECK(gap >= 2 - 1e-9 && gap <= 6 + 1e-9);
    }
    // Baked, the upper lid closes once per blink.
    c.face_layer = L;
    bake_face_layer(c, rig(), nullptr, table(), false, {});
    int closes = 0;
    bool shut = false;
    for (int f = 0; f <= c.end_frame; ++f) {
        const bool now = curve_euler(c, "mFaceEyeLidUpperLeft", f).y > 16;  // half the table's 32 degrees
        closes += now && !shut;
        shut = now;
    }
    CHECK_EQ(closes, int(ev.blinks.size()));
    unbake_face_layer(c, skel(), table());
    CHECK(c.curves.empty() && !c.face_layer->baked);
}

TEST(face_layer_no_saccade_exceeds_eye_limit) {
    for (std::uint32_t seed : {1u, 2u, 3u, 99u, 12345u}) {
        Clip c = seconds(30);
        FaceLayer L;
        L.seed = seed;
        L.eye_limit = 8;
        L.blinks = false;
        const FaceEvents ev = face_layer_events(c, L);
        CHECK(ev.saccades.size() >= 10);
        for (auto& s : ev.saccades) {
            CHECK(std::hypot(s.yaw, s.pitch) <= 8 + 1e-9);
            // Eyes Alive's amplitude-duration relation, from the previous fixation.
            CHECK(s.duration >= 0.025 && s.duration <= 0.025 + 0.0024 * 16 + 1e-9);
        }
        c.face_layer = L;
        bake_face_layer(c, rig(), nullptr, table(), false, {});
        for (int f = 0; f <= c.end_frame; ++f) {
            const Vec3 e = curve_euler(c, "mEyeLeft", f);
            CHECK(std::hypot(e.y, e.z) <= 8 + 0.2);  // the bake's key reduction allows 0.1 degrees
        }
    }
}

TEST(face_layer_same_seed_same_result) {
    auto bake = [](std::uint32_t seed) {
        Clip c = seconds(20);
        c.face_layer = FaceLayer{};
        c.face_layer->seed = seed;
        bake_face_layer(c, rig(), nullptr, table(), false, {});
        return c.curves;
    };
    CHECK(bake(42) == bake(42));
    CHECK(!(bake(42) == bake(43)));
    // Re-baking starts from the pre-bake keys, so it gives the same result again.
    Clip c = seconds(20);
    key_euler(c, "mEyeLeft", 0, {0, 3, 4});
    c.face_layer = FaceLayer{};
    bake_face_layer(c, rig(), nullptr, table(), false, {});
    const auto once = c.curves;
    bake_face_layer(c, rig(), nullptr, table(), false, {});
    CHECK(c.curves == once);
    unbake_face_layer(c, skel(), table());
    CHECK_NEAR(curve_euler(c, "mEyeLeft", 50).z, 4, 1e-9);
}

TEST(face_layer_clean_loop_seam) {
    for (std::uint32_t seed : {1u, 5u, 8u}) {
        Clip c = seconds(12);
        c.loop = true;
        c.loop_in = 15, c.loop_out = 330;
        key_euler(c, "mHead", 0, {0, 5, 0});
        c.face_layer = FaceLayer{};
        c.face_layer->seed = seed;
        const FaceEvents ev = face_layer_events(c, *c.face_layer);
        for (double b : ev.blinks) {
            CHECK(b + 0.25 <= 11.0 + 1e-9);  // no blink runs over loop-out (330 / 30 s)
            CHECK(!(b < 0.5 && b + 0.25 > 0.5));  // nor over loop-in
        }
        if (!ev.blinks.empty()) {  // blinks keep blink_min apart across the seam too
            double first = 1e9, last = 0;
            for (double b : ev.blinks)
                if (b >= 0.5) first = std::min(first, b), last = std::max(last, b);
            if (last > 0) CHECK((11.0 - last) + (first - 0.5) >= 2 - 1e-9);
        }
        bake_face_layer(c, rig(), nullptr, table(), false, {});
        CHECK(loop_seam_jumps(c, 0.2).empty());
        const Vec3 at_in = saccade_offset(ev, 0.5), at_out = saccade_offset(ev, 11.0);
        CHECK(at_in == Vec3{} && at_out == Vec3{});
    }
}

TEST(face_layer_looks_at_its_target) {
    Clip c = seconds(2);
    key_euler(c, "mHead", 0, {0, 0, -10});
    key_euler(c, "mHead", 60, {0, 0, 15});
    FaceLayer L;
    L.saccades = false, L.blinks = false;
    L.head_share = 0.4;
    c.face_layer = L;
    const Vec3 target{1.2, 0.35, 1.9};
    bake_face_layer(c, rig(), nullptr, table(), false, [&](double, Vec3& w) { return w = target, true; });
    CHECK(c.curves.count("mHead"));
    for (int f = 0; f <= c.end_frame; f += 3)
        for (const char* eye : {"mEyeLeft", "mEyeRight", "mFaceEyeAltLeft", "mFaceEyeAltRight"})
            CHECK(miss(c, eye, f, target) < 1.0);
}

// --- Look-at tool ---------------------------------------------------------------------------------------------

TEST(look_at_hits_target_within_one_degree) {
    Clip c = seconds(3);
    key_euler(c, "mNeck", 0, {5, 10, -20});
    key_euler(c, "mNeck", 90, {-5, -5, 25});
    key_euler(c, "mHead", 45, {0, 12, 0});
    // A target moving past the avatar, like another actor's bone.
    const LookTarget moving = [](double f, Vec3& w) { return w = Vec3{1.0, -0.8 + f / 60.0, 1.5 + 0.1 * std::sin(f / 10)}, true; };
    const std::vector<int> nodes = {skel().find("mHead"), skel().find("mEyeLeft"), skel().find("mEyeRight")};
    LookAtOptions opt;
    opt.max_turn = 80;
    std::string why;
    CHECK(look_at_bake(c, rig(), nodes, moving, opt, nullptr, why));
    for (int f = 0; f <= c.end_frame; f += 5) {
        Vec3 t;
        moving(f, t);
        for (const char* b : {"mHead", "mEyeLeft", "mEyeRight"}) CHECK(miss(c, b, f, t) < 1.0);
    }
    // The limit: a target behind the avatar turns the head only max_turn from straight ahead.
    Clip d = seconds(1);
    opt.max_turn = 40;
    CHECK(look_at_bake(d, rig(), {skel().find("mHead")}, [](double, Vec3& w) { return w = {-2, 0.5, 1.7}, true; }, opt,
                       nullptr, why));
    const Quat q = euler_to_quat(curve_euler(d, "mHead", 10));
    CHECK_NEAR(angle_deg(q.rotate({1, 0, 0}), {1, 0, 0}), 40, 0.5);
    // The weight: half-way turns about half as far.
    Clip h = seconds(1);
    opt.max_turn = 90, opt.weight = 0.5;
    CHECK(look_at_bake(h, rig(), {skel().find("mHead")}, [](double, Vec3& w) { return w = {0, 3, 1.75}, true; }, opt,
                       nullptr, why));
    CHECK_NEAR(curve_euler(h, "mHead", 0).z, 45, 3);
    CHECK(!look_at_bake(h, rig(), {0}, moving, opt, nullptr, why));  // the hips are refused
}

TEST(face_layer_keeps_head_keys_it_did_not_bake) {
    Clip c = seconds(2);
    c.face_layer = FaceLayer{};
    bake_face_layer(c, rig(), nullptr, table(), false, {});  // no look-at: the head is not the layer's
    key_euler(c, "mHead", 30, {0, 0, 20});
    bake_face_layer(c, rig(), nullptr, table(), false, {});
    CHECK_NEAR(curve_euler(c, "mHead", 30).z, 20, 1e-9);
    // With a look-at the head is baked, and Clear puts back the head keys from before that bake.
    const LookTarget left = [](double, Vec3& w) { return w = {0.5, 2, 1.7}, true; };
    bake_face_layer(c, rig(), nullptr, table(), false, left);
    CHECK(c.face_layer->head_baked && curve_euler(c, "mHead", 0).z > 20);
    bake_face_layer(c, rig(), nullptr, table(), false, {});  // the look-at taken away: the head goes back
    CHECK_NEAR(curve_euler(c, "mHead", 0).z, 20, 1e-9);
    bake_face_layer(c, rig(), nullptr, table(), false, left);
    unbake_face_layer(c, skel(), table());
    CHECK_NEAR(curve_euler(c, "mHead", 0).z, 20, 1e-9);
    CHECK(!c.curves.count("mEyeLeft"));
}

TEST(face_layer_saves_with_the_project) {
    Project p;
    p.clip = seconds(4);
    FaceLayer L;
    L.seed = 77, L.look = "actor", L.actor = "Partner", L.bone = "mHead", L.point = {1, 2, 3}, L.head_share = 0.25;
    p.clip.face_layer = L;
    bake_face_layer(p.clip, rig(), nullptr, table(), false, {});
    Project q;
    std::string err;
    CHECK(load_project(save_project(p), q, err, ""));
    CHECK(q.clip.face_layer == p.clip.face_layer);
}
