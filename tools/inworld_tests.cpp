// Writes the .anim files for the open in-world questions (docs/spec/README.md, "Needs an
// in-world test") plus instructions. Usage: vats_inworld_tests <data/character> <out dir>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "vats/anim_file.h"

using namespace vats;

namespace {

bool save(const std::string& path, const AnimFile& f) {
    auto bytes = write_anim(f);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    std::printf("%-40s %8zu bytes\n", path.c_str(), bytes.size());
    return bool(out);
}

AnimFile base(float duration) {
    AnimFile f;
    f.base_priority = 4;
    f.duration = duration;
    f.loop = 1;
    f.loop_in = 0;
    f.loop_out = duration;
    f.ease_in = f.ease_out = 0.2f;
    return f;
}

AnimJoint hold(const std::string& name, const Quat& q, float duration) {
    AnimJoint j;
    j.name = name;
    j.priority = -1;
    auto c = encode_rotation(q);
    j.rot.push_back({0, c[0], c[1], c[2]});
    j.rot.push_back({f32_to_u16(duration, 0.f, duration), c[0], c[1], c[2]});
    return j;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <data/character dir> <output dir>\n", argv[0]);
        return 2;
    }
    Skeleton skel;
    std::string err;
    if (!skel.load_dir(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::string out = argv[2];
    const float dur = 2.0f;
    const Quat chest_rest = skel[skel.find("Chest")].rest;

    // Test 1: does an attachment-point rotation key replace its default rotation, or add to it?
    AnimFile replace = base(dur);
    replace.joints.push_back(hold("Chest", chest_rest, dur));  // what VATs writes for "no change"
    AnimFile add = base(dur);
    add.joints.push_back(hold("Chest", Quat{}, dur));          // identity
    bool ok = save(out + "/1a_chest_rest.anim", replace) && save(out + "/1b_chest_identity.anim", add);

    // Test 2: upload size and joint limits. Every joint and attachment point, keyed every frame.
    for (float seconds : {2.f, 8.f, 20.f, 60.f}) {
        AnimFile big = base(seconds);
        const int frames = static_cast<int>(seconds * 30);
        for (int i = 0; i < skel.size(); ++i) {
            AnimJoint j;
            j.name = skel[i].name;
            for (int fr = 0; fr <= frames; ++fr) {
                // A slow wobble, different per joint, so no key is redundant.
                Quat q = skel[i].rest * euler_to_quat({3.0 * std::sin(0.1 * fr + i), 0, 0});
                auto c = encode_rotation(q);
                j.rot.push_back({f32_to_u16(fr / 30.f, 0.f, seconds), c[0], c[1], c[2]});
            }
            big.joints.push_back(std::move(j));
        }
        auto problems = validate_anim(big, skel, true);
        if (!problems.empty()) std::fprintf(stderr, "warning: %s\n", problems.front().c_str());
        char name[64];
        std::snprintf(name, sizeof name, "/2_size_%02ds_180joints.anim", static_cast<int>(seconds));
        ok = save(out + name, big) && ok;
    }

    std::ofstream readme(out + "/README.txt");
    readme << R"(VATs in-world tests. Upload on the Aditi beta grid (free uploads) or the main grid.
Upload each .anim as an animation and play it on yourself.

TEST 1 - attachment point rotation (1a_chest_rest.anim, 1b_chest_identity.anim)
  Rez a cone or an arrow-shaped prim, wear it on Chest, and note which way it points.
  Play 1a, then 1b, and watch the worn object.
    - If 1a leaves it unchanged and 1b turns it: a key REPLACES the point's default rotation
      (what VATs assumes).
    - If 1b leaves it unchanged and 1a turns it: a key is ADDED to the default rotation.
  Report which one moved, and roughly how (e.g. "1b tipped it 90 degrees to the left").

TEST 2 - upload limits (2_size_*.anim)
  Try uploading each file, smallest first. Report the first one that fails and the exact
  error message. Each file keys all 180 joints and attachment points on every frame at 30 fps.
)";
    return ok && readme ? 0 : 1;
}
