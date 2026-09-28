// The Inventory's Projects and Animations folders: scanning, the metadata cache, bad files and safe file operations.
#include <filesystem>
#include <fstream>
#include <random>

#include "check.h"
#include "vats/anim_file.h"
#include "vats/file_library.h"
#include "vats/project.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("vats-lib-" + std::to_string(std::random_device{}()));
        fs::create_directories(path / "lib");
        fs::create_directories(path / "outside");
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string lib() const { return path.generic_string() + "/lib/"; }
    std::string outside() const { return path.generic_string() + "/outside/"; }
};

void write(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary);
    f << bytes;
}

std::string anim_bytes(float seconds, int priority, bool loop) {
    AnimFile a;
    a.duration = seconds, a.base_priority = priority, a.loop = loop, a.loop_out = seconds;
    AnimJoint j;
    j.name = "mHead";
    j.priority = priority;
    j.rot = {{0, 32767, 32767, 32767}, {65535, 32767, 32767, 32767}};
    j.pos = {{0, 32767, 32767, 32767}};
    a.joints.push_back(j);
    std::vector<std::uint8_t> b = write_anim(a);
    return std::string(b.begin(), b.end());
}

std::string project_text(int fps, int end, int actors) {
    Project p;
    p.clip.fps = fps, p.clip.end_frame = end, p.clip.priority = 4, p.clip.loop = true;
    if (actors > 1) {
        p.actors.resize(actors);
        p.actors[1].name = "Second";
    }
    return save_project(p);
}

}  // namespace

TEST(library_scan_reads_metadata_and_bad_files) {
    TempDir t;
    write(t.lib() + "walk.anim", anim_bytes(2.5f, 4, true));
    write(t.lib() + "Broken.anim", "not an animation");
    write(t.lib() + "couple.vat", project_text(24, 48, 2));
    write(t.lib() + "notes.txt", "ignored");
    fs::create_directories(t.lib() + "sub.anim");  // a folder with the extension is not a file

    LibScanner s;
    std::vector<LibFile> anims = s.scan(t.lib(), LibKind::Anim);
    CHECK(anims.size() == 2);
    if (anims.size() != 2) return;
    CHECK(anims[0].name == "Broken");  // sorted ignoring case
    CHECK(!anims[0].error.empty());
    CHECK(anims[1].name == "walk" && anims[1].error.empty());
    CHECK_NEAR(anims[1].seconds, 2.5, 1e-6);
    CHECK(anims[1].priority == 4 && anims[1].loop && anims[1].keys == 3);

    std::vector<LibFile> projects = s.scan(t.lib(), LibKind::Project);
    CHECK(projects.size() == 1);
    if (projects.size() != 1) return;
    CHECK(projects[0].error.empty());
    CHECK(projects[0].fps == 24 && projects[0].actors == 2 && projects[0].priority == 4 && projects[0].loop);
    CHECK_NEAR(projects[0].seconds, 2.0, 1e-9);

    // A changed file is read again; an unchanged one comes from the cache.
    write(t.lib() + "walk.anim", anim_bytes(10.f, 2, false) + std::string(7, '\0'));
    anims = s.scan(t.lib(), LibKind::Anim);
    CHECK(anims.size() == 2 && anims[1].priority == 2 && !anims[1].loop);

    CHECK(s.scan(t.lib() + "missing/", LibKind::Anim).empty());  // no folder: no items, no error
}

TEST(library_safe_names) {
    CHECK(lib_safe_name("../../etc/passwd") == "etcpasswd");
    CHECK(lib_safe_name("  My Walk  ") == "My Walk");
    CHECK(lib_safe_name("a:b*c?\"<>|\\d") == "abcd");
    CHECK(lib_safe_name("...") == "");
    CHECK(lib_safe_name("con") == "con_");
    CHECK(lib_safe_name(std::string(300, 'x')).size() == 100);
}

TEST(library_rename_duplicate_delete) {
    TempDir t;
    const std::vector<std::string> roots{t.lib()};
    const std::string a = t.lib() + "a.anim", b = t.lib() + "b.anim";
    write(a, anim_bytes(1, 3, false));
    write(b, anim_bytes(1, 3, false));
    std::string out, err;

    CHECK(!lib_rename(roots, a, "b", out, err));  // taken
    CHECK(!err.empty());
    CHECK(lib_rename(roots, a, "../outside/c", out, err));  // the name cannot leave the folder
    CHECK(out == t.lib() + "outsidec.anim" && fs::exists(out) && !fs::exists(a));

    CHECK(lib_duplicate(roots, b, out, err) && out == t.lib() + "b copy.anim");
    CHECK(lib_duplicate(roots, b, out, err) && out == t.lib() + "b copy 2.anim");
    CHECK(fs::file_size(out) == fs::file_size(b));

    CHECK(lib_delete(roots, t.lib() + "b copy.anim", err) && !fs::exists(t.lib() + "b copy.anim"));
}

TEST(library_refuses_paths_outside) {
    TempDir t;
    const std::vector<std::string> roots{t.lib()};
    const std::string victim = t.outside() + "keep.anim";
    write(victim, anim_bytes(1, 3, false));
    std::string out, err;
    CHECK(!lib_contains(roots, victim));
    CHECK(!lib_delete(roots, victim, err) && fs::exists(victim));
    CHECK(!lib_delete(roots, t.lib() + "../outside/keep.anim", err) && fs::exists(victim));
    CHECK(!lib_rename(roots, victim, "x", out, err) && fs::exists(victim));
    CHECK(!lib_duplicate(roots, victim, out, err));
    CHECK(!lib_delete(roots, t.lib(), err) && fs::exists(t.lib()));  // not the folder itself
    CHECK(!lib_delete({}, victim, err));
    write(t.lib() + "x.anim", "x");
    CHECK(lib_contains(roots, t.lib() + "./x.anim"));
}
