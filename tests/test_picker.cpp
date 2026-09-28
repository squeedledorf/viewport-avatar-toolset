// The body picker (08 PK): groups, page layouts, framing on extreme shapes, click tests and the silhouette.
#include <map>
#include <set>
#include <string>

#include "check.h"
#include "fixtures.h"
#include "vats/picker.h"

using namespace vats;

namespace {

// The Picker at the default layout's size: a 255 px panel, a 239 x 262 canvas, 15 px text.
constexpr double kW = 239, kH = 262, kLine = 15;
// Dots of different groups at least this far apart (px): a comfortable click radius at 255 px.
constexpr double kMinSpacing = 7;

double text_w(const std::string& s) { return 7.5 * double(s.size()); }

Shape uniform(double k) {
    Shape s{std::vector<Vec3>(size_t(skel().size()), Vec3{k, k, k}), std::vector<Vec3>(size_t(skel().size()))};
    return s;
}
Shape scaled(std::initializer_list<std::pair<const char*, double>> bones) {
    Shape s = uniform(1);
    for (auto [b, k] : bones)
        for (const char* side : {"", "Left", "Right"})
            if (int n = skel().find(std::string(b) + side); n >= 0) s.scale[size_t(n)] = {k, k, k};
    return s;
}

// The shapes a picker must keep in its canvas: SL's tallest and shortest, long legs on a short torso, long arms.
const std::map<std::string, Shape>& shapes() {
    static const std::map<std::string, Shape> s = {
        {"default", uniform(1)},
        {"tall", uniform(1.35)},
        {"short", uniform(0.7)},
        {"long legs, short torso", scaled({{"mHip", 1.4}, {"mKnee", 1.4}, {"mAnkle", 1.3}, {"mTorso", 0.8}, {"mChest", 0.8}})},
        {"long arms", scaled({{"mCollar", 1.3}, {"mShoulder", 1.45}, {"mElbow", 1.45}, {"mWrist", 1.3}})},
    };
    return s;
}

struct Page {
    PickerLayout layout;
    PickerScreen screen;
    std::vector<PickerLabel> labels;
    std::vector<std::vector<V2>> silhouette;  // canvas pixels
    PickerRect canvas, strip;
};

Page page_on(const Shape& shape, PickerPage page, int view, const Pose& pose) {
    const Skeleton& s = skel();
    const std::vector<Xform> chart = s.global_pose(picker_chart_pose(s), &shape);
    const std::vector<Xform> g = s.global_pose(pose, &shape);
    const auto parts = picker_parts(s, g, chart, &shape, page, view);
    Page p;
    p.layout = picker_layout(s, g, parts, &shape, page, view);
    p.canvas = {0, 0, kW, kH};
    std::vector<V2> fit_pts = picker_extent(p.layout);
    const auto sil = picker_silhouette(s, page, view, picker_anchors(s, g, parts, &shape, page, view));
    for (const auto& loop : sil) fit_pts.insert(fit_pts.end(), loop.begin(), loop.end());
    const PickerFit fit = picker_fit(fit_pts, picker_fit_area(page, p.canvas, kLine, page == PickerPage::Face ? 2 : 0));
    p.screen = picker_screen(p.layout, fit, kLine * 0.6);
    const PickerRect labels = picker_label_area(page, p.canvas, kLine, page == PickerPage::Face ? 2 : 0);
    p.strip = {labels.x + labels.w - 47, labels.y, 47, 19};  // the backdrop and pose buttons
    p.labels = picker_labels(s, page, view, p.screen, labels, {p.strip}, text_w, kLine);
    for (const auto& loop : sil) {
        std::vector<V2> px;
        for (V2 q : loop) px.push_back(fit.px(q));
        p.silhouette.push_back(px);
    }
    return p;
}

bool in_polygon(V2 p, const std::vector<V2>& poly) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) &&
            p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            in = !in;
    return in;
}

int node(const char* name) { return skel().find(name); }
V2 dot_at(const PickerScreen& s, const char* name) {
    for (const PickerDot& d : s.dots)
        if (d.node == node(name)) return d.p;
    return {-1e9, -1e9};
}

}  // namespace

TEST(picker_groups_name_real_bones) {
    std::map<std::string, size_t> size;
    for (const PickerGroup& g : picker_groups()) {
        CHECK(!g.bones.empty());
        CHECK(picker_group_nodes(skel(), g).size() == g.bones.size());  // every name is in the skeleton, once
        size[g.name] = g.bones.size();
    }
    CHECK(size["Head"] == 2 && size["Spine"] == 3);
    CHECK(size["Right Arm"] == 4 && size["Left Arm"] == 4);  // collar to hand
    CHECK(size["Right Leg"] == 5 && size["Left Leg"] == 5);
    CHECK(size["Left Index finger"] == 3 && size["Right Thumb"] == 3);
    CHECK(size["Right Hand"] == 15 && size["Knuckle row 2"] == 10);  // a row is that joint of every finger, both hands
    CHECK(size["Wings"] == 11 && size["Tail"] == 6 && size["Hind Limbs"] == 9 && size["Groin"] == 1);
    // Every face bone but the face root has a chip, and the chips take both sides.
    std::set<std::string> chips;
    for (const PickerGroup& g : picker_groups())
        if (g.kind == PickerGroupKind::Chip) chips.insert(g.bones.begin(), g.bones.end());
    for (int i = 0; i < skel().joint_count(); ++i) {
        const std::string& n = skel()[i].name;
        if (skel()[i].category == Category::Face && n != "mFaceRoot") CHECK(chips.count(n));
    }
    CHECK(chips.count("mFaceEyebrowOuterLeft") && chips.count("mFaceEyebrowOuterRight"));
}

TEST(picker_every_page_bone_has_a_dot_in_its_group) {
    const Pose rest(size_t(skel().size()));
    std::set<std::string> dotted;
    for (int p = 0; p < kPickerPageCount; ++p)
        for (int v = 0; v < picker_view_count(PickerPage(p)); ++v) {
            const Page pg = page_on(shapes().at("default"), PickerPage(p), v, picker_chart_pose(skel()));
            for (const PickerDot& d : pg.layout.dots) {
                dotted.insert(skel()[d.node].name);
                // Every dot but the hands' wrists belongs to a group of its page, which names it.
                if (skel()[d.node].name.rfind("mWrist", 0) == 0 && p == int(PickerPage::Hands)) continue;
                CHECK(d.group >= 0);
                if (d.group < 0) continue;
                const PickerGroup& g = picker_groups()[size_t(d.group)];
                CHECK(g.page == PickerPage(p));
                CHECK(std::find(g.bones.begin(), g.bones.end(), skel()[d.node].name) != g.bones.end());
            }
        }
    // Every group bone is somewhere on a page, except the inside of the mouth (the Mouth chip) and the face root.
    for (const PickerGroup& g : picker_groups())
        for (const std::string& b : g.bones)
            if (g.label != "Mouth") CHECK(dotted.count(b));
    (void)rest;
}

TEST(picker_left_and_right_as_the_avatar_faces_you) {
    const Shape& sh = shapes().at("default");
    const Pose chart = picker_chart_pose(skel());
    const Page front = page_on(sh, PickerPage::Body, 0, chart), back = page_on(sh, PickerPage::Body, 1, chart);
    CHECK(dot_at(front.screen, "mWristRight").x < dot_at(front.screen, "mWristLeft").x);  // its right on your left
    CHECK(dot_at(back.screen, "mWristRight").x > dot_at(back.screen, "mWristLeft").x);    // from behind, the other way
    CHECK(dot_at(front.screen, "mHead").y < dot_at(front.screen, "mPelvis").y);           // head up
    // Hands: the right hand on the left, both backs to you with the thumbs outward; the palms turn them inward.
    const Page hb = page_on(sh, PickerPage::Hands, 0, chart), hp = page_on(sh, PickerPage::Hands, 1, chart);
    for (const Page* h : {&hb, &hp}) {
        double right_max = -1e9, left_min = 1e9;
        for (const PickerDot& d : h->screen.dots) {
            const bool left = skel()[d.node].name.find("Left") != std::string::npos;
            (left ? left_min : right_max) = left ? std::min(left_min, d.p.x) : std::max(right_max, d.p.x);
        }
        CHECK(right_max < left_min);
    }
    CHECK(dot_at(hb.screen, "mHandThumb3Right").x < dot_at(hb.screen, "mHandPinky3Right").x);
    CHECK(dot_at(hp.screen, "mHandThumb3Right").x > dot_at(hp.screen, "mHandPinky3Right").x);
    CHECK(dot_at(hb.screen, "mHandMiddle3Left").y < dot_at(hb.screen, "mWristLeft").y);  // fingers up
    const Page face = page_on(sh, PickerPage::Face, 0, chart);
    CHECK(dot_at(face.screen, "mEyeRight").x < dot_at(face.screen, "mEyeLeft").x);
    CHECK(dot_at(face.screen, "mFaceForeheadCenter").y < dot_at(face.screen, "mFaceChin").y);
    // The eye and the mesh-head eye share a spot: a ring there, a second click takes the other.
    CHECK((dot_at(face.screen, "mEyeLeft") - dot_at(face.screen, "mFaceEyeAltLeft")).length() < 1);
}

TEST(picker_hands_follow_the_hand_not_the_arm) {
    // The Hands page shows the hand turned fingers up whatever the arm does: a raised arm leaves it unchanged.
    const Skeleton& s = skel();
    const Shape& sh = shapes().at("default");
    Pose raised = picker_chart_pose(s);
    raised.rot[size_t(node("mShoulderLeft"))] = Quat::axis_angle({1, 0, 0}, 1.2) * raised.rot[size_t(node("mShoulderLeft"))];
    raised.rot[size_t(node("mElbowLeft"))] = Quat::axis_angle({0, 0, 1}, 0.8);
    const Page a = page_on(sh, PickerPage::Hands, 0, picker_chart_pose(s)), b = page_on(sh, PickerPage::Hands, 0, raised);
    for (const char* n : {"mWristLeft", "mHandIndex3Left", "mHandThumb2Left"})
        CHECK((dot_at(a.screen, n) - dot_at(b.screen, n)).length() < 0.5);
}

TEST(picker_extreme_shapes_stay_in_the_canvas_and_apart) {
    const Pose chart = picker_chart_pose(skel());
    for (const auto& [name, shape] : shapes())
        for (int p = 0; p < kPickerPageCount; ++p)
            for (int v = 0; v < picker_view_count(PickerPage(p)); ++v) {
                const Page pg = page_on(shape, PickerPage(p), v, chart);
                const PickerRect area = picker_fit_area(PickerPage(p), pg.canvas, kLine, p == int(PickerPage::Face) ? 2 : 0);
                for (const PickerDot& d : pg.screen.dots) {
                    const bool in = PickerRect{area.x - 0.5, area.y - 0.5, area.w + 1, area.h + 1}.contains(d.p);
                    if (!in) std::fprintf(stderr, "  %s %s/%s: %s outside (%.1f %.1f in %.1f %.1f %.1f %.1f)\n", name.c_str(), picker_page_name(PickerPage(p)),
                                          picker_view_name(PickerPage(p), v), skel()[d.node].name.c_str(), d.p.x, d.p.y, area.x, area.y, area.w, area.h);
                    CHECK(in);
                }
                for (const PickerCap& c : pg.screen.caps) CHECK(pg.canvas.contains(c.tip));
                // Nothing under the backdrop and pose buttons in the top right corner.
                for (const PickerDot& d : pg.screen.dots) CHECK(!pg.strip.contains(d.p));
                for (const PickerCap& c : pg.screen.caps) CHECK(!pg.strip.contains(c.tip));
                for (const PickerLabel& l : pg.labels) CHECK(l.r.inside(pg.canvas));
                // Labels never cover each other.
                for (size_t i = 0; i < pg.labels.size(); ++i)
                    for (size_t j = i + 1; j < pg.labels.size(); ++j) CHECK(!pg.labels[i].r.overlaps(pg.labels[j].r));
                // Dots of different groups a comfortable click apart; within a group, a second click takes the next.
                double closest = 1e9;
                std::string pair;
                for (size_t i = 0; i < pg.screen.dots.size(); ++i)
                    for (size_t j = i + 1; j < pg.screen.dots.size(); ++j) {
                        const PickerDot &a = pg.screen.dots[i], &b = pg.screen.dots[j];
                        if (a.group == b.group && a.group >= 0) continue;
                        const double d = (a.p - b.p).length();
                        if (d < closest) closest = d, pair = skel()[a.node].name + " / " + skel()[b.node].name;
                    }
                if (closest < kMinSpacing)
                    std::fprintf(stderr, "  %s %s/%s: %s %.1f px apart\n", name.c_str(), picker_page_name(PickerPage(p)),
                                 picker_view_name(PickerPage(p), v), pair.c_str(), closest);
                CHECK(closest >= kMinSpacing);
            }
}

TEST(picker_silhouette_follows_the_proportions) {
    // Style B's outline is bent to the body: every Body dot and every hand joint lies inside it, even on long legs.
    const Pose chart = picker_chart_pose(skel());
    for (const auto& [name, shape] : shapes())
        for (auto [page, view] : {std::pair{PickerPage::Body, 0}, {PickerPage::Body, 1}, {PickerPage::Hands, 0},
                                  {PickerPage::Hands, 1}}) {
            const Page pg = page_on(shape, page, view, chart);
            CHECK(!pg.silhouette.empty());
            for (const PickerDot& d : pg.screen.dots) {
                bool in = false;
                for (const auto& loop : pg.silhouette) in = in || in_polygon(d.p, loop);
                if (!in) std::fprintf(stderr, "  %s %s: %s outside the silhouette\n", name.c_str(), picker_page_name(page),
                                      skel()[d.node].name.c_str());
                CHECK(in);
            }
        }
}

TEST(picker_hit_tests_dots_then_lines) {
    PickerScreen s;
    s.dots = {{1, {10, 10}, 0}, {2, {13, 10}, 1}, {3, {50, 50}, 2}};
    s.lines = {{3, {50, 50}, {90, 50}, 2}, {4, {10, 30}, {40, 30}, 3}};
    // Nearest dot first, then the other dot in reach.
    CHECK((picker_hits(s, {12, 10}, 6, 4) == std::vector<int>{2, 1}));
    CHECK((picker_hits(s, {10.5, 10}, 6, 4) == std::vector<int>{1, 2}));
    // On a line away from its dot: the line's bone; a dot and a line of the same bone count once.
    CHECK((picker_hits(s, {70, 52}, 6, 4) == std::vector<int>{3}));
    CHECK((picker_hits(s, {51, 50}, 6, 4) == std::vector<int>{3}));
    CHECK(picker_hits(s, {70, 60}, 6, 4).empty());
    CHECK((picker_hits(s, {12, 29}, 6, 4) == std::vector<int>{4}));
    // A fingertip circle.
    s.caps = {{7, {100, 100}, {0, -1}}};
    CHECK(picker_cap_hit(s, {103, 101}, 6) == 7 && picker_cap_hit(s, {110, 100}, 6) == -1);
}

TEST(picker_second_click_cycles_through_stacked_bones) {
    PickerCycle c;
    const std::vector<int> stack = {5, 6, 7};  // three dots on one spot (crossed arms)
    CHECK(c.click({20, 20}, stack) == 5);
    CHECK(c.click({21, 20}, stack) == 6);                   // again on the same spot: the one underneath
    CHECK(c.click({20, 21}, {6, 7, 5}) == 7);               // the same bones in another order still cycle
    CHECK(c.click({20, 20}, stack) == 5);                   // round again after the last
    CHECK(c.click({40, 20}, {9}) == 9);                     // elsewhere: starts over
    CHECK(c.click({20, 20}, stack) == 5);
    CHECK(c.click({20, 20}, {5, 6}) == 5);                  // other bones under it now: starts over
    CHECK(c.click({20, 20}, {}) == -1);
    // A real stack: crossed arms put the hands on the chest; clicking there reaches every bone under the cursor.
    PickerScreen s;
    s.dots = {{node("mWristRight"), {100, 80}, 2}, {node("mWristLeft"), {101, 81}, 4}, {node("mChest"), {101, 79}, 1}};
    std::set<int> reached;
    PickerCycle cc;
    for (int k = 0; k < 3; ++k) reached.insert(cc.click({100.5, 80}, picker_hits(s, {100.5, 80}, 7, 4)));
    CHECK(reached.size() == 3);
}

TEST(picker_bone_labels_read_as_body_parts) {
    CHECK(picker_bone_label("mKneeRight") == "Right Shin");
    CHECK(picker_bone_label("mElbowLeft") == "Left Forearm");
    CHECK(picker_bone_label("mHandIndex2Left") == "Left Index 2");
    CHECK(picker_bone_label("mFaceLipCornerRight") == "Right Lip Corner");
    CHECK(picker_bone_label("mWing3Left") == "Left Wing 3");
    CHECK(picker_bone_label("mTail4") == "Tail 4");
    CHECK(picker_bone_label("mHindLimbsRoot") == "Hind Limbs Root");
    CHECK(picker_bone_label("Left Pec") == "Left Pec");
}
