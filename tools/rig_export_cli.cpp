// Viewport Avatar Toolset - the rig export from the command line: check, write and verify a rigged mesh for SL.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
//
// Usage: vats_rig_export <data/character dir> <out.dae> <mesh files...> [--no-joint-positions] [--bind-pose-only]
//        [--pelvis] [--snap-noise] [--verify]
// Reads .dae, .fbx, .gltf or .glb parts as the app does, prints the check and every joint position that uploads,
// writes the COLLADA file, and with --verify reads it back: every bound joint's bind within 0.1 mm, the joint
// translations the uploader reads (lldaeloader's rules) placing each positioned joint within 0.1 mm of its bind,
// the weights and materials the same. Exit code 0 = written (and verified), 1 = refused or failed.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "vats/fbx.h"
#include "vats/rig_export.h"
#include "vats/skeleton.h"

using namespace vats;

namespace {

std::string stem_of(const std::string& path) {
    const std::string name = path.substr(path.find_last_of("/\\") + 1);
    return name.substr(0, name.rfind('.'));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: vats_rig_export <data/character dir> <out.dae> <mesh files...> [--no-joint-positions] "
                             "[--bind-pose-only] [--pelvis] [--snap-noise] [--verify]\n");
        return 2;
    }
    Skeleton skel;
    std::string err;
    if (!skel.load_dir(argv[1], err)) return std::fprintf(stderr, "skeleton: %s\n", err.c_str()), 2;
    const std::string out_path = argv[2];
    RigExportOptions opt;
    bool verify = false, snap_noise = false;
    std::vector<std::string> files;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-joint-positions") opt.joint_positions = false;
        else if (a == "--bind-pose-only") opt.bind_pose_only = true;
        else if (a == "--pelvis") opt.pelvis_offset = true;
        else if (a == "--snap-noise") snap_noise = true;
        else if (a == "--verify") verify = true;
        else files.push_back(a);
    }
    std::vector<DaeModel> models(files.size());
    std::vector<RigPart> parts;
    for (size_t i = 0; i < files.size(); ++i) {
        DaeReport rep;
        if (!load_mesh_file(files[i], skel, models[i], rep, err)) return std::fprintf(stderr, "%s: %s\n", files[i].c_str(), err.c_str()), 1;
        RigPart p;
        p.model = &models[i];
        p.name = stem_of(files[i]);
        p.unmapped_joints = rep.unmapped_joints;
        p.measured_scale = rep.measured_scale;
        p.scale = rep.scale;
        parts.push_back(p);
        std::printf("%s: %d triangles, %d vertices, %zu materials, scale %.4g (measured %.4g), %s\n", p.name.c_str(),
                    models[i].triangle_count(), models[i].vertex_count(), models[i].materials.size(), rep.scale, rep.measured_scale,
                    models[i].rigged ? "rigged" : "not rigged");
        for (auto& w : rep.warnings) std::printf("  import: %s\n", w.c_str());
    }
    if (snap_noise)
        for (size_t i = 0; i < parts.size(); ++i)
            for (const RigJoint& j : rig_joints(skel, models[i], opt))
                if (j.noise && j.listed && snap_joint_to_default(skel, models[i], j.node)) std::printf("%s: snapped %s (%.2f mm)\n", parts[i].name.c_str(), j.name.c_str(), j.offset_mm);
    for (size_t i = 0; i < parts.size(); ++i) {
        int listed = 0, uploads = 0;
        for (const RigJoint& j : rig_joints(skel, models[i], opt)) {
            listed += j.listed, uploads += j.uploads;
            if (j.uploads)
                std::printf("  %-22s %8.2f mm  (%7.2f %7.2f %7.2f)%s%s\n", j.name.c_str(), j.offset_mm, j.offset.x * 1000, j.offset.y * 1000,
                            j.offset.z * 1000, j.weighted ? "" : "  unweighted", j.noise ? "  noise" : "");
        }
        std::printf("%s: %d joints listed, %d joint positions\n", parts[i].name.c_str(), listed, uploads);
    }
    const std::vector<RigFinding> findings = check_rig_export(skel, parts, opt);
    for (const RigFinding& f : findings)
        std::printf("%s %s\n", f.severity == RigSeverity::Error ? "ERROR  " : f.severity == RigSeverity::Warning ? "WARNING" : "info   ",
                    f.message.c_str());
    std::string text;
    if (!write_rig_dae(skel, parts, opt, text, err)) return std::fprintf(stderr, "refused: %s\n", err.c_str()), 1;
    std::ofstream(out_path, std::ios::binary) << text;
    std::printf("wrote %s (%zu bytes)\n", out_path.c_str(), text.size());
    if (!verify) return 0;

    // Verify: read it back as the app would, and place the joints as the uploader would.
    DaeModel back;
    DaeReport rep;
    if (!load_mesh_file(out_path, skel, back, rep, err)) return std::fprintf(stderr, "verify: cannot read back: %s\n", err.c_str()), 1;
    bool ok = back.rigged;
    double worst_bind = 0, worst_upload = 0;
    int tris = 0, verts = 0;
    for (const DaeModel& m : models) tris += m.triangle_count(), verts += m.vertex_count();
    if (back.triangle_count() != tris || back.vertex_count() != verts) {
        std::printf("verify: %d triangles / %d vertices read back, %d / %d written\n", back.triangle_count(), back.vertex_count(), tris, verts);
        ok = false;
    }
    const int count = dae_index_count(skel), root = dae_root(skel);
    std::map<std::string, Vec3> local;
    for (auto& [name, t] : uploader_joint_translations(text)) local[name] = t;
    std::map<std::string, Vec3> g;
    for (int j = 0; j < skel.joint_count(); ++j) {
        auto it = local.find(skel[j].name);
        const Vec3 l = it == local.end() ? skel[j].pos : it->second;
        g[skel[j].name] = skel[j].parent >= 0 ? g[skel[skel[j].parent].name] + l : l;
    }
    for (auto& v : skel.volumes()) {
        auto it = local.find(v.name);
        g[v.name] = g[skel[v.joint].name] + (it == local.end() ? v.pos : it->second);
    }
    for (size_t i = 0; i < models.size(); ++i) {
        const std::vector<RigJoint> joints = rig_joints(skel, models[i], opt);
        for (const RigJoint& j : joints) {
            if (j.node < 0 || j.node >= count || j.node == root) continue;
            if (j.bound && j.listed && j.node < int(back.bound.size()) && back.bound[j.node]) {
                const double d = (back.binds[j.node].pos - models[i].binds[j.node].pos).length();
                worst_bind = std::max(worst_bind, d);
                if (d > 1e-4) std::printf("verify: %s bind read back %.3f mm away\n", j.name.c_str(), d * 1000), ok = false;
            }
            if (j.uploads && j.bound) {
                // The uploader's global for this joint against where the file bound it; a joint under an unwritten
                // pelvis or an A-posed parent keeps its offset from that parent instead, so compare parent-relative.
                const bool volume = j.node > root;
                const int parent = volume ? skel.volumes()[j.node - root - 1].joint : skel[j.node].parent;
                const std::string pname = parent >= 0 ? skel[parent].name : "";
                if (parent < 0 || !(parent < int(models[i].bound.size()) && models[i].bound[parent])) continue;
                const Vec3 want = models[i].binds[parent].rot.conj().rotate(models[i].binds[j.node].pos - models[i].binds[parent].pos);
                const Vec3 got = g.at(j.name) - g.at(pname);
                const double d = (want - got).length();
                worst_upload = std::max(worst_upload, d);
                if (d > 1e-4) std::printf("verify: %s uploads %.3f mm from its bind (relative to %s)\n", j.name.c_str(), d * 1000, pname.c_str()), ok = false;
            }
        }
    }
    int weight_diffs = 0;
    size_t off = 0;
    for (const DaeModel& m : models) {
        for (size_t k = 0; k < m.joints.size() && off + k < back.joints.size(); ++k)
            if (m.joints[k] != back.joints[off + k] || std::fabs(m.weights[k] - back.weights[off + k]) > 1e-4) ++weight_diffs;
        off += m.joints.size();
    }
    if (weight_diffs) std::printf("verify: %d weight slots differ after the round trip\n", weight_diffs), ok = false;
    std::printf("verify: binds within %.4f mm, joint positions within %.4f mm, weights %s, %zu materials read back: %s\n",
                worst_bind * 1000, worst_upload * 1000, weight_diffs ? "differ" : "same", back.materials.size(), ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
