// Add Community Folder... (08 CF): a community content folder's subfolders go onto the Inventory's folder lists
// through lib_add_folder, the same path Add Folder... takes.
#include <filesystem>
#include <random>

#include "check.h"
#include "vats/file_library.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("vats-community-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string str() const { return path.generic_string(); }
};

// The two lists the settings keep, filled the way draw_file_prompt fills them.
struct Lists {
    std::vector<std::string> projects, anims;
    int add(const std::string& root) {
        int n = 0;
        for (auto& [kind, dir] : community_folders(root)) n += lib_add_folder(kind == LibKind::Project ? projects : anims, dir);
        return n;
    }
};

}  // namespace

TEST(community_folder_lists_its_subfolders) {
    TempDir t;
    for (const char* sub : {"poses", "clips", "projects", "face-mapping"}) fs::create_directories(t.path / sub);
    Lists l;
    CHECK_EQ(l.add(t.str()), 3);
    CHECK(l.anims == (std::vector<std::string>{t.str() + "/poses/", t.str() + "/clips/"}));
    CHECK(l.projects == std::vector<std::string>{t.str() + "/projects/"});
    CHECK_EQ(l.add(t.str() + "/"), 0);  // added again: nothing new
}

TEST(community_folder_without_subfolders_is_listed_whole) {
    TempDir t;
    Lists l;
    CHECK_EQ(l.add(t.str()), 2);
    CHECK(l.projects == std::vector<std::string>{t.str() + "/"});
    CHECK(l.anims == std::vector<std::string>{t.str() + "/"});
    CHECK(community_folders("").empty());
}

TEST(lib_add_folder_normalises_and_skips_duplicates) {
    std::vector<std::string> list;
    CHECK(lib_add_folder(list, "C:\\anims"));
    CHECK(!lib_add_folder(list, "C:/anims/"));
    CHECK(!lib_add_folder(list, ""));
    CHECK(list == std::vector<std::string>{"C:/anims/"});
}
