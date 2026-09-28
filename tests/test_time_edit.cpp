#include <cmath>
#include <fstream>
#include <iterator>

#include "check.h"
#include "fixtures.h"
#include "vats/audio.h"
#include "vats/curve_ops.h"
#include "vats/dynamics.h"
#include "vats/edit.h"
#include "vats/loop_tools.h"
#include "vats/project.h"
#include "vats/retarget.h"
#include "vats/time_edit.h"

using namespace vats;

namespace {

Clip keyed(std::initializer_list<int> frames, const char* track = "mElbowLeft") {
    Clip c;
    c.fps = 30, c.end_frame = 40, c.loop = true, c.loop_in = 10, c.loop_out = 30;
    for (int f : frames) key_euler(c, track, f, {0, 0, double(f)});
    return c;
}

std::vector<double> frames_of(const Clip& c, const char* track = "mElbowLeft") {
    std::vector<double> out;
    for (auto& k : c.curves.at(track).begin()->second.keys) out.push_back(k.frame);
    return out;
}

}  // namespace

TEST(time_insert_moves_keys_loop_and_pins) {
    Clip c = keyed({0, 10, 20});
    c.pins.push_back(Pin{"mWristLeft", "mWristLeft", "", 12, 25});
    insert_time(c, 10, 5);
    CHECK((frames_of(c) == std::vector<double>{0, 15, 25}));
    CHECK_EQ(c.end_frame, 45);
    CHECK_EQ(c.loop_in, 15);
    CHECK_EQ(c.loop_out, 35);
    CHECK_EQ(c.pins[0].from, 17);
    CHECK_EQ(c.pins[0].to, 30);
    // Values travel with their keys.
    CHECK(std::fabs(curve_euler(c, "mElbowLeft", 25).z - 20) < 1e-9);
}

TEST(time_remove_closes_the_gap) {
    Clip c = keyed({0, 10, 20, 30});
    c.pins.push_back(Pin{"mWristLeft", "mWristLeft", "", 12, 18});  // held only inside the cut: goes
    c.pins.push_back(Pin{"mWristRight", "mWristRight", "", 25, -1});
    remove_time(c, 10, 20);
    CHECK((frames_of(c) == std::vector<double>{0, 10, 20}));  // 10 went, 20 -> 10, 30 -> 20
    CHECK_EQ(c.end_frame, 30);
    CHECK_EQ(c.loop_in, 10);
    CHECK_EQ(c.loop_out, 20);
    CHECK_EQ(c.pins.size(), size_t(1));
    CHECK_EQ(c.pins[0].from, 15);
    CHECK_EQ(c.pins[0].to, -1);
    CHECK(std::fabs(curve_euler(c, "mElbowLeft", 10).z - 20) < 1e-9);
}

TEST(time_scale_stretches_the_range_and_shifts_the_rest) {
    Clip c = keyed({0, 10, 20, 30});
    scale_time(c, 10, 20, 20);  // 10 frames become 20
    CHECK((frames_of(c) == std::vector<double>{0, 10, 30, 40}));
    CHECK_EQ(c.end_frame, 50);
    CHECK_EQ(c.loop_out, 40);
    // Squashing keeps the order and the values: 11 -> 10.1, 20 -> 11.
    Clip d = keyed({0, 10, 11, 20});
    scale_time(d, 10, 20, 1);
    std::vector<double> got = frames_of(d);
    CHECK(got.size() == 4 && std::fabs(got[2] - 10.1) < 1e-9 && got[3] == 11);
    CHECK(std::fabs(curve_euler(d, "mElbowLeft", 11).z - 20) < 1e-9);
}

TEST(time_edits_on_selected_tracks_leave_the_rest) {
    Clip c = keyed({0, 10, 20});
    key_euler(c, "mElbowRight", 10, {0, 0, 1});
    insert_time(c, 5, 10, {"mElbowLeft"});
    CHECK((frames_of(c) == std::vector<double>{0, 20, 30}));
    CHECK((frames_of(c, "mElbowRight") == std::vector<double>{10}));
    CHECK_EQ(c.loop_in, 10);        // loop points only follow whole-clip edits
    CHECK_EQ(c.end_frame, 40);      // 30 still fits
    insert_time(c, 0, 20, {"mElbowLeft"});
    CHECK_EQ(c.end_frame, 50);      // keys past the end extend the clip
}

TEST(time_copy_paste_and_paste_insert_mirrored) {
    Clip c = keyed({0, 10, 20});
    KeyRange r = copy_range(c, 10, 20, {"mElbowLeft"});
    CHECK_EQ(r.length, 10);
    CHECK_EQ(r.tracks.at("mElbowLeft").begin()->second.keys.size(), size_t(2));
    paste_range(c, r, 30, false);
    CHECK((frames_of(c) == std::vector<double>{0, 10, 20, 30, 40}));
    CHECK(std::fabs(curve_euler(c, "mElbowLeft", 40).z - 20) < 1e-9);
    // Paste-insert at 0 on a fresh clip opens room first; mirrored lands on the right arm.
    Clip d = keyed({0, 5});
    paste_range(d, r, 0, true, &skel());
    CHECK(d.curves.count("mElbowRight") == 1);
    CHECK((frames_of(d) == std::vector<double>{0, 5}));  // left arm untouched: the insert was on the right arm only
    CHECK((frames_of(d, "mElbowRight") == std::vector<double>{0, 10}));
}

TEST(audio_decodes_every_format) {
    for (const char* ext : {"wav", "flac", "ogg", "mp3"}) {
        AudioData a;
        std::string err;
        const bool ok = load_audio_file(std::string(VATS_TEST_FILES) + "/tone." + ext, a, err);
        CHECK(ok);
        if (!ok) continue;
        CHECK_EQ(a.rate, 22050);
        CHECK_EQ(a.channels, 1);
        CHECK(std::fabs(a.seconds() - 0.5) < 0.06);  // MP3 and Vorbis pad a little
        float peak = 0;
        for (float p : a.peaks) peak = std::max(peak, p);
        CHECK(peak > 0.08f && peak <= 0.2f);  // ffmpeg's sine source is 1/8 full scale
    }
    AudioData a;
    std::string err;
    std::vector<std::uint8_t> junk(4096, 0x5a);
    CHECK(!decode_audio(junk, a, err));
    CHECK(!err.empty());
}

TEST(audio_beats_and_snap) {
    AudioTrack a;
    a.bpm = 120, a.beat_offset = 0.25;  // a beat every 0.5 s from 0.25
    a.beats = {1.1};
    std::vector<double> b = beat_times(a, 0, 1.3);
    CHECK((b == std::vector<double>{0.25, 0.75, 1.1, 1.25}));
    CHECK(std::fabs(snap_to_beat(a, 0.8, 0.1) - 0.75) < 1e-12);
    CHECK(std::fabs(snap_to_beat(a, 0.5, 0.1) - 0.5) < 1e-12);  // nothing near
    a.offset = 1;  // sliding the audio carries its beats along
    CHECK((beat_times(a, 0, 2.3) == std::vector<double>{1.25, 1.75, 2.1, 2.25}));
}

TEST(beat_snapped_frame_only_while_snapping) {
    Clip c;  // 30 fps; beats every 0.5 s: frames 0, 15, 30, ...
    CHECK_EQ(beat_snapped_frame(c, 17), 17.0);  // no audio track
    c.audio = AudioTrack{};
    c.audio->bpm = 120;
    CHECK_EQ(beat_snapped_frame(c, 17), 17.0);  // Snap to Beats off
    c.audio->snap = true;
    CHECK_EQ(beat_snapped_frame(c, 17), 15.0);   // within 3 frames
    CHECK_EQ(beat_snapped_frame(c, 22), 22.0);   // too far from 15 and 30
    CHECK_EQ(beat_snapped_frame(c, 28.4), 30.0);
}

TEST(scale_length_with_keys_maps_last_frame_and_loop) {
    Clip press;
    press.end_frame = 30, press.loop = true, press.loop_in = 0, press.loop_out = 30;
    Clip c = press;
    scale_length_with_keys(c, press, 0, 2);  // the right handle, pivot at 0: twice as long
    CHECK(c.end_frame == 60 && c.loop_in == 0 && c.loop_out == 60);
    press.loop_in = 10, press.loop_out = 20;
    scale_length_with_keys(c, press, 0, 1.5);
    CHECK(c.end_frame == 45 && c.loop_in == 15 && c.loop_out == 30);
    scale_length_with_keys(c, press, 30, 0.5);  // the left handle, pivot at the end: 15 to 30
    CHECK(c.end_frame == 30 && c.loop_in == 20 && c.loop_out == 25);
}

TEST(audio_track_saves_and_loads) {
    Project p;
    p.clip.audio = AudioTrack{"music/song.ogg", 1.5, 0.8, 128, 0.1, {2.0, 3.5}, true};
    const std::string text = save_project(p);
    Project q;
    std::string err;
    CHECK(load_project(text, q, err));
    CHECK(q.clip.audio.has_value());
    CHECK(q.clip.audio == p.clip.audio);
}

// Every time edit moves a baked chain's pre-bake keys (DynChain::source) with the curves, so a re-bake starts from keys
// in the same time as the rest of the clip; a stretch used to leave them where they were. Idle layers alike.
TEST(time_edits_move_the_pre_bake_keys) {
    auto baked = [] {
        Clip c = keyed({0, 10, 20, 30, 40}, "mTail1");
        c.dynamics.push_back(dyn_preset("tail", "mTail1", 2));
        bake_dynamics(c, Rig(skel()), nullptr);
        c.idle.push_back({});
        c.idle[0].baked = true, c.idle[0].source["mTail1"] = c.dynamics[0].source.at("mTail1");
        return c;
    };
    auto source_frames = [](const Clip& c) {
        std::vector<double> out, idle;
        for (auto& k : c.dynamics[0].source.at("mTail1").begin()->second.keys) out.push_back(k.frame);
        for (auto& k : c.idle[0].source.at("mTail1").begin()->second.keys) idle.push_back(k.frame);
        if (idle != out) check::fail(__FILE__, __LINE__, "the idle layer's source moved differently");
        return out;
    };
    Clip c = baked();
    CHECK((source_frames(c) == std::vector<double>{0, 10, 20, 30, 40}));
    scale_time(c, 10, 30, 40);  // Stretch Range: 10..30 to 10..50
    CHECK((source_frames(c) == std::vector<double>{0, 10, 30, 50, 60}));
    c = baked();
    insert_time(c, 10, 5);
    CHECK((source_frames(c) == std::vector<double>{0, 15, 25, 35, 45}));
    c = baked();
    remove_time(c, 10, 20);
    CHECK((source_frames(c) == std::vector<double>{0, 10, 20, 30}));
    c = baked();
    CHECK(cycle_offset(c, 20));  // Start Cycle at Frame 20 (loop 10..30)
    CHECK((source_frames(c) == std::vector<double>{0, 10, 20, 30, 40}));
    CHECK_NEAR(c.dynamics[0].source.at("mTail1").at("rot_z").evaluate(10), 20.0, 1e-6);  // the old frame 20
    c = baked();
    retime_clip(c, 60);
    CHECK((source_frames(c) == std::vector<double>{0, 20, 40, 60, 80}));
    c = baked();
    reverse_clip(c);
    CHECK_NEAR(c.dynamics[0].source.at("mTail1").at("rot_z").evaluate(0), 40.0, 1e-6);
    c = slice_clip(baked(), 10, 30);
    CHECK((source_frames(c) == std::vector<double>{0, 10, 20}));
}
