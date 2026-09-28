#include "check.h"
#include "fixtures.h"
#include "vats/bvh.h"

using namespace vats;

namespace {

Clip bvh_clip() {
    Clip c;
    c.end_frame = 20;
    auto key = [&](const char* track, const char* ch, double f, double v) { c.curves[track][ch].set_key(f, v); };
    key("mShoulderRight", "rot_x", 0, 0);
    key("mShoulderRight", "rot_x", 20, -70);
    key("mShoulderRight", "rot_y", 10, 25);
    key("mKneeLeft", "rot_y", 0, 0);
    key("mKneeLeft", "rot_y", 20, 90);
    key("mPelvis", "pos_x", 0, 0);
    key("mPelvis", "pos_x", 20, 0.3);
    key("mPelvis", "rot_z", 20, 45);
    return c;
}

double rot_error_deg(const Quat& a, const Quat& b) {
    return 2.0 * std::acos(std::min(1.0, std::fabs(a.normalized().dot(b.normalized())))) * kRadToDeg;
}

}  // namespace

TEST(bvh_layout) {
    auto r = export_bvh(skel(), bvh_clip());
    const std::string& t = r.text;
    CHECK(t.rfind("HIERARCHY\nROOT mPelvis\n{\n", 0) == 0);
    CHECK(t.find("CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation") != std::string::npos);
    CHECK(t.find("JOINT mShoulderRight") != std::string::npos);
    CHECK(t.find("JOINT mCollarRight") != std::string::npos);  // ancestors come along
    CHECK(t.find("JOINT mHead") == std::string::npos);         // unanimated branches do not
    CHECK(t.find("Frames: 22\nFrame Time: 0.033333\n") != std::string::npos);
    CHECK(t.back() == '\n');
    CHECK(t.find('\r') == std::string::npos);
    CHECK(t.find("-0.000000") == std::string::npos);
    CHECK(r.lost.empty());
}

TEST(bvh_round_trip) {
    Clip c = bvh_clip();
    auto r = export_bvh(skel(), c);
    auto back = import_bvh(skel(), r.text);
    CHECK(back.ok);
    CHECK_EQ(back.clip.fps, 30);
    CHECK_EQ(back.clip.end_frame, 20);
    const Skeleton& s = skel();
    double worst_rot = 0, worst_pos = 0;
    for (int f = 0; f <= 20; ++f) {
        Pose a = evaluate_curves(s, c, f), b = evaluate_curves(s, back.clip, f);
        for (int i = 0; i < s.joint_count(); ++i) worst_rot = std::max(worst_rot, rot_error_deg(a.rot[i], b.rot[i]));
        worst_pos = std::max(worst_pos, (a.offset[0] - b.offset[0]).length());
    }
    CHECK(worst_rot < 1e-3);
    CHECK(worst_pos < 1e-6);
    // T7: writing what we read gives the same text.
    CHECK(export_bvh(s, back.clip).text == r.text);
}

// IO-35: the optional reduction keeps far fewer keys and stays within its tolerance.
TEST(bvh_import_reduction) {
    Clip c = bvh_clip();
    auto text = export_bvh(skel(), c).text;
    auto full = import_bvh(skel(), text);
    auto lean = import_bvh(skel(), text, {0.05, 0.0005});
    CHECK(lean.ok);
    const auto& fk = full.clip.curves.at("mKneeLeft").at("rot_y").keys;
    const auto& lk = lean.clip.curves.at("mKneeLeft").at("rot_y").keys;
    CHECK_EQ(fk.size(), size_t(21));
    CHECK(lk.size() < fk.size());
    CHECK(lean.clip.curves.at("mPelvis").at("pos_x").keys.size() < 21);
    const Skeleton& s = skel();
    double worst = 0;
    for (int f = 0; f <= 20; ++f) {
        Pose a = evaluate_curves(s, full.clip, f), b = evaluate_curves(s, lean.clip, f);
        for (int i = 0; i < s.joint_count(); ++i) worst = std::max(worst, rot_error_deg(a.rot[i], b.rot[i]));
    }
    CHECK(worst < 0.5);
}

TEST(bvh_reports_losses) {
    Clip c = bvh_clip();
    c.curves["Left Hand"]["rot_x"].set_key(0, 10);
    c.curves["mElbowLeft"]["pos_x"].set_key(0, 0.01);
    auto r = export_bvh(skel(), c);
    CHECK_EQ(r.lost.size(), size_t(2));
    BvhExportOptions opt;
    opt.joint_positions = true;
    auto r2 = export_bvh(skel(), c, opt);
    CHECK_EQ(r2.lost.size(), size_t(1));
    size_t elbow = r2.text.find("JOINT mElbowLeft");
    CHECK(elbow != std::string::npos && r2.text.find("CHANNELS 6", elbow) < r2.text.find("JOINT", elbow + 1));
    auto back = import_bvh(skel(), r2.text);
    CHECK(back.ok);
    CHECK_NEAR(back.clip.curves["mElbowLeft"]["pos_x"].evaluate(0), 0.01, 1e-5);
}

TEST(bvh_foreign_file) {
    // Poser-style names, XYZ rotation order, one frame of reference.
    const char* text =
        "HIERARCHY\nROOT hip\n{\n OFFSET 0 0 0\n CHANNELS 6 Xposition Yposition Zposition Xrotation Yrotation Zrotation\n"
        " JOINT abdomen\n {\n  OFFSET 0 3 0\n  CHANNELS 3 Xrotation Yrotation Zrotation\n"
        "  End Site\n  {\n   OFFSET 0 1 0\n  }\n }\n JOINT Mystery\n {\n  OFFSET 0 0 0\n  CHANNELS 3 Zrotation Xrotation Yrotation\n"
        "  End Site\n  {\n   OFFSET 0 1 0\n  }\n }\n}\nMOTION\nFrames: 3\nFrame Time: 0.04\n"
        "0 40 0 0 0 0 0 0 0 0 0 0\n"
        "0 40 10 0 0 0 30 0 0 0 0 0\n"
        "0 40 20 0 90 0 0 0 0 0 0 0\n";
    auto r = import_bvh(skel(), text);
    CHECK(r.ok);
    CHECK_EQ(r.clip.fps, 25);
    CHECK_EQ(r.clip.end_frame, 1);
    CHECK_EQ(r.report.size(), size_t(1));  // Mystery skipped
    // abdomen = mTorso; BVH X rotation is a rotation about SL Y.
    Pose p = evaluate_curves(skel(), r.clip, 0);
    Quat want = Quat::axis_angle({0, 1, 0}, 30 * kDegToRad);
    CHECK(rot_error_deg(p.rot[skel().find("mTorso")], want) < 1e-6);
    // The hip moved 10 inches along BVH Z = SL X, relative to the reference frame.
    CHECK_NEAR(r.clip.curves["mPelvis"]["pos_x"].evaluate(0), 10 * kMetresPerInch, 1e-9);
    // Frame 2: the hip turned 90 degrees about BVH Y = SL Z.
    Pose p1 = evaluate_curves(skel(), r.clip, 1);
    CHECK(rot_error_deg(p1.rot[0], Quat::axis_angle({0, 0, 1}, kPi / 2)) < 1e-6);
}

TEST(bvh_rejects_garbage) {
    CHECK(!import_bvh(skel(), "").ok);
    CHECK(!import_bvh(skel(), "HIERARCHY ROOT hip { OFFSET 0 0").ok);
    CHECK(!import_bvh(skel(), "HIERARCHY ROOT hip { OFFSET 0 0 0 CHANNELS 3 Xrotation Yrotation Zrotation }"
                              " MOTION Frames: 5 Frame Time: 0.1 1 2 3").ok);
    std::string deep = "HIERARCHY ROOT hip {";
    for (int i = 0; i < 10000; ++i) deep += " JOINT j {";
    CHECK(!import_bvh(skel(), deep).ok);
}
