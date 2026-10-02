// Map Rig on the humanoid rig families people bring (spec 08 RM-2): VRoid, Daz Genesis 8, Character Creator, 3ds Max
// Biped, MMD, Mixamo, Blender's anonymous Bone.NNN, Rigify (metarig and DEF), Unreal, and Unreal under a prefix no table
// knows. The rigs are built here (CC0): each family's bone names and hierarchy (names are facts) on one shared T-pose
// body, so every rig has the same right answer. Each bone carries what it is (a key), and the key says which SL joint it
// must land on: twist, carpal and second-segment bones fold, every limb keeps all its joints, and no joint is taken twice.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "fixtures.h"
#include "vats/rig_map.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

// The body: left side and centre, Z up, facing +X, metres.
const std::map<std::string, Vec3>& at() {
    static const std::map<std::string, Vec3> p = {
        {"root", {0, 0, 0}},          {"hips", {0, 0, 1.0}},         {"pelvis", {0, 0, 0.98}},     {"spine", {0, 0, 1.1}},
        {"spine2", {0, 0, 1.2}},      {"chest", {0, 0, 1.3}},        {"uchest", {0, 0, 1.4}},      {"neck", {0, 0, 1.55}},
        {"neck2", {0, 0, 1.6}},       {"head", {0, 0, 1.65}},        {"headtop", {0, 0, 1.85}},    {"eye", {0.08, 0.03, 1.72}},
        {"jaw", {0.03, 0, 1.62}},     {"clav", {0.02, 0.03, 1.48}},  {"uarm", {0, 0.18, 1.45}},    {"uarmtw", {0, 0.31, 1.45}},
        {"farm", {0, 0.45, 1.45}},    {"farmtw", {0, 0.57, 1.45}},   {"hand", {0, 0.70, 1.45}},    {"carp", {0.0, 0.74, 1.45}},
        {"thumb1", {0.03, 0.73, 1.43}}, {"thumb2", {0.05, 0.76, 1.43}}, {"thumb3", {0.06, 0.78, 1.43}}, {"thumb4", {0.07, 0.80, 1.43}},
        {"index1", {0.02, 0.80, 1.45}}, {"index2", {0.02, 0.84, 1.45}}, {"index3", {0.02, 0.87, 1.45}}, {"index4", {0.02, 0.89, 1.45}},
        {"middle1", {0, 0.81, 1.45}},  {"middle2", {0, 0.85, 1.45}},  {"middle3", {0, 0.88, 1.45}},  {"middle4", {0, 0.90, 1.45}},
        {"ring1", {-0.02, 0.80, 1.45}}, {"ring2", {-0.02, 0.84, 1.45}}, {"ring3", {-0.02, 0.87, 1.45}}, {"ring4", {-0.02, 0.89, 1.45}},
        {"pinky1", {-0.04, 0.79, 1.45}}, {"pinky2", {-0.04, 0.82, 1.45}}, {"pinky3", {-0.04, 0.84, 1.45}}, {"pinky4", {-0.04, 0.86, 1.45}},
        {"thigh", {0, 0.1, 0.95}},    {"thightw", {0, 0.1, 0.72}},   {"shin", {0.02, 0.1, 0.5}},   {"shintw", {0.01, 0.1, 0.3}},
        {"foot", {0, 0.1, 0.08}},     {"meta", {0.06, 0.1, 0.04}},   {"toe", {0.12, 0.1, 0.02}},   {"toeend", {0.18, 0.1, 0.02}},
        {"ik", {0, 0.1, 0.0}},          {"tail0", {-0.05, 0, 0.98}},   {"tail1", {-0.15, 0, 0.98}},  {"tail2", {-0.25, 0, 0.98}},
        {"tail3", {-0.35, 0, 0.98}},  {"tail4", {-0.45, 0, 0.98}},   {"tail5", {-0.55, 0, 0.98}},  {"tail6", {-0.65, 0, 0.98}},
        {"ear", {-0.02, 0.06, 1.8}},  {"lid", {0.08, 0.03, 1.72}},   {"tuft", {0.02, 0.1, 1.75}},  {"breast", {0.1, 0.09, 1.33}}};
    return p;
}

struct Row {
    std::string name, parent, key;
    double w = 1;
};

struct Rig {
    std::string what;
    std::vector<SourceBone> bones;
    std::vector<std::string> keys;
};

std::string with_side(std::string s, const std::string& side) {
    for (size_t i = s.find("{S}"); i != std::string::npos; i = s.find("{S}")) s.replace(i, 3, side);
    return s;
}

// Rows naming {S} come twice, left (as listed) and right (mirrored across the body); parents are put first.
Rig make(const char* what, const std::vector<Row>& rows, const std::string& l, const std::string& r) {
    std::vector<Row> out;
    std::vector<Vec3> pos;
    for (const Row& row : rows) {
        Vec3 p = at().at(row.key);
        if ((row.name + row.parent).find("{S}") == std::string::npos) {
            out.push_back(row), pos.push_back(p);
            continue;
        }
        for (int s : {1, -1})
            out.push_back({with_side(row.name, s > 0 ? l : r), with_side(row.parent, s > 0 ? l : r), row.key, row.w}),
                pos.push_back({p.x, p.y * s, p.z});
    }
    Rig rig{what, {}, {}};
    std::map<std::string, int> index;
    while (rig.bones.size() < out.size())
        for (size_t i = 0; i < out.size(); ++i)
            if (!index.count(out[i].name) && (out[i].parent.empty() || index.count(out[i].parent))) {
                index[out[i].name] = int(rig.bones.size());
                rig.bones.push_back({out[i].name, out[i].parent.empty() ? -1 : index[out[i].parent], {Quat{}, pos[i]}, out[i].w > 0, out[i].w});
                rig.keys.push_back(out[i].key);
            }
    return rig;
}

// Fingers named by fmt ({f} the finger, {k} the segment), three segments, and a weightless tip when tip > 0.
std::vector<Row> fingers(const std::string& fmt, const std::string& parent, const std::vector<std::pair<std::string, std::string>>& names,
                         int tip = 0, std::vector<std::string> segs = {"1", "2", "3"}) {
    std::vector<Row> rows;
    for (const auto& [key, name] : names) {
        auto bone = [&](const std::string& k) {
            std::string s = fmt;
            if (s.find("{f}") != std::string::npos) s.replace(s.find("{f}"), 3, name);
            s.replace(s.find("{k}"), 3, k);
            return s;
        };
        std::string prev = parent;
        for (size_t k = 0; k < segs.size(); ++k) rows.push_back({bone(segs[k]), prev, key + std::to_string(k + 1)}), prev = bone(segs[k]);
        if (tip) rows.push_back({bone(std::to_string(tip)), prev, key + "4", 0});
    }
    return rows;
}

const std::vector<std::pair<std::string, std::string>> kFive = {
    {"thumb", "Thumb"}, {"index", "Index"}, {"middle", "Middle"}, {"ring", "Ring"}, {"pinky", "Pinky"}};

std::vector<Row> operator+(std::vector<Row> a, const std::vector<Row>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

std::vector<Row> unreal_rows(const std::string& p) {
    return {{p + "root", "", "root", 0},
            {p + "pelvis", p + "root", "hips"},
            {p + "spine_01", p + "pelvis", "spine"},
            {p + "spine_02", p + "spine_01", "spine2"},
            {p + "spine_03", p + "spine_02", "chest"},
            {p + "neck_01", p + "spine_03", "neck"},
            {p + "head", p + "neck_01", "head"},
            {p + "clavicle_{S}", p + "spine_03", "clav"},
            {p + "upperarm_{S}", p + "clavicle_{S}", "uarm"},
            {p + "upperarm_twist_01_{S}", p + "upperarm_{S}", "uarmtw"},
            {p + "lowerarm_{S}", p + "upperarm_{S}", "farm"},
            {p + "lowerarm_twist_01_{S}", p + "lowerarm_{S}", "farmtw"},
            {p + "hand_{S}", p + "lowerarm_{S}", "hand"},
            {p + "thigh_{S}", p + "pelvis", "thigh"},
            {p + "thigh_twist_01_{S}", p + "thigh_{S}", "thightw"},
            {p + "calf_{S}", p + "thigh_{S}", "shin"},
            {p + "calf_twist_01_{S}", p + "calf_{S}", "shintw"},
            {p + "foot_{S}", p + "calf_{S}", "foot"},
            {p + "ball_{S}", p + "foot_{S}", "toe"},
            {p + "ik_foot_root", p + "root", "root", 0},
            {p + "ik_foot_{S}", p + "ik_foot_root", "ik", 0}};
}

std::vector<Rig> humanoids() {
    std::vector<Rig> rigs;
    rigs.push_back(make("VRoid", std::vector<Row>{
        {"Root", "", "root", 0}, {"J_Bip_C_Hips", "Root", "hips"}, {"J_Bip_C_Spine", "J_Bip_C_Hips", "spine"},
        {"J_Bip_C_Chest", "J_Bip_C_Spine", "chest"}, {"J_Bip_C_UpperChest", "J_Bip_C_Chest", "uchest"},
        {"J_Bip_C_Neck", "J_Bip_C_UpperChest", "neck"}, {"J_Bip_C_Head", "J_Bip_C_Neck", "head"},
        {"J_Adj_{S}_FaceEye", "J_Bip_C_Head", "eye"}, {"J_Bip_{S}_Shoulder", "J_Bip_C_UpperChest", "clav"},
        {"J_Bip_{S}_UpperArm", "J_Bip_{S}_Shoulder", "uarm"}, {"J_Bip_{S}_LowerArm", "J_Bip_{S}_UpperArm", "farm"},
        {"J_Bip_{S}_Hand", "J_Bip_{S}_LowerArm", "hand"}, {"J_Bip_{S}_UpperLeg", "J_Bip_C_Hips", "thigh"},
        {"J_Bip_{S}_LowerLeg", "J_Bip_{S}_UpperLeg", "shin"}, {"J_Bip_{S}_Foot", "J_Bip_{S}_LowerLeg", "foot"},
        {"J_Bip_{S}_ToeBase", "J_Bip_{S}_Foot", "toe"}} +
        fingers("J_Bip_{S}_{f}{k}", "J_Bip_{S}_Hand", {{"thumb", "Thumb"}, {"index", "Index"}, {"middle", "Middle"}, {"ring", "Ring"}, {"pinky", "Little"}}),
        "L", "R"));

    // Daz Genesis 8: Bend and Twist bones in the chain, carpals between the hand and four fingers, the legs off a
    // pelvis beside the spine.
    std::vector<Row> daz = {
        {"hip", "", "hips"}, {"pelvis", "hip", "pelvis"}, {"{S}ThighBend", "pelvis", "thigh"}, {"{S}ThighTwist", "{S}ThighBend", "thightw"},
        {"{S}Shin", "{S}ThighTwist", "shin"}, {"{S}Foot", "{S}Shin", "foot"}, {"{S}Metatarsals", "{S}Foot", "meta"},
        {"{S}Toe", "{S}Metatarsals", "toe"}, {"abdomenLower", "hip", "spine"}, {"abdomenUpper", "abdomenLower", "spine2"},
        {"chestLower", "abdomenUpper", "chest"}, {"chestUpper", "chestLower", "uchest"}, {"neckLower", "chestUpper", "neck"},
        {"neckUpper", "neckLower", "neck2"}, {"head", "neckUpper", "head"}, {"{S}Eye", "head", "eye"}, {"lowerJaw", "head", "jaw"},
        {"{S}Collar", "chestUpper", "clav"}, {"{S}ShldrBend", "{S}Collar", "uarm"}, {"{S}ShldrTwist", "{S}ShldrBend", "uarmtw"},
        {"{S}ForearmBend", "{S}ShldrTwist", "farm"}, {"{S}ForearmTwist", "{S}ForearmBend", "farmtw"},
        {"{S}Hand", "{S}ForearmTwist", "hand"}, {"{S}Thumb1", "{S}Hand", "thumb1"}, {"{S}Thumb2", "{S}Thumb1", "thumb2"},
        {"{S}Thumb3", "{S}Thumb2", "thumb3"}};
    const char* daz_fingers[][2] = {{"index", "Index"}, {"middle", "Mid"}, {"ring", "Ring"}, {"pinky", "Pinky"}};
    for (int i = 0; i < 4; ++i) {
        const std::string carpal = "{S}Carpal" + std::to_string(i + 1), f = std::string("{S}") + daz_fingers[i][1], k = daz_fingers[i][0];
        daz = daz + std::vector<Row>{{carpal, "{S}Hand", "carp"}, {f + "1", carpal, k + "1"}, {f + "2", f + "1", k + "2"}, {f + "3", f + "2", k + "3"}};
    }
    rigs.push_back(make("Daz Genesis 8", daz, "l", "r"));

    // Character Creator 3/4: twist bones beside the limb bones, not in their chain; the face under a weightless bone.
    rigs.push_back(make("Character Creator", std::vector<Row>{
        {"CC_Base_BoneRoot", "", "root", 0}, {"CC_Base_Hip", "CC_Base_BoneRoot", "hips"}, {"CC_Base_Pelvis", "CC_Base_Hip", "pelvis"},
        {"CC_Base_{S}_Thigh", "CC_Base_Pelvis", "thigh"}, {"CC_Base_{S}_ThighTwist01", "CC_Base_{S}_Thigh", "thightw"},
        {"CC_Base_{S}_Calf", "CC_Base_{S}_Thigh", "shin"}, {"CC_Base_{S}_CalfTwist01", "CC_Base_{S}_Calf", "shintw"},
        {"CC_Base_{S}_Foot", "CC_Base_{S}_Calf", "foot"}, {"CC_Base_{S}_ToeBase", "CC_Base_{S}_Foot", "toe"},
        {"CC_Base_Waist", "CC_Base_Hip", "spine"}, {"CC_Base_Spine01", "CC_Base_Waist", "spine2"},
        {"CC_Base_Spine02", "CC_Base_Spine01", "chest"}, {"CC_Base_NeckTwist01", "CC_Base_Spine02", "neck"},
        {"CC_Base_NeckTwist02", "CC_Base_NeckTwist01", "neck2"}, {"CC_Base_Head", "CC_Base_NeckTwist02", "head"},
        {"CC_Base_FacialBone", "CC_Base_Head", "head", 0}, {"CC_Base_JawRoot", "CC_Base_FacialBone", "jaw"},
        {"CC_Base_{S}_Eye", "CC_Base_FacialBone", "eye"}, {"CC_Base_{S}_Clavicle", "CC_Base_Spine02", "clav"},
        {"CC_Base_{S}_Upperarm", "CC_Base_{S}_Clavicle", "uarm"}, {"CC_Base_{S}_UpperarmTwist01", "CC_Base_{S}_Upperarm", "uarmtw"},
        {"CC_Base_{S}_Forearm", "CC_Base_{S}_Upperarm", "farm"}, {"CC_Base_{S}_ForearmTwist01", "CC_Base_{S}_Forearm", "farmtw"},
        {"CC_Base_{S}_Hand", "CC_Base_{S}_Forearm", "hand"}} +
        fingers("CC_Base_{S}_{f}{k}", "CC_Base_{S}_Hand", {{"thumb", "Thumb"}, {"index", "Index"}, {"middle", "Mid"}, {"ring", "Ring"}, {"pinky", "Pinky"}}),
        "L", "R"));

    // 3ds Max Biped: the legs off the first spine bone, the clavicles off the neck, Finger0 the thumb, Nub tips.
    std::vector<Row> bip = {
        {"Bip01", "", "root", 0}, {"Bip01 Pelvis", "Bip01", "hips", 0}, {"Bip01 Spine", "Bip01 Pelvis", "spine"},
        {"Bip01 Spine1", "Bip01 Spine", "chest"}, {"Bip01 Spine2", "Bip01 Spine1", "uchest"}, {"Bip01 Neck", "Bip01 Spine2", "neck"},
        {"Bip01 Head", "Bip01 Neck", "head"}, {"Bip01 HeadNub", "Bip01 Head", "headtop", 0},
        {"Bip01 {S} Clavicle", "Bip01 Neck", "clav"}, {"Bip01 {S} UpperArm", "Bip01 {S} Clavicle", "uarm"},
        {"Bip01 {S} Forearm", "Bip01 {S} UpperArm", "farm"}, {"Bip01 {S} Hand", "Bip01 {S} Forearm", "hand"},
        {"Bip01 {S} Thigh", "Bip01 Spine", "thigh"}, {"Bip01 {S} Calf", "Bip01 {S} Thigh", "shin"},
        {"Bip01 {S} Foot", "Bip01 {S} Calf", "foot"}, {"Bip01 {S} Toe0", "Bip01 {S} Foot", "toe"},
        {"Bip01 {S} Toe0Nub", "Bip01 {S} Toe0", "toeend", 0}};
    const char* keys5[] = {"thumb", "index", "middle", "ring", "pinky"};
    for (int i = 0; i < 5; ++i) {
        const std::string b = "Bip01 {S} Finger" + std::to_string(i), k = keys5[i];
        bip = bip + std::vector<Row>{{b, "Bip01 {S} Hand", k + "1"}, {b + "1", b, k + "2"}, {b + "2", b + "1", k + "3"}, {b + "Nub", b + "2", k + "4", 0}};
    }
    rigs.push_back(make("3ds Max Biped", bip, "L", "R"));

    // MMD: Japanese names, sides as 左/右, full-width digits, the legs off a lower body beside the upper body, twist
    // bones in the arm chain, IK bones off the root.
    std::vector<Row> mmd = {
        {"全ての親", "", "root", 0}, {"センター", "全ての親", "hips", 0}, {"下半身", "センター", "pelvis"}, {"上半身", "センター", "spine"},
        {"上半身2", "上半身", "chest"}, {"首", "上半身2", "neck"}, {"頭", "首", "head"}, {"{S}目", "頭", "eye"},
        {"{S}肩", "上半身2", "clav"}, {"{S}腕", "{S}肩", "uarm"}, {"{S}腕捩", "{S}腕", "uarmtw"}, {"{S}ひじ", "{S}腕捩", "farm"},
        {"{S}手捩", "{S}ひじ", "farmtw"}, {"{S}手首", "{S}手捩", "hand"}, {"{S}足", "下半身", "thigh"}, {"{S}ひざ", "{S}足", "shin"},
        {"{S}足首", "{S}ひざ", "foot"}, {"{S}つま先", "{S}足首", "toe", 0}, {"{S}足ＩＫ", "全ての親", "ik", 0}};
    const char* mmd_fingers[][2] = {{"thumb", "親指"}, {"index", "人指"}, {"middle", "中指"}, {"ring", "薬指"}, {"pinky", "小指"}};
    for (auto& [k, f] : mmd_fingers)
        mmd = mmd + fingers(std::string("{S}") + f + "{k}", "{S}手首", {{k, ""}}, 0,
                            std::string(k) == "thumb" ? std::vector<std::string>{"０", "１", "２"} : std::vector<std::string>{"１", "２", "３"});
    rigs.push_back(make("MMD", mmd, "左", "右"));

    // Mixamo, with the namespace written "mixamorig_" (no table name matches) and as "mixamorig:".
    for (const std::string ns : {"mixamorig_", "mixamorig:"}) {
        std::vector<Row> mx = {
            {"Hips", "", "hips"}, {"Spine", "Hips", "spine"}, {"Spine1", "Spine", "chest"}, {"Spine2", "Spine1", "uchest"},
            {"Neck", "Spine2", "neck"}, {"Head", "Neck", "head"}, {"HeadTop_End", "Head", "headtop", 0},
            {"{S}Shoulder", "Spine2", "clav"}, {"{S}Arm", "{S}Shoulder", "uarm"}, {"{S}ForeArm", "{S}Arm", "farm"},
            {"{S}Hand", "{S}ForeArm", "hand"}, {"{S}UpLeg", "Hips", "thigh"}, {"{S}Leg", "{S}UpLeg", "shin"},
            {"{S}Foot", "{S}Leg", "foot"}, {"{S}ToeBase", "{S}Foot", "toe"}, {"{S}Toe_End", "{S}ToeBase", "toeend", 0}};
        mx = mx + fingers("{S}Hand{f}{k}", "{S}Hand", kFive, 4);
        for (Row& r : mx) r.name = ns + r.name, r.parent = r.parent.empty() ? "" : ns + r.parent;
        rigs.push_back(make(ns == "mixamorig_" ? "Mixamo (mixamorig_)" : "Mixamo", mx, "Left", "Right"));
    }

    // Blender's default names: nothing but positions to go by.
    rigs.push_back(make("Blender Bone.NNN", std::vector<Row>{
        {"Bone", "", "hips"}, {"Bone.001", "Bone", "spine"}, {"Bone.002", "Bone.001", "chest"}, {"Bone.003", "Bone.002", "neck"},
        {"Bone.004", "Bone.003", "head"}, {"Bone.005{S}", "Bone.002", "clav"}, {"Bone.006{S}", "Bone.005{S}", "uarm"},
        {"Bone.007{S}", "Bone.006{S}", "farm"}, {"Bone.008{S}", "Bone.007{S}", "hand"}, {"Bone.009{S}", "Bone", "thigh"},
        {"Bone.010{S}", "Bone.009{S}", "shin"}, {"Bone.011{S}", "Bone.010{S}", "foot"}, {"Bone.012{S}", "Bone.011{S}", "toe"}},
        "a", "b"));

    // A Rigify metarig's bones: .00N segments, palms between the hand and four fingers, pelvis and breast bones.
    std::vector<Row> rg = {
        {"spine", "", "hips"}, {"spine.001", "spine", "spine"}, {"spine.002", "spine.001", "chest"}, {"spine.003", "spine.002", "uchest"},
        {"spine.004", "spine.003", "neck"}, {"spine.005", "spine.004", "neck2"}, {"spine.006", "spine.005", "head"},
        {"shoulder.{S}", "spine.003", "clav"}, {"upper_arm.{S}", "shoulder.{S}", "uarm"}, {"forearm.{S}", "upper_arm.{S}", "farm"},
        {"hand.{S}", "forearm.{S}", "hand"}, {"thigh.{S}", "spine", "thigh"}, {"shin.{S}", "thigh.{S}", "shin"},
        {"foot.{S}", "shin.{S}", "foot"}, {"toe.{S}", "foot.{S}", "toe"}, {"pelvis.{S}", "spine", "pelvis"},
        {"breast.{S}", "spine.003", "breast"}};
    for (int k = 1; k <= 3; ++k)
        rg.push_back({"thumb.0" + std::to_string(k) + ".{S}", k == 1 ? "hand.{S}" : "thumb.0" + std::to_string(k - 1) + ".{S}", "thumb" + std::to_string(k)});
    const char* rg_fingers[][2] = {{"index", "f_index"}, {"middle", "f_middle"}, {"ring", "f_ring"}, {"pinky", "f_pinky"}};
    for (int i = 0; i < 4; ++i) {
        const std::string palm = "palm.0" + std::to_string(i + 1) + ".{S}", f = rg_fingers[i][1], k = rg_fingers[i][0];
        rg.push_back({palm, "hand.{S}", "carp"});
        for (int s = 1; s <= 3; ++s)
            rg.push_back({f + ".0" + std::to_string(s) + ".{S}", s == 1 ? palm : f + ".0" + std::to_string(s - 1) + ".{S}", k + std::to_string(s)});
    }
    rigs.push_back(make("Rigify metarig", rg, "L", "R"));

    // Rigify's deform bones: every limb bone in two segments, the second named .001.
    rigs.push_back(make("Rigify DEF", std::vector<Row>{
        {"root", "", "root", 0}, {"DEF-spine", "root", "hips"}, {"DEF-spine.001", "DEF-spine", "spine"},
        {"DEF-spine.002", "DEF-spine.001", "spine2"}, {"DEF-spine.003", "DEF-spine.002", "chest"},
        {"DEF-spine.004", "DEF-spine.003", "neck"}, {"DEF-spine.005", "DEF-spine.004", "neck2"}, {"DEF-spine.006", "DEF-spine.005", "head"},
        {"DEF-shoulder.{S}", "DEF-spine.003", "clav"}, {"DEF-upper_arm.{S}", "DEF-shoulder.{S}", "uarm"},
        {"DEF-upper_arm.{S}.001", "DEF-upper_arm.{S}", "uarmtw"}, {"DEF-forearm.{S}", "DEF-upper_arm.{S}.001", "farm"},
        {"DEF-forearm.{S}.001", "DEF-forearm.{S}", "farmtw"}, {"DEF-hand.{S}", "DEF-forearm.{S}.001", "hand"},
        {"DEF-thigh.{S}", "DEF-spine", "thigh"}, {"DEF-thigh.{S}.001", "DEF-thigh.{S}", "thightw"},
        {"DEF-shin.{S}", "DEF-thigh.{S}.001", "shin"}, {"DEF-shin.{S}.001", "DEF-shin.{S}", "shintw"},
        {"DEF-foot.{S}", "DEF-shin.{S}.001", "foot"}, {"DEF-toe.{S}", "DEF-foot.{S}", "toe"}},
        "L", "R"));

    // Unreal's mannequin, twist bones beside the limb bones and IK bones off the root; then the same rig under a
    // prefix (a game's rip).
    rigs.push_back(make("Unreal", unreal_rows(""), "l", "r"));
    rigs.push_back(make("Unreal, prefixed", unreal_rows("G_"), "l", "r"));
    return rigs;
}

// The SL joints a bone may land on, by what it is: "" folds into its parent's joint (a twist, a carpal, a second
// segment) or is dropped (no weights). chest_bone: the bone the arms hang from (the neck's parent when the clavicles
// hang off the neck, as a Biped's do), where mChest belongs.
std::set<std::string> allowed(const Rig& rig, int i, int chest_bone) {
    const std::string& k = rig.keys[size_t(i)];
    const SourceBone& b = rig.bones[size_t(i)];
    const double y = b.bind.pos.y;
    const std::string side = y > 1e-6 ? "Left" : y < -1e-6 ? "Right" : "";
    std::string want;
    static const std::map<std::string, std::string> limb = {
        {"eye", "mEye"},   {"clav", "mCollar"}, {"uarm", "mShoulder"}, {"farm", "mElbow"}, {"hand", "mWrist"},
        {"thigh", "mHip"}, {"shin", "mKnee"},   {"foot", "mAnkle"},    {"toe", "mFoot"},   {"toeend", "mToe"}};
    static const std::map<std::string, std::string> centre = {
        {"hips", "mPelvis"}, {"pelvis", "mPelvis"}, {"spine", "mTorso"}, {"neck", "mNeck"},
        {"head", "mHead"},   {"headtop", "mSkull"}, {"jaw", "mFaceJaw"}};
    if (auto it = limb.find(k); it != limb.end() && !side.empty()) want = it->second + side;
    else if (auto c = centre.find(k); c != centre.end() && side.empty()) want = c->second;
    else if ((k == "chest" || k == "uchest") && i == chest_bone) want = "mChest";
    else if (k == "breast" && !side.empty()) want = side == "Left" ? "LEFT_PEC" : "RIGHT_PEC";  // a soft-body helper
    else if (k.size() > 1 && std::isdigit(static_cast<unsigned char>(k.back())) && k.back() != '4' && k.rfind("spine", 0) != 0 &&
             k.rfind("neck", 0) != 0) {
        std::string f = k.substr(0, k.size() - 1);
        f[0] = char(std::toupper(static_cast<unsigned char>(f[0])));
        want = "mHand" + f + k.back() + side;
    }
    std::set<std::string> ok{want};
    if (b.weight <= 0 || k == "pelvis") ok.insert("");  // a weightless bone or a second pelvis may fold
    if (k == "pelvis" && !side.empty()) ok.insert("mPelvis");
    if (k == "carp") ok.insert("mWrist" + side);
    if (k.rfind("tail", 0) == 0) ok = {k.back() < '6' ? "mTail" + std::to_string(k.back() - '0' + 1) : ""};
    if (k == "ear") ok = {"mFaceEar1" + side};
    if (k == "lid" || k == "tuft") ok = {""};  // eyelids and hair fold into the head
    return ok;
}

struct Score {
    int wrong = 0;
    std::string list;
};

Score score(const Rig& rig, const RigMap& m) {
    Score s;
    int chest = -1;
    for (size_t i = 0; i < rig.bones.size(); ++i)
        if (rig.keys[i] == "clav") {
            chest = rig.bones[i].parent;
            if (rig.keys[size_t(chest)] == "neck") chest = rig.bones[size_t(chest)].parent;
        }
    bool pelvis = false;
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        const RigMapBone* b = m.find(rig.bones[i].name);
        const std::string got = b ? b->target : "?";
        pelvis |= got == "mPelvis";
        const std::set<std::string> ok = allowed(rig, int(i), chest);
        if (!ok.count(got)) {
            ++s.wrong;
            s.list += "    " + rig.bones[i].name + " -> " + (got.empty() ? "(fold)" : got) + ", want " +
                      (ok.begin()->empty() && ok.size() == 1 ? "(fold)" : *ok.rbegin()) + "\n";
        }
    }
    if (!pelvis) ++s.wrong, s.list += "    no mPelvis\n";
    if (m.turn != 0) ++s.wrong, s.list += "    turned " + std::to_string(m.turn * 90) + " degrees\n";
    return s;
}

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

}  // namespace

TEST(rig_map_bones_sit_only_on_joints_placed_by_other_rules) {
    // A beard hanging forward from the head: its second bone sits right in front of the jaw, so it joins mFaceJaw; the
    // first is near that bone only, not the jaw, and folds into the head. It must not sit on a joint its own descendant
    // sat on.
    Rig rig = humanoids()[1];  // Daz, which has a jaw
    const int head = int(std::find_if(rig.bones.begin(), rig.bones.end(), [](const SourceBone& b) { return b.name == "head"; }) - rig.bones.begin());
    rig.bones.push_back({"Beard1", head, {Quat{}, {0.10, 0, 1.62}}, true, 1});
    rig.bones.push_back({"Beard2", int(rig.bones.size()) - 1, {Quat{}, {0.06, 0, 1.62}}, true, 1});
    // A scarf off the neck passing the left collar: it hangs from the neck, so it does not join the collar.
    const int neck = int(std::find_if(rig.bones.begin(), rig.bones.end(), [](const SourceBone& b) { return b.name == "neckLower"; }) - rig.bones.begin());
    rig.bones.push_back({"Scarf", neck, {Quat{}, {0.03, 0.05, 1.48}}, true, 1});
    const RigMap m = suggest_rig_map(skel(), rig.bones, {});
    CHECK(m.find("Beard2")->target == "mFaceJaw");
    CHECK(m.find("Beard1")->target.empty());
    CHECK(m.find("Scarf")->target.empty());
    CHECK(m.find("head")->target == "mHead");
}

TEST(rig_map_check_flags_bones_out_of_place) {
    const std::vector<Rig> rigs = humanoids();
    // A hand on the other side's wrist: not below that side's forearm.
    const Rig& vroid = rigs[0];
    RigMap m = suggest_rig_map(skel(), vroid.bones, {});
    for (const std::string& p : check_rig_map(skel(), vroid.bones, m)) CHECK(p.empty());
    m.find("J_Bip_L_Hand")->target = "mWristRight";
    std::vector<std::string> p = check_rig_map(skel(), vroid.bones, m);
    const size_t hand = size_t(std::find_if(vroid.bones.begin(), vroid.bones.end(), [](const SourceBone& b) { return b.name == "J_Bip_L_Hand"; }) - vroid.bones.begin());
    CHECK(p[hand].find("not below J_Bip_R_LowerArm") != std::string::npos);
    // A thigh's second segment on the knee, which the shin is on (what Rigify's deform bones once got): the two do not sit
    // together, so it is not a merge, and both show.
    const Rig& def = *std::find_if(rigs.begin(), rigs.end(), [](const Rig& r) { return r.what == "Rigify DEF"; });
    m = suggest_rig_map(skel(), def.bones, {});
    m.find("DEF-thigh.L.001")->target = "mKneeLeft";
    p = check_rig_map(skel(), def.bones, m);
    size_t flagged = 0;
    for (size_t i = 0; i < p.size(); ++i)
        if (!p[i].empty()) {
            ++flagged;
            CHECK((def.bones[i].name == "DEF-thigh.L.001" || def.bones[i].name == "DEF-shin.L") && p[i].find("shares mKneeLeft") != std::string::npos);
        }
    CHECK(flagged == 2);
    // Palms side by side on one wrist (the mech's) merge, and are not flagged.
    std::vector<SourceBone> palms = vroid.bones;
    const int wrist = int(hand);
    for (const char* n : {"PalmA", "PalmB"}) palms.push_back({n, wrist, {Quat{}, vroid.bones[hand].bind.pos}, true, 1});
    m = suggest_rig_map(skel(), palms, {});
    m.find("PalmA")->target = m.find("PalmB")->target = "mWristLeft";
    for (const std::string& q : check_rig_map(skel(), palms, m)) CHECK(q.empty());
}

TEST(rig_map_flattened_auto_rig_pro) {
    // An Auto-Rig Pro character exported with its deform bones only: every bone hangs off the armature root, so only
    // the names (its table) and positions say what is what. A long tail would make it look like it lies down.
    std::vector<Row> rows = {
        {"rig", "", "root", 0}, {"root.x", "rig", "hips"}, {"spine_01.x", "rig", "spine"}, {"spine_02.x", "rig", "chest"},
        {"neck.x", "rig", "neck"}, {"head.x", "rig", "head"}, {"c_eye_offset.{S}", "head.x", "eye", 0}, {"c_eye.{S}", "c_eye_offset.{S}", "eye"},
        {"{S}_eyelid", "head.x", "lid"}, {"{S}_ear", "head.x", "ear"}, {"{S}_Head_Tuft", "head.x", "tuft"},
        {"shoulder.{S}", "spine_02.x", "clav"}, {"arm_stretch.{S}", "rig", "uarm"}, {"forearm_stretch.{S}", "rig", "farm"},
        {"forearm_twist.{S}", "forearm_stretch.{S}", "farmtw", 0}, {"hand.{S}", "rig", "hand"},
        {"thigh_stretch.{S}", "rig", "thigh"}, {"leg_stretch.{S}", "rig", "shin"}, {"foot.{S}", "root.x", "foot"},
        {"toes_01.{S}", "foot.{S}", "toe"}, {"thigh_twist.{S}", "root.x", "thightw", 0}};
    for (int k = 0; k <= 6; ++k)
        rows.push_back({"c_tail_0" + std::to_string(k) + ".x", k ? "c_tail_0" + std::to_string(k - 1) + ".x" : "root.x", "tail" + std::to_string(k)});
    rows = rows + fingers("{f}Finger{k}_{S}", "hand.{S}", {{"index", "Index"}, {"middle", "Middle"}, {"ring", "Ring"}});
    Rig rig = make("Auto-Rig Pro, flattened", rows, "l", "r");
    for (SourceBone& b : rig.bones)  // ARP's own sides are .l and .r; the character's own bones here say Left_ and Right_
        for (const auto& [from, to] : {std::pair<std::string, std::string>{"l_", "Left_"}, {"r_", "Right_"}})
            if (b.name.rfind(from, 0) == 0) b.name.replace(0, from.size(), to);
    const RigMap m = suggest_rig_map(skel(), rig.bones, tables());
    const Score s = score(rig, m);
    if (s.wrong) check::fail(__FILE__, __LINE__, rig.what + ": " + std::to_string(s.wrong) + " bones wrong\n" + s.list);
    CHECK(!m.along_ground && m.find("rig")->target.empty());
}

TEST(rig_map_family_tables) {
    // Each family with a table of its own is recognised by it; a Biped under any prefix too ("* L Thigh").
    const std::map<std::string, std::string> want = {{"Daz Genesis 8", "Daz Genesis 8"}, {"Character Creator", "Character Creator"},
                                                     {"3ds Max Biped", "3ds Max Biped"}, {"MMD", "MMD"}};
    std::vector<Rig> rigs = humanoids();
    Rig renamed = *std::find_if(rigs.begin(), rigs.end(), [](const Rig& r) { return r.what == "3ds Max Biped"; });
    for (SourceBone& b : renamed.bones) b.name.replace(0, 5, "Hero_Rig");  // "Bip01 L Thigh" -> "Hero_Rig L Thigh"
    renamed.what = "3ds Max Biped";
    rigs.push_back(renamed);
    for (const Rig& rig : rigs) {
        const auto it = want.find(rig.what);
        if (it == want.end()) continue;
        const RigMap m = suggest_rig_map(skel(), rig.bones, tables());
        CHECK(!m.notes.empty() && m.notes[0].find("The names are " + it->second) == 0);
        CHECK(score(rig, m).wrong == 0);
    }
}

TEST(rig_map_table_choice_follows_the_hierarchy) {
    // Two tables name the same bones; one has the knees and hips the wrong way round. The names alone tie, and the one
    // whose mapping the armature nests wins, whichever comes first.
    const Rig vroid = humanoids()[0];
    RigTable good{"Good", "", {}}, bad{"Bad", "", {}};
    const std::pair<const char*, const char*> names[] = {
        {"mPelvis", "J_Bip_C_Hips"}, {"mTorso", "J_Bip_C_Spine"}, {"mChest", "J_Bip_C_UpperChest"}, {"mNeck", "J_Bip_C_Neck"},
        {"mHead", "J_Bip_C_Head"}, {"mShoulderLeft", "J_Bip_L_UpperArm"}, {"mElbowLeft", "J_Bip_L_LowerArm"},
        {"mShoulderRight", "J_Bip_R_UpperArm"}, {"mElbowRight", "J_Bip_R_LowerArm"}, {"mHipLeft", "J_Bip_L_UpperLeg"},
        {"mKneeLeft", "J_Bip_L_LowerLeg"}, {"mHipRight", "J_Bip_R_UpperLeg"}, {"mKneeRight", "J_Bip_R_LowerLeg"}};
    for (auto& [sl, n] : names) good.bones[sl] = bad.bones[sl] = {n};
    std::swap(bad.bones["mHipLeft"], bad.bones["mKneeLeft"]);
    std::swap(bad.bones["mHipRight"], bad.bones["mKneeRight"]);
    for (const std::vector<RigTable>& order : {std::vector<RigTable>{bad, good}, std::vector<RigTable>{good, bad}}) {
        const RigMap m = suggest_rig_map(skel(), vroid.bones, order);
        CHECK(!m.notes.empty() && m.notes[0].find("The names are Good's") == 0);
        CHECK(m.find("J_Bip_L_UpperLeg")->target == "mHipLeft");
    }
}

TEST(rig_map_humanoid_families) {
    for (const Rig& rig : humanoids())
        for (bool with_tables : {true, false}) {
            // The tables help but must not be needed for families whose names say enough; Bone.NNN and the prefixed
            // Unreal rig have no table either way.
            const RigMap m = suggest_rig_map(skel(), rig.bones, with_tables ? tables() : std::vector<RigTable>{});
            Score s = score(rig, m);
            for (const std::string& p : check_rig_map(skel(), rig.bones, m))
                if (!p.empty()) ++s.wrong, s.list += "    flagged: " + p + "\n";
            if (s.wrong)
                check::fail(__FILE__, __LINE__, rig.what + (with_tables ? "" : " (no tables)") + ": " + std::to_string(s.wrong) +
                                                    " of " + std::to_string(rig.bones.size()) + " bones wrong\n" + s.list);
        }
}

TEST(rig_map_reads_names_as_the_viewer_does) {
    // A game's Head, Neck, Chest, Pelvis and L_Hand are no SL names: the viewer matches joint names exactly, so the rig
    // goes to the mapping window instead of loading on fitted-mesh volumes and attachment points.
    DaeReport game, sl;
    for (const char* n : {"Pelvis", "Spine", "Chest", "Neck", "Head", "L_Hand", "R_Hand"}) game.bones.push_back({n, -1, {}, true, 1});
    CHECK(rig_needs_mapping(skel(), game));
    // SL's own names, LL's aliases and volumes, also after an exporter's prefix, are SL-named.
    for (const char* n : {"mPelvis", "Armature_mTorso", "rig:mChest", "neck", "head", "lThigh", "BELLY"}) sl.bones.push_back({n, -1, {}, true, 1});
    CHECK(!rig_needs_mapping(skel(), sl));
    // In a suggestion, bones with SL's names or LL's aliases are certain (Daz's hip, head, lShin).
    const Rig daz = humanoids()[1];
    const RigMap m = suggest_rig_map(skel(), daz.bones, {});
    CHECK(m.find("hip")->target == "mPelvis" && m.find("hip")->confidence == 100);
    CHECK(m.find("lShin")->target == "mKneeLeft" && m.find("lShin")->confidence == 100);
    CHECK(m.find("abdomenLower")->confidence < 100);
}
