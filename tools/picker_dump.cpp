// Viewport Avatar Toolset - dumps a posed avatar for picker design work: joint positions and the skinned body mesh
// with each vertex's main joint, as JSON, and optionally the pose as a project the app can open.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
//
// Usage: vats_picker_dump <data/character dir> <out.json> [--vat out.vat] [--pose <slug>]... [--mirror-pose <slug>]...
//                         [--bone <name> <x> <y> <z>]... [--hip <x> <y> <z>]
//        vats_picker_dump <data/character dir> <out.json> --silhouette
// --silhouette writes what tools/picker_silhouette.py traces style B's outlines from: for every picker page view, the
// Linden body in the chart pose (SL default shape) carried into page space, as the picker's own maths place it
// (picker_parts, picker_anchors): {"views": [{"page": 0, "view": 0, "anchors": [[name, ax, ay, bx, by], ...],
// "tris": [x, y, ...]}]}, three (x, y) page points per skin triangle kept by the part's clip. The Face view keeps 3 cm
// more neck and says where its cut is ("cut": page z), so the outline can end in a straight line there.
// Poses are built-in slugs, applied in order at frame 0; --mirror-pose applies one mirrored (a hand pose on the right
// hand); --bone keys one bone's Euler degrees after them, --hip the pelvis's offset in metres (to move the body in
// front of a fixed camera). The body is the SL default female shape the app starts with.
// JSON: {"joints": {name: [x, y, z]}, "tips": {name: [x, y, z]},"positions": [x, y, z, ...], "faces": [a, b, c, ...], "main": [node, ...],
// "nodes": [name, ...]}, SL world space (X forward, Y left, Z up), metres.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "vats/avatar_mesh.h"
#include "vats/clip.h"
#include "vats/picker.h"
#include "vats/pose_ops.h"
#include "vats/pose_presets.h"
#include "vats/project.h"
#include "vats/skeleton.h"

using namespace vats;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: vats_picker_dump <character dir> <out.json> [--vat f] [--pose slug] "
                             "[--mirror-pose slug] [--bone name x y z] [--hip x y z]\n");
        return 2;
    }
    const std::string dir = std::string(argv[1]) + "/";
    std::string err;
    Skeleton skel;
    AvatarMesh mesh;
    if (!skel.load_dir(dir, err) || !mesh.load(skel, dir, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    mesh.build(Body::SLDefault);
    const Shape* shape = mesh.shape(Body::SLDefault);

    if (argc == 4 && std::string(argv[3]) == "--silhouette") {
        const std::vector<Xform> chart = skel.global_pose(picker_chart_pose(skel), shape);
        std::vector<float> pos, nrm;
        mesh.skin(chart, shape, pos, nrm);
        std::ostringstream o;
        o << "{\"views\":[";
        bool first_view = true;
        for (int page = 0; page < kPickerPageCount; ++page)
            for (int view = 0; view < picker_view_count(PickerPage(page)); ++view) {
                const auto parts = picker_parts(skel, chart, chart, shape, PickerPage(page), view);
                o << (first_view ? "" : ",") << "{\"page\":" << page << ",\"view\":" << view << ",\"anchors\":[";
                first_view = false;
                const auto anchors = picker_anchors(skel, chart, parts, shape, PickerPage(page), view);
                for (size_t k = 0; k < anchors.size(); ++k)
                    o << (k ? "," : "") << "[\"" << skel[anchors[k].node].name << "\"," << anchors[k].a.x << ','
                      << anchors[k].a.y << ',' << anchors[k].b.x << ',' << anchors[k].b.y << ']';
                o << "],\"tris\":[";
                bool first = true;
                for (const MeshPart& part : mesh.parts()) {
                    if (part.material != Material::Skin || part.name.find("lash") != std::string::npos) continue;
                    for (std::uint32_t k = part.first_index; k + 2 < part.first_index + part.index_count; k += 3)
                        for (const PickerPart& pp : parts) {
                            Vec3 q[3], c;
                            for (int v = 0; v < 3; ++v) {
                                const std::uint32_t i = mesh.indices()[k + std::uint32_t(v)];
                                q[v] = pp.to_page.apply({pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]});
                                c += q[v] * (1.0 / 3);
                            }
                            // The face keeps a little more below its cut, which the tracing makes a straight line.
                            const bool face = page == int(PickerPage::Face);
                            if (!picker_keeps(pp, c) && !(face && picker_keeps(pp, c + Vec3{0, 0, 0.03}))) continue;
                            for (int v = 0; v < 3; ++v) o << (first ? "" : ",") << q[v].y << ',' << q[v].z, first = false;
                        }
                }
                o << "]";
                if (page == int(PickerPage::Face) && !parts.empty()) o << ",\"cut\":" << parts[0].lo.z;
                o << "}";
            }
        o << "]}\n";
        std::ofstream(argv[2]) << o.str();
        return 0;
    }

    Clip clip;
    std::string vat;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--vat" && i + 1 < argc) {
            vat = argv[++i];
        } else if ((a == "--pose" || a == "--mirror-pose") && i + 1 < argc) {
            const std::string id = std::string("builtin:") + argv[++i];
            bool found = false;
            for (const LibraryItem& it : builtin_poses(skel))
                if (it.id == id) apply_pose(clip, skel, it, 0, a == "--mirror-pose"), found = true;
            if (!found) return std::fprintf(stderr, "no built-in pose %s\n", argv[i]), 1;
        } else if (a == "--bone" && i + 4 < argc) {
            LibraryItem it;
            it.kind = "pose";
            it.bones[argv[i + 1]] = {std::stod(argv[i + 2]), std::stod(argv[i + 3]), std::stod(argv[i + 4])};
            apply_pose(clip, skel, it, 0, false);
            i += 4;
        } else if (a == "--hip" && i + 3 < argc) {
            LibraryItem it;
            it.kind = "pose";
            it.hip = Vec3{std::stod(argv[i + 1]), std::stod(argv[i + 2]), std::stod(argv[i + 3])};
            apply_pose(clip, skel, it, 0, false);
            i += 3;
        } else {
            return std::fprintf(stderr, "unknown argument %s\n", a.c_str()), 2;
        }
    }

    const std::vector<Xform> globals = skel.global_pose(evaluate_curves(skel, clip, 0), shape);
    std::vector<float> pos, nrm;
    mesh.skin(globals, shape, pos, nrm);

    std::ostringstream o;
    o << "{\"joints\":{";
    for (int i = 0; i < skel.size(); ++i) {
        const Vec3 p = globals[i].pos;
        o << (i ? "," : "") << '"' << skel[i].name << "\":[" << p.x << ',' << p.y << ',' << p.z << ']';
    }
    o << "},\"tips\":{";  // each bone's tail: where a face bone meets the feature it moves (the pivots stack up)
    for (int i = 0; i < skel.size(); ++i) {
        const Vec3 p = globals[i].apply(skel[i].end);
        o << (i ? "," : "") << '"' << skel[i].name << "\":[" << p.x << ',' << p.y << ',' << p.z << ']';
    }
    o << "},\"nodes\":[";
    for (int i = 0; i < skel.size(); ++i) o << (i ? "," : "") << '"' << skel[i].name << '"';
    o << "],\"positions\":[";
    for (size_t i = 0; i < pos.size(); ++i) o << (i ? "," : "") << pos[i];
    o << "],\"faces\":[";
    bool first = true;
    for (const MeshPart& part : mesh.parts()) {
        if (part.material != Material::Skin || part.name.find("lash") != std::string::npos) continue;
        for (std::uint32_t k = part.first_index; k < part.first_index + part.index_count; ++k)
            o << (first ? "" : ",") << mesh.indices()[k], first = false;
    }
    o << "],\"main\":[";
    const std::vector<Influence>& inf = mesh.influences();
    for (size_t i = 0; i < inf.size(); ++i) o << (i ? "," : "") << (inf[i].blend < 0.5f ? inf[i].a : inf[i].b);
    o << "]}\n";
    std::ofstream(argv[2]) << o.str();

    if (!vat.empty()) {
        Project p;
        p.clip = clip;
        std::ofstream(vat) << save_project(p);
    }
    return 0;
}
