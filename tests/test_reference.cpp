// Reference pictures (spec 08 RF): the project round trip, the sequence's picture per timeline frame, the numbered
// files of a sequence, the view lock and the quads.
#include <filesystem>
#include <fstream>

#include "check.h"
#include "vats/project.h"
#include "vats/reference.h"

using namespace vats;
namespace fs = std::filesystem;

TEST(reference_project_round_trip) {
    Project p;
    p.clip.end_frame = 60;
    Reference r;
    r.path = "refs/walk_0001.png";
    r.sequence = true;
    r.in_scene = true;
    r.view = ReferenceView::Right;
    r.opacity = 0.35, r.scale = 1.2, r.offset_x = -0.25, r.offset_y = 0.5;
    r.flip_x = true;
    r.frame_offset = -12, r.fps = 24;
    r.extra.set("future", 7);
    p.clip.reference = r;
    const std::string text = save_project(p);
    Project back;
    std::string err;
    CHECK(load_project(text, back, err));
    CHECK(back.clip.reference.has_value());
    CHECK(back.clip.reference == r);
    CHECK_EQ(save_project(back), text);

    // Absent stays absent; a bad field fails the load.
    p.clip.reference.reset();
    CHECK(load_project(save_project(p), back, err) && !back.clip.reference);
    std::string bad = text;
    bad.replace(bad.find("\"right\""), 7, "\"sideways\"");
    Project none;
    CHECK(!load_project(bad, none, err));
    CHECK(err.find("reference") != std::string::npos);
}

TEST(reference_sequence_index) {
    Reference r;
    r.fps = 30;
    CHECK_EQ(sequence_index(r, 0, 30, 0), -1);
    CHECK_EQ(sequence_index(r, 0, 30, 10), 0);
    CHECK_EQ(sequence_index(r, 7, 30, 10), 7);
    CHECK_EQ(sequence_index(r, 7.9, 30, 10), 7);
    CHECK_EQ(sequence_index(r, 50, 30, 10), 9);  // held on the last picture
    // A 24 fps sequence on a 30 fps timeline: frame 5 is 1/6 s, picture 4.
    r.fps = 24;
    CHECK_EQ(sequence_index(r, 5, 30, 100), 4);
    CHECK_EQ(sequence_index(r, 30, 30, 100), 24);  // one second in
    // An offset: the first picture shows on frame 10, and before it.
    r.fps = 30, r.frame_offset = 10;
    CHECK_EQ(sequence_index(r, 3, 30, 100), 0);
    CHECK_EQ(sequence_index(r, 10, 30, 100), 0);
    CHECK_EQ(sequence_index(r, 25, 30, 100), 15);
    // A negative offset skips the first pictures.
    r.frame_offset = -5;
    CHECK_EQ(sequence_index(r, 0, 30, 100), 5);
    // Half the timeline's rate: each picture holds two frames.
    r.frame_offset = 0, r.fps = 15;
    CHECK_EQ(sequence_index(r, 1, 30, 100), 0);
    CHECK_EQ(sequence_index(r, 2, 30, 100), 1);
}

TEST(reference_sequence_files) {
    const fs::path dir = fs::temp_directory_path() / "vats_test_reference";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (const char* n : {"walk_0010.png", "walk_0002.png", "walk_0001.png", "walk_0003.png", "run_0001.png", "walk_0004.jpg",
                          "walk_notes.png"})
        std::ofstream(dir / n) << "x";
    const auto files = sequence_files((dir / "walk_0003.png").string());
    CHECK_EQ(files.size(), size_t(4));
    if (files.size() == 4) {
        CHECK_EQ(fs::path(files[0]).filename().string(), std::string("walk_0001.png"));
        CHECK_EQ(fs::path(files[3]).filename().string(), std::string("walk_0010.png"));  // by number, not by text
    }
    const auto one = sequence_files((dir / "walk_notes.png").string());  // no digits: just the picture
    CHECK_EQ(one.size(), size_t(1));
    fs::remove_all(dir);
}

TEST(reference_view_lock_and_quads) {
    Reference r;
    CHECK(reference_shows(r, {0.3, -0.8, 0.2}));
    r.view = ReferenceView::Right;  // the camera on the avatar's right, -Y
    CHECK(reference_shows(r, {0, -1, 0}));
    CHECK(reference_shows(r, {0.2, -1, 0.1}));
    CHECK(!reference_shows(r, {1, 0, 0}));
    r.hidden = true;
    CHECK(!reference_shows(r, {0, -1, 0}));

    // Backdrop: a square picture in a 2:1 view at scale 1 fills the height and half the width, centred.
    Reference b;
    auto q = reference_backdrop_quad(b, 2, 1);
    CHECK_NEAR(q[0].x, -0.5, 1e-9);
    CHECK_NEAR(q[0].y, -1, 1e-9);
    CHECK_NEAR(q[2].x, 0.5, 1e-9);
    CHECK_NEAR(q[2].y, 1, 1e-9);
    b.flip_x = true;  // left and right swap
    q = reference_backdrop_quad(b, 2, 1);
    CHECK_NEAR(q[0].x, 0.5, 1e-9);
    CHECK_NEAR(q[1].x, -0.5, 1e-9);

    // In the scene, Front: 1.5 m behind the actor along -X, standing on the ground, 2 m tall.
    Reference s;
    s.in_scene = true;
    const auto w = reference_scene_quad(s, {0.2, 0, 1.0}, 0.5);
    CHECK_NEAR(w[0].x, 0.2 - 1.5, 1e-9);
    CHECK_NEAR(w[0].z, 0, 1e-9);
    CHECK_NEAR(w[2].z, 2, 1e-9);
    CHECK_NEAR(w[1].y - w[0].y, 1, 1e-9);  // 1 m wide; the camera at +X sees +Y on its right
}
