// The UI's pure helpers (ui/widgets.h): slider travel, counts, spans, zoom and tool window placement.
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
