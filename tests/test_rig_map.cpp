// Putting any rigged model on SL's skeleton (spec 08 RM): the CC0 mech of tools/mech_rig.h renamed, Y up and in
// centimetres, mapped back by the suggestion; weights folding into a mapped joint; the mapping file; and the CC0 example
// mech shipped in data/bodies/mech (Animated Mech Pack by Quaternius), posed with Auto IK and exported for SL.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "../tools/mech_rig.h"
#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/avatar_mesh.h"
#include "vats/dae.h"
#include "vats/edit.h"
#include "vats/fbx.h"
#include "vats/rig.h"
#include "vats/rig_export.h"
#include "vats/rig_map.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

int node(const char* name) { return skel().find(name); }

const std::vector<RigTable>& tables() {
    static const std::vector<RigTable> t = [] {
        std::vector<RigTable> out;
        for (const auto& e : fs::directory_iterator(fs::path(VATS_DATA_DIR) / ".." / "retarget")) {
            std::ifstream f(e.path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            RigTable table;
            std::string err;
            if (parse_rig_table(ss.str(), table, err) && !table.bones.empty()) out.push_back(table);
        }
        return out;
    }();
    return t;
}

// The mech's SL joints under the names a rig of its own might have: no rig table knows them.
std::string foreign_name(int n) {
    static const std::map<std::string, std::string> names = {
        {"mPelvis", "Hips"},       {"mTorso", "Spine"},         {"mChest", "Chest"},          {"mNeck", "Neck"},
        {"mHead", "Head"},         {"mHindLimbsRoot", "Haunches"}, {"mCollar", "Clavicle"},   {"mShoulder", "UpperArm"},
        {"mElbow", "Forearm"},     {"mWrist", "Hand"},          {"mHip", "Thigh"},            {"mKnee", "Shin"},
        {"mAnkle", "Foot"},        {"mFoot", "Toe"},            {"mHindLimb1", "BackThigh"},  {"mHindLimb2", "BackShin"},
        {"mHindLimb3", "BackHock"}, {"mHindLimb4", "BackFoot"}};
    std::string sl = skel()[n].name, side;
    for (const char* s : {"Left", "Right"})
        if (sl.size() > std::string(s).size() && sl.ends_with(s)) side = s[0] == 'L' ? ".L" : ".R", sl.resize(sl.size() - std::string(s).size());
    auto it = names.find(sl);
    return (it != names.end() ? it->second : sl) + side;
}

// The renamed mech as a file would carry it: a joint tree, an IK foot at the root and a knee pole with no weights,
// _end tips (the head's places mSkull; the hind foot's has nothing left to place), every length in centimetres, Y up,
// facing -Y as a Blender rig does.
std::string foreign_mech(const mech::Body& body) {
    const std::vector<Xform> rest = skel().global_pose(Pose(skel().size()));
    auto at = [&](const char* n) { return rest[node(n)].pos; };
    mech::Foreign f{foreign_name, 100, {}};
    f.helpers = {{"IK_Foot.L", "", at("mAnkleLeft")},
                 {"IK_Foot.R", "", at("mAnkleRight")},
                 {"Pole.L", "Hips", at("mKneeLeft") + Vec3{0.5, 0, 0}},
                 {"Head_end", "Head", body.find(node("mHead"))->tail},
                 {"BackFoot.L_end", "BackFoot.L", body.find(node("mHindLimb4Left"))->tail}};
    return mech::dae(skel(), body, -1, true, &f);
}

Shape body_shape(const DaeModel& m) {
    Shape s;
    shape_from_binds(skel(), {&m}, nullptr, s);
    rig_axes_from_parts(skel(), {&m}, s);
    return s;
}

// The worst distance between two models' skinned vertices, at rest and in a pose that turns the spine, an arm, both
// kinds of leg and the head.
double skin_gap(const DaeModel& a, const DaeModel& b) {
    if (a.vertex_count() != b.vertex_count() || !a.vertex_count()) return 1e9;
    Pose pose(skel().size());
    const std::pair<const char*, Vec3> turns[] = {{"mPelvis", {0, 0, 0.3}},       {"mChest", {0.2, 0.3, 0}},
                                                  {"mShoulderLeft", {0.6, 0.2, 0}}, {"mElbowLeft", {0, 0, 0.9}},
                                                  {"mKneeRight", {0, 0.7, 0}},      {"mHindLimb2Left", {0.4, 0.5, 0.1}},
                                                  {"mHead", {0, 0.3, -0.4}},       {"mHindLimbsRoot", {0, 0.2, 0}}};
    double worst = 0;
    const Shape sa = body_shape(a), sb = body_shape(b);
    for (int posed = 0; posed < 2; ++posed) {
        if (posed)
            for (auto& [n, axis] : turns) pose.rot[node(n)] = Quat::axis_angle(axis.normalized(), axis.length());
        std::vector<float> pa, pb, na, nb;
        skin_prop(a, skel(), skel().global_pose(pose, &sa), &sa, pa, na);
        skin_prop(b, skel(), skel().global_pose(pose, &sb), &sb, pb, nb);
        for (size_t i = 0; i < pa.size(); i += 3)
            worst = std::max(worst, (Vec3{pa[i], pa[i + 1], pa[i + 2]} - Vec3{pb[i], pb[i + 1], pb[i + 2]}).length());
    }
    return worst;
}

std::string temp_dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / (std::string("vats_rig_map_") + name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d.string();
}

}  // namespace

TEST(rig_map_recovers_a_renamed_y_up_centimetre_rig) {
    const mech::Body body = mech::build(skel());
    const std::string text = foreign_mech(body);
    DaeModel plain, mapped, original;
    DaeReport rp, rm, ro;
    std::string err;
    CHECK(load_dae(text, "", skel(), plain, rp, err));
    CHECK(rp.unmapped_joints.size() >= 20);  // no SL names (a few aliases at most) until it is mapped
    CHECK(rp.bones.size() == body.bones.size() + 5);
    RigMap map = suggest_rig_map(skel(), rp.bones, tables());
    CHECK(map.turn == 1);  // written facing -Y: a quarter turn faces it along SL's +X
    // Every renamed bone finds its SL joint again; the helpers map to nothing, the head's tip places mSkull.
    for (const mech::Bone& b : body.bones) {
        const RigMapBone* m = map.find(foreign_name(b.node));
        CHECK(m && m->target == skel()[b.node].name);
        if (m && m->target != skel()[b.node].name)
            std::fprintf(stderr, "  %s -> %s (want %s): %s\n", m->source.c_str(), m->target.c_str(), skel()[b.node].name.c_str(),
                         m->reason.c_str());
    }
    for (const char* h : {"IK_Foot.L", "IK_Foot.R", "Pole.L", "BackFoot.L_end"}) CHECK(map.find(h) && map.find(h)->target.empty());
    CHECK(map.find("Head_end") && map.find("Head_end")->target == "mSkull");
    map.height = 0;  // the file's own size: centimetres, declared, so the original metres
    const RigMapResult r = resolve_rig_map(skel(), rp.bones, map);
    CHECK(r.dropped.size() == 4 && r.folded.empty() && r.problems.empty());
    CHECK(load_dae(text, "", skel(), mapped, rm, err, &r.remap));
    CHECK(mapped.rigged && rm.unmapped_joints.empty());
    CHECK(std::fabs(rm.scale - 0.01) < 1e-12);
    // Skinned on SL's skeleton at the model's own joint positions, it is the SL-named original within a millimetre.
    CHECK(load_dae(mech::dae(skel(), body), "", skel(), original, ro, err));
    CHECK(skin_gap(mapped, original) < 0.001);
    // Only the renamed file asks to be mapped before it can be a mesh body.
    CHECK(rig_needs_mapping(skel(), rp) && !rig_needs_mapping(skel(), ro) && !rig_needs_mapping(skel(), rm));
    for (const mech::Bone& b : body.bones) CHECK((mapped.binds[b.node].pos - original.binds[b.node].pos).length() < 1e-4);
}

TEST(rig_map_folds_unmapped_weights_into_the_mapped_parent) {
    // Three bones in a chain; the middle one has no SL joint: its share joins its parent's, the weights still sum to 1.
    const std::string dae = R"(<?xml version="1.0"?><COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
<asset><unit meter="1"/><up_axis>Z_UP</up_axis></asset>
<library_geometries><geometry id="g"><mesh>
<source id="p"><float_array id="pa" count="9">0 0 1 0.1 0 1.2 0 0.1 1.4</float_array><technique_common><accessor source="#pa" count="3" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
<vertices id="v"><input semantic="POSITION" source="#p"/></vertices><triangles count="1"><input semantic="VERTEX" source="#v" offset="0"/><p>0 1 2</p></triangles>
</mesh></geometry></library_geometries>
<library_controllers><controller id="c"><skin source="#g"><bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</bind_shape_matrix>
<source id="j"><Name_array id="ja" count="3">Root Mid End</Name_array><technique_common><accessor source="#ja" count="3" stride="1"><param name="JOINT" type="name"/></accessor></technique_common></source>
<source id="b"><float_array id="ba" count="48">1 0 0 0 0 1 0 0 0 0 1 -1 0 0 0 1 1 0 0 0 0 1 0 0 0 0 1 -1.2 0 0 0 1 1 0 0 0 0 1 0 0 0 0 1 -1.4 0 0 0 1</float_array><technique_common><accessor source="#ba" count="3" stride="16"><param name="TRANSFORM" type="float4x4"/></accessor></technique_common></source>
<source id="w"><float_array id="wa" count="6">0.2 0.3 0.5 1 0.6 0.4</float_array><technique_common><accessor source="#wa" count="6" stride="1"><param name="WEIGHT" type="float"/></accessor></technique_common></source>
<joints><input semantic="JOINT" source="#j"/><input semantic="INV_BIND_MATRIX" source="#b"/></joints>
<vertex_weights count="3"><input semantic="JOINT" source="#j" offset="0"/><input semantic="WEIGHT" source="#w" offset="1"/><vcount>3 1 2</vcount><v>0 0 1 1 2 2 1 3 2 4 1 5</v></vertex_weights>
</skin></controller></library_controllers>
<library_visual_scenes><visual_scene id="s"><node id="Root" name="Root" type="JOINT"><matrix>1 0 0 0 0 1 0 0 0 0 1 1 0 0 0 1</matrix>
<node id="Mid" name="Mid" type="JOINT"><matrix>1 0 0 0 0 1 0 0 0 0 1 0.2 0 0 0 1</matrix><node id="End" name="End" type="JOINT"><matrix>1 0 0 0 0 1 0 0 0 0 1 0.2 0 0 0 1</matrix></node></node></node>
<node id="m"><instance_controller url="#c"/></node></visual_scene></library_visual_scenes><scene><instance_visual_scene url="#s"/></scene></COLLADA>)";
    DaeModel plain, m;
    DaeReport rp, rm;
    std::string err;
    CHECK(load_dae(dae, "", skel(), plain, rp, err));
    CHECK(rp.bones.size() == 3 && rp.bones[1].parent == 0 && rp.bones[2].parent == 1);
    CHECK(std::fabs(rp.bones[1].weight - 1.7) < 1e-6);  // 0.3 + 1 + 0.4
    RigMap map;
    map.bones = {{"Root", "mPelvis", 100, ""}, {"Mid", "", 100, ""}, {"End", "mTorso", 100, ""}};
    const RigMapResult r = resolve_rig_map(skel(), rp.bones, map);
    CHECK(r.folded == std::vector<std::string>{"Mid into Root (mPelvis)"});
    CHECK(r.folded_into[1] == 0 && r.node[1] == node("mPelvis"));
    CHECK(load_dae(dae, "", skel(), m, rm, err, &r.remap));
    const int pelvis = node("mPelvis"), torso = node("mTorso");
    auto weight = [&](int v, int joint) {
        double w = 0;
        for (int k = 0; k < 4; ++k) w += m.joints[v * 4 + k] == joint ? m.weights[v * 4 + k] : 0;
        return w;
    };
    const double want[3][2] = {{0.5, 0.5}, {1, 0}, {0.4, 0.6}};  // mPelvis, mTorso per vertex
    // The vertices are in the file's order (one triangle), whatever the loader's own order.
    for (int v = 0; v < 3; ++v) {
        const Vec3 p{m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]};
        const int src = p.z < 1.1 ? 0 : p.z < 1.3 ? 1 : 2;
        CHECK_NEAR(weight(v, pelvis), want[src][0], 1e-6);
        CHECK_NEAR(weight(v, torso), want[src][1], 1e-6);
        CHECK_NEAR(m.weights[v * 4] + m.weights[v * 4 + 1] + m.weights[v * 4 + 2] + m.weights[v * 4 + 3], 1, 1e-6);
    }
    // A weighted bone with no mapped ancestor joins the first mapped bone rather than being lost.
    map.bones = {{"Root", "", 100, ""}, {"Mid", "mPelvis", 100, ""}, {"End", "mTorso", 100, ""}};
    const RigMapResult orphan = resolve_rig_map(skel(), rp.bones, map);
    CHECK(orphan.node[0] == pelvis && orphan.folded.size() == 1);
}

TEST(rig_map_file_round_trip_and_reload) {
    const mech::Body body = mech::build(skel());
    const std::string text = foreign_mech(body);
    DaeModel plain;
    DaeReport rp;
    std::string err;
    CHECK(load_dae(text, "", skel(), plain, rp, err));
    RigMap map = suggest_rig_map(skel(), rp.bones, tables());
    map.find("Spine")->target = "mSpine2";  // an edit survives
    map.height = 1.5;
    RigMap back;
    CHECK(parse_rig_map_json(write_rig_map_json(map), back, err));
    CHECK(back.bones.size() == map.bones.size() && back.turn == map.turn && back.height == 1.5);
    for (size_t i = 0; i < map.bones.size() && i < back.bones.size(); ++i)
        CHECK(back.bones[i].source == map.bones[i].source && back.bones[i].target == map.bones[i].target);
    CHECK(!parse_rig_map_json("{\"bones\": {}}", back, err) && !err.empty());
    CHECK(!parse_rig_map_json("{\"vats-rig-map\": 2, \"bones\": {}}", back, err));
    CHECK(rig_map_path("/a/b/George.fbx") == "/a/b/George.rigmap.json");
    CHECK(rig_map_path("/a.b/mech") == "/a.b/mech.rigmap.json");
    // Beside its model, the mapping is picked up whenever the model is read: a body re-imported, a project reopened.
    const std::string dir = temp_dir("reload");
    std::ofstream(dir + "/mech.dae") << text;
    DaeModel m;
    DaeReport r;
    CHECK(load_mesh_file(dir + "/mech.dae", skel(), m, r, err));
    CHECK(!r.remapped);
    CHECK(read_rig_map_file(dir + "/mech.rigmap.json", back, err) == false && err.empty());  // none yet
    std::ofstream(rig_map_path(dir + "/mech.dae")) << write_rig_map_json(map);
    CHECK(load_mesh_file(dir + "/mech.dae", skel(), m, r, err));
    CHECK(m.rigged && r.remapped);
    CHECK(std::fabs((m.bounds_max.z - m.bounds_min.z) - 1.5) < 1e-4);  // made 1.5 m tall
    CHECK(m.bound[node("mSpine2")]);
    RigMap none = map;  // a mapping that maps nothing: the file as it is, with a warning, never unloadable
    for (RigMapBone& b : none.bones) b.target.clear();
    std::ofstream(rig_map_path(dir + "/mech.dae")) << write_rig_map_json(none);
    CHECK(load_mesh_file(dir + "/mech.dae", skel(), m, r, err));
    CHECK(!r.remapped && !r.warnings.empty() && r.warnings.back().find("mech.rigmap.json") != std::string::npos);
    std::ofstream(rig_map_path(dir + "/mech.dae")) << "{ broken";
    CHECK(load_mesh_file(dir + "/mech.dae", skel(), m, r, err));  // a broken mapping: the file as it is, with a warning
    CHECK(!r.remapped && !r.warnings.empty() && r.warnings.back().find("mech.rigmap.json") != std::string::npos);
    fs::remove_all(dir);
}

TEST(rig_map_faces_by_the_face) {
    // No sides and no legs: a jaw ahead of the head, the model facing -Y, says which way is forward.
    auto bone = [](const char* name, int parent, Vec3 at) { return SourceBone{name, parent, {Quat{}, at}, true, 1}; };
    const std::vector<SourceBone> bones = {bone("Hips", -1, {0, 0, 1}), bone("Spine", 0, {0, 0, 1.3}), bone("Head", 1, {0, 0, 1.6}),
                                           bone("Jaw", 2, {0, -0.08, 1.55})};
    const RigMap map = suggest_rig_map(skel(), bones, {});
    CHECK(map.turn == 1);
    CHECK(map.facing.find("eyes and jaw") != std::string::npos);
    CHECK(map.find("Head")->target == "mHead" && map.find("Jaw")->target == "mFaceJaw");
}

TEST(rig_map_soft_body_helpers_go_on_collision_volumes) {
    // A CC0 humanoid built here with a game rig's soft-body helpers: butt cheeks off the hips, breasts off the chest, a
    // belly and love handles off the spine. They go on SL's collision volumes (which avatar physics moves) instead of
    // folding into the joint they hang from; a "bust" bone off the head is no breast.
    auto bone = [](const char* name, int parent, Vec3 at, double w = 1) { return SourceBone{name, parent, {Quat{}, at}, true, w}; };
    const std::vector<SourceBone> bones = {
        bone("Hips", -1, {0, 0, 1}),            bone("Spine", 0, {0, 0, 1.15}),           bone("Chest", 1, {0, 0, 1.3}),
        bone("Neck", 2, {0, 0, 1.5}),           bone("Head", 3, {0, 0, 1.6}),             bone("Thigh.L", 0, {0, 0.1, 0.95}),
        bone("Shin.L", 5, {0, 0.1, 0.5}),       bone("Foot.L", 6, {0, 0.1, 0.08}),        bone("Thigh.R", 0, {0, -0.1, 0.95}),
        bone("Shin.R", 8, {0, -0.1, 0.5}),      bone("Foot.R", 9, {0, -0.1, 0.08}),       bone("UpperArm.L", 2, {0, 0.18, 1.45}),
        bone("Forearm.L", 11, {0, 0.45, 1.45}), bone("Hand.L", 12, {0, 0.7, 1.45}),       bone("UpperArm.R", 2, {0, -0.18, 1.45}),
        bone("Forearm.R", 14, {0, -0.45, 1.45}), bone("Hand.R", 15, {0, -0.7, 1.45}),     bone("Butt_Ctrl", 0, {-0.05, 0, 0.95}, 0),
        bone("butt_left", 17, {-0.08, 0.07, 0.9}), bone("butt_right", 17, {-0.08, -0.07, 0.9}), bone("Breast.L", 2, {0.1, 0.09, 1.33}),
        bone("Breast.R", 2, {0.1, -0.09, 1.33}), bone("Belly", 1, {0.1, 0, 1.12}),        bone("LoveHandle.L", 1, {0, 0.13, 1.1}),
        bone("Bust_Bow", 4, {0.08, 0, 1.75})};
    const RigMap map = suggest_rig_map(skel(), bones, {});
    const std::pair<const char*, const char*> want[] = {{"butt_left", "BUTT"},          {"butt_right", "BUTT"},
                                                        {"Breast.L", "LEFT_PEC"},       {"Breast.R", "RIGHT_PEC"},
                                                        {"Belly", "BELLY"},             {"LoveHandle.L", "LEFT_HANDLE"},
                                                        {"Bust_Bow", ""},               {"Chest", "mChest"}};
    for (const auto& [b, t] : want) {
        CHECK(map.find(b) && map.find(b)->target == t);
        if (map.find(b) && map.find(b)->target != t) std::fprintf(stderr, "  %s -> %s (want %s)\n", b, map.find(b)->target.c_str(), t);
    }
    CHECK(map.find("Breast.L")->reason.find("collision volume") != std::string::npos);
    // Resolved, the weights go to the volumes' own SK-40 indices, not to the joints they hang from.
    const RigMapResult r = resolve_rig_map(skel(), bones, map);
    CHECK(r.problems.empty());
    const int butt = map_skin_joint(skel(), "BUTT"), leg = map_skin_joint(skel(), "L_UPPER_LEG");
    CHECK(r.node[20] == map_skin_joint(skel(), "LEFT_PEC") && r.node[22] == map_skin_joint(skel(), "BELLY"));
    // A cheek on one side shares its weight with that side's thigh: loaded on L_UPPER_LEG, then spread by height between
    // BUTT (at the cheek) and the leg (from half way to the knee), so a leg lift takes the lower cheek along.
    CHECK(r.node[18] == leg && r.node[19] == map_skin_joint(skel(), "R_UPPER_LEG"));
    const RigMapResult::Resample* split = nullptr;
    for (const RigMapResult::Resample& s : r.resample)
        if (s.only == leg) split = &s;
    CHECK(split && split->nodes == std::vector<int>({butt, leg}));
    if (split) {
        DaeModel m;
        m.rigged = true;
        const int root = dae_root(skel());
        for (double z : {0.9, 0.8, 0.6}) {  // at the cheek, a quarter of the way to the knee, past half way
            m.positions.insert(m.positions.end(), {-0.08f, 0.07f, float(z)});
            m.joints.insert(m.joints.end(), {leg, node("mPelvis"), root, root});
            m.weights.insert(m.weights.end(), {0.8f, 0.2f, 0, 0});
        }
        spread_spare_weights(skel(), *split, m);
        auto w = [&](int v, int j) {
            double x = 0;
            for (int k = 0; k < 4; ++k) x += m.joints[size_t(v * 4 + k)] == j ? m.weights[size_t(v * 4 + k)] : 0;
            return x;
        };
        CHECK_NEAR(w(0, butt), 0.8, 1e-6);
        CHECK(w(1, butt) > 0.1 && w(1, leg) > 0.1);
        CHECK_NEAR(w(2, leg), 0.8, 1e-6);
        for (int v = 0; v < 3; ++v) CHECK_NEAR(w(v, butt) + w(v, leg) + w(v, node("mPelvis")), 1, 1e-6);
    }
    // The volumes keep SL's place against their joints, wherever the model puts the joint; not at the helper bone.
    const CollisionVolume& bv = skel().volumes()[size_t(butt - dae_root(skel()) - 1)];
    CHECK((r.remap.binds.at(butt).pos - (bones[0].bind.pos + bv.pos)).length() < 1e-9);
}

TEST(rig_map_mirror_names) {
    CHECK(mirror_bone_name("UpperArm.L") == "UpperArm.R");
    CHECK(mirror_bone_name("thigh_r") == "thigh_l");
    CHECK(mirror_bone_name("mixamorig:LeftHandIndex1") == "mixamorig:RightHandIndex1");
    CHECK(mirror_bone_name("Index2.L_end") == "Index2.R_end");
    CHECK(mirror_bone_name("Bip01 L Thigh") == "Bip01 R Thigh");
    CHECK(mirror_bone_name("PalmR") == "");  // a palm's R is a letter of its name, not a side
    CHECK(mirror_bone_name("Spine") == "");
}

TEST(rig_map_avatar_height_is_the_linden_body) {
    AvatarMesh mesh;
    std::string err;
    CHECK(mesh.load(skel(), VATS_DATA_DIR, err));
    mesh.build(Body::SLDefault);
    const Shape* sh = mesh.shape(Body::SLDefault);
    std::vector<float> p, n;
    mesh.skin(skel().global_pose(Pose(skel().size()), sh), sh, p, n);
    float lo = 1e9f, hi = -1e9f;
    for (size_t i = 2; i < p.size(); i += 3) lo = std::min(lo, p[i]), hi = std::max(hi, p[i]);
    CHECK(std::fabs((hi - lo) - kSlAvatarHeight) < 0.005);
}

// The example body: the CC0 mech George (Animated Mech Pack by Quaternius), its armature and skin as data/bodies/mech
// keeps them, mapped by the file beside it.
TEST(rig_map_example_mech_poses_and_exports) {
    const std::string path = (fs::path(VATS_DATA_DIR) / ".." / "bodies" / "mech" / "George.dae").lexically_normal().string();
    DaeModel plain, m;
    DaeReport rp, r;
    std::string err;
    CHECK(load_mesh_file_as_is(path, skel(), plain, rp, err));
    // The suggestion alone maps it as the shipped mapping does: nothing had to be fixed by hand.
    RigMap shipped;
    CHECK(read_rig_map_file(rig_map_path(path), shipped, err));
    const RigMap suggested = suggest_rig_map(skel(), rp.bones, tables());
    CHECK(suggested.bones.size() == shipped.bones.size() && suggested.turn == shipped.turn);
    for (const RigMapBone& b : suggested.bones) CHECK(shipped.find(b.source) && shipped.find(b.source)->target == b.target);
    CHECK(load_mesh_file(path, skel(), m, r, err));
    CHECK(m.rigged && r.remapped);
    CHECK(std::fabs((m.bounds_max.z - m.bounds_min.z) - shipped.height) < 1e-3);
    // The legs are digitigrade, three segments and a foot: mHip > mKnee > mAnkle > mFoot, each at the mech's own joint.
    const Shape s = body_shape(m);
    const std::vector<Xform> rest = skel().global_pose(Pose(skel().size()), &s);
    for (const char* j : {"mHipLeft", "mKneeLeft", "mAnkleLeft", "mFootLeft", "mWristRight", "mHead"})
        CHECK((rest[node(j)].pos - m.binds[node(j)].pos).length() < 1e-4);
    CHECK(rest[node("mKneeLeft")].pos.x > rest[node("mHipLeft")].pos.x);   // the knee forward
    CHECK(rest[node("mAnkleLeft")].pos.x < rest[node("mKneeLeft")].pos.x); // the hock back
    // Auto IK: the left foot dragged forward and up; the leg reaches it about its own hinges, the hip stays.
    const Rig rig(skel());
    Clip c;
    const int ankle = node("mAnkleLeft");
    const AutoIkChain chain = auto_ik_chain(rig, c, 0, ankle);
    CHECK(!chain.bones.empty() && chain.bones.front() == node("mHipLeft"));
    const Evaluation start = evaluate(rig, c, 0, &s);
    const Vec3 target = start.globals[ankle].pos + Vec3{0.15, 0, 0.12};
    key_auto_ik(c, rig, 0, chain, start, target, &s);
    const Evaluation posed = evaluate(rig, c, 0, &s);
    CHECK((posed.globals[ankle].pos - target).length() < 0.01);
    CHECK((posed.globals[node("mHipLeft")].pos - start.globals[node("mHipLeft")].pos).length() < 1e-6);
    std::vector<float> before, after, n;
    skin_prop(m, skel(), start.globals, &s, before, n);
    skin_prop(m, skel(), posed.globals, &s, after, n);
    double moved = 0;
    for (size_t i = 0; i < before.size(); ++i) moved = std::max(moved, double(std::fabs(after[i] - before[i])));
    CHECK(moved > 0.05);  // the leg's mesh followed
    // Exported for SL and read back: SL names only, and every joint where the mech has it.
    RigPart part{&m, "George", r.unmapped_joints, r.measured_scale, r.scale};
    const std::vector<RigFinding> findings = check_rig_export(skel(), {part}, {});
    CHECK(!rig_export_refused(findings));
    std::string text;
    CHECK(write_rig_dae(skel(), {part}, {}, text, err));
    DaeModel back;
    DaeReport rb;
    CHECK(load_dae(text, "", skel(), back, rb, err));
    CHECK(back.rigged && rb.unmapped_joints.empty() && back.triangle_count() == m.triangle_count());
    for (const auto& [name, at] : uploader_joint_translations(text)) CHECK(skel().find(name) >= 0 && name != "mRoot");
    for (int j = 0; j < skel().joint_count(); ++j)
        if (m.bound[j]) CHECK((back.binds[j].pos - m.binds[j].pos).length() < 1e-4);
}

namespace {

// A CC0 four-legged rig as a game export might carry it: anonymous "Bone.NNN" names, a level spine from the hips to
// the head, front legs off the chest, hind legs and a tail off the hips, paws pointing forward, IK targets with no
// weights. Built facing +X, then turned `quarter` quarter turns about Z (a Blender export faces -Y: quarter 3).
std::vector<SourceBone> quadruped(int quarter) {
    std::vector<SourceBone> b;
    const Quat q = Quat::axis_angle({0, 0, 1}, quarter * kPi / 2);
    auto add = [&](const std::string& name, int parent, Vec3 at, double w) {
        b.push_back({name, parent, {Quat{}, q.rotate(at)}, w > 0, w});
        return int(b.size()) - 1;
    };
    int id = 1;
    auto next = [&] { return "Bone.0" + std::string(id < 10 ? "0" : "") + std::to_string(id++); };
    const int root = add("Armature", -1, {0, 0, 0}, 0);
    const int hips = add("Bone", root, {-0.5, 0, 0.8}, 2);
    const int spine = add(next(), hips, {-0.1, 0, 0.8}, 2);
    const int chest = add(next(), spine, {0.3, 0, 0.82}, 2);
    const int head = add(next(), chest, {0.5, 0, 1.0}, 5);
    add("Bone.003_end", head, {0.75, 0, 1.0}, 0);
    auto leg = [&](int parent, double x, double y) {
        const int a = add(next(), parent, {x, y, 0.72}, 1);
        const int k = add(next(), a, {x + 0.02, y, 0.38}, 1);
        const int p = add(next(), k, {x, y, 0.05}, 1);
        add(b[size_t(p)].name + "_end", p, {x + 0.1, y, 0.05}, 0);
    };
    leg(chest, 0.3, 0.15), leg(chest, 0.3, -0.15), leg(hips, -0.5, 0.15), leg(hips, -0.5, -0.15);
    const int tail = add(next(), hips, {-0.6, 0, 0.85}, 0.6);
    const int tip = add(next(), tail, {-0.8, 0, 0.9}, 0.4);
    add(b[size_t(tip)].name + "_end", tip, {-1.0, 0, 1.0}, 0);
    add("IKFrontLeft", root, {0.3, 0.15, 0.05}, 0);
    add("IKFrontRight", root, {0.3, -0.15, 0.05}, 0);
    return b;
}

std::string target_of(const RigMap& m, const char* source) { return m.find(source) ? m.find(source)->target : "?"; }

}  // namespace

TEST(rig_map_quadruped_front_legs_on_arms_or_bento) {
    const std::vector<SourceBone> bones = quadruped(3);  // facing -Y, as Blender exports
    const RigMap a = suggest_rig_map(skel(), bones, {});
    CHECK(a.quadruped && !a.bento && a.along_ground && a.turn == 1);
    // Layout (a), the default: the spine level from the hips to the head, the front legs on the arms, the back legs on
    // the legs, the tail on mTail.
    const std::pair<const char*, const char*> want_a[] = {
        {"Bone", "mPelvis"},            {"Bone.001", "mTorso"},        {"Bone.002", "mChest"},      {"Bone.003", "mHead"},
        {"Bone.003_end", "mSkull"},     {"Bone.004", "mShoulderLeft"}, {"Bone.005", "mElbowLeft"},  {"Bone.006", "mWristLeft"},
        {"Bone.007", "mShoulderRight"}, {"Bone.010", "mHipLeft"},      {"Bone.011", "mKneeLeft"},   {"Bone.012", "mAnkleLeft"},
        {"Bone.012_end", "mFootLeft"},  {"Bone.013", "mHipRight"},     {"Bone.016", "mTail1"},      {"Bone.017", "mTail2"},
        {"Bone.017_end", "mTail3"},     {"IKFrontLeft", ""}};
    for (auto& [src, sl] : want_a) {
        CHECK(target_of(a, src) == sl);
        if (target_of(a, src) != sl) std::fprintf(stderr, "  (a) %s -> %s, want %s\n", src, target_of(a, src).c_str(), sl);
    }
    CHECK(resolve_rig_map(skel(), bones, a).folded.empty());  // every weighted bone has a joint of its own
    // Layout (b), Bento: the back legs on the hind limbs, the front legs on the legs; the spine and tail as before.
    const RigMap b = suggest_rig_map(skel(), bones, {}, true);
    CHECK(b.quadruped && b.bento && b.turn == 1);
    const std::pair<const char*, const char*> want_b[] = {
        {"Bone.002", "mChest"},         {"Bone.004", "mHipLeft"},       {"Bone.005", "mKneeLeft"},
        {"Bone.006", "mAnkleLeft"},     {"Bone.010", "mHindLimb1Left"}, {"Bone.012", "mHindLimb3Left"},
        {"Bone.012_end", "mHindLimb4Left"}, {"Bone.016", "mTail1"}};
    for (auto& [src, sl] : want_b) {
        CHECK(target_of(b, src) == sl);
        if (target_of(b, src) != sl) std::fprintf(stderr, "  (b) %s -> %s, want %s\n", src, target_of(b, src).c_str(), sl);
    }
    // The layout and how the size is measured go in the mapping file.
    RigMap back;
    std::string err;
    CHECK(parse_rig_map_json(write_rig_map_json(b), back, err));
    CHECK(back.quadruped && back.bento && back.along_ground);
    // Upright bodies stay measured floor to top; a lying one by the longer side of its footprint.
    DaeModel m;
    m.bounds_min = {-1, -0.2, 0}, m.bounds_max = {1, 0.2, 1};
    CHECK(rig_map_size(m, a) == 2 && rig_map_size(m, RigMap{}) == 1);
}

TEST(rig_map_quadruped_faces_by_its_head_tail_and_paws) {
    // No names say a side: the head (heavier than the tail's end) and the paws say which way is forward, whichever way
    // the file has the animal facing.
    for (int k = 0; k < 4; ++k) {
        const RigMap m = suggest_rig_map(skel(), quadruped(k), {});
        CHECK(m.turn == (4 - k) % 4);
        CHECK(m.facing.find("head and tail") != std::string::npos && m.facing.find("toes") != std::string::npos);
        CHECK(target_of(m, "Bone.004") == "mShoulderLeft");  // the left side found from where it sits
    }
    // The head alone, for a body with no feet to go by (a fish): heavier than its tail's end.
    std::vector<SourceBone> fish;
    auto add = [&](const char* n, int p, Vec3 at, double w) { fish.push_back({n, p, {Quat{}, at}, w > 0, w}); };
    add("Bone", -1, {0, 0, 0.5}, 2);  // the body, facing -Y
    add("Bone.001", 0, {0, -0.3, 0.5}, 6);  // the head
    add("Bone.001_end", 1, {0, -0.7, 0.5}, 0);
    add("Bone.002", 0, {0, 0.3, 0.5}, 1);  // the tail
    add("Bone.002_end", 3, {0, 0.9, 0.5}, 0);
    add("Bone.003", 0, {0.15, -0.1, 0.45}, 0.5);  // the fins
    add("Bone.003_end", 5, {0.35, 0.05, 0.4}, 0);
    add("Bone.004", 0, {-0.15, -0.1, 0.45}, 0.5);
    add("Bone.004_end", 7, {-0.35, 0.05, 0.4}, 0);
    const RigMap f = suggest_rig_map(skel(), fish, {});
    CHECK(f.turn == 1 && f.along_ground && !f.quadruped);
    CHECK(target_of(f, "Bone.001") == "mHead" && target_of(f, "Bone.002") == "mTail1");
    CHECK(target_of(f, "Bone.003") == "mWing1Left" && target_of(f, "Bone.004") == "mWing1Right");
}

// A thigh split into a weightless "_twist" upper half and a weighted "_stretch" lower half (Auto-Rig Pro): the hip
// joint goes where the twist half starts, so the leg bends at the hip, not half way down the thigh.
TEST(rig_map_twist_half_places_the_hip) {
    auto bone = [](const char* name, int parent, Vec3 at, double w) { return SourceBone{name, parent, {Quat{}, at}, true, w}; };
    const std::vector<SourceBone> bones = {bone("root.x", -1, {0, 0, 1.0}, 1), bone("thigh_twist.l", 0, {0, 0.1, 0.84}, 0),
                                           bone("thigh_stretch.l", 0, {0, 0.1, 0.66}, 1), bone("leg_stretch.l", 2, {0, 0.1, 0.48}, 1)};
    RigMap map;
    for (auto [src, tgt] : {std::pair{"root.x", "mPelvis"}, {"thigh_twist.l", ""}, {"thigh_stretch.l", "mHipLeft"},
                            {"leg_stretch.l", "mKneeLeft"}})
        map.bones.push_back({src, tgt, 100, "test", ""});
    const RigMapResult r = resolve_rig_map(skel(), bones, map);
    CHECK_NEAR(r.remap.binds.at(skel().find("mHipLeft")).pos.z, 0.84, 1e-9);
    CHECK_NEAR(r.remap.binds.at(skel().find("mKneeLeft")).pos.z, 0.48, 1e-9);
}

// An Auto-Rig Pro export without arm_twist.l: the shoulder is where its control, c_arm_twist_offset.l, sits, not
// half way down the upper arm where arm_stretch.l starts (the arm folded at the shoulder blade).
TEST(rig_map_twist_control_places_the_shoulder) {
    auto bone = [](const char* name, int parent, Vec3 at, double w) { return SourceBone{name, parent, {Quat{}, at}, true, w}; };
    const std::vector<SourceBone> bones = {bone("root.x", -1, {0, 0, 1.0}, 1), bone("spine_02.x", 0, {0, 0, 1.24}, 1),
                                           bone("shoulder.l", 1, {0, 0.05, 1.40}, 1),
                                           bone("c_arm_twist_offset.l", 1, {0, 0.16, 1.37}, 0),
                                           bone("arm_stretch.l", 0, {0, 0.29, 1.36}, 1), bone("forearm_stretch.l", 0, {0, 0.42, 1.35}, 1)};
    RigMap map;
    for (auto [src, tgt] : {std::pair{"root.x", "mPelvis"}, {"spine_02.x", "mChest"}, {"shoulder.l", "mCollarLeft"},
                            {"c_arm_twist_offset.l", ""}, {"arm_stretch.l", "mShoulderLeft"}, {"forearm_stretch.l", "mElbowLeft"}})
        map.bones.push_back({src, tgt, 100, "test", ""});
    const RigMapResult r = resolve_rig_map(skel(), bones, map);
    CHECK_NEAR(r.remap.binds.at(skel().find("mShoulderLeft")).pos.y, 0.16, 1e-9);
    CHECK_NEAR(r.remap.binds.at(skel().find("mElbowLeft")).pos.y, 0.42, 1e-9);
}

// A part made for SL (every bone SL's own name, such as a devkit's upper body alone) keeps its own size: made
// "SL-like" by its height, it was blown up to a whole avatar's and drawn far out of the view (design review #17).
TEST(rig_map_keeps_an_sl_rigged_part_at_its_size) {
    auto bone = [](const char* name, int parent, Vec3 at) { return SourceBone{name, parent, {Quat{}, at}, true, 1}; };
    const std::vector<SourceBone> upper = {bone("mPelvis", -1, {0, 0, 1.0}), bone("mTorso", 0, {0, 0, 1.08}),
                                           bone("mChest", 1, {0, 0, 1.25}), bone("mNeck", 2, {0, 0, 1.5})};
    CHECK(suggest_rig_map(skel(), upper, {}).height == 0);
    // Bones of its own (a game rig): made SL-like as before.
    const std::vector<SourceBone> own = {bone("Hips", -1, {0, 0, 1.0}), bone("Spine", 0, {0, 0, 1.2}),
                                         bone("Neck", 1, {0, 0, 1.5}), bone("Head", 2, {0, 0, 1.6})};
    CHECK(suggest_rig_map(skel(), own, {}).height == kSlAvatarHeight);
}
