// Spare chains (spec 08 RM-8): a part SL has no joint for, put on a Bento chain the body leaves free. A CC0 sash built
// in code (six bones in a strip off the hips of tools/mech_rig.h's renamed mech) rides the right wing's four joints.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>

#include "../tools/mech_rig.h"
#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/dae.h"
#include "vats/dynamics.h"
#include "vats/edit.h"
#include "vats/fbx.h"
#include "vats/pose_ops.h"
#include "vats/pose_tools.h"
#include "vats/rig.h"
#include "vats/rig_export.h"
#include "vats/rig_map.h"
#include "vats/suggest_limits.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

int node(const char* name) { return skel().find(name); }

// The mech with a six-bone sash on the tail's nodes: a straight strip hanging forward and down from the hips, its
// bones all turned alike so that each box's end meets the next box's start corner to corner.
mech::Body sash_body() {
    mech::Body body = mech::build(skel());
    const Vec3 top{0.14, -0.10, 1.02}, step = Vec3{0.25, 0, -1}.normalized() * 0.08;
    for (int k = 0; k < 6; ++k) {
        const Vec3 head = top + step * double(k), tail = head + step;
        body.bones.push_back({node(("mTail" + std::to_string(k + 1)).c_str()), head, tail, mech::axes_rolled(top, top + step, 0), 0.03, 2});
    }
    return body;
}

std::string sash_name(int n) {
    const std::string sl = skel()[n].name;
    if (sl.rfind("mTail", 0) == 0) return "Sash.00" + sl.substr(5);
    static const std::map<std::string, std::string> names = {{"mPelvis", "Hips"}, {"mTorso", "Spine"}, {"mChest", "Chest"},
                                                             {"mNeck", "Neck"},   {"mHead", "Head"}};
    const auto it = names.find(sl);
    return it != names.end() ? it->second : "b_" + sl;  // the rest under names no table or keyword knows
}

// The sash mech as a foreign file, Y up and in centimetres, facing -Y.
std::string sash_dae(const mech::Body& body) {
    mech::Foreign f{sash_name, 100, {}};
    return mech::dae(skel(), body, -1, true, &f);
}

std::string temp_dir(const char* name) {
    const fs::path d = fs::temp_directory_path() / (std::string("vats_spare_") + name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d.string();
}

int bone_index(const std::vector<SourceBone>& bones, const std::string& name) {
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].name == name) return int(i);
    return -1;
}

struct SashFile {
    std::string dir, path;
    std::vector<SourceBone> bones;
    RigMap map;
};

// Written to a file, read as it is, suggested, the sash put on the right wing, at the file's own size.
SashFile sash_on_wing(const char* dir_name) {
    SashFile s;
    s.dir = temp_dir(dir_name);
    s.path = s.dir + "/sash.dae";
    std::ofstream(s.path) << sash_dae(sash_body());
    DaeModel plain;
    DaeReport r;
    std::string err;
    CHECK(load_mesh_file_as_is(s.path, skel(), plain, r, err));
    s.bones = r.bones;
    s.map = suggest_rig_map(skel(), s.bones, {});
    s.map.height = 0;
    const std::vector<int> chain = spare_chain_from(s.bones, bone_index(s.bones, "Sash.001"));
    CHECK(chain.size() == 6);
    use_spare_chain(s.map, s.bones, chain, "WingRight");
    return s;
}

double weight_of(const DaeModel& m, size_t v, int joint) {
    double w = 0;
    for (size_t k = 0; k < 4; ++k) w += m.joints[v * 4 + k] == joint ? m.weights[v * 4 + k] : 0;
    return w;
}

}  // namespace

TEST(spare_chain_spreads_six_bones_over_four_joints_and_bends_smoothly) {
    SashFile s = sash_on_wing("spread");
    CHECK(s.map.spare_labels["WingRight"] == "sash");
    CHECK(s.map.find("Sash.001")->target == "mWing1Right" && s.map.find("Sash.006")->target == "mWing4Right");
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_rig_mapped(s.path, skel(), s.map, m, r, err, &s.bones));
    const std::vector<int> wing = {node("mWing1Right"), node("mWing2Right"), node("mWing3Right"), node("mWing4Right")};
    // Every vertex's weights still sum to 1; the sash's are all on the wing, and many are shared by two of its joints.
    size_t sash = 0, shared = 0;
    for (size_t v = 0; v < size_t(m.vertex_count()); ++v) {
        double total = 0, on_wing = 0;
        int joints = 0;
        for (size_t k = 0; k < 4; ++k) total += m.weights[v * 4 + k];
        for (int j : wing) on_wing += weight_of(m, v, j), joints += weight_of(m, v, j) > 1e-6;
        CHECK_NEAR(total, 1, 1e-5);
        if (on_wing <= 0) continue;
        CHECK_NEAR(on_wing, 1, 1e-5);
        ++sash, shared += joints == 2;
    }
    CHECK(sash == 6 * 24);
    CHECK(shared > sash / 3);
    CHECK(m.labels.at("mWing3Right") == "sash");
    // Bent at its second and third joints, the strip stays whole: corners where one box meets the next move together
    // (a plain fold puts each box rigidly on one joint, and they part at every joint the bend crosses).
    Shape shape;
    shape_from_binds(skel(), {&m}, nullptr, shape);
    Pose pose(skel().size());
    pose.rot[node("mWing2Right")] = Quat::axis_angle({0, 1, 0}, 0.7);
    pose.rot[node("mWing3Right")] = Quat::axis_angle({1, 0, 0}, 0.5);
    std::vector<float> rest, bent, n;
    skin_prop(m, skel(), skel().global_pose(Pose(skel().size()), &shape), &shape, rest, n);
    skin_prop(m, skel(), skel().global_pose(pose, &shape), &shape, bent, n);
    double tear = 0, moved = 0;
    int meetings = 0;
    for (size_t a = 0; a < rest.size(); a += 3)
        for (size_t b = a + 3; b < rest.size(); b += 3) {
            const Vec3 ra{rest[a], rest[a + 1], rest[a + 2]}, rb{rest[b], rest[b + 1], rest[b + 2]};
            if ((ra - rb).length() > 1e-5 || weight_of(m, a / 3, wing[0]) + weight_of(m, a / 3, wing[1]) + weight_of(m, a / 3, wing[2]) +
                                                         weight_of(m, a / 3, wing[3]) <= 0)
                continue;
            ++meetings;
            tear = std::max(tear, (Vec3{bent[a], bent[a + 1], bent[a + 2]} - Vec3{bent[b], bent[b + 1], bent[b + 2]}).length());
        }
    for (size_t i = 0; i < rest.size(); ++i) moved = std::max(moved, double(std::fabs(bent[i] - rest[i])));
    CHECK(meetings >= 5 * 4);
    CHECK(tear < 1e-4);
    CHECK(moved > 0.05);
    fs::remove_all(s.dir);
}

TEST(spare_chain_joints_sit_on_the_chain_and_export_with_sl_names) {
    SashFile s = sash_on_wing("export");
    const mech::Body body = sash_body();
    const mech::Bone& first = body.bones[body.bones.size() - 6];
    const mech::Bone& last = body.bones.back();
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_rig_mapped(s.path, skel(), s.map, m, r, err, &s.bones));
    // Six bones, four joints: spaced evenly from the first head to the last tip, the first on the first head.
    const double len = (last.tail - first.head).length();
    for (int k = 0; k < 4; ++k) {
        const Vec3 want = first.head + (last.tail - first.head) * (double(k) / 4);
        CHECK((m.binds[node(("mWing" + std::to_string(k + 1) + "Right").c_str())].pos - want).length() < 1e-4);
    }
    CHECK(len > 0.4);
    // Up to as many bones as joints, each head places the next joint: the sash's last two on the tongue.
    RigMap two = s.map;
    clear_spare_chain(two, "WingRight");
    CHECK(two.find("Sash.001")->target.empty() && !two.spare_labels.count("WingRight"));
    std::vector<int> chain = spare_chain_from(s.bones, bone_index(s.bones, "Sash.005"));
    CHECK(chain.size() == 2);
    use_spare_chain(two, s.bones, chain, "Tongue", "sash end");
    DaeModel t;
    CHECK(load_rig_mapped(s.path, skel(), two, t, r, err, &s.bones));
    CHECK((t.binds[node("mFaceTongueBase")].pos - body.bones[body.bones.size() - 2].head).length() < 1e-4);
    CHECK((t.binds[node("mFaceTongueTip")].pos - last.head).length() < 1e-4);
    CHECK(t.labels.at("mFaceTongueTip") == "sash end" && !t.labels.count("mWing1Right"));
    // Exported for SL and read back: SL's names, the wing joints where the sash put them.
    RigPart part{&m, "sash", r.unmapped_joints, r.measured_scale, r.scale};
    // The check lists the chain's joint positions, asks for Lock scale, and says what else would move it.
    auto spare_finding = [&](const RigExportOptions& o) {
        for (const RigFinding& x : check_rig_export(skel(), {part}, o))
            if (x.rule == "spare_chains") return std::optional<RigFinding>(x);
        return std::optional<RigFinding>();
    };
    const std::optional<RigFinding> note = spare_finding({});
    CHECK(note && note->severity == RigSeverity::Info && note->joints.size() == 4);
    if (note) {
        for (const char* says : {"sash", "mWing1Right", "cm off", "Lock scale", "Shape-proof", "priority", "Bento wings"})
            CHECK(note->message.find(says) != std::string::npos);
    }
    RigExportOptions none;
    none.joint_positions = false;
    const std::optional<RigFinding> warn = spare_finding(none);
    CHECK(warn && warn->severity == RigSeverity::Warning && warn->message.find("write joint positions") != std::string::npos);
    RigPart plain_part = part;
    DaeModel unlabelled = m;
    unlabelled.labels.clear();
    plain_part.model = &unlabelled;
    for (const RigFinding& x : check_rig_export(skel(), {plain_part}, {})) CHECK(x.rule != "spare_chains");
    std::string text;
    CHECK(write_rig_dae(skel(), {part}, {}, text, err));
    DaeModel back;
    DaeReport rb;
    CHECK(load_dae(text, "", skel(), back, rb, err));
    CHECK(rb.unmapped_joints.empty());
    for (int k = 1; k <= 4; ++k) {
        const int j = node(("mWing" + std::to_string(k) + "Right").c_str());
        CHECK(back.bound[j] && (back.binds[j].pos - m.binds[j].pos).length() < 1e-4);
    }
    // An animation of the sash keys the wing by SL's name.
    Clip clip;
    clip.end_frame = 10;
    key_rotation(clip, "mWing2Right", 0, Quat{});
    key_rotation(clip, "mWing2Right", 10, Quat::axis_angle({0, 1, 0}, 0.5));
    const AnimExportResult anim = export_anim(skel(), clip);
    CHECK(anim.errors.empty() && anim.file.joints.size() == 1 && anim.file.joints[0].name == "mWing2Right");
    fs::remove_all(s.dir);
}

TEST(spare_chain_mapping_file_round_trip) {
    SashFile s = sash_on_wing("json");
    s.map.spare_labels["WingRight"] = "red sash";
    RigMap back;
    std::string err;
    CHECK(parse_rig_map_json(write_rig_map_json(s.map), back, err));
    CHECK(back.spare_labels["WingRight"] == "red sash");
    CHECK(spare_chain_bones(s.bones, back, "WingRight") == spare_chain_bones(s.bones, s.map, "WingRight"));
    CHECK(back.find("Sash.003")->spare == "WingRight" && back.find("Sash.003")->target == s.map.find("Sash.003")->target);
    // The wing is taken now; the left one is free; a slot the mapping file does not know is left out.
    CHECK(spare_slot_free(back, *find_spare_slot("WingLeft")) && spare_slot_free(back, *find_spare_slot("WingRight")));
    RigMap other = back;
    other.find("Hips")->target = "mWing1Left";
    CHECK(!spare_slot_free(other, *find_spare_slot("WingLeft")));
    CHECK(parse_rig_map_json(R"({"vats-rig-map": 1, "bones": {"A": ""}, "spares": {"Antlers": {"label": "x", "bones": ["A"]}}})", back, err));
    CHECK(back.find("A")->spare.empty() && back.spare_labels.empty());
    fs::remove_all(s.dir);
}

TEST(spare_chain_bones_lose_their_wing_template_and_mirror_partner) {
    SashFile s = sash_on_wing("templates");
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_rig_mapped(s.path, skel(), s.map, m, r, err, &s.bones));
    Shape shape;
    shape_from_binds(skel(), {&m}, nullptr, shape);
    rig_axes_from_parts(skel(), {&m}, shape);
    Skeleton reused = skel();
    std::vector<int> nodes;
    for (const auto& [joint, label] : m.labels) nodes.push_back(reused.find(joint));
    CHECK(nodes.size() == 4);
    reused.set_reused(nodes);
    const int w2 = node("mWing2Right"), w2l = node("mWing2Left");
    // Not mirror-paired either way, while every other bone keeps its partner; an empty call pairs them again.
    CHECK(skel().mirror(w2) == w2l && reused.mirror(w2) == w2 && reused.mirror(w2l) == w2l);
    CHECK(reused.mirror(node("mElbowLeft")) == node("mElbowRight"));
    CHECK(reused.mirror_of("mWing2Right") == "mWing2Right" && reused.mirror_of("pin:mElbowLeft") == "pin:mElbowRight");
    Skeleton again = reused;
    again.set_reused({});
    CHECK(again.mirror(w2) == w2l && !again.reused(w2));
    // No wing template: a cone about the bone as the sash places it, where the wing's template is a fold-back hinge.
    const RigConstraints plain = template_limits(skel(), &shape), mine = template_limits(reused, &shape);
    CHECK(plain.find("mWing2Right") && plain.find("mWing2Right")->kind == JointLimitKind::Hinge);
    const JointLimit* cone = mine.find("mWing2Right");
    CHECK(cone && cone->kind == JointLimitKind::Cone);
    const Vec3 along = Vec3{0.25, 0, -1}.normalized();
    if (cone) CHECK(from_joint_frame(&shape, w2, cone->bone_axis).normalized().dot(along) > 0.999);
    CHECK(mine.find("mWing2Left") && mine.find("mWing2Left")->kind == JointLimitKind::Hinge);  // the free wing keeps it
    // Mirror Live and the mirrored export leave the sash's keys where they are.
    Clip clip;
    clip.end_frame = 10;
    key_rotation(clip, "mWing2Right", 0, Quat::axis_angle({0, 1, 0}, 0.4));
    key_rotation(clip, "mElbowLeft", 0, Quat::axis_angle({0, 0, 1}, 0.4));
    Clip live = clip;
    mirror_live(live, reused, 0, {"mWing2Right", "mElbowLeft"}, false);
    CHECK(!live.curves.count("mWing2Left") && live.curves.count("mElbowRight"));
    const Clip flipped = mirrored_clip(reused, clip), paired = mirrored_clip(skel(), clip);
    CHECK(flipped.curves.count("mWing2Right") && flipped.curves.at("mWing2Right") == clip.curves.at("mWing2Right"));
    CHECK(!flipped.curves.count("mWing2Left") && flipped.curves.count("mElbowRight"));
    CHECK(paired.curves.count("mWing2Left") && !paired.curves.count("mWing2Right"));
    // The sash hangs from the hips in the model, and the wing from the chest in SL: the finder says so.
    const RigMapResult res = resolve_rig_map(skel(), s.bones, s.map);
    const SpareHang hang = spare_hang(skel(), s.bones, res, spare_chain_bones(s.bones, s.map, "WingRight"), *find_spare_slot("WingRight"));
    CHECK(hang.model == "mPelvis" && hang.sl == "mChest");
    fs::remove_all(s.dir);
}

TEST(spare_chain_follow_through_swings_settles_and_keys_only_its_chain) {
    SashFile s = sash_on_wing("follow");
    DaeModel m;
    DaeReport r;
    std::string err;
    CHECK(load_rig_mapped(s.path, skel(), s.map, m, r, err, &s.bones));
    Shape shape;
    shape_from_binds(skel(), {&m}, nullptr, shape);
    rig_axes_from_parts(skel(), {&m}, shape);
    // The body turns its hips and leans at frames 0..12, then stands still to frame 72.
    Clip clip;
    clip.fps = 24, clip.end_frame = 72;
    key_rotation(clip, "mPelvis", 0, Quat{});
    key_rotation(clip, "mPelvis", 6, Quat::axis_angle({0, 0, 1}, 0.6));
    key_rotation(clip, "mPelvis", 12, Quat{});
    key_rotation(clip, "mTorso", 0, Quat{});
    key_rotation(clip, "mTorso", 12, Quat::axis_angle({0, 1, 0}, 0.3));
    key_rotation(clip, "mWing2Right", 40, Quat::axis_angle({1, 0, 0}, 0.1));  // a key of its own, replaced by the bake
    const Clip before = clip;
    const Rig rig(skel());
    const int i = bake_follow_through(clip, rig, &shape, "mWing1Right", 4, "scarf");
    CHECK(i == 0 && clip.dynamics.size() == 1 && clip.dynamics[0].baked && !clip.dynamics[0].fans);
    // Keys on the chain's four joints, and nowhere else: not the fan beside mWing4Right, not the body.
    const std::vector<std::string> chain = {"mWing1Right", "mWing2Right", "mWing3Right", "mWing4Right"};
    for (const auto& [track, curves] : clip.curves)
        if (std::find(chain.begin(), chain.end(), track) == chain.end())
            CHECK(before.curves.count(track) && before.curves.at(track) == curves);
    for (const auto& [track, curves] : before.curves)
        if (std::find(chain.begin(), chain.end(), track) == chain.end()) CHECK(clip.curves.count(track));
    CHECK(!clip.curves.count("mWing4FanRight"));
    // It swings while the body moves, and settles once it stands still.
    double swing = 0, late = 0;
    Quat at_60[4];
    for (int f = 0; f <= 72; ++f) {
        const Evaluation e = evaluate(rig, clip, f, &shape);
        for (size_t k = 0; k < chain.size(); ++k) {
            const Quat q = e.pose.rot[size_t(node(chain[k].c_str()))];
            if (f <= 30) swing = std::max(swing, q.angle() * kRadToDeg);
            if (f == 60) at_60[k] = q;
            if (f > 60) late = std::max(late, (at_60[k].conj() * q).angle() * kRadToDeg);
        }
    }
    CHECK(swing > 5);
    CHECK(late < 0.5);
    // Again: set up afresh and baked from the keys it had first, so the chain's own key comes back on Unbake.
    CHECK(bake_follow_through(clip, rig, &shape, "mWing1Right", 4, "hair") == 0 && clip.dynamics.size() == 1);
    unbake_dynamics(clip, skel(), 0);
    CHECK(clip.curves.at("mWing2Right") == before.curves.at("mWing2Right") && !clip.curves.count("mWing1Right"));
    fs::remove_all(s.dir);
}

TEST(spare_chain_presets_round_trip_and_fit_the_next_model) {
    SashFile s = sash_on_wing("presets");
    s.map.spare_labels["WingRight"] = "red sash";
    // Saved, written and read back, the fragment puts the chain on a fresh mapping of a model whose sash bones are
    // spelt otherwise.
    std::vector<SparePreset> saved{spare_preset_from(s.map, s.bones, "sash on right wing")}, back;
    std::string err;
    CHECK(saved[0].chains.size() == 1 && saved[0].chains[0].bones.size() == 6);
    CHECK(parse_spare_presets_json(write_spare_presets_json(saved), back, err));
    CHECK(back.size() == 1 && back[0].name == "sash on right wing" && back[0].chains[0].slot == "WingRight" &&
          back[0].chains[0].label == "red sash" && back[0].chains[0].bones == saved[0].chains[0].bones);
    std::vector<SourceBone> next = s.bones;
    for (SourceBone& b : next)
        if (b.name.rfind("Sash.", 0) == 0) b.name = "SASH_" + b.name.substr(5);
    RigMap fresh = suggest_rig_map(skel(), next, {});
    CHECK(apply_spare_preset(fresh, next, back[0]) == 1 && spare_chain_bones(next, fresh, "WingRight").size() == 6);
    CHECK(spare_chain_bones(next, fresh, "WingRight").size() == 6 && fresh.spare_labels["WingRight"] == "red sash");
    CHECK(fresh.find("SASH_001")->target == "mWing1Right");
    // Applied again, it puts the same chain on again, nothing else.
    CHECK(apply_spare_preset(fresh, next, back[0]) == 1 && spare_chain_bones(next, fresh, "WingRight").size() == 6);
    // A built-in goes on the bone picked, on its side of the body: a ponytail on the tail, a scarf on whichever wing.
    RigMap tail = suggest_rig_map(skel(), s.bones, {});
    const int sash = bone_index(s.bones, "Sash.001");
    CHECK(apply_spare_preset(tail, s.bones, builtin_spare_presets()[1], sash) == 1 && spare_chain_bones(s.bones, tail, "Tail").size() == 6);
    auto bone = [](const char* name, int parent, Vec3 at) { return SourceBone{name, parent, {Quat{}, at}, true, 1}; };
    const std::vector<SourceBone> tufts = {bone("Hips", -1, {0, 0, 1}), bone("Head", 0, {0, 0, 1.6}),
                                           bone("Tuft.L", 1, {0, 0.06, 1.7}), bone("Tuft.R", 1, {0, -0.06, 1.7})};
    RigMap two;
    for (const SourceBone& b : tufts) two.bones.push_back({b.name, "", 0, "", ""});
    CHECK(apply_spare_preset(two, tufts, builtin_spare_presets()[0], 2) == 1);
    CHECK(two.find("Tuft.L")->spare == "WingLeft" && two.find("Tuft.L")->target == "mWing1Left");
    // A two-sided built-in takes the mirror-named bone for its second chain.
    RigMap skirt = two;
    clear_spare_chain(skirt, "WingLeft");
    CHECK(apply_spare_preset(skirt, tufts, builtin_spare_presets()[2], 3) == 2);
    CHECK(skirt.find("Tuft.R")->spare == "HindRight" && skirt.find("Tuft.L")->spare == "HindLeft");
    CHECK(!parse_spare_presets_json("{\"presets\": []}", back, err) && !err.empty());
    fs::remove_all(s.dir);
}
