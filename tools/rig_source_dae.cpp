// Viewport Avatar Toolset - a rigged model's own armature and skin, written as a compact COLLADA file.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
//
// Usage: vats_rig_source_dae <data/character dir> <model (.fbx, .dae, .gltf, .glb)> <out.dae>
// Keeps only what VATs reads from a rigged model, as the model has it: every bone (names, hierarchy, binds, helpers and
// _end tips too), the triangles, normals and UVs, the weights by the model's own bones, and the materials' colours. No
// animation, no SL names: the result still needs mapping (Tools > Map Rig to Second Life). Written Z up in metres at
// the file's own size, facing the way the file faces. Made for the example body (data/bodies/mech, from a CC0 FBX).
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "vats/fbx.h"
#include "vats/skeleton.h"

using namespace vats;

namespace {

std::string matrix(const Xform& x) {
    const Vec3 c[3] = {x.rot.rotate({1, 0, 0}), x.rot.rotate({0, 1, 0}), x.rot.rotate({0, 0, 1})};
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g 0 0 0 1", c[0].x, c[1].x, c[2].x,
                  x.pos.x, c[0].y, c[1].y, c[2].y, x.pos.y, c[0].z, c[1].z, c[2].z, x.pos.z);
    return buf;
}

std::string num(double v, const char* f) {
    char buf[32];
    std::snprintf(buf, sizeof buf, f, v);
    std::string s = buf;
    if (s == "-0" || s.find_first_not_of("-0.") == std::string::npos) return "0";  // -0.0000 and 0.0000
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: vats_rig_source_dae <data/character dir> <model> <out.dae>\n");
        return 2;
    }
    Skeleton skel;
    std::string err;
    if (!skel.load_dir(argv[1], err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    DaeModel plain;
    DaeReport first;
    if (!load_mesh_file_as_is(argv[2], skel, plain, first, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const std::vector<SourceBone>& bones = first.bones;
    // Each bone stands for itself: a remap onto distinct SK-40 indices used only as labels keeps every bone's weights
    // apart, with the vertices at the bind, in the file's own size and facing.
    if (bones.empty() || int(bones.size()) >= dae_index_count(skel) - 1) return std::fprintf(stderr, "no armature to write\n"), 1;
    SkinRemap labels;
    for (size_t i = 0; i < bones.size(); ++i) labels.joints[bones[i].name] = int(i) < dae_root(skel) ? int(i) : int(i) + 1;
    DaeModel m;
    DaeReport rep;
    if (!load_mesh_file_as_is(argv[2], skel, m, rep, err, &labels) || !m.rigged)
        return std::fprintf(stderr, "%s\n", err.empty() ? "the model has no skin" : err.c_str()), 1;
    std::map<int, int> bone_of;  // label -> bone
    for (auto& [name, label] : labels.joints)
        for (size_t i = 0; i < bones.size(); ++i)
            if (bones[i].name == name) bone_of[label] = int(i);

    // The skin's joints: the bones that carry weights or a skin bind, in the file's order.
    std::vector<int> skin;
    std::map<int, int> slot;
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].skinned || bones[i].weight > 0) slot[int(i)] = int(skin.size()), skin.push_back(int(i));

    std::string pos, nrm, uv, w, vcount, v;
    int wcount = 0;
    const int nv = m.vertex_count();
    for (int i = 0; i < nv; ++i) {
        for (int k = 0; k < 3; ++k) pos += num(m.positions[i * 3 + k], "%.4f") + " ", nrm += num(m.normals[i * 3 + k], "%.3f") + " ";
        uv += num(m.uvs[i * 2], "%.4f") + " " + num(1 - m.uvs[i * 2 + 1], "%.4f") + " ";
        int n = 0;
        for (int k = 0; k < 4; ++k) {
            const float wt = m.weights[i * 4 + k];
            auto b = bone_of.find(m.joints[i * 4 + k]);
            if (!(wt > 0) || b == bone_of.end() || !slot.count(b->second)) continue;
            w += num(wt, "%.4f") + " ";
            v += std::to_string(slot[b->second]) + " " + std::to_string(wcount++) + " ";
            ++n;
        }
        vcount += std::to_string(n) + " ";
    }
    std::string fx, mats, prims, binds;
    for (size_t g = 0; g < m.groups.size(); ++g) {
        const DaeGroup& grp = m.groups[g];
        const DaeMaterial& mat = m.materials[size_t(grp.material)];
        const std::string id = "m" + std::to_string(g);
        fx += "<effect id=\"fx" + std::to_string(g) + "\"><profile_COMMON><technique sid=\"c\"><lambert><diffuse><color>" +
              num(mat.rgba[0], "%.3f") + " " + num(mat.rgba[1], "%.3f") + " " + num(mat.rgba[2], "%.3f") +
              " 1</color></diffuse></lambert></technique></profile_COMMON></effect>";
        mats += "<material id=\"" + id + "\" name=\"" + mat.name + "\"><instance_effect url=\"#fx" + std::to_string(g) + "\"/></material>";
        std::string p;
        for (std::uint32_t k = grp.first_index; k < grp.first_index + grp.index_count; ++k) p += std::to_string(m.indices[k]) + " ";
        prims += "<triangles count=\"" + std::to_string(grp.index_count / 3) + "\" material=\"" + id +
                 "\"><input semantic=\"VERTEX\" source=\"#g-v\" offset=\"0\"/><input semantic=\"TEXCOORD\" source=\"#g-t\" "
                 "offset=\"0\" set=\"0\"/><p>" + p + "</p></triangles>\n";
        binds += "<instance_material symbol=\"" + id + "\" target=\"#" + id + "\"/>";
    }
    std::string names, ibm;
    for (int b : skin) names += bones[size_t(b)].name + " ", ibm += matrix(bones[size_t(b)].bind.inverse()) + " ";
    // The joint tree: each bone's node placed against its parent's bind.
    std::vector<std::vector<int>> kids(bones.size());
    std::vector<int> roots;
    for (size_t i = 0; i < bones.size(); ++i) (bones[i].parent >= 0 ? kids[size_t(bones[i].parent)] : roots).push_back(int(i));
    std::string tree;
    auto node = [&](auto& self, int b) -> void {
        const SourceBone& sb = bones[size_t(b)];
        const Xform local = sb.parent >= 0 ? bones[size_t(sb.parent)].bind.inverse() * sb.bind : sb.bind;
        tree += "<node id=\"j" + std::to_string(b) + "\" sid=\"" + sb.name + "\" name=\"" + sb.name + "\" type=\"JOINT\"><matrix>" +
                matrix(local) + "</matrix>";
        for (int k : kids[size_t(b)]) self(self, k);
        tree += "</node>";
    };
    for (int r : roots) node(node, r);

    auto source = [](const std::string& id, const std::string& data, int n, int stride, const std::string& params,
                     const char* array = "float_array") {
        return "<source id=\"" + id + "\"><" + array + " id=\"" + id + "-a\" count=\"" + std::to_string(n * stride) + "\">" + data +
               "</" + array + "><technique_common><accessor source=\"#" + id + "-a\" count=\"" + std::to_string(n) + "\" stride=\"" +
               std::to_string(stride) + "\">" + params + "</accessor></technique_common></source>\n";
    };
    const std::string xyz = "<param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/>";
    const int nj = int(skin.size());
    std::ofstream o(argv[3], std::ios::binary);
    o << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" "
         "version=\"1.4.1\">\n<asset><contributor><authoring_tool>vats_rig_source_dae</authoring_tool></contributor>"
         "<unit name=\"meter\" meter=\"1\"/><up_axis>Z_UP</up_axis></asset>\n"
      << "<library_effects>" << fx << "</library_effects>\n<library_materials>" << mats << "</library_materials>\n"
      << "<library_geometries><geometry id=\"g\"><mesh>\n"
      << source("g-p", pos, nv, 3, xyz) << source("g-n", nrm, nv, 3, xyz)
      << source("g-t", uv, nv, 2, "<param name=\"S\" type=\"float\"/><param name=\"T\" type=\"float\"/>")
      << "<vertices id=\"g-v\"><input semantic=\"POSITION\" source=\"#g-p\"/><input semantic=\"NORMAL\" source=\"#g-n\"/></vertices>\n"
      << prims << "</mesh></geometry></library_geometries>\n"
      << "<library_controllers><controller id=\"skin\"><skin source=\"#g\"><bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"
         "</bind_shape_matrix>\n"
      << source("skin-j", names, nj, 1, "<param name=\"JOINT\" type=\"name\"/>", "Name_array")
      << source("skin-b", ibm, nj, 16, "<param name=\"TRANSFORM\" type=\"float4x4\"/>")
      << source("skin-w", w, wcount, 1, "<param name=\"WEIGHT\" type=\"float\"/>")
      << "<joints><input semantic=\"JOINT\" source=\"#skin-j\"/><input semantic=\"INV_BIND_MATRIX\" source=\"#skin-b\"/></joints>\n"
      << "<vertex_weights count=\"" << nv << "\"><input semantic=\"JOINT\" source=\"#skin-j\" offset=\"0\"/><input semantic=\"WEIGHT\" "
         "source=\"#skin-w\" offset=\"1\"/><vcount>" << vcount << "</vcount><v>" << v << "</v></vertex_weights></skin></controller>"
         "</library_controllers>\n"
      << "<library_visual_scenes><visual_scene id=\"Scene\">" << tree
      << "<node id=\"model\" name=\"model\"><instance_controller url=\"#skin\"><skeleton>#j" << (roots.empty() ? 0 : roots[0])
      << "</skeleton><bind_material><technique_common>" << binds
      << "</technique_common></bind_material></instance_controller></node></visual_scene></library_visual_scenes>\n"
         "<scene><instance_visual_scene url=\"#Scene\"/></scene>\n</COLLADA>\n";
    std::printf("%s: %zu bones (%d in the skin), %d vertices, %d triangles\n", argv[3], bones.size(), nj, nv, m.triangle_count());
    return o ? 0 : 1;
}
