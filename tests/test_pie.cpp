// The Tab pie (ui/pie.h): picking a slot by direction from the centre, and the slots each ring offers.
#include <cmath>
#include <set>
#include <string>

#include "check.h"
#include "pie.h"

using namespace vats;

TEST(pie_picks_by_direction_outside_the_dead_zone) {
    const float dead = 12;
    CHECK(pie_dir_at(0, 0, dead) == -1);
    CHECK(pie_dir_at(8, -8, dead) == -1);  // 11.3 px out: still the centre
    CHECK(pie_dir_at(0, -40, dead) == int(PieDir::N));  // screen y runs down
    CHECK(pie_dir_at(40, 0, dead) == int(PieDir::E));
    CHECK(pie_dir_at(0, 40, dead) == int(PieDir::S));
    CHECK(pie_dir_at(-40, 0, dead) == int(PieDir::W));
    CHECK(pie_dir_at(30, -30, dead) == int(PieDir::NE));
    CHECK(pie_dir_at(-30, 30, dead) == int(PieDir::SW));
    // Each direction owns 45 degrees: 22 degrees off East is still East, 23 is the next one.
    const float r = 50, d22 = 22 * 3.14159265f / 180, d23 = 23 * 3.14159265f / 180;
    CHECK(pie_dir_at(r * std::cos(d22), -r * std::sin(d22), dead) == int(PieDir::E));
    CHECK(pie_dir_at(r * std::cos(d23), -r * std::sin(d23), dead) == int(PieDir::NE));
    CHECK(pie_dir_at(r * std::cos(d23), r * std::sin(d23), dead) == int(PieDir::SE));
    CHECK(pie_dir_at(-1, -200, dead) == int(PieDir::N));  // just left of up wraps to N, not NW
}

TEST(pie_rings_offer_eight_slots_greyed_without_a_selection) {
    for (PieRing ring : {PieRing::Main, PieRing::More}) {
        std::set<int> dirs;
        for (const PieSlot& s : pie_ring(ring)) dirs.insert(int(s.dir));
        CHECK(pie_ring(ring).size() == size_t(kPieSlots) && dirs.size() == size_t(kPieSlots));  // one per direction
    }
    auto enabled = [](PieRing ring, bool sel, const std::string& id) {
        for (const PieItem& it : pie_items(ring, sel))
            if (id == it.slot->id) return it.enabled;
        return false;
    };
    // Nothing selected: the tools and toggles work, Set Key and IK / FK wait for a bone.
    CHECK(enabled(PieRing::Main, false, "tool_move") && enabled(PieRing::Main, false, "auto_ik"));
    CHECK(enabled(PieRing::Main, false, "respect_joint_limits") && enabled(PieRing::Main, false, "more"));
    CHECK(!enabled(PieRing::Main, false, "key") && !enabled(PieRing::Main, false, "ik_toggle"));
    CHECK(enabled(PieRing::Main, true, "key") && enabled(PieRing::Main, true, "ik_toggle"));
    // The More ring acts on the bone: all of it but Scale and Back waits for one.
    CHECK(!enabled(PieRing::More, false, "mirror_bone") && !enabled(PieRing::More, false, "reset_bone"));
    CHECK(!enabled(PieRing::More, false, "edit_limits") && !enabled(PieRing::More, false, "relax"));
    CHECK(enabled(PieRing::More, false, "back") && enabled(PieRing::More, false, "tool_scale"));
    CHECK(enabled(PieRing::More, true, "mirror_bone") && enabled(PieRing::More, true, "frame_selected"));
    // More and Back share the bottom, so down-then-down returns.
    for (const PieSlot& s : pie_ring(PieRing::Main))
        if (std::string(s.id) == "more") CHECK(s.dir == PieDir::S);
    for (const PieSlot& s : pie_ring(PieRing::More))
        if (std::string(s.id) == "back") CHECK(s.dir == PieDir::S);
}
