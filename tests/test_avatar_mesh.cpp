#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>

#include "check.h"
#include "fixtures.h"
#include "vats/avatar_mesh.h"
#include "vats/facecap.h"
#include "vats/footlock.h"

using namespace vats;

namespace {

AvatarMesh& mesh() {
    static AvatarMesh m = [] {
        AvatarMesh a;
        std::string err;
        if (!a.load(skel(), VATS_DATA_DIR, err)) {
            std::fprintf(stderr, "cannot load avatar mesh: %s\n", err.c_str());
            std::exit(2);
        }
        return a;
    }();
    return m;
}

std::vector<std::uint8_t> read_llm(const char* name) {
    std::ifstream f(std::string(VATS_DATA_DIR) + "/" + name, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::vector<std::string> names(const std::vector<int>& nodes) {
    std::vector<std::string> out;
    for (int n : nodes) out.push_back(n < 0 ? "mRoot" : skel()[n].name);
    return out;
}

Vec3 at(const std::vector<float>& v, size_t i) { return {v[i * 3], v[i * 3 + 1], v[i * 3 + 2]}; }

void skin_pose(const Pose& pose, const Shape* shape, std::vector<float>& pos, std::vector<float>& nrm) {
    mesh().skin(skel().global_pose(pose, shape), shape, pos, nrm);
}

}  // namespace

TEST(avatar_mesh_file_counts) {
    struct Row {
        AvatarMesh::File f;
        int verts, faces, joints, morphs, remaps;
    } rows[] = {{AvatarMesh::Head, 1132, 1844, 2, 95, 175},
                {AvatarMesh::UpperBody, 2211, 3688, 12, 30, 342},
                {AvatarMesh::LowerBody, 977, 1654, 7, 24, 135},
                {AvatarMesh::Eyelashes, 48, 46, 1, 34, 2},
                {AvatarMesh::Eye, 145, 272, 0, 2, 0}};
    for (auto& r : rows) {
        const LlmMesh& m = mesh().file(r.f);
        CHECK_EQ(m.vertex_count(), r.verts);
        CHECK_EQ(m.face_count(), r.faces);
        CHECK_EQ(static_cast<int>(m.skin_joints.size()), r.joints);
        CHECK_EQ(static_cast<int>(m.morphs.size()), r.morphs);
        CHECK_EQ(static_cast<int>(m.remaps.size()), r.remaps);
        CHECK_EQ(m.weights.size(), r.f == AvatarMesh::Eye ? size_t(0) : size_t(r.verts));
    }
    CHECK(mesh().file(AvatarMesh::Head).find_morph("Male_Head"));
    CHECK(mesh().file(AvatarMesh::UpperBody).find_morph("Male_Torso"));
    CHECK(mesh().file(AvatarMesh::LowerBody).find_morph("Male_Legs"));
}

TEST(avatar_mesh_palettes) {
    using V = std::vector<std::string>;
    CHECK(names(mesh().palette(AvatarMesh::Head)) == (V{"mChest", "mNeck", "mHead"}));
    CHECK(names(mesh().palette(AvatarMesh::UpperBody)) ==
          (V{"mRoot", "mPelvis", "mTorso", "mChest", "mNeck", "mChest", "mCollarLeft", "mShoulderLeft", "mElbowLeft",
             "mWristLeft", "mChest", "mCollarRight", "mShoulderRight", "mElbowRight", "mWristRight"}));
    CHECK(names(mesh().palette(AvatarMesh::LowerBody)) ==
          (V{"mRoot", "mPelvis", "mHipRight", "mKneeRight", "mAnkleRight", "mPelvis", "mHipLeft", "mKneeLeft",
             "mAnkleLeft"}));
    // The viewer's rule gives mHead its base ancestor mNeck first (the spec lists [mHead, mHead]); every
    // shipped eyelash weight is 1.0, so both skin every vertex to mHead.
    CHECK(names(mesh().palette(AvatarMesh::Eyelashes)) == (V{"mNeck", "mHead"}));
    CHECK(mesh().palette(AvatarMesh::Eye).empty());
}

TEST(avatar_mesh_zero_pose_female) {
    mesh().build(Body::Female);
    const auto& parts = mesh().parts();
    CHECK_EQ(parts.size(), size_t(6));
    CHECK(parts[4].material == Material::Eye && parts[5].material == Material::Eye);
    CHECK(parts[0].material == Material::Skin && parts[3].material == Material::Skin);
    CHECK_EQ(mesh().vertex_count(), 1132 + 2211 + 977 + 48 + 2 * 145);
    CHECK_EQ(mesh().indices().size(), size_t(3 * (1844 + 3688 + 1654 + 46 + 2 * 272)));
    for (auto i : mesh().indices()) CHECK(i < static_cast<std::uint32_t>(mesh().vertex_count()));

    std::vector<float> pos, nrm;
    skin_pose(Pose(skel().size()), nullptr, pos, nrm);
    auto g = skel().global_pose(Pose(skel().size()));
    double worst = 0;
    AvatarMesh::File files[] = {AvatarMesh::Head, AvatarMesh::UpperBody, AvatarMesh::LowerBody, AvatarMesh::Eyelashes,
                                AvatarMesh::Eye, AvatarMesh::Eye};
    for (int p = 0; p < 6; ++p) {
        const LlmMesh& f = mesh().file(files[p]);
        Vec3 origin = p == 4 ? g[skel().find("mEyeLeft")].pos : p == 5 ? g[skel().find("mEyeRight")].pos : Vec3{};
        for (int v = 0; v < f.vertex_count(); ++v)
            worst = std::max(worst, (at(pos, parts[p].first_vertex + v) - (at(f.coords, v) + origin)).length());
    }
    CHECK(worst < 1e-5);
}

TEST(avatar_mesh_male) {
    std::vector<float> female, male, nrm;
    mesh().build(Body::Female);
    skin_pose(Pose(skel().size()), nullptr, female, nrm);
    mesh().build(Body::Male);
    const Shape* shape = body_shape(skel(), Body::Male);
    CHECK(shape);
    CHECK(!body_shape(skel(), Body::Female) && !body_shape(skel(), Body::SkeletonOnly));
    skin_pose(Pose(skel().size()), shape, male, nrm);
    CHECK_EQ(male.size(), female.size());
    double diff = 0;
    for (size_t i = 0; i < male.size(); ++i) diff = std::max(diff, double(std::fabs(male[i] - female[i])));
    CHECK(diff > 0.05);
    const MeshPart& legs = mesh().parts()[2];
    double min_z = 1e9;
    for (auto v = legs.first_vertex; v < legs.first_vertex + legs.vertex_count; ++v)
        min_z = std::min(min_z, double(male[v * 3 + 2]));
    CHECK_NEAR(min_z, 0.0, 0.02);
    // Top of the head rises with the taller skeleton.
    double top_f = -1e9, top_m = -1e9;
    for (size_t v = 0; v < male.size() / 3; ++v) top_f = std::max(top_f, double(female[v * 3 + 2]));
    for (size_t v = 0; v < male.size() / 3; ++v) top_m = std::max(top_m, double(male[v * 3 + 2]));
    CHECK(top_m > top_f + 0.03);
}

TEST(avatar_mesh_sl_default) {
    // Every morph the default shapes name exists in its .llm file.
    for (bool male : {false, true})
        for (int f = 0; f < ShapeMeshCount; ++f)
            for (auto& [name, w] : mesh().sl_default(male).morphs[f]) {
                if (!mesh().file(AvatarMesh::File(f)).find_morph(name))
                    std::fprintf(stderr, "  missing morph %s in file %d\n", name.c_str(), f);
                CHECK(mesh().file(AvatarMesh::File(f)).find_morph(name));
            }
    CHECK(mesh().shape(Body::SLDefault) == &mesh().sl_default(false).shape);
    CHECK(mesh().shape(Body::SLDefaultMale) == &mesh().sl_default(true).shape);
    CHECK(mesh().shape(Body::Male) == &skel().male_shape() && !mesh().shape(Body::Female));

    std::vector<float> female, def, def_male, nrm;
    mesh().build(Body::Female);
    skin_pose(Pose(skel().size()), nullptr, female, nrm);
    mesh().build(Body::SLDefault);
    skin_pose(Pose(skel().size()), mesh().shape(Body::SLDefault), def, nrm);
    mesh().build(Body::SLDefaultMale);
    skin_pose(Pose(skel().size()), mesh().shape(Body::SLDefaultMale), def_male, nrm);
    CHECK_EQ(def.size(), female.size());
    double diff = 0;
    for (size_t i = 0; i < def.size(); ++i) diff = std::max(diff, double(std::fabs(def[i] - female[i])));
    CHECK(diff > 0.02);
    // Soles on the ground, and the male default stands taller.
    const MeshPart& legs = mesh().parts()[2];
    double min_z = 1e9, top = -1e9, top_m = -1e9;
    for (auto v = legs.first_vertex; v < legs.first_vertex + legs.vertex_count; ++v)
        min_z = std::min(min_z, double(def[v * 3 + 2]));
    for (size_t v = 0; v < def.size() / 3; ++v) top = std::max(top, double(def[v * 3 + 2]));
    for (size_t v = 0; v < def.size() / 3; ++v) top_m = std::max(top_m, double(def_male[v * 3 + 2]));
    CHECK_NEAR(min_z, 0.0, 0.02);
    CHECK(top_m > top + 0.03);
    mesh().build(Body::Female);
}

// The sole points the ground checks use (footlock.h) sit where the body mesh's soles are: the lowest foot vertex and
// the lowest sole point agree within a centimetre at rest, with the toes lifted (the heel digs in), with the foot
// pointed and with the toes bent, on both default bodies.
TEST(avatar_mesh_sole_points_match_the_mesh) {
    const int ankle = skel().find("mAnkleLeft"), toe = skel().find("mToeLeft");
    for (Body b : {Body::SLDefault, Body::SLDefaultMale}) {
        mesh().build(b);
        const Shape* shape = mesh().shape(b);
        for (auto [node, pitch] : {std::pair{ankle, 0.0}, {ankle, -30.0}, {ankle, 40.0}, {toe, 35.0}}) {
            Pose pose(skel().size());
            pose.rot[node] = euler_to_quat({0, pitch, 0});
            const std::vector<Xform> g = skel().global_pose(pose, shape);
            std::vector<float> pos, nrm;
            mesh().skin(g, shape, pos, nrm);
            double mesh_low = 1e9;  // the left foot: vertices beside the ankle and below its height at rest
            for (size_t v = 0; v < pos.size() / 3; ++v)
                if (std::fabs(pos[v * 3 + 1] - g[ankle].pos.y) < 0.07 && pos[v * 3 + 2] < 0.08 && pos[v * 3] > -0.15)
                    mesh_low = std::min(mesh_low, double(pos[v * 3 + 2]));
            double sole = 1e9;
            for (const Vec3& p : sole_points(skel(), g, 0)) sole = std::min(sole, p.z);
            if (std::fabs(sole - mesh_low) > 0.01)
                std::fprintf(stderr, "  body %d %s pitch %.0f: sole %.4f, mesh %.4f\n", int(b), skel()[node].name.c_str(),
                             pitch, sole, mesh_low);
            CHECK_NEAR(sole, mesh_low, 0.01);
        }
    }
    mesh().build(Body::Female);
}

TEST(avatar_mesh_shoulder_moves_left_hand) {
    mesh().build(Body::Male, false);
    const Shape* shape = body_shape(skel(), Body::Male);
    std::vector<float> rest, posed, nrm;
    skin_pose(Pose(skel().size()), shape, rest, nrm);
    Pose p(skel().size());
    p.rot[skel().find("mShoulderLeft")] = Quat::axis_angle({0, 0, 1}, 90 * kDegToRad);
    skin_pose(p, shape, posed, nrm);
    int wl = skel().find("mWristLeft"), wr = skel().find("mWristRight");
    int left = 0, right = 0;
    for (size_t v = 0; v < mesh().influences().size(); ++v) {
        const Influence& f = mesh().influences()[v];
        int dom = f.blend < 0.5f ? f.a : f.b;
        double moved = (at(posed, v) - at(rest, v)).length();
        if (dom == wl) {
            ++left;
            CHECK(moved > 0.3);
            // +90 degrees about Z turns +Y into -X: the arm swings back, behind the body.
            CHECK(posed[v * 3] < -0.3);
        }
        if (dom == wr) {
            ++right;
            CHECK(moved < 1e-6);
        }
    }
    CHECK(left > 50 && right > 50);
    for (size_t v = 0; v < nrm.size() / 3; ++v) CHECK_NEAR(at(nrm, v).length(), 1.0, 1e-5);
}

TEST(avatar_mesh_winding_ccw) {
    mesh().build(Body::Female);
    const auto& idx = mesh().indices();
    const auto& c = mesh().rest_coords();
    std::vector<float> pos, nrm;
    skin_pose(Pose(skel().size()), nullptr, pos, nrm);
    int agree = 0, total = 0;
    for (size_t t = 0; t < idx.size(); t += 3) {
        Vec3 a = at(c, idx[t]), b = at(c, idx[t + 1]), d = at(c, idx[t + 2]);
        Vec3 n = at(nrm, idx[t]) + at(nrm, idx[t + 1]) + at(nrm, idx[t + 2]);
        agree += (b - a).cross(d - a).dot(n) > 0;
        ++total;
    }
    CHECK(agree > total * 0.95);
}

TEST(avatar_mesh_finger_weights) {
    mesh().build(Body::Male, false);
    auto off = mesh().influences();
    mesh().build(Body::Male, true);
    auto on = mesh().influences();
    CHECK_EQ(on.size(), off.size());
    int wl = skel().find("mWristLeft"), wr = skel().find("mWristRight");
    int index1 = skel().find("mHandIndex1Left");
    auto is_hand = [&](const Influence& f) {
        int dom = f.blend <= 0.001f ? f.a : f.blend >= 0.999f ? f.b : -1;
        return dom == wl || dom == wr;
    };
    int changed = 0, index_bound = 0;
    for (size_t v = 0; v < on.size(); ++v) {
        bool same = on[v].a == off[v].a && on[v].b == off[v].b && on[v].blend == off[v].blend;
        if (!same) {
            ++changed;
            CHECK(is_hand(off[v]));
        }
        index_bound += on[v].b == index1;
    }
    CHECK(changed > 50);
    CHECK(index_bound > 5);

    // Curling the left index finger moves only re-weighted hand vertices, and only with the flag on.
    const Shape* shape = body_shape(skel(), Body::Male);
    Pose p(skel().size());
    p.rot[index1] = Quat::axis_angle({0, 0, 1}, 60 * kDegToRad);
    std::vector<float> rest, posed, nrm;
    skin_pose(Pose(skel().size()), shape, rest, nrm);
    skin_pose(p, shape, posed, nrm);
    int moved = 0;
    for (size_t v = 0; v < on.size(); ++v)
        if ((at(posed, v) - at(rest, v)).length() > 1e-6) {
            ++moved;
            CHECK(is_hand(off[v]));
        }
    CHECK(moved > 5);
    mesh().build(Body::Male, false);
    skin_pose(Pose(skel().size()), shape, rest, nrm);
    skin_pose(p, shape, posed, nrm);
    CHECK(rest == posed);
}

TEST(avatar_mesh_skeleton_only) {
    mesh().build(Body::SkeletonOnly);
    CHECK(mesh().parts().empty() && mesh().indices().empty() && mesh().vertex_count() == 0);
    std::vector<float> pos{1, 2, 3}, nrm;
    skin_pose(Pose(skel().size()), nullptr, pos, nrm);
    CHECK(pos.empty() && nrm.empty());
}

TEST(avatar_mesh_rejects_bad_files) {
    std::vector<std::uint8_t> head = read_llm("avatar_head.llm");
    LlmMesh m;
    std::string err;
    CHECK(parse_llm(head, m, err));
    for (size_t len : {size_t(0), size_t(40), size_t(100), head.size() / 2, head.size() - 5}) {
        std::vector<std::uint8_t> cut(head.begin(), head.begin() + len);
        CHECK(!parse_llm(cut, m, err));
    }
    std::vector<std::uint8_t> bad = head;
    bad[0] = 'X';
    CHECK(!parse_llm(bad, m, err));

    // An out-of-range morph index drops that entry and keeps the morph.
    size_t nv = 1132, nf = 1844;
    size_t first_entry = 65 + nv * (12 * 3 + 8 + 4) + 2 + nf * 6 + 2 + 2 * 64 + 64 + 4;
    CHECK(parse_llm(head, m, err));
    size_t before = m.morphs[0].index.size();
    bad = head;
    bad[first_entry] = 0xff;
    bad[first_entry + 1] = 0xff;
    bad[first_entry + 2] = 0xff;
    CHECK(parse_llm(bad, m, err));
    CHECK_EQ(m.morphs.size(), size_t(95));
    CHECK_EQ(m.morphs[0].index.size(), before - 1);
}

TEST(avatar_mesh_skin_speed) {
    mesh().build(Body::Male);
    const Shape* shape = body_shape(skel(), Body::Male);
    Pose p(skel().size());
    p.rot[skel().find("mElbowLeft")] = Quat::axis_angle({0, 0, 1}, 1.0);
    auto g = skel().global_pose(p, shape);
    std::vector<float> pos, nrm;
    mesh().skin(g, shape, pos, nrm);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 200; ++i) mesh().skin(g, shape, pos, nrm);
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(s < 1.0);
}

// The face cam (spec 09 build 20, item 53): ARKit weights become the Linden head's own expression morphs, each a morph
// the head file has, quantised to twentieths; building with one moves the head.
TEST(face_cam_linden_head_morphs) {
    const auto some = linden_head_morphs({{"eyeBlinkLeft", 1.0}, {"jawOpen", 0.52}, {"mouthSmileLeft", 0.4},
                                          {"mouthSmileRight", 0.2}, {"browInnerUp", 1.0}});
    CHECK(some.size() == 3);
    CHECK(some[0].first == "Blink_Left" && some[0].second == 1.f);
    CHECK(some[1].first == "Express_Open_Mouth" && some[1].second == 0.5f);
    CHECK(std::fabs(some[2].second - 0.3f) < 1e-6f && some[2].first == "Express_Smile");
    std::map<std::string, double> full;
    for (const char* n : {"eyeBlinkLeft", "eyeBlinkRight", "jawOpen", "mouthSmileLeft", "mouthSmileRight", "mouthFrownLeft",
                          "mouthFrownRight", "mouthPucker"})
        full[n] = 1.0;
    const auto all = linden_head_morphs(full);
    CHECK(all.size() == 6);
    for (const auto& [name, w] : all) CHECK(mesh().file(AvatarMesh::Head).find_morph(name) != nullptr);
    BodyShape bs = mesh().sl_default(false);
    AvatarMesh a = mesh();
    a.build(bs);
    const std::vector<float> rest = a.rest_coords();
    bs.morphs[ShapeHead].push_back({"Express_Open_Mouth", 1.f});
    a.build(bs);
    CHECK(a.rest_coords() != rest);
}
