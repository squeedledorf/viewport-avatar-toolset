// Hostile and malformed inputs: every reader must return an error, never crash, hang or exhaust memory.
// Each case reproduces a defect found in review; the fuzz smoke test at the end mutates small valid
// inputs for every reader with a fixed seed.
#include <cstring>
#include <sstream>

#include "check.h"
#include "fixtures.h"
#include "vats/anim_convert.h"
#include "vats/anim_file.h"
#include "vats/bvh.h"
#include "vats/dae.h"
#include "vats/dynamics.h"
#include "vats/edit.h"
#include "vats/mocap.h"
#include "vats/project.h"
#include "vats/retarget.h"
#include "vats/rig.h"

using namespace vats;

namespace {

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

std::string base64(const std::vector<std::uint8_t>& in) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3) {
        std::uint32_t v = in[i] << 16 | (i + 1 < in.size() ? in[i + 1] << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
        out += abc[v >> 18 & 63];
        out += abc[v >> 12 & 63];
        out += i + 1 < in.size() ? abc[v >> 6 & 63] : '=';
        out += i + 2 < in.size() ? abc[v & 63] : '=';
    }
    return out;
}

std::vector<std::uint8_t> floats(const std::vector<float>& f) {
    std::vector<std::uint8_t> b(f.size() * 4);
    std::memcpy(b.data(), f.data(), b.size());
    return b;
}

// One node rotated by an animation whose key times are `times` (seconds). Accessor 0 = times, 1 = quats.
std::string gltf_anim(const std::vector<float>& times, const std::string& extra_nodes = "") {
    std::vector<float> data = times;
    for (size_t i = 0; i < times.size(); ++i) data.insert(data.end(), {0, 0, 0, 1});
    std::ostringstream o;
    const size_t n = times.size();
    o << R"({"asset":{"version":"2.0"},"nodes":[{"name":"hips"})" << extra_nodes << "],"
      << R"("buffers":[{"byteLength":)" << data.size() * 4 << R"(,"uri":"data:application/octet-stream;base64,)"
      << base64(floats(data)) << R"("}],)"
      << R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":)" << n * 4 << R"(},)"
      << R"({"buffer":0,"byteOffset":)" << n * 4 << R"(,"byteLength":)" << n * 16 << "}],"
      << R"("accessors":[{"bufferView":0,"componentType":5126,"count":)" << n << R"(,"type":"SCALAR"},)"
      << R"({"bufferView":1,"componentType":5126,"count":)" << n << R"(,"type":"VEC4"}],)"
      << R"("animations":[{"channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}}],)"
      << R"("samplers":[{"input":0,"output":1}]}]})";
    return o.str();
}

bool gltf(const std::string& text, std::string& err, SourceAnim* out = nullptr, const std::string& dir = ".") {
    SourceAnim tmp;
    return read_gltf_source(bytes(text), dir, out ? *out : tmp, err);
}

OscMessage bone_msg(const std::string& name) {
    OscMessage m;
    m.address = "/VMC/Ext/Bone/Pos";
    m.args.push_back({'s', 0, name});
    for (double v : {0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0}) m.args.push_back({'f', v, {}});
    return m;
}

const Rig& rig() {
    static Rig r(skel());
    return r;
}

}  // namespace

TEST(hostile_gltf_stride_wraps) {
    // 2049-byte buffer; a stride of 2^64 - 2048 used to wrap the bounds check and read before the buffer.
    std::string zeros;
    for (int i = 0; i < 683; ++i) zeros += "AAAA";
    for (const char* view : {R"("byteStride":18446744073709549568)", R"("byteOffset":-8)"}) {
        std::string t = std::string(R"({"nodes":[{"name":"a"}],"buffers":[{"byteLength":2049,"uri":"data:,)") + zeros +
                        R"("}],"bufferViews":[{"buffer":0,"byteLength":2049,)" + view +
                        R"(}],"accessors":[{"bufferView":0,"byteOffset":2044,"componentType":5126,"count":2,"type":"SCALAR"}],)"
                        R"("animations":[{"channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}}],)"
                        R"("samplers":[{"input":0,"output":0}]}]})";
        std::string err;
        CHECK(!gltf(t, err));
        CHECK(err.find("buffer") != std::string::npos);
    }
}

TEST(hostile_gltf_node_cycle) {
    std::string err;
    CHECK(!gltf(R"({"nodes":[{"children":[1]},{"children":[0]}],"animations":[]})", err));
    CHECK(err.find("cycle") != std::string::npos);
    CHECK(!gltf(R"({"nodes":[{"children":[0]}],"animations":[]})", err));  // its own child
    CHECK(err.find("cycle") != std::string::npos);
}

TEST(hostile_gltf_huge_duration) {
    std::string err;
    SourceAnim src;
    CHECK(gltf(gltf_anim({0.f, 3e9f}), err, &src));  // used to overflow int and throw length_error
    CHECK_EQ(src.frames(), 30 * 600 + 1);             // capped at 10 minutes
    CHECK(gltf(gltf_anim({0.f, std::nanf("")}), err, &src));
    CHECK_EQ(src.frames(), 1);
}

TEST(hostile_gltf_allocation_bombs) {
    std::string err;
    // count 50 million MAT4 with no bufferView used to allocate 3 GB of zeros.
    CHECK(!gltf(R"({"nodes":[{"name":"a"}],"accessors":[{"componentType":5126,"count":50000000,"type":"MAT4"}],)"
                R"("animations":[{"channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}}],)"
                R"("samplers":[{"input":0,"output":0}]}]})",
                err));
    // 20,000 joints for 10 minutes would be 20 GB of samples.
    std::string many;
    for (int i = 0; i < 20000; ++i) many += R"(,{"name":"n)" + std::to_string(i) + "\"}";
    CHECK(!gltf(gltf_anim({0.f, 600.f}, many), err));
    CHECK(err.find("too large") != std::string::npos);
}

TEST(hostile_gltf_buffer_uri_escape) {
    for (const char* uri : {"../dev/zero", "/etc/passwd", "../../../../../../dev/zero", "a/../../x.bin"}) {
        std::string t = std::string(R"({"nodes":[{"name":"a"}],"buffers":[{"byteLength":4,"uri":")") + uri +
                        R"("}],"animations":[{"channels":[],"samplers":[]}]})";
        std::string err;
        CHECK(!gltf(t, err, nullptr, "/tmp"));
        CHECK(err.find("beside") != std::string::npos);
    }
}

TEST(hostile_bvh_no_channels_and_huge_frame_time) {
    const std::string hier = "HIERARCHY\nROOT hip\n{\n OFFSET 0 0 0\n CHANNELS 0\n End Site\n {\n  OFFSET 0 1 0\n }\n}\n";
    std::string err;
    SourceAnim src;
    CHECK(!read_bvh_source(hier + "MOTION\nFrames: 1000000\nFrame Time: 0.0333\n", src, err));
    CHECK(err.find("channels") != std::string::npos);
    BvhImportResult r = import_bvh(skel(), hier + "MOTION\nFrames: 1000000\nFrame Time: 0.0333\n", {});
    CHECK(!r.ok && !r.error.empty());

    const std::string pelvis = "HIERARCHY\nROOT hip\n{\n OFFSET 0 0 0\n CHANNELS 3 Zrotation Xrotation Yrotation\n}\n";
    CHECK(!read_bvh_source(pelvis + "MOTION\nFrames: 2\nFrame Time: 10000000\n0 0 0\n0 0 0\n", src, err));
    CHECK(read_bvh_source(pelvis + "MOTION\nFrames: 2\nFrame Time: 0.0333\n0 0 0\n0 0 0\n", src, err));
}

TEST(hostile_vmc_unknown_names_and_loaded) {
    VmcState s;
    for (int i = 0; i < 3000; ++i) CHECK(!apply_vmc(bone_msg("junk" + std::to_string(i)), s));
    CHECK(s.bones.empty());
    CHECK(apply_vmc(bone_msg("Hips"), s));
    CHECK_EQ(s.bones.size(), size_t(1));

    OscMessage ok;
    ok.address = "/VMC/Ext/OK";
    ok.args.push_back({'d', 1e300, {}});
    CHECK(apply_vmc(ok, s));
    CHECK_EQ(s.loaded, 0);
    ok.args[0].num = 1;
    CHECK(apply_vmc(ok, s));
    CHECK_EQ(s.loaded, 1);
}

TEST(hostile_dynamics_overlapping_unbake) {
    Clip c;
    c.end_frame = 30;
    c.loop_out = 30;
    key_euler(c, "mPelvis", 0, {0, 0, 0});
    key_euler(c, "mPelvis", 10, {0, 0, 45});
    key_euler(c, "mTail2", 0, {0, 10, 0});
    key_euler(c, "mTail2", 20, {0, -10, 0});
    const Track original = c.curves.at("mTail2");
    c.dynamics.push_back(dyn_preset("tail", "mTail1", 3));
    c.dynamics.push_back(dyn_preset("tail", "mTail2", 2));
    bake_dynamics(c, rig(), nullptr);
    unbake_dynamics(c, skel(), 0);
    unbake_dynamics(c, skel(), 1);
    CHECK(c.curves.count("mTail2") && c.curves.at("mTail2") == original);
    CHECK(!c.curves.count("mTail1") && !c.curves.count("mTail3"));
}

TEST(hostile_project_actor_clip_fields_and_single_actor) {
    const std::string two = R"({"format":"vats-project","version":2,"fps":30,"end_frame":10,"active":0,"actors":[)"
                            R"({"name":"A"},{"name":"B","clip":{"fps":30,"end_frame":10,"future_field":{"x":1}}}]})";
    Project p;
    std::string err;
    CHECK(load_project(two, p, err));
    CHECK(save_project(p).find("future_field") != std::string::npos);

    Project q;
    CHECK(load_project(R"({"format":"vats-project","version":2,"actors":[{"name":"A"}],"active":0})", q, err));
    CHECK(q.actors.empty());
    CHECK_EQ(q.active, 0);
}

TEST(hostile_rig_table_empty_mirror) {
    RigTable t;
    std::string err;
    CHECK(parse_rig_table(R"({"name":"x","bones":{"mShoulderLeft":["LeftArm"]},"mirror":[["","R"],["Left","Right"]]})",
                          t, err));
    CHECK(t.bones.count("mShoulderRight") && t.bones.at("mShoulderRight").at(0) == "RightArm");
}

// Seeded mutations of small valid inputs for every reader; passing means nothing crashed or hung.
TEST(hostile_fuzz_smoke) {
    std::uint64_t seed = 0x5eed5eed5eedULL;
    auto rnd = [&](std::uint64_t n) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return n ? (seed >> 33) % n : 0;
    };
    auto mutate = [&](std::string s) {
        for (int k = 0, m = 1 + int(rnd(4)); k < m && !s.empty(); ++k) {
            size_t at = rnd(s.size());
            switch (rnd(5)) {
                case 0: s[at] = char(rnd(256)); break;
                case 1: s.insert(at, 1, "0123456789-.e\"{}[],"[rnd(19)]); break;
                case 2: s.erase(at, 1 + rnd(8)); break;
                case 3: s.insert(at, s.substr(at, rnd(16))); break;
                case 4: s.resize(at); break;
            }
        }
        return s;
    };
    Clip small;
    small.end_frame = 4;
    small.loop_out = 4;
    key_euler(small, "mPelvis", 0, {0, 0, 0});
    key_euler(small, "mPelvis", 4, {0, 0, 30});
    const auto anim_bytes = write_anim(export_anim(skel(), small).file);
    const std::string anim(anim_bytes.begin(), anim_bytes.end());
    Project proj;
    proj.clip = small;
    const std::string project = save_project(proj);
    const std::string bvh = "HIERARCHY\nROOT hip\n{\n OFFSET 0 0 0\n CHANNELS 6 Xposition Yposition Zposition Zrotation "
                            "Xrotation Yrotation\n JOINT abdomen\n {\n  OFFSET 0 5 0\n  CHANNELS 3 Zrotation Xrotation "
                            "Yrotation\n  End Site\n  {\n   OFFSET 0 5 0\n  }\n }\n}\nMOTION\nFrames: 2\nFrame Time: "
                            "0.0333\n0 40 0 0 0 0 0 0 0\n1 40 0 5 0 0 10 0 0\n";
    const std::string gl = gltf_anim({0.f, 0.5f, 1.f});
    const std::string rokoko =
        R"({"version":3,"scene":{"actors":[{"name":"a","body":{"hip":{"position":{"x":0,"y":1,"z":0},)"
        R"("rotation":{"x":0,"y":0,"z":0,"w":1}}},"meta":{"hasFace":true},"face":{"jawOpen":40}}]}})";
    const std::string vts = R"({"FaceFound":true,"Rotation":{"x":1,"y":2,"z":3},"EyeLeft":{"x":1,"y":2,"z":3},)"
                            R"("EyeRight":{"x":1,"y":2,"z":3},"BlendShapes":[{"k":"JawOpen","v":0.5}]})";
    std::string llf(4 + 37 + 4 + 6 + 16, '\0');  // version, id, name length, name, frame time and rate
    llf[44] = 6;
    llf += char(61);
    llf += std::string(61 * 4, '\0');  // 61 zero floats
    std::string osc;
    {
        OscMessage m = bone_msg("Hips");
        (void)m;  // the OSC seed is a hand-made bundle below
        const char raw[] = "#bundle\0\0\0\0\0\0\0\0\1\0\0\0\x14/VMC/Ext/OK\0,i\0\0\0\0\0\1";
        osc.assign(raw, sizeof raw - 1);
    }
    const std::string dae = R"(<?xml version="1.0"?><COLLADA><library_geometries><geometry id="g"><mesh>)"
                            R"(<source id="p"><float_array count="9">0 0 0 1 0 0 0 1 0</float_array><technique_common>)"
                            R"(<accessor source="#p" count="3" stride="3"/></technique_common></source>)"
                            R"(<vertices id="v"><input semantic="POSITION" source="#p"/></vertices>)"
                            R"(<triangles count="1"><input semantic="VERTEX" source="#v" offset="0"/><p>0 1 2</p>)"
                            R"(</triangles></mesh></geometry></library_geometries><library_visual_scenes><visual_scene>)"
                            R"(<node><instance_geometry url="#g"/></node></visual_scene></library_visual_scenes></COLLADA>)";
    for (int i = 0; i < 3000; ++i) {
        std::string err;
        SourceAnim src;
        AnimFile f;
        Project p;
        VmcState vs;
        DaeModel dm;
        DaeReport dr;
        std::string actor;
        std::vector<OscMessage> msgs;
        std::string g = mutate(gl);
        read_gltf_source(bytes(g), ".", src, err);
        read_bvh_source(mutate(bvh), src, err);
        import_bvh(skel(), mutate(bvh), {});
        auto a = bytes(mutate(anim));
        parse_anim(a, f, err);
        load_project(mutate(project), p, err);
        std::string r = mutate(rokoko);
        apply_rokoko(reinterpret_cast<const std::uint8_t*>(r.data()), r.size(), vs, "", actor, err);
        apply_vts(mutate(vts), vs);
        std::string l = mutate(llf);
        apply_live_link_face(reinterpret_cast<const std::uint8_t*>(l.data()), l.size(), vs);
        std::string o = mutate(osc);
        if (parse_osc(reinterpret_cast<const std::uint8_t*>(o.data()), o.size(), msgs))
            for (auto& m : msgs) apply_vmc(m, vs);
        if (i % 4 == 0) load_dae(mutate(dae), ".", skel(), dm, dr, err);
    }
    CHECK(true);  // reaching here is the test
}
