// The UI's pure helpers (ui/widgets.h): slider travel, counts, spans, zoom and tool window placement.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"
#include "widgets.h"

using namespace vats;

TEST(slider_travel_default_and_int_steps) {
    CHECK_NEAR(slider_travel(200, 0), 300, 1e-6);   // 1.5 widths over the range
    CHECK_NEAR(slider_travel(200, 120), 120, 1e-6); // a slider's own
    CHECK_NEAR(slider_travel(200, 0, 5), 120, 1e-6);   // 24 px a step
    CHECK_NEAR(slider_travel(200, 0, 2), 100, 1e-6);   // at least half the width
    CHECK_NEAR(slider_travel(200, 0, 60), 300, 1e-6);  // at most 1.5 widths
    // A 0..1 slider 250 px wide: 0.0027 per pixel, not the 0.004 a jumping slider gives.
    CHECK(1.0 / slider_travel(250, 0) < 0.003);
}

TEST(count_noun_plurals) {
    CHECK(count_noun(1, "bone") == "1 bone");
    CHECK(count_noun(0, "bone") == "0 bones");
    CHECK(count_noun(3, "bone") == "3 bones");
    CHECK(count_noun(2, "key", "keys") == "2 keys");
    CHECK(count_noun(1, "body", "bodies") == "1 body");
    CHECK(count_noun(4, "body", "bodies") == "4 bodies");
}

TEST(ensure_span_keeps_tiny_ranges) {
    double lo = 0.01, hi = 0.03;  // a 2 cm jiggle
    ensure_span(lo, hi, 0.002);
    CHECK_NEAR(lo, 0.01, 1e-12);
    CHECK_NEAR(hi, 0.03, 1e-12);
    lo = hi = 5;  // flat: widened about its value
    ensure_span(lo, hi, 0.1);
    CHECK_NEAR(lo, 4.95, 1e-12);
    CHECK_NEAR(hi, 5.05, 1e-12);
}

TEST(zoom_about_keeps_the_pointer_value) {
    double lo = 0, hi = 10;
    zoom_about(lo, hi, 7.5, 0.5);
    CHECK_NEAR(hi - lo, 5, 1e-12);
    CHECK_NEAR((7.5 - lo) / (hi - lo), 0.75, 1e-12);  // the pointer stays at 75% of the height
}

TEST(place_window_stays_on_screen_and_off_the_avatar) {
    const Box work{0, 20, 1200, 780};
    // Wider than the screen allows: shrunk and inside.
    Box b = place_window(work, 2000, 2000, 600, {}, 10, 20);
    CHECK(b.x0 >= work.x0 && b.x1 <= work.x1 && b.y0 >= work.y0 && b.y1 <= work.y1);
    // Room beside the avatar: never over its centre line, right side first.
    b = place_window(work, 400, 300, 600, {}, 10, 20);
    CHECK(!(b.x0 < 600 && 600 < b.x1));
    CHECK_NEAR(b.x1, 1190, 1e-3);
    // The right side taken by another window: the left instead.
    const Box other{790, 30, 1190, 700};
    b = place_window(work, 400, 300, 600, {other}, 10, 20);
    CHECK_NEAR(b.x0, 10, 1e-3);
    CHECK(!(b.x0 < 600 && 600 < b.x1));
}

TEST(tool_windows_open_over_the_view_when_it_has_room) {
    const Box work{0, 20, 1600, 980}, view{290, 40, 1210, 590};  // the 3D view between Bones and Properties
    // Room for the window: the view, so it never lands on Properties.
    Box area = tool_window_area(work, view, 400, 200, 10);
    CHECK(area.x0 == view.x0 && area.x1 == view.x1 && area.y1 == view.y1);
    Box b = place_window(area, 400, 700, 750, {}, 10, 20);
    CHECK(b.x1 <= view.x1 && b.y1 <= view.y1);  // shrunk into the view, not spilling over the panels
    // A view too narrow for it: the whole work area.
    area = tool_window_area(work, Box{290, 40, 600, 590}, 400, 200, 10);
    CHECK(area.x0 == work.x0 && area.x1 == work.x1);
}

TEST(clamp_window_pulls_a_fixed_window_on_screen) {
    const Box work{0, 20, 1600, 980};
    Box b = clamp_window(work, Box{1400, 900, 1760, 1060});  // off the bottom-right corner
    CHECK_NEAR(b.x1, 1600, 1e-3);
    CHECK_NEAR(b.y1, 980, 1e-3);
    CHECK_NEAR(b.w(), 360, 1e-3);
    b = clamp_window(work, Box{100, 100, 460, 260});  // inside: unchanged
    CHECK_NEAR(b.x0, 100, 1e-3);
    CHECK_NEAR(b.y0, 100, 1e-3);
}

// The Hand Poser opened in the view's middle, over the hands it poses: beside the view where a panel leaves room.
TEST(beside_view_keeps_the_hand_poser_off_the_avatar) {
    const Box work{0, 20, 1600, 980};
    BesidePlace p = beside_view(work, Box{300, 20, 1200, 700}, 370, 10);  // 400 px of panel on the right: there
    CHECK_EQ(p.side, 1);
    CHECK_NEAR(p.scale, 1, 1e-6);
    p = beside_view(work, Box{490, 20, 1090, 700}, 712, 10);  // the testers' Pose layout at 2x: shrunk to fit the right
    CHECK_EQ(p.side, 1);
    CHECK_NEAR(p.scale, 490.f / 712.f, 1e-4);
    p = beside_view(work, Box{900, 20, 1600, 700}, 370, 10);  // the view at the right edge: on its left
    CHECK_EQ(p.side, -1);
    p = beside_view(work, Box{100, 20, 1500, 700}, 712, 10);  // no room beside: in the corner, half the view at most
    CHECK_EQ(p.side, 0);
    CHECK_NEAR(p.scale, (700.f - 20) / 712.f, 1e-4);
}

// Counts are written with count_noun ("1 key", "3 keys"), never "key(s)" (docs/wiki/STYLE.md, UI text).
TEST(ui_text_has_no_plural_s_in_brackets) {
    const std::filesystem::path ui = std::filesystem::path(VATS_ASSETS_DIR) / ".." / ".." / "ui";
    int found = 0;
    for (const auto& e : std::filesystem::directory_iterator(ui)) {
        if (e.path().extension() != ".cpp") continue;
        std::ifstream f(e.path());
        std::string line;
        for (int n = 1; std::getline(f, line); ++n) {
            // "(s)" after a letter, inside a string literal (an odd number of quotes before it)
            const size_t at = line.find("(s)");
            if (at == std::string::npos || at == 0 || !std::isalpha((unsigned char)line[at - 1]) ||
                std::count(line.begin(), line.begin() + long(at), '"') % 2 == 0)
                continue;
            std::printf("  %s:%d: %s\n", e.path().filename().string().c_str(), n, line.c_str());
            ++found;
        }
    }
    CHECK(found == 0);
}

// Inventory > Bodies (design review #21): a body named by its folder, and the same files imported again found.
TEST(body_names_and_reimports) {
    CHECK(body_name({"/home/a/Reborn/head.dae", "/home/a/Reborn/upper.dae", "/home/a/Reborn/lower.dae"}) == "Reborn");
    CHECK(body_name({"C:\\bodies\\Maitreya\\body.fbx", "C:\\bodies\\Maitreya\\feet.fbx"}) == "Maitreya");
    CHECK(body_name({"/home/a/Downloads/mech.dae"}) == "mech");  // one file: its own name, not Downloads
    CHECK(body_name({"/a/x/head.dae", "/a/y/upper.dae"}) == "head");  // no shared folder: the first file's
    CHECK(body_name({"head.dae", "upper.dae"}) == "head");
    CHECK(body_name({}) == "Body");
    CHECK(same_files({"/a/head.dae", "/a/upper.dae"}, {"/a/upper.dae", "/a/head.dae"}));
    CHECK(!same_files({"/a/head.dae", "/a/upper.dae"}, {"/a/head.dae"}));
    CHECK(!same_files({}, {}));
}
