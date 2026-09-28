// Lip sync (08 LS): the loudness envelope and the LPC vowel classifier (tier 1), Rhubarb Lip Sync cues (tier 2),
// and keying them onto the mouth bones as one change that can be taken back.
#include <cmath>
#include <fstream>
#include <sstream>

#include "check.h"
#include "vats/edit.h"
#include "vats/lip_sync.h"
#include "vats/project.h"

using namespace vats;

namespace {

std::string read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const FaceTable& table() {
    static FaceTable t = [] {
        FaceTable out;
        std::string err;
        if (!parse_face_table(read(std::string(VATS_DATA_DIR) + "/../retarget/face-arkit.json"), out, err))
            std::fprintf(stderr, "face table: %s\n", err.c_str());
        return out;
    }();
    return t;
}

const LipShapes& shapes() {
    static LipShapes s = [] {
        LipShapes out;
        std::string err;
        if (!parse_lip_shapes(read(std::string(VATS_DATA_DIR) + "/../retarget/lip-shapes.json"), out, err))
            std::fprintf(stderr, "lip shapes: %s\n", err.c_str());
        return out;
    }();
    return s;
}

// A vowel: a 120 Hz pulse train through two-pole resonators at the formants (bandwidths 80, 90, 120 Hz), the
// classic cascade synthesiser. seconds of it at rate, peak amplitude 0.5.
std::vector<float> vowel(double f1, double f2, double seconds, int rate) {
    std::vector<double> x(size_t(seconds * rate), 0.0);
    for (size_t n = 0; n < x.size(); n += size_t(rate / 120)) x[n] = 1;
    const double formants[3][2] = {{f1, 80}, {f2, 90}, {2500, 120}};
    for (auto& [f, bw] : formants) {
        const double r = std::exp(-kPi * bw / rate), c = 2 * r * std::cos(2 * kPi * f / rate);
        double y1 = 0, y2 = 0;
        for (double& v : x) {
            const double y = v + c * y1 - r * r * y2;
            y2 = y1, y1 = y, v = y;
        }
    }
    double peak = 0;
    for (double v : x) peak = std::max(peak, std::fabs(v));
    std::vector<float> out;
    for (double v : x) out.push_back(float(0.5 * v / peak));
    return out;
}

// The jawOpen weight the jaw's keys hold at a frame (the table's jawOpen turns mFaceJaw alone).
double jaw_open(const Clip& c, int frame) {
    const Vec3 full = table().shapes.at("jawOpen").at(0).rot;
    return curve_euler(c, "mFaceJaw", frame).dot(full) / full.dot(full);
}

}  // namespace

TEST(lpc_levinson_durbin_recovers_an_ar2_process) {
    // x[n] = 1.3 x[n-1] - 0.4 x[n-2] + e[n]: A(z) = 1 - 1.3 z^-1 + 0.4 z^-2.
    std::vector<double> x(20000, 0.0);
    std::uint32_t s = 12345;
    for (size_t n = 2; n < x.size(); ++n) {
        s = s * 1664525u + 1013904223u;
        x[n] = 1.3 * x[n - 1] - 0.4 * x[n - 2] + (double(s >> 8) / double(1u << 24) - 0.5);
    }
    const std::vector<double> a = lpc(x, 2);
    CHECK_EQ(a.size(), size_t(3));
    CHECK_NEAR(a[0], 1, 1e-12);
    CHECK_NEAR(a[1], -1.3, 0.02);
    CHECK_NEAR(a[2], 0.4, 0.02);
    CHECK_NEAR(lpc(std::vector<double>(100, 0.0), 4)[1], 0, 0);  // silence: no prediction
}

TEST(lip_vowel_classifier_tells_open_from_rounded_and_wide) {
    for (int rate : {44100, 16000}) {
        const std::vector<float> a = vowel(730, 1090, 0.3, rate), u = vowel(300, 870, 0.3, rate), i = vowel(270, 2290, 0.3, rate);
        const Formants fa = lpc_formants(a, rate, a.size() / 2), fu = lpc_formants(u, rate, u.size() / 2);
        CHECK_NEAR(fa.f1, 730, 120);
        CHECK_NEAR(fa.f2, 1090, 150);
        CHECK_NEAR(fu.f1, 300, 100);
        CHECK_NEAR(fu.f2, 870, 150);
        CHECK_EQ(vowel_class(fa), std::string("open"));
        CHECK_EQ(vowel_class(fu), std::string("rounded"));
        CHECK_EQ(vowel_class(lpc_formants(i, rate, i.size() / 2)), std::string("wide"));
    }
}

TEST(lip_envelope_opens_the_jaw_only_inside_the_burst) {
    const int rate = 44100;
    AudioData audio;
    audio.rate = rate, audio.channels = 1;
    audio.pcm.assign(size_t(rate), 0.f);  // 1 s of silence, a 1 s /a/, 1 s of silence
    const std::vector<float> burst = vowel(730, 1090, 1.0, rate);
    audio.pcm.insert(audio.pcm.end(), burst.begin(), burst.end());
    audio.pcm.insert(audio.pcm.end(), size_t(rate), 0.f);
    Clip c;
    c.fps = 30, c.end_frame = 90;
    LipSync ls = lip_sync_from_audio(audio, 0, c.fps, c.end_frame, {});
    CHECK_EQ(ls.from, 0);
    CHECK_EQ(ls.to, 90);
    CHECK_EQ(ls.level.size(), size_t(91));
    apply_lip_sync(c, table(), shapes(), ls);
    // The burst runs from frame 30 (1.0 s) to frame 60 (2.0 s).
    for (int f = 0; f <= 90; ++f) {
        if (f >= 31 && f <= 59) CHECK(jaw_open(c, f) > 0.5);
        if (f < 29 || f > 61) CHECK(jaw_open(c, f) <= 0.5);
    }
    CHECK_NEAR(jaw_open(c, 10), 0, 1e-9);
    CHECK_NEAR(jaw_open(c, 80), 0, 1e-9);
    // The burst reads as one open vowel between rests.
    CHECK_EQ(ls.cues.size(), size_t(3));
    CHECK_EQ(ls.cues[1].shape, std::string("open"));
    CHECK_EQ(ls.cues[1].frame, 30);
    CHECK_EQ(ls.cues[2].shape, std::string("X"));
    CHECK_EQ(ls.cues[2].frame, 60);
    // The audio starting half a second later moves it all 15 frames.
    Clip later;
    later.fps = 30, later.end_frame = 90;
    apply_lip_sync(later, table(), shapes(), lip_sync_from_audio(audio, 0.5, 30, 90, {}));
    CHECK(jaw_open(later, 50) > 0.5);
    CHECK(jaw_open(later, 40) <= 0.5);
}

TEST(lip_rhubarb_json_and_tsv_key_on_the_frame) {
    std::vector<RhubarbCue> json, tsv;
    std::string err;
    CHECK(parse_rhubarb(read(std::string(VATS_TEST_FILES) + "/rhubarb.json"), json, err));
    CHECK(parse_rhubarb(read(std::string(VATS_TEST_FILES) + "/rhubarb.tsv"), tsv, err));
    CHECK_EQ(json.size(), size_t(5));
    CHECK_EQ(tsv.size(), size_t(6));
    for (size_t i = 0; i < 5 && i < json.size() && i < tsv.size(); ++i) {
        CHECK_NEAR(json[i].start, tsv[i].start, 1e-12);
        CHECK_EQ(json[i].shape, tsv[i].shape);
    }
    CHECK(!parse_rhubarb("{\"mouthCues\": [{\"start\": 0, \"value\": \"Q\"}]}", json, err));
    CHECK(!parse_rhubarb("hello world\n", json, err));
    CHECK(parse_rhubarb(read(std::string(VATS_TEST_FILES) + "/rhubarb.json"), json, err));

    Clip c;
    c.fps = 30, c.end_frame = 45;
    const LipSync ls = lip_sync_from_cues(json, 0, c.fps, c.end_frame, 0, -1);
    // 0.2 s = frame 6 (D), 0.5 s = 15 (A), 0.7 s = 21 (F), 1.0 s = 30 (X).
    CHECK_EQ(ls.cues.size(), size_t(5));
    apply_lip_sync(c, table(), shapes(), ls);
    const double d = shapes().shapes.at("D").at("jawOpen"), f = shapes().shapes.at("F").at("jawOpen");
    CHECK_NEAR(jaw_open(c, 5), 0, 1e-6);
    CHECK_NEAR(jaw_open(c, 6), d, 1e-6);
    CHECK_NEAR(jaw_open(c, 14), d, 1e-6);
    CHECK_NEAR(jaw_open(c, 15), 0, 1e-6);  // A: closed
    CHECK_NEAR(jaw_open(c, 20), 0, 1e-6);
    CHECK_NEAR(jaw_open(c, 21), f, 1e-6);
    CHECK_NEAR(jaw_open(c, 29), f, 1e-6);
    CHECK_NEAR(jaw_open(c, 30), 0, 1e-6);
    // A range: the cue running at its first frame starts there; frames outside it are not keyed.
    Clip part;
    part.fps = 30, part.end_frame = 45;
    apply_lip_sync(part, table(), shapes(), lip_sync_from_cues(json, 0, 30, 45, 10, 25));
    CHECK_EQ(part.lip_sync->cues.front().frame, 10);
    CHECK_EQ(part.lip_sync->cues.front().shape, std::string("D"));
    CHECK_NEAR(jaw_open(part, 10), d, 1e-6);
    CHECK_NEAR(jaw_open(part, 6), 0, 1e-6);
    CHECK_NEAR(jaw_open(part, 29), 0, 1e-6);
}

TEST(lip_sync_adds_to_the_face_and_takes_back_exactly) {
    Clip c;
    c.fps = 30, c.end_frame = 45;
    key_euler(c, "mFaceJaw", 0, {0, 3, 0});  // a face already there: a slightly open jaw, and a lid
    key_euler(c, "mFaceJaw", 45, {0, 6, 0});
    key_euler(c, "mFaceEyeLidUpperLeft", 20, {0, 10, 0});
    const Clip original = c;
    std::vector<RhubarbCue> cues;
    std::string err;
    CHECK(parse_rhubarb(read(std::string(VATS_TEST_FILES) + "/rhubarb.json"), cues, err));
    LipSync ls = lip_sync_from_cues(cues, 0, 30, 45, 0, -1);
    apply_lip_sync(c, table(), shapes(), ls);
    const Vec3 full = table().shapes.at("jawOpen").at(0).rot;
    CHECK_NEAR((curve_euler(c, "mFaceJaw", 10) - curve_euler(original, "mFaceJaw", 10) - full * 0.7).length(), 0, 1e-6);
    CHECK(c.curves.count("mFaceEyeLidUpperLeft") && c.curves.at("mFaceEyeLidUpperLeft") == original.curves.at("mFaceEyeLidUpperLeft"));
    // A nudge: D starts two frames later. Frames 6 and 7 go back to the face as it was.
    LipSync nudged = *c.lip_sync;
    nudged.cues[1].frame = 8;
    apply_lip_sync(c, table(), shapes(), nudged);
    for (int f : {6, 7}) CHECK_NEAR((curve_euler(c, "mFaceJaw", f) - curve_euler(original, "mFaceJaw", f)).length(), 0, 1e-6);
    CHECK(jaw_open(c, 8) > 0.7);
    remove_lip_sync(c, table(), shapes());
    CHECK(!c.lip_sync);
    for (int f = 0; f <= 45; ++f)
        CHECK_NEAR((curve_euler(c, "mFaceJaw", f) - curve_euler(original, "mFaceJaw", f)).length(), 0, 1e-6);
}

TEST(lip_sync_saves_with_the_project) {
    Project p;
    p.clip.fps = 30, p.clip.end_frame = 45;
    std::vector<RhubarbCue> cues;
    std::string err;
    CHECK(parse_rhubarb(read(std::string(VATS_TEST_FILES) + "/rhubarb.json"), cues, err));
    LipSync ls = lip_sync_from_cues(cues, 0, 30, 45, 0, -1);
    ls.positions = true;
    ls.level = {0.5, 1};
    p.clip.lip_sync = ls;
    Project back;
    CHECK(load_project(save_project(p), back, err));
    CHECK(back.clip.lip_sync && *back.clip.lip_sync == ls);
}
