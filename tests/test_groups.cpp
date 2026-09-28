// Couples and groups (spec 08 section 2).
#include <cmath>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/edit.h"
#include "vats/export_name.h"
#include "vats/history.h"
#include "vats/project.h"
#include "vats/rig.h"

using namespace vats;

namespace {

Project two_actors() {
    Project p;
    p.clip.end_frame = 20;
    p.clip.loop_out = 20;
    key_euler(p.clip, "mShoulderRight", 0, {0, 0, -70});
    key_euler(p.clip, "mElbowRight", 0, {0, 0, -100});  // bent, so a bound hand stays within the arm's reach
    Actor a, b;
    a.name = "Lead";
    b.name = "Partner";
    b.pos = {0.6, 0, 0};
    b.rot_z = 180;
    b.body = "sl-default-male";
    b.colour = {0.2f, 0.4f, 0.8f};
    key_euler(b.clip, "mShoulderLeft", 0, {0, 0, 70});
    key_euler(b.clip, "mShoulderLeft", 20, {0, 0, 55});
    p.actors = {a, b};
    sync_actor_timing(p);  // as the app keeps it: one timeline
    return p;
}

// What the app does: the other actor's bone, in the pinned actor's space.
ExternalTarget resolver(const Rig& rig, const Project& p, int self) {
    return [&rig, &p, self](const Pin& pin, double frame, Xform& out) {
        int t = -1;
        for (int i = 0; i < int(p.actors.size()); ++i)
            if (p.actors[i].name == pin.target_actor) t = i;
        int bone = rig.skeleton().find(pin.target);
        if (t < 0 || t == self || bone < 0) return false;
        Rig plain(rig.skeleton());  // the target actor's own pins are not followed
        Evaluation e = evaluate(plain, actor_clip(p, t), frame, nullptr);
        out = p.actors[self].placement().inverse() * p.actors[t].placement() * e.globals[bone];
        return true;
    };
}

}  // namespace

TEST(groups_save_load_round_trip) {
    Project p = two_actors();
    Pin pin;
    pin.joint = "mWristRight";
    pin.via = "mWristRight";
    pin.target = "mWristLeft";
    pin.target_actor = "Partner";
    pin.pos = {0.05, 0, 0};
    p.clip.pins.push_back(pin);
    std::string text = save_project(p);
    CHECK(text.find("\"version\": 2") != std::string::npos);
    CHECK(text.find("\"actors\"") != std::string::npos);
    CHECK(text.find("\"target_actor\": \"Partner\"") != std::string::npos);
    Project q;
    std::string err;
    CHECK(load_project(text, q, err));
    CHECK_EQ(err, std::string());
    CHECK(!q.read_only);
    CHECK_EQ(q.actors.size(), size_t(2));
    CHECK_EQ(q.active, 0);
    CHECK(q.clip == p.clip);
    CHECK(q.actors[1] == p.actors[1]);
    CHECK_EQ(q.actors[0].name, std::string("Lead"));
    CHECK_EQ(save_project(q), text);

    // The active actor's clip is the top level; switching moves clips, not copies of them.
    set_active_actor(q, 1);
    CHECK(q.clip == p.actors[1].clip);
    CHECK(actor_clip(q, 0) == p.clip);
    Project r;
    CHECK(load_project(save_project(q), r, err));
    CHECK_EQ(r.active, 1);
    CHECK(actor_clip(r, 0) == p.clip);
    CHECK(actor_clip(r, 1) == p.actors[1].clip);
}

TEST(groups_single_actor_file_unchanged) {
    Project p;
    key_euler(p.clip, "mChest", 5, {10, 0, 0});
    std::string text = save_project(p);
    CHECK(text.find("\"version\": 1") != std::string::npos);  // a plain project stays version 1 (GR-5)
    CHECK(text.find("actors") == std::string::npos);
    CHECK(text.find("target_actor") == std::string::npos);
    Project q;
    std::string err;
    CHECK(load_project(text, q, err));
    CHECK_EQ(save_project(q), text);
    // One actor in a file is a plain project.
    std::string one = R"({"format": "vats-project", "version": 2, "actors": [{"name": "Solo"}], "fps": 24})";
    CHECK(load_project(one, q, err));
    CHECK(q.actors.empty());
    CHECK_EQ(q.clip.fps, 24);
}

TEST(groups_timing_is_shared) {
    Project p = two_actors();
    p.clip.fps = 24;
    p.clip.end_frame = 48;
    p.clip.loop = true;
    sync_actor_timing(p);
    CHECK_EQ(p.actors[1].clip.fps, 24);
    CHECK_EQ(p.actors[1].clip.end_frame, 48);
    CHECK(p.actors[1].clip.loop);
}

TEST(groups_cross_actor_pin_follows_the_other_actor) {
    Project p = two_actors();
    Rig rig(skel());
    rig.external = resolver(rig, p, 0);
    const int wr = skel().find("mWristRight"), wl = skel().find("mWristLeft");
    std::string why;
    CHECK(pin_to_actor(p.clip, rig, 0, wr, "Partner", "mWristLeft", nullptr, why));
    CHECK_EQ(why, std::string());
    CHECK_EQ(p.clip.pins.size(), size_t(1));
    CHECK_EQ(p.clip.pins[0].target_actor, std::string("Partner"));
    // At every frame the Lead's right wrist keeps its offset to the Partner's left wrist, in the scene.
    Rig plain(skel());
    Xform held;
    for (double f : {0.0, 10.0, 20.0}) {
        Xform mine = p.actors[0].placement() * evaluate(rig, p.clip, f, nullptr).globals[wr];
        Xform theirs = p.actors[1].placement() * evaluate(plain, p.actors[1].clip, f, nullptr).globals[wl];
        Xform rel = theirs.inverse() * mine;
        if (f == 0) held = rel;
        CHECK((rel.pos - held.pos).length() < 1e-6);
    }
    // The partner really moved, so the pin did the work.
    Vec3 a = evaluate(plain, p.actors[1].clip, 0, nullptr).globals[wl].pos;
    Vec3 b = evaluate(plain, p.actors[1].clip, 20, nullptr).globals[wl].pos;
    CHECK((a - b).length() > 0.05);
    // Without a resolver the pin is skipped rather than aiming at the wrong skeleton.
    Rig bare(skel());
    CHECK((evaluate(bare, p.clip, 20, nullptr).globals[wr].pos - evaluate(plain, Clip{}, 20, nullptr).globals[wr].pos)
              .length() > 1e-3);  // the Lead's own arm key still applies
    CHECK(!pin_to_actor(p.clip, bare, 0, wr, "Partner", "mWristLeft", nullptr, why));
}

TEST(groups_export_names) {
    ExportNaming n{"Hug", 1, "", "[NAME]_[#]", "Lead"};
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("Hug_01_Lead.anim"));
    n.pattern = "[ACTOR]-[NAME]";
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("Lead-Hug.anim"));
    n.actor.clear();
    n.pattern = "[NAME]_[#]";
    CHECK_EQ(export_file_name(n, "", false, "anim"), std::string("Hug_01.anim"));
}

TEST(groups_undo_actor_steps) {
    History h;
    Project p = two_actors();
    // A clip edit on actor 1, then a scene step that adds a third actor.
    set_active_actor(p, 1);
    h.set_actor(1);
    h.begin(p.clip);
    key_euler(p.clip, "mHead", 0, {0, 20, 0});
    CHECK(h.commit("Rotate", p.clip));
    Clip clip_before = p.clip;
    SceneState before{p.actors, p.active};
    Actor c;
    c.name = "Third";
    p.actors.push_back(c);
    h.record_scene("Add Actor", clip_before, before, p.clip, {p.actors, p.active});

    History::Restore r = h.undo_step();
    CHECK(r.scene.has_value());
    CHECK_EQ(r.scene->actors.size(), size_t(2));
    CHECK(r.clip == clip_before);
    r = h.undo_step();
    CHECK(!r.scene.has_value());
    CHECK_EQ(r.actor, 1);  // the rotate belonged to actor 1
    CHECK(r.clip.curves.count("mHead") == 0);
    r = h.redo_step();
    CHECK_EQ(r.actor, 1);
    CHECK(r.clip.curves.count("mHead") == 1);
    r = h.redo_step();
    CHECK(r.scene && r.scene->actors.size() == 3);
}

namespace {

// A .anim as a file would give it: 24 fps, 2 s, a left-arm wave keyed on whole seconds, its own loop and a bind.
Clip loaded_anim() {
    Clip c;
    c.fps = 24;
    c.end_frame = 48;
    c.loop = true, c.loop_in = 12, c.loop_out = 36;
    c.priority = 5;
    key_euler(c, "mShoulderLeft", 0, {0, 0, 10});
    key_euler(c, "mShoulderLeft", 24, {0, 0, 80});
    key_euler(c, "mShoulderLeft", 48, {0, 0, 10});
    AnimExportResult r = export_anim(skel(), c);
    CHECK(r.errors.empty());
    AnimFile f;
    std::string err;
    CHECK(parse_anim(write_anim(r.file), f, err));
    Clip back = import_anim(skel(), f).clip;
    Pin pin;  // a bind to someone this project doesn't have
    pin.joint = pin.via = "mWristLeft";
    pin.target = "mWristRight";
    pin.target_actor = "Stranger";
    back.pins.push_back(pin);
    return back;
}

}  // namespace

TEST(groups_load_anim_into_actor_retimes_and_grows_the_scene) {
    Project p = two_actors();  // 30 fps, 20 frames, no loop
    p.actors[1].clip.export_settings.set("name", std::string("Kiss"));
    const Clip file = loaded_anim();
    CHECK_EQ(file.fps, 24);
    ActorLoad r = load_into_actor(p, 1, file);
    CHECK_EQ(r.file_fps, 24);
    CHECK_EQ(r.fps, 30);
    CHECK_EQ(r.clip_frames, 60);  // 2 s at 30 fps
    CHECK_EQ(r.scene_was, 20);
    CHECK_EQ(r.scene_now, 60);
    CHECK_EQ(r.dropped_binds, 1);
    const Clip& c = p.actors[1].clip;
    CHECK_EQ(c.fps, 30);
    CHECK_EQ(c.end_frame, 60);
    CHECK_EQ(p.clip.end_frame, 60);  // the whole scene grew
    CHECK(!c.loop);                  // the scene's loop, not the file's
    CHECK_EQ(c.priority, 5);         // per actor: the file's
    CHECK(c.pins.empty());
    CHECK(c.export_settings.find("name") && c.export_settings.find("name")->str == "Kiss");  // the actor's own export settings
    CHECK(c.curves.count("mShoulderLeft") && !c.curves.count("mShoulderRight"));  // replaced, not merged
    // Keys keep their timing: the file's 1 s key (frame 24 at 24 fps) is at frame 30.
    for (auto& [ch, curve] : c.curves.at("mShoulderLeft")) {
        std::vector<double> frames;
        for (auto& k : curve.keys) frames.push_back(k.frame);
        CHECK(std::find(frames.begin(), frames.end(), 30.0) != frames.end());
        CHECK_NEAR(frames.back(), 60, 1e-9);
    }
    // The pose at each second is the file's.
    for (double s : {0.0, 1.0, 2.0}) {
        Pose a = evaluate_curves(skel(), file, s * 24), b = evaluate_curves(skel(), c, s * 30);
        const int n = skel().find("mShoulderLeft");
        CHECK(std::fabs(std::fabs(a.rot[n].dot(b.rot[n])) - 1) < 1e-9);
    }
    // A shorter file leaves the scene's length alone; the active actor can be loaded into as well.
    Clip short_clip;
    short_clip.end_frame = 10;
    key_euler(short_clip, "mHead", 0, {0, 0, 20});
    r = load_into_actor(p, 0, short_clip);
    CHECK_EQ(r.scene_now, 60);
    CHECK_EQ(r.clip_frames, 10);
    CHECK_EQ(p.clip.end_frame, 60);
    CHECK(p.clip.curves.count("mHead") && !p.clip.curves.count("mShoulderRight"));
}

TEST(groups_load_into_actor_is_one_undo_step) {
    // What the app does: the load as a scene step (scene_edit), so undo gives back the actor and the scene's length.
    History h;
    Project p = two_actors();
    const Project before = p;
    Clip clip_before = p.clip;
    SceneState scene_before{p.actors, p.active};
    load_into_actor(p, 1, loaded_anim());
    h.record_scene("Load Animation", clip_before, scene_before, p.clip, {p.actors, p.active});
    History::Restore r = h.undo_step();
    CHECK(r.scene && r.scene->actors == before.actors);
    CHECK(r.clip == before.clip);
    CHECK_EQ(r.clip.end_frame, 20);
    r = h.redo_step();
    CHECK(r.scene && r.scene->actors[1].clip.curves.count("mShoulderLeft"));
    CHECK_EQ(r.clip.end_frame, 60);
}
