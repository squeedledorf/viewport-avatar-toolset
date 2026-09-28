#include <cmath>
#include <set>

#include "check.h"
#include "vats/audio.h"
#include "vats/edit.h"
#include "vats/retime.h"

using namespace vats;

namespace {

Clip keyed(std::initializer_list<int> frames) {
    Clip c;
    c.fps = 30, c.end_frame = 40, c.loop = true, c.loop_in = 10, c.loop_out = 30;
    for (int f : frames) key_euler(c, "mElbowLeft", f, {0, 0, double(f)});
    return c;
}

std::vector<double> frames_of(const Clip& c, const char* track = "mElbowLeft", const char* ch = nullptr) {
    std::vector<double> out;
    const auto& t = c.curves.at(track);
    for (auto& k : (ch ? t.at(ch) : t.begin()->second).keys) out.push_back(k.frame);
    return out;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

}  // namespace

TEST(retime_marker_drag_shifts_later_keys_and_scales_between) {
    Clip c = keyed({0, 10, 20, 30});
    c.pins.push_back(Pin{"mWristLeft", "mWristLeft", "", 12, 25});
    c.curves["ik.armLeft"]["blend"].set_key(5, 1);
    c.curves["ik.armLeft"]["blend"].set_key(25, 0);
    std::vector<double> markers{10, 20};
    drag_marker(c, markers, 1, 30);  // 10..20 becomes 10..30, the rest moves 10 later
    CHECK((frames_of(c) == std::vector<double>{0, 10, 30, 40}));
    CHECK((markers == std::vector<double>{10, 30}));
    CHECK_EQ(c.end_frame, 50);
    CHECK_EQ(c.loop_in, 10);
    CHECK_EQ(c.loop_out, 40);
    CHECK_EQ(c.pins[0].from, 14);  // inside the stretch: 12 -> 10 + 2 x 2
    CHECK_EQ(c.pins[0].to, 35);
    CHECK((frames_of(c, "ik.armLeft", "blend") == std::vector<double>{5, 35}));  // before the range stays
    CHECK(near(curve_euler(c, "mElbowLeft", 30).z, 20));  // values travel with their keys

    // The first marker scales from frame 0; every later marker moves with it.
    Clip d = keyed({0, 10, 20, 30});
    std::vector<double> m2{10, 20};
    drag_marker(d, m2, 0, 5);
    CHECK((frames_of(d) == std::vector<double>{0, 5, 15, 25}));
    CHECK((m2 == std::vector<double>{5, 15}));
    CHECK_EQ(d.end_frame, 35);
    CHECK_EQ(d.loop_in, 5);

    // Fractional: a beat between frames; loop points and the length round.
    Clip e = keyed({0, 10, 20, 30});
    std::vector<double> m3{20};
    drag_marker(e, m3, 0, 25.5);
    std::vector<double> got = frames_of(e);
    CHECK(got.size() == 4 && near(got[1], 12.75) && near(got[2], 25.5) && near(got[3], 35.5));
    CHECK_EQ(e.end_frame, 46);

    // Never past the marker before it; a marker at frame 0 stays put.
    Clip f = keyed({0, 10, 20});
    std::vector<double> m4{10, 20};
    drag_marker(f, m4, 1, 3);
    CHECK((m4 == std::vector<double>{10, 11}));
    std::vector<double> m5{0};
    Clip g = keyed({0, 10});
    drag_marker(g, m5, 0, 8);
    CHECK((frames_of(g) == std::vector<double>{0, 10}) && m5[0] == 0);
}

TEST(retime_marker_snaps_to_beats) {
    Clip c = keyed({0});
    c.audio = AudioTrack{};
    c.audio->bpm = 120;       // a beat every 15 frames at 30 fps
    c.audio->beats = {1.11};  // a tapped beat at frame 33.3
    CHECK(near(snap_marker(c, 14, false), 14));  // snapping off: where the mouse is
    CHECK(near(snap_marker(c, 14.4, true), 14));
    c.audio->snap = true;
    CHECK(near(snap_marker(c, 14, false), 15));
    CHECK(near(snap_marker(c, 32, false), 33.3));
    CHECK(near(snap_marker(c, 32, true), 33));  // Snap frames rounds the beat
    CHECK(near(snap_marker(c, 22.5, false), 22.5));  // no beat within 3 frames
    c.audio->offset = 0.1;  // sliding the audio moves the grid
    CHECK(near(snap_marker(c, 16, false), 18));
}

TEST(split_dance_cuts_on_beats) {
    Clip c;
    c.fps = 30, c.end_frame = 4500, c.loop = true, c.loop_in = 100, c.loop_out = 4400;  // 150 s
    c.ease_in = 0.5, c.ease_out = 0.7;
    for (int f = 0; f <= 4500; f += 7) key_euler(c, "mElbowLeft", f, {0, 0, std::sin(f * 0.01) * 40});
    c.audio = AudioTrack{};
    c.audio->bpm = 130, c.audio->beat_offset = 0.1;
    std::set<int> beat_frames;
    for (double t : beat_times(*c.audio, 0, 150)) beat_frames.insert(int(std::lround(t * 30)));

    const std::vector<int> cuts = dance_cuts(c);
    CHECK_EQ(cuts.size(), size_t(2));
    int start = 0;
    for (int cut : cuts) {
        CHECK(beat_frames.count(cut) == 1);  // on a beat
        CHECK(cut - start <= 1800);
        auto next = beat_frames.upper_bound(cut);  // and the last one before the 60 s limit
        CHECK(next == beat_frames.end() || *next > start + 1800);
        start = cut;
    }
    const std::vector<Clip> parts = split_dance(c, cuts);
    CHECK_EQ(parts.size(), size_t(3));
    int total = 0;
    for (size_t k = 0; k < parts.size(); ++k) {
        const Clip& p = parts[k];
        CHECK(p.end_frame <= 1800);
        CHECK_EQ(p.fps, 30);
        CHECK(p.loop && p.loop_in == 0 && p.loop_out == p.end_frame);
        CHECK(near(p.ease_in, k == 0 ? 0.5 : 0));
        CHECK(near(p.ease_out, k == 2 ? 0.7 : 0));
        total += p.end_frame;
        if (k) {  // the join: this part's first frame is the previous part's last
            const Clip& q = parts[k - 1];
            CHECK(near(curve_euler(p, "mElbowLeft", 0).z, curve_euler(q, "mElbowLeft", q.end_frame).z));
            CHECK(near(curve_euler(p, "mElbowLeft", 0).z, curve_euler(c, "mElbowLeft", cuts[k - 1]).z));
        }
    }
    CHECK_EQ(total, 4500);  // the duration is kept
}

TEST(split_dance_without_beats_and_with_tapped_beats) {
    Clip c;
    c.fps = 30, c.end_frame = 4000;
    key_euler(c, "mElbowLeft", 0, {0, 0, 0});
    CHECK((dance_cuts(c) == std::vector<int>{1800, 3600}));  // no audio: at the limit
    c.end_frame = 1800;
    CHECK(dance_cuts(c).empty());  // exactly 60 s fits
    CHECK_EQ(split_dance(c, {}).size(), size_t(1));
    c.end_frame = 4000;
    c.audio = AudioTrack{};
    c.audio->beats = {10.0, 55.0, 61.0, 100.0};  // tapped only
    CHECK((dance_cuts(c) == std::vector<int>{1650, 3000}));  // 61 s is past the first limit, 100 s within the second
}
