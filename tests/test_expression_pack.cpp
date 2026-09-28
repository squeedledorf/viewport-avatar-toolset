// Expression packs (08 EX): one short face-only .anim per expression, with ease, priority and one naming pattern.
#include <algorithm>
#include <fstream>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/edit.h"
#include "vats/expression_pack.h"

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

}  // namespace

TEST(expression_pack_names_count_face_bones_and_ease) {
    ExpressionPackOptions opt;
    opt.positions = true;
    const std::vector<PackFile> pack = expression_pack(table(), starter_expressions(), opt);
    const std::vector<std::string> names = {"Face_smile", "Face_big_smile", "Face_frown", "Face_surprise",
                                            "Face_wink_l", "Face_wink_r", "Face_angry", "Face_sad",
                                            "Face_blink_loop", "Face_idle_breathing", "Face_kiss", "Face_tongue_out"};
    CHECK_EQ(pack.size(), names.size());
    const std::vector<std::string> face = table().bones();
    for (size_t i = 0; i < pack.size() && i < names.size(); ++i) {
        CHECK_EQ(pack[i].name, names[i]);
        const AnimExportResult r = export_anim(skel(), pack[i].clip);
        CHECK(r.errors.empty());
        CHECK(!r.file.joints.empty());
        for (const AnimJoint& j : r.file.joints)
            if (std::find(face.begin(), face.end(), j.name) == face.end()) check::fail(__FILE__, __LINE__, "not a face bone: " + j.name);
        CHECK_NEAR(r.file.ease_in, 0.3, 1e-6);
        CHECK_NEAR(r.file.ease_out, 0.3, 1e-6);
        CHECK_EQ(r.file.base_priority, 4);
        CHECK_EQ(r.file.loop, 1);
        CHECK(r.file.duration <= 4.0f + 1e-4f);
    }
    // A wink moves the left lids only, and holds for the length.
    const Clip& wink = pack[4].clip;
    CHECK(wink.curves.count("mFaceEyeLidUpperLeft") && !wink.curves.count("mFaceEyeLidUpperRight"));
    CHECK_EQ(wink.end_frame, 60);
    CHECK_NEAR(curve_euler(wink, "mFaceEyeLidUpperLeft", 30).y, 32, 1e-6);
    // The blink loop is open at the seam and shut mid-blink.
    const Clip& blink = pack[8].clip;
    CHECK_EQ(blink.end_frame, 120);
    CHECK(blink.loop && blink.loop_in == 0 && blink.loop_out == 120);
    CHECK_NEAR(curve_euler(blink, "mFaceEyeLidUpperLeft", 0).y, 0, 1e-6);
    CHECK_NEAR(curve_euler(blink, "mFaceEyeLidUpperLeft", 63).y, 32, 1e-6);
    CHECK_NEAR(curve_euler(blink, "mFaceEyeLidUpperLeft", 120).y, 0, 1e-6);
}

TEST(expression_pack_respects_move_face_bones) {
    ExpressionPackOptions opt;  // Move face bones off
    opt.prefix = "My Face!", opt.priority = 5, opt.loop = false, opt.length = 1;
    std::vector<std::string> skipped;
    const std::vector<PackFile> pack = expression_pack(table(), starter_expressions(), opt, &skipped);
    // Frown and sad only move bones in the default head's table.
    CHECK_EQ(skipped, (std::vector<std::string>{"frown", "sad"}));
    CHECK_EQ(pack.size(), size_t(10));
    for (const PackFile& p : pack) {
        CHECK(p.name.rfind("My_Face_", 0) == 0);
        for (auto& [bone, track] : p.clip.curves) CHECK(!p.clip.has_channels(bone, kPosChannels));
        const AnimExportResult r = export_anim(skel(), p.clip);
        CHECK_EQ(r.file.base_priority, 5);
        for (const AnimJoint& j : r.file.joints) CHECK(j.pos.empty());
    }
    CHECK_EQ(pack[0].clip.loop, false);
    CHECK_EQ(pack[0].clip.end_frame, 30);
    CHECK(pack[6].name == "My_Face_blink_loop" && pack[6].clip.loop);  // it loops whatever the setting
}

TEST(expression_pack_from_a_face_pose) {
    LibraryItem pose;
    pose.name = "Smug grin #2";
    pose.kind = "face";
    pose.bones = {{"mFaceJaw", {0, 8, 0}}, {"mFaceEyeLidUpperLeft", {0, 0, 0}}};
    pose.offsets = {{"mFaceLipCornerLeft", {0, 0, 0.003}}};
    ExpressionPackOptions opt;
    Clip c = expression_clip(table(), face_pose_expression(pose), opt);
    CHECK_EQ(c.curves.size(), size_t(1));  // bones at rest are left to other animations; offsets need positions
    CHECK_NEAR(curve_euler(c, "mFaceJaw", 10).y, 8, 1e-6);
    opt.positions = true;
    c = expression_clip(table(), face_pose_expression(pose), opt);
    CHECK_NEAR(curve_offset(c, "mFaceLipCornerLeft", 10).z, 0.003, 1e-9);
    CHECK_EQ(expression_anim_name("Face", pose.name), std::string("Face_smug_grin_2"));
    CHECK_EQ(expression_anim_name("", "wink L"), std::string("wink_l"));
    CHECK(expression_anim_name("Face", std::string(100, 'a')).size() == 63);
    Expression smile = face_pose_expression(pose);
    smile.name = "Smile";
    const std::vector<PackFile> two = expression_pack(table(), {starter_expressions()[0], smile}, opt);
    CHECK(two.size() == 2 && two[0].name == "Face_smile" && two[1].name == "Face_smile_2");
}
