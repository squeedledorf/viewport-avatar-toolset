// Viewport Avatar Toolset - writes the Linden body as rigged COLLADA parts, a stand-in "devkit" for testing
// mesh-body import (spec 08 BD) without shipping or needing anyone's real devkit.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
//
// Usage: vats_make_test_body <data/character dir> <out dir> [--long-legs] [--bento-face] [--scale k]
// Writes head.dae, upper.dae and lower.dae (SL default female shape, rest pose), weighted to SL joint names.
// --long-legs binds the knees 5 cm and the ankles 10 cm lower, like a devkit with its own joint positions (BD-3).
// --scale k makes the whole body k times the size: a very tall (1.35) or short (0.7) avatar.
// --bento-face also writes head-bento.dae, the head weighted to the Bento face bones by distance (so face
// tracking shows on a surface, spec 08 MC-5), and eyes.dae, the eyeballs on mFaceEyeAltLeft/Right.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vats/avatar_mesh.h"
#include "vats/skeleton.h"

using namespace vats;

namespace {

using Weights = std::vector<std::pair<int, float>>;  // node, weight; at most 4, summing to 1

// Optional colours: one material per group, each triangle in one group.
struct Colours {
    std::vector<std::array<float, 3>> rgb;  // per group
    std::vector<int> group;                 // per triangle of the part
};

std::string matrix(const Xform& x) {
    Vec3 c[3] = {x.rot.rotate({1, 0, 0}), x.rot.rotate({0, 1, 0}), x.rot.rotate({0, 0, 1})};
    char buf[400];
    std::snprintf(buf, sizeof buf, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g 0 0 0 1 ", c[0].x, c[1].x,
                  c[2].x, x.pos.x, c[0].y, c[1].y, c[2].y, x.pos.y, c[0].z, c[1].z, c[2].z, x.pos.z);
    return buf;
}

// One rigged mesh: the vertices of part (positions and normals from pos/nrm), each with its weights.
void write_dae(const std::string& path, const Skeleton& skel, const std::vector<Xform>& globals, const AvatarMesh& mesh,
               const MeshPart& part, const std::vector<float>& pos, const std::vector<float>& nrm,
               const std::vector<Weights>& weights, const Colours* colours = nullptr) {
    std::vector<int> joints;  // used nodes, first-use order
    auto slot = [&](int node) {
        for (size_t i = 0; i < joints.size(); ++i)
            if (joints[i] == node) return int(i);
        joints.push_back(node);
        return int(joints.size() - 1);
    };
    std::string p, n, w, vcount, v;
    int wcount = 0;
    for (std::uint32_t i = part.first_vertex; i < part.first_vertex + part.vertex_count; ++i) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "%.6f %.6f %.6f ", pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
        p += buf;
        std::snprintf(buf, sizeof buf, "%.5f %.5f %.5f ", nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);
        n += buf;
        const Weights& ws = weights[i - part.first_vertex];
        vcount += std::to_string(ws.size()) + " ";
        for (auto& [node, weight] : ws) {
            std::snprintf(buf, sizeof buf, "%.6f ", weight);
            w += buf;
            v += std::to_string(slot(node)) + " " + std::to_string(wcount++) + " ";
        }
    }
    const size_t groups = colours ? colours->rgb.size() : 1;
    std::vector<std::string> tris(groups);
    std::vector<int> counts(groups, 0);
    for (std::uint32_t k = part.first_index; k < part.first_index + part.index_count; k += 3) {
        const int g = colours ? colours->group[(k - part.first_index) / 3] : 0;
        ++counts[g];
        for (int c = 0; c < 3; ++c) tris[g] += std::to_string(mesh.indices()[k + c] - part.first_vertex) + " ";
    }
    std::string effects, materials;
    for (size_t g = 0; colours && g < groups; ++g) {
        char buf[300];
        std::snprintf(buf, sizeof buf,
                      "<effect id=\"fx%zu\"><profile_COMMON><technique sid=\"c\"><lambert><diffuse><color>%.3f %.3f %.3f "
                      "1</color></diffuse></lambert></technique></profile_COMMON></effect>",
                      g, colours->rgb[g][0], colours->rgb[g][1], colours->rgb[g][2]);
        effects += buf;
        materials += "<material id=\"m" + std::to_string(g) + "\"><instance_effect url=\"#fx" + std::to_string(g) + "\"/></material>";
    }
    std::string jn, ibm;
    for (int j : joints) jn += skel[j].name + " ", ibm += matrix(globals[j].inverse());
    const int nv = int(part.vertex_count), nj = int(joints.size());
    std::ofstream o(path);
    o << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
         "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
         "<asset><unit name=\"meter\" meter=\"1\"/><up_axis>Z_UP</up_axis></asset>\n"
      << (colours ? "<library_effects>" + effects + "</library_effects>\n<library_materials>" + materials +
                        "</library_materials>\n"
                  : std::string())
      << "<library_geometries><geometry id=\"g\"><mesh>\n"
      << "<source id=\"g-p\"><float_array id=\"g-pa\" count=\"" << nv * 3 << "\">" << p
      << "</float_array><technique_common><accessor source=\"#g-pa\" count=\"" << nv
      << "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" "
         "type=\"float\"/></accessor></technique_common></source>\n"
      << "<source id=\"g-n\"><float_array id=\"g-na\" count=\"" << nv * 3 << "\">" << n
      << "</float_array><technique_common><accessor source=\"#g-na\" count=\"" << nv
      << "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" "
         "type=\"float\"/></accessor></technique_common></source>\n"
         "<vertices id=\"g-v\"><input semantic=\"POSITION\" source=\"#g-p\"/><input semantic=\"NORMAL\" "
         "source=\"#g-n\"/></vertices>\n"
      << [&] {
             std::string t;
             for (size_t g = 0; g < groups; ++g)
                 if (counts[g])
                     t += "<triangles count=\"" + std::to_string(counts[g]) + "\"" +
                          (colours ? " material=\"m" + std::to_string(g) + "\"" : std::string()) +
                          "><input semantic=\"VERTEX\" source=\"#g-v\" offset=\"0\"/><p>" + tris[g] + "</p></triangles>\n";
             return t;
         }()
      << "</mesh></geometry></library_geometries>\n"
      << "<library_controllers><controller id=\"ctl\"><skin source=\"#g\"><bind_shape_matrix>1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 "
         "1</bind_shape_matrix>\n"
      << "<source id=\"ctl-j\"><Name_array id=\"ctl-ja\" count=\"" << nj << "\">" << jn
      << "</Name_array><technique_common><accessor source=\"#ctl-ja\" count=\"" << nj
      << "\" stride=\"1\"><param name=\"JOINT\" type=\"name\"/></accessor></technique_common></source>\n"
      << "<source id=\"ctl-ibm\"><float_array id=\"ctl-ibma\" count=\"" << nj * 16 << "\">" << ibm
      << "</float_array><technique_common><accessor source=\"#ctl-ibma\" count=\"" << nj
      << "\" stride=\"16\"><param name=\"TRANSFORM\" type=\"float4x4\"/></accessor></technique_common></source>\n"
      << "<source id=\"ctl-w\"><float_array id=\"ctl-wa\" count=\"" << wcount << "\">" << w
      << "</float_array><technique_common><accessor source=\"#ctl-wa\" count=\"" << wcount
      << "\" stride=\"1\"><param name=\"WEIGHT\" type=\"float\"/></accessor></technique_common></source>\n"
         "<joints><input semantic=\"JOINT\" source=\"#ctl-j\"/><input semantic=\"INV_BIND_MATRIX\" "
         "source=\"#ctl-ibm\"/></joints>\n"
      << "<vertex_weights count=\"" << nv << "\"><input semantic=\"JOINT\" source=\"#ctl-j\" offset=\"0\"/><input "
         "semantic=\"WEIGHT\" source=\"#ctl-w\" offset=\"1\"/><vcount>"
      << vcount << "</vcount><v>" << v << "</v></vertex_weights></skin></controller></library_controllers>\n"
      << "<library_visual_scenes><visual_scene id=\"Scene\"><node id=\"mesh\"><instance_controller "
         "url=\"#ctl\"/></node></visual_scene></library_visual_scenes>\n"
         "<scene><instance_visual_scene url=\"#Scene\"/></scene>\n</COLLADA>\n";
    std::printf("%s: %d vertices, %d joints\n", path.c_str(), nv, nj);
}

// The Linden weights of a part: one or two joints per vertex (virtual root -> mPelvis).
std::vector<Weights> linden_weights(const Skeleton& skel, const AvatarMesh& mesh, const MeshPart& part) {
    std::vector<Weights> out;
    for (std::uint32_t i = part.first_vertex; i < part.first_vertex + part.vertex_count; ++i) {
        const Influence& in = mesh.influences()[i];
        const int a = in.a < 0 ? skel.find("mPelvis") : in.a, b = in.b < 0 ? skel.find("mPelvis") : in.b;
        if (in.blend <= 0 || a == b) out.push_back({{a, 1.f}});
        else out.push_back({{a, 1 - in.blend}, {b, in.blend}});
    }
    return out;
}

// Bento face weights by distance: each face bone pulls the vertices near it (Gaussian falloff with a
// per-bone reach); mHead keeps the rest. Below the mouth line only the jaw's bones (lower lip, chin)
// and the lip corners may pull, so the lower lip and chin follow the jaw; above it the jaw's bones may
// not. ponytail: a distance heuristic for a test head, not an auto-rigger (that is spec 08 RG-1).
std::vector<Weights> bento_face_weights(const Skeleton& skel, const std::vector<Xform>& globals,
                                        const std::vector<float>& pos, const MeshPart& part) {
    auto at = [&](const char* name) { return globals[skel.find(name)].pos; };
    const int head = skel.find("mHead");
    const double mouth_z = (at("mFaceLipCornerLeft").z + at("mFaceLipCornerRight").z) / 2;
    struct Pull {
        const char* bone;
        Vec3 where;    // where it pulls from (the joint, or a side offset where joints coincide)
        double reach;  // metres
        int region;    // 0 above the mouth line, 1 below (the jaw), 2 either
    };
    const Vec3 lipside{0, 0.011, 0}, lid{0.010, 0, 0.006};  // lids: on the eyeball's front, above and below
    const Vec3 eye_l = at("mFaceEyeAltLeft"), eye_r = at("mFaceEyeAltRight");
    const std::vector<Pull> pulls = {
        {"mFaceChin", at("mFaceChin"), 0.028, 1},
        {"mFaceJaw", at("mFaceChin") + Vec3{-0.03, 0, 0.005}, 0.035, 1},
        {"mFaceLipLowerCenter", at("mFaceLipLowerCenter"), 0.008, 1},
        {"mFaceLipLowerLeft", at("mFaceLipLowerLeft") + lipside, 0.009, 1},
        {"mFaceLipLowerRight", at("mFaceLipLowerRight") - lipside, 0.009, 1},
        {"mFaceLipUpperCenter", at("mFaceLipUpperCenter"), 0.008, 0},
        {"mFaceLipUpperLeft", at("mFaceLipUpperLeft") + lipside, 0.009, 0},
        {"mFaceLipUpperRight", at("mFaceLipUpperRight") - lipside, 0.009, 0},
        // The corner joints sit well inside the mouth; pull from the mouth's surface at their side.
        {"mFaceLipCornerLeft", Vec3{at("mFaceLipUpperCenter").x - 0.006, at("mFaceLipCornerLeft").y, at("mFaceLipCornerLeft").z}, 0.012, 2},
        {"mFaceLipCornerRight", Vec3{at("mFaceLipUpperCenter").x - 0.006, at("mFaceLipCornerRight").y, at("mFaceLipCornerRight").z}, 0.012, 2},
        {"mFaceCheekUpperLeft", at("mFaceCheekUpperLeft"), 0.016, 0},
        {"mFaceCheekUpperRight", at("mFaceCheekUpperRight"), 0.016, 0},
        {"mFaceCheekLowerLeft", at("mFaceCheekLowerLeft"), 0.016, 2},
        {"mFaceCheekLowerRight", at("mFaceCheekLowerRight"), 0.016, 2},
        {"mFaceNoseCenter", at("mFaceNoseCenter"), 0.010, 0},
        {"mFaceNoseLeft", at("mFaceNoseLeft"), 0.008, 0},
        {"mFaceNoseRight", at("mFaceNoseRight"), 0.008, 0},
        {"mFaceNoseBase", at("mFaceNoseBase"), 0.008, 0},
        {"mFaceNoseBridge", at("mFaceNoseBridge"), 0.010, 0},
        {"mFaceEyeLidUpperLeft", eye_l + lid, 0.013, 0},
        {"mFaceEyeLidUpperRight", eye_r + lid, 0.013, 0},
        {"mFaceEyeLidLowerLeft", eye_l + Vec3{lid.x, 0, -lid.z}, 0.011, 0},
        {"mFaceEyeLidLowerRight", eye_r + Vec3{lid.x, 0, -lid.z}, 0.011, 0},
        {"mFaceEyecornerInnerLeft", at("mFaceEyecornerInnerLeft"), 0.006, 0},
        {"mFaceEyecornerInnerRight", at("mFaceEyecornerInnerRight"), 0.006, 0},
        {"mFaceEyebrowInnerLeft", at("mFaceEyebrowInnerLeft"), 0.011, 0},
        {"mFaceEyebrowCenterLeft", at("mFaceEyebrowCenterLeft"), 0.011, 0},
        {"mFaceEyebrowOuterLeft", at("mFaceEyebrowOuterLeft"), 0.011, 0},
        {"mFaceEyebrowInnerRight", at("mFaceEyebrowInnerRight"), 0.011, 0},
        {"mFaceEyebrowCenterRight", at("mFaceEyebrowCenterRight"), 0.011, 0},
        {"mFaceEyebrowOuterRight", at("mFaceEyebrowOuterRight"), 0.011, 0},
        {"mFaceForeheadCenter", at("mFaceForeheadCenter"), 0.018, 0},
    };
    std::vector<Weights> out;
    for (std::uint32_t i = part.first_vertex; i < part.first_vertex + part.vertex_count; ++i) {
        const Vec3 p{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]};
        const int region = p.z < mouth_z ? 1 : 0;
        Weights ws{{head, 0.3f}};  // the head keeps a share everywhere, so nothing tears away from it
        // The rim of each eye socket belongs to its lids (upper above the eye's centre, lower below);
        // the sparse Linden head has only a ring of vertices there, so they must move nearly rigidly.
        const Vec3& eye = p.y > 0 ? eye_l : eye_r;
        const bool rim = (p - eye).length() < 0.02 && p.x > eye.x;
        for (const Pull& pl : pulls) {
            if (pl.region != 2 && pl.region != region) continue;
            const std::string_view bone = pl.bone;
            const bool lid_bone = bone.find("EyeLid") != std::string_view::npos;
            if (rim && lid_bone && (bone.find("Upper") != std::string_view::npos) != (p.z > eye.z)) continue;
            const double d = (p - pl.where).length() / pl.reach;
            const bool corner = bone.find("LipCorner") != std::string_view::npos;
            if (d < 3) ws.push_back({skel.find(pl.bone), float(std::exp(-d * d) * (rim && lid_bone ? 4 : corner ? 2 : 1))});
        }
        std::sort(ws.begin(), ws.end(), [](auto& a, auto& b) { return a.second > b.second; });
        if (ws.size() > 4) ws.resize(4);
        float sum = 0;
        for (auto& w : ws) sum += w.second;
        for (auto& w : ws) w.second /= sum;
        out.push_back(ws);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    bool long_legs = false, bento = false;
    double scale = 1;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        long_legs = long_legs || a == "--long-legs";
        bento = bento || a == "--bento-face";
        if (a == "--scale" && i + 1 < argc) scale = std::atof(argv[++i]);
    }
    if (argc < 3 || scale <= 0) {
        std::fprintf(stderr, "usage: %s <character dir> <out dir> [--long-legs] [--bento-face] [--scale k]\n", argv[0]);
        return 2;
    }
    std::string dir = argv[1], out = argv[2], err;
    Skeleton skel;
    AvatarMesh mesh;
    if (!skel.load_dir(dir, err) || !mesh.load(skel, dir, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    mesh.build(Body::SLDefault);
    Shape shaped = *mesh.shape(Body::SLDefault);
    if (long_legs)
        for (const char* j : {"mKneeLeft", "mKneeRight", "mAnkleLeft", "mAnkleRight"}) shaped.offset[skel.find(j)].z -= 0.05;
    for (Vec3& s : shaped.scale) s = s * scale;  // a very tall or short devkit
    const Shape* shape = &shaped;
    auto globals = skel.global_pose(Pose(skel.size()), shape);
    std::vector<float> pos, nrm;
    mesh.skin(globals, shape, pos, nrm);

    const char* names[] = {"head", "upper", "lower"};
    for (int f = 0; f < 3; ++f) {
        const MeshPart& part = mesh.parts()[f];
        write_dae(out + "/" + names[f] + ".dae", skel, globals, mesh, part, pos, nrm, linden_weights(skel, mesh, part));
    }
    if (bento) {
        const MeshPart& head = mesh.parts()[0];
        write_dae(out + "/head-bento.dae", skel, globals, mesh, head, pos, nrm, bento_face_weights(skel, globals, pos, head));
        // Both eyeballs in one file, rigid on the Bento eyes (mFaceEyeAlt* sit where mEye* do, so they
        // turn in place). The Linden eye parts are stored one after the other.
        MeshPart eyes;
        for (const MeshPart& part : mesh.parts()) {
            if (part.material != Material::Eye) continue;
            if (eyes.vertex_count == 0) eyes = part;
            else if (part.first_vertex == eyes.first_vertex + eyes.vertex_count &&
                     part.first_index == eyes.first_index + eyes.index_count)
                eyes.vertex_count += part.vertex_count, eyes.index_count += part.index_count;
        }
        std::vector<Weights> ws;
        for (std::uint32_t i = eyes.first_vertex; i < eyes.first_vertex + eyes.vertex_count; ++i)
            ws.push_back({{skel.find(pos[i * 3 + 1] > 0 ? "mFaceEyeAltLeft" : "mFaceEyeAltRight"), 1.f}});
        // Sclera, iris and pupil by how far a triangle faces forward from its eye's centre, so the gaze
        // and the lids closing over it are visible on an untextured eye.
        Colours c{{{0.93f, 0.92f, 0.90f}, {0.24f, 0.40f, 0.55f}, {0.04f, 0.04f, 0.05f}}, {}};
        for (std::uint32_t k = eyes.first_index; k < eyes.first_index + eyes.index_count; k += 3) {
            Vec3 mid;
            for (int t = 0; t < 3; ++t) {
                const std::uint32_t i = mesh.indices()[k + t];
                mid += Vec3{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]} * (1.0 / 3);
            }
            const Vec3 centre = globals[skel.find(mid.y > 0 ? "mFaceEyeAltLeft" : "mFaceEyeAltRight")].pos;
            const double facing = (mid - centre).normalized().x;
            c.group.push_back(facing > 0.975 ? 2 : facing > 0.87 ? 1 : 0);
        }
        if (eyes.vertex_count) write_dae(out + "/eyes.dae", skel, globals, mesh, eyes, pos, nrm, ws, &c);
    }
    return 0;
}
