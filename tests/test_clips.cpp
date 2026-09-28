// Several clips per project, batch export names and AO notecards (spec 08 CL).
#include <algorithm>
#include <fstream>
#include <sstream>

#include "check.h"
#include "vats/ao_notecard.h"
#include "vats/clips.h"
#include "vats/edit.h"
#include "vats/export_name.h"
#include "vats/history.h"
#include "vats/key_tags.h"
#include "vats/reference.h"
#include "vats/project.h"

using namespace vats;

namespace {

bool keyed(const Clip& c, const char* track) { return c.curves.count(track) != 0; }

// An AO set: stand (the loaded clip) and walk, each keyed on its own bone.
Project ao_set() {
    Project p;
    p.clip.fps = 24;
    p.clip.end_frame = 48, p.clip.loop_out = 48, p.clip.loop = true;
    p.clip.priority = 4;
    p.clip.export_settings.set("pattern", "[NAME]_[CLIP]");
    key_euler(p.clip, "mHead", 0, {0, 10, 0});
    name_clips(p);
    p.clips[0].name = "stand1";
    p.clips[0].ao_state = "Standing";
    add_clip(p, "walk", false);
    p.clips[1].ao_state = "Walking";
    key_euler(p.clip, "mHipLeft", 0, {20, 0, 0});
    return p;
}

std::string read_fixture(const char* name) {
    std::ifstream f(std::string(VATS_TEST_FILES) + "/" + name, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

}  // namespace

TEST(clips_add_inherits_defaults_and_switches) {
    Project p = ao_set();
    CHECK_EQ(clip_count(p), 2);
    CHECK_EQ(p.active_clip, 1);
    // The new clip took the stand's settings but none of its keys (CL-2).
    CHECK_EQ(p.clip.fps, 24);
    CHECK_EQ(p.clip.end_frame, 48);
    CHECK_EQ(p.clip.priority, 4);
    CHECK(p.clip.loop);
    CHECK(p.clip.export_settings.find("pattern"));
    CHECK(!keyed(p.clip, "mHead"));
    CHECK(keyed(p.clip, "mHipLeft"));
    set_active_clip(p, 0);
    CHECK(keyed(p.clip, "mHead") && !keyed(p.clip, "mHipLeft"));
    CHECK(keyed(p.clips[1].clip, "mHipLeft"));
    CHECK(p.clips[0].clip == Clip{});  // the active slot holds nothing
    // Duplicate copies the keys and the AO state.
    add_clip(p, "stand2", true);
    CHECK_EQ(p.active_clip, 1);
    CHECK(keyed(p.clip, "mHead"));
    CHECK_EQ(p.clips[1].ao_state, std::string("Standing"));
    CHECK_EQ(clip_name(p, 2), std::string("walk"));
}

TEST(clips_delete_and_move) {
    Project p = ao_set();
    add_clip(p, "run", false);  // stand1, walk, run (active)
    move_clip(p, 2, 0);          // run, stand1, walk
    CHECK_EQ(clip_name(p, 0), std::string("run"));
    CHECK_EQ(p.active_clip, 0);
    move_clip(p, 2, 1);  // run, walk, stand1: the active clip stays run
    CHECK_EQ(p.active_clip, 0);
    set_active_clip(p, 1);
    CHECK(keyed(p.clip, "mHipLeft"));
    delete_clip(p, 1);  // the active one: its neighbour becomes active
    CHECK_EQ(clip_count(p), 2);
    CHECK_EQ(clip_name(p, p.active_clip), std::string("run"));
    delete_clip(p, 0);
    delete_clip(p, 0);  // never the last
    CHECK_EQ(clip_count(p), 1);
    CHECK(keyed(p.clip, "mHead"));
}

TEST(clips_round_trip) {
    Project p = ao_set();
    p.clips[0].extra.set("future", 1);
    std::string text = save_project(p);
    CHECK(text.find("\"version\": 3") != std::string::npos);
    Project q;
    std::string err;
    CHECK(load_project(text, q, err));
    CHECK(!q.read_only);
    CHECK(q.clips == p.clips);
    CHECK_EQ(q.active_clip, 1);
    CHECK(q.clip == p.clip);
    CHECK_EQ(save_project(q), text);
    set_active_clip(q, 0);  // saved from the other clip, it loads the same
    Project r;
    CHECK(load_project(save_project(q), r, err));
    CHECK_EQ(r.active_clip, 0);
    CHECK(keyed(r.clip, "mHead"));
    CHECK(keyed(r.clips[1].clip, "mHipLeft"));
}

TEST(clips_with_actors_round_trip_and_share_timing) {
    Project p = ao_set();  // walk active
    Actor a, b;
    a.name = "Lead", b.name = "Partner";
    p.actors = {a, b};
    sync_actor_timing(p);  // the partner gets one clip per take, timed like the lead's
    CHECK_EQ(p.actors[1].clips.size(), size_t(2));
    key_euler(p.actors[1].clip, "mShoulderLeft", 0, {0, 0, 70});  // partner's walk
    set_active_clip(p, 0);                                           // every actor switches
    CHECK(!keyed(p.actors[1].clip, "mShoulderLeft"));
    CHECK_EQ(p.actors[1].clip.end_frame, 48);
    p.clip.end_frame = 60;  // a longer stand: the partner's stand follows, not its walk
    sync_actor_timing(p);
    CHECK_EQ(p.actors[1].clip.end_frame, 60);
    CHECK_EQ(p.actors[1].clips[1].end_frame, 48);
    set_active_actor(p, 1);  // the partner's takes come along
    CHECK(keyed(p.clips[1].clip, "mShoulderLeft"));
    CHECK(keyed(p.actors[0].clips[1], "mHipLeft"));
    std::string err, text = save_project(p);
    Project q;
    CHECK(load_project(text, q, err));
    CHECK_EQ(save_project(q), text);
    set_active_clip(q, 1);
    CHECK(keyed(q.clip, "mShoulderLeft"));
    CHECK(keyed(q.actors[0].clip, "mHipLeft"));
}

TEST(clips_old_projects_load_as_one_clip) {
    Project p;
    std::string err;
    CHECK(load_project(R"({"format": "vats-project", "version": 1, "fps": 24, "end_frame": 12})", p, err));
    CHECK(p.clips.empty());
    CHECK_EQ(clip_count(p), 1);
    CHECK_EQ(clip_name(p, 0), std::string("Clip"));
    CHECK_EQ(p.clip.fps, 24);
    std::string two = R"({"format": "vats-project", "version": 2, "fps": 30, "end_frame": 10, "active": 0,
        "actors": [{"name": "A"}, {"name": "B", "clip": {"fps": 30, "end_frame": 10}}]})";
    CHECK(load_project(two, p, err));
    CHECK(p.clips.empty() && p.actors.size() == 2);
    CHECK(save_project(p).find("\"version\": 2") != std::string::npos);  // one clip: still version 2
    // Naming the only clip makes a list of one, saved as version 3.
    name_clips(p);
    p.clips[0].name = "hug";
    CHECK(save_project(p).find("\"version\": 3") != std::string::npos);
    CHECK(load_project(save_project(p), p, err));
    CHECK_EQ(clip_name(p, 0), std::string("hug"));
}

TEST(clips_switch_is_one_undo_step) {
    // What App::scene_edit and App::apply_restore do.
    History h;
    Project p = ao_set();  // walk active
    auto scene = [](const Project& q) { return SceneState{q.actors, q.active, q.clips, q.active_clip}; };
    auto restore = [](Project& q, History::Restore r) {
        if (r.scene) q.actors = r.scene->actors, q.active = r.scene->active, q.clips = r.scene->clips, q.active_clip = r.scene->active_clip;
        q.clip = std::move(r.clip);
    };
    // A key on walk, switch to stand1, a key on stand1.
    h.begin(p.clip);
    key_euler(p.clip, "mChest", 0, {5, 0, 0});
    CHECK(h.commit("Rotate", p.clip));
    Clip before = p.clip;
    SceneState sb = scene(p);
    set_active_clip(p, 0);
    h.record_scene("Switch Clip", before, sb, p.clip, scene(p));
    h.begin(p.clip);
    key_euler(p.clip, "mNeck", 0, {5, 0, 0});
    CHECK(h.commit("Rotate", p.clip));

    restore(p, h.undo_step());  // stand1 loses mNeck
    CHECK_EQ(p.active_clip, 0);
    CHECK(!keyed(p.clip, "mNeck") && keyed(p.clip, "mHead"));
    restore(p, h.undo_step());  // back on walk, with its mChest
    CHECK_EQ(p.active_clip, 1);
    CHECK(keyed(p.clip, "mChest") && keyed(p.clip, "mHipLeft"));
    CHECK(keyed(p.clips[0].clip, "mHead"));
    restore(p, h.undo_step());
    CHECK(!keyed(p.clip, "mChest"));
    restore(p, h.redo_step());
    restore(p, h.redo_step());  // on stand1 again
    CHECK_EQ(p.active_clip, 0);
    CHECK(keyed(p.clip, "mHead"));
    CHECK(keyed(p.clips[1].clip, "mChest"));
    restore(p, h.redo_step());
    CHECK(keyed(p.clip, "mNeck"));

    // A list edit (add) is a scene step too: undo removes the clip and returns to where it was.
    before = p.clip, sb = scene(p);
    add_clip(p, "run", false);
    h.record_scene("Add Clip", before, sb, p.clip, scene(p));
    restore(p, h.undo_step());
    CHECK_EQ(clip_count(p), 2);
    CHECK_EQ(p.active_clip, 0);
    CHECK(keyed(p.clip, "mNeck"));
}

TEST(clips_settings_to_all) {
    Project p = ao_set();  // walk active
    set_active_clip(p, 0);
    p.clips[1].clip.export_settings.set("name", "Walker");
    p.clip.fps = 30;  // stand: 30 fps, priority 5, a new pattern
    p.clip.priority = 5;
    p.clip.export_settings.set("pattern", "[CLIP]");
    p.clip.export_settings.set("name", "Stander");
    apply_settings_to_all_clips(p);
    const Clip& walk = p.clips[1].clip;
    CHECK_EQ(walk.fps, 30);
    CHECK_EQ(walk.end_frame, 60);  // retimed: still 2 s
    CHECK_EQ(walk.priority, 5);
    CHECK_EQ(walk.export_settings.find("pattern")->str, std::string("[CLIP]"));
    CHECK_EQ(walk.export_settings.find("name")->str, std::string("Walker"));  // its own name stays
}

TEST(clips_export_names) {
    ExportNaming n{"AO", 1, "", "[NAME]_[CLIP]", ""};
    n.clip = "stand1";
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("AO_stand1.anim"));
    n.pattern = "[NAME]_[#]";  // no [CLIP]: appended, before an appended actor
    n.actor = "Lead";
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("AO_01_stand1_Lead.anim"));
    n.pattern = "[ACTOR]-[CLIP]-[NAME]";
    CHECK_EQ(export_file_name(n, "", true, "anim"), std::string("Lead-stand1-AO_mirrored.anim"));
    n.clip.clear(), n.actor.clear();
    n.pattern = "[NAME]_[CLIP]_[#]";  // no clip: the token goes, its separator collapses
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("AO_01.anim"));
}

TEST(clips_ao_notecards_match_fixtures) {
    const std::vector<AoClip> clips = {
        {"Standing", "AO_stand1"},  {"Walking", "AO_walk"},  {"Standing", "AO_stand2"},
        {"Running", "AO_run"},      {"Sitting", "AO_sit"},   {"Sitting On Ground", "AO_groundsit"},
        {"Jumping", "AO_jump1"},    {"Jumping", "AO_jump2"}, {"Typing", "AO_type"},
        {"", "AO_unused"},
    };
    AoNotecard fs = ao_notecard(AoFormat::Firestorm, clips);
    CHECK_EQ(fs.text, read_fixture("ao_firestorm.txt"));
    CHECK(fs.warnings.empty());
    AoNotecard z = ao_notecard(AoFormat::Zhao, clips);
    CHECK_EQ(z.text, read_fixture("ao_zhao.txt"));
    CHECK_EQ(z.warnings.size(), size_t(2));  // Typing is not a ZHAO-II state; one Jumping only
    CHECK(z.warnings[0].find("Typing") != std::string::npos);
    CHECK(z.warnings[1].find("AO_jump2") != std::string::npos);
    AoNotecard comma = ao_notecard(AoFormat::Firestorm, {{"Standing", "a,b"}});
    CHECK(comma.text.empty() && comma.warnings.size() == 1);
}

TEST(clips_zhao_long_lines_repeat_the_token) {
    std::vector<AoClip> clips;
    for (int i = 0; i < 20; ++i) clips.push_back({"Standing", "stand_number_" + std::string(i < 10 ? "0" : "") + std::to_string(i)});
    AoNotecard z = ao_notecard(AoFormat::Zhao, clips);
    std::stringstream s(z.text);
    int lines = 0, anims = 0;
    for (std::string line; std::getline(s, line); ++lines) {
        CHECK(line.size() <= 255);
        CHECK(line.rfind("[ Standing ]", 0) == 0);
        anims += 1 + int(std::count(line.begin(), line.end(), '|'));
    }
    CHECK_EQ(lines, 2);
    CHECK_EQ(anims, 20);
    const std::string fs = ao_notecard(AoFormat::Firestorm, clips).text;  // Firestorm reads the whole line
    CHECK_EQ(std::count(fs.begin(), fs.end(), '\n'), 1L);
}

TEST(clips_hostile_counts_fail) {
    std::string many = R"({"format": "vats-project", "version": 3, "clips": [)";
    for (int i = 0; i < 20001; ++i) many += i ? ",{}" : "{}";
    many += "]}";
    Project p;
    std::string err;
    CHECK(!load_project(many, p, err));
    CHECK(err.find("too many clips") != std::string::npos);
}

namespace {
// Clip fields added after clips (and loop_tangents): each must be written per clip, not only for the active one.
void set_later_fields(Clip& c) {
    c.loop_tangents = true;
    c.ik_pull["ArmRight"] = 0.5;  // 08 RC-1
    key_euler(c, "mElbowLeft", 6, {0, 0, 40});
    tag_keys_at(c, {"mElbowLeft"}, 6, KeyTag::Breakdown);  // 08 KT-1
    c.selection_sets = {{"Arm", {"mShoulderLeft", "mElbowLeft"}}};  // 08 SS-1
    LipSync ls;  // 08 LS
    ls.from = 0, ls.to = 10;
    ls.cues = {{0, "X"}, {4, "A"}};
    c.lip_sync = ls;
    Reference ref;  // 08 RF
    ref.path = "walk-side.png", ref.opacity = 0.3, ref.in_scene = true;
    c.reference = ref;
}
}  // namespace

TEST(clips_carry_every_clip_field) {
    Project p = ao_set();  // walk active; stand is stored in clips[0]
    set_later_fields(p.clips[0].clip);
    Actor a, b;
    a.name = "Lead", b.name = "Partner";
    p.actors = {a, b};
    sync_actor_timing(p);
    set_later_fields(p.actors[1].clips[0]);  // the partner's stand
    std::string err, text = save_project(p);
    Project q;
    CHECK(load_project(text, q, err));
    CHECK(q.clips[0].clip == p.clips[0].clip);
    CHECK(q.actors[1].clips[0] == p.actors[1].clips[0]);
    CHECK_EQ(save_project(q), text);
}
