// Batch retarget and saved mappings (spec 07 RT-12..RT-14): a temp folder of BVH files through the whole
// pipeline, the report rows checked.
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_file.h"
#include "vats/batch_retarget.h"
#include "vats/bvh.h"
#include "vats/project.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path = fs::temp_directory_path() / ("vats-batch-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const std::string& name) const { return (path / name).generic_string(); }
};

std::string read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write(const std::string& path, const std::string& bytes) { std::ofstream(path, std::ios::binary) << bytes; }

const BatchWrite kWrite = [](const std::string& path, const std::string& data, std::string& why) {
    std::ofstream f(path, std::ios::binary);
    f << data;
    if (!f) why = "cannot write";
    return bool(f);
};

std::string tables_dir() { return std::string(VATS_DATA_DIR) + "/../retarget"; }

// The folder: the Mixamo walk (tests/data/make_mixamo_walk.py) twice, a BVH on SL's own joint names (no rig
// table has them), a broken BVH and a file that is not an animation.
void fill(const TempDir& dir) {
    const std::string walk = read(std::string(VATS_TEST_FILES) + "/mixamo_walk.bvh");
    write(dir.file("walk.bvh"), walk);
    write(dir.file("b_walk.bvh"), walk);
    Clip c;
    c.end_frame = 10;
    c.curves["mHead"]["rot_x"].set_key(0, 0);
    c.curves["mHead"]["rot_x"].set_key(10, 20);
    write(dir.file("sl_names.bvh"), export_bvh(skel(), c).text);
    write(dir.file("broken.bvh"), "HIERARCHY\nROOT");
    write(dir.file("readme.txt"), "not an animation");
}

}  // namespace

TEST(batch_retarget_folder_to_projects) {
    TempDir dir;
    fill(dir);
    BatchRetargetOptions opt;
    opt.tables = load_rig_tables(tables_dir());
    CHECK(opt.tables.size() >= 5);
    BatchReport rep = batch_retarget(skel(), Rig(skel()), dir.path.generic_string(), opt, kWrite);
    CHECK(rep.mixamo);
    CHECK_EQ(rep.out_dir, dir.file("retargeted"));
    CHECK_EQ(rep.rows.size(), size_t(4));  // readme.txt is not read
    if (rep.rows.size() != 4) return;
    CHECK_EQ(rep.rows[0].file, std::string("b_walk.bvh"));  // sorted by name
    CHECK_EQ(rep.rows[1].file, std::string("broken.bvh"));
    CHECK_EQ(rep.rows[2].file, std::string("sl_names.bvh"));
    CHECK_EQ(rep.rows[3].file, std::string("walk.bvh"));

    for (int i : {0, 3}) {
        const BatchRow& r = rep.rows[size_t(i)];
        CHECK(r.fits);
        CHECK(r.bytes > 1000 && r.bytes < kAnimMaxUploadBytes);
        CHECK_EQ(r.frames, 61);
        CHECK_EQ(r.fps, 30);
        CHECK(r.notes.rfind("Mixamo rig", 0) == 0);
        Project p;
        std::string err;
        CHECK(load_project(read(dir.file("retargeted/" + r.output)), p, err));
        CHECK(p.clip.curves.count("mPelvis") && p.clip.curves.count("mElbowLeft"));
    }
    CHECK_EQ(rep.rows[3].output, std::string("walk.vat"));
    CHECK(rep.rows[1].output.empty() && !rep.rows[1].notes.empty() && !rep.rows[1].fits);
    CHECK(rep.rows[2].output.empty());
    CHECK_EQ(rep.rows[2].notes, std::string("no rig table matches its bone names"));
    CHECK(!fs::exists(dir.path / "retargeted" / "broken.vat"));
}

TEST(batch_retarget_to_anim_and_with_a_chosen_table) {
    TempDir dir;
    fill(dir);
    BatchRetargetOptions opt;
    opt.tables = load_rig_tables(tables_dir());
    opt.anim = true;
    BatchReport rep = batch_retarget(skel(), Rig(skel()), dir.path.generic_string(), opt, kWrite);
    CHECK_EQ(rep.rows.size(), size_t(4));
    if (rep.rows.size() != 4) return;
    const BatchRow& r = rep.rows[3];
    CHECK_EQ(r.output, std::string("walk.anim"));
    const std::string bytes = read(dir.file("retargeted/walk.anim"));
    CHECK_EQ(bytes.size(), r.bytes);  // the report's size is the written file's
    AnimFile a;
    std::string err;
    CHECK(parse_anim(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), a, err));
    CHECK(validate_anim(a, skel(), true).empty());

    // A table that does not fit the rig: the files are listed with what it leaves unmapped.
    for (int i = 0; i < int(opt.tables.size()); ++i)
        if (opt.tables[size_t(i)].name == "Unreal Mannequin") opt.table = i;
    CHECK(opt.table >= 0);
    rep = batch_retarget(skel(), Rig(skel()), dir.path.generic_string(), opt, kWrite);
    CHECK(rep.rows.size() == 4 && rep.rows[3].output.empty());
    CHECK(rep.rows.size() == 4 && rep.rows[3].notes.rfind("Unreal Mannequin does not map ", 0) == 0);
}

TEST(saved_mapping_reads_back_as_the_same_map) {
    SourceAnim src;
    std::string err;
    CHECK(read_source_file(std::string(VATS_TEST_FILES) + "/mixamo_walk.bvh", src, err));
    CHECK(is_mixamo(src));
    BoneMap map;
    const std::vector<RigTable> tables = load_rig_tables(tables_dir());
    CHECK(best_rig_table(tables, src, map) >= 0);
    map.erase("mKneeRight");  // one side unmapped: the saved table must not copy the left side's bone
    map.erase("mNeck");
    RigTable t;
    CHECK(parse_rig_table(rig_table_json("My rig", map, src), t, err));
    CHECK_EQ(t.name, std::string("My rig"));
    BoneMap back;
    apply_rig_table(t, src, back);
    CHECK(back == map);
    CHECK(!back.count("mKneeRight") && back.count("mKneeLeft"));

    SourceAnim plain = src;
    for (auto& j : plain.joints) j.name = j.name.substr(j.name.find(':') + 1);
    CHECK(!is_mixamo(plain));
}
