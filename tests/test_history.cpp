#include "check.h"
#include "vats/history.h"

using namespace vats;

// AM-124: no undo or redo while a step is open (a drag in progress).
TEST(history_blocks_undo_while_open) {
    History h;
    Clip a, b;
    b.fps = 24;
    h.begin(a);
    CHECK(h.commit("Frame Rate", b));
    CHECK(h.can_undo());
    h.begin(b);
    CHECK(!h.can_undo());
    CHECK(!h.can_redo());
    h.cancel();
    CHECK(h.can_undo());
    CHECK_EQ(h.undo().fps, a.fps);
    CHECK(h.can_redo());
}
