// Mesh parts and shape keys (spec 08 SK): each source mesh object a named part, morph targets as shape keys, the look
// (hidden parts, key values) in shown_model, the rig export, the project and the mapping file. The files are written
// here, CC0 like everything VATs makes. FBX's are in test_fbx.cpp.
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

#include "check.h"
#include "fixtures.h"
#include "vats/dae.h"
#include "vats/fbx.h"
#include "vats/gltf_mesh.h"
#include "vats/json.h"
#include "vats/project.h"
#include "vats/rig_export.h"
#include "vats/rig_map.h"

using namespace vats;
namespace fs = std::filesystem;

namespace {

Vec3 vertex(const DaeModel& m, std::uint32_t v) { return {m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]}; }
Vec3 normal(const DaeModel& m, std::uint32_t v) { return {m.normals[v * 3], m.normals[v * 3 + 1], m.normals[v * 3 + 2]}; }

const DaePart* part(const DaeModel& m, const std::string& name) {
    for (const DaePart& p : m.parts)
        if (p.name == name) return &p;
    return nullptr;
}

// ---- glTF: two mesh nodes, Body and Scarf, one triangle each, in glTF's Y-up axes (SL: (x, y, z) -> (x, -z, y)).
// Body has "Grow" (a sparse accessor, moving its vertex 0 by +1 up) and "Smile"; Scarf has "Grow" too (dense, every
// vertex +2 forward). Body's mesh weights start Smile at 1.

struct Gltf {
    std::vector<std::uint8_t> bin;
    Json views = Json::array(), accessors = Json::array();

    int view(const void* data, size_t bytes) {
        Json v = Json::object();
        v.set("buffer", 0);
        v.set("byteOffset", int(bin.size()));
        v.set("byteLength", int(bytes));
        const auto* p = static_cast<const std::uint8_t*>(data);
        bin.insert(bin.end(), p, p + bytes);
        while (bin.size() % 4) bin.push_back(0);
        views.push(std::move(v));
        return int(views.arr.size()) - 1;
    }
    int floats(const std::vector<float>& f, const char* type, int width) {
        Json a = Json::object();
        a.set("bufferView", view(f.data(), f.size() * 4));
        a.set("componentType", 5126);
        a.set("count", int(f.size()) / width);
        a.set("type", type);
        accessors.push(std::move(a));
        return int(accessors.arr.size()) - 1;
    }
    // A VEC3 accessor of count elements, zero but for the listed ones (glTF's sparse storage, no bufferView).
    int sparse(int count, const std::vector<std::uint16_t>& at, const std::vector<float>& values) {
        Json idx = Json::object(), val = Json::object(), sp = Json::object(), a = Json::object();
        idx.set("bufferView", view(at.data(), at.size() * 2));
        idx.set("componentType", 5123);
        val.set("bufferView", view(values.data(), values.size() * 4));
        sp.set("count", int(at.size()));
        sp.set("indices", std::move(idx));
        sp.set("values", std::move(val));
        a.set("componentType", 5126);
        a.set("count", count);
        a.set("type", "VEC3");
        a.set("sparse", std::move(sp));
        accessors.push(std::move(a));
        return int(accessors.arr.size()) - 1;
    }
};

std::string base64(const std::vector<std::uint8_t>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < in.size(); i += 3) {
        unsigned v = unsigned(in[i]) << 16 | (i + 1 < in.size() ? unsigned(in[i + 1]) << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
        o += t[v >> 18], o += t[(v >> 12) & 63];
        o += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        o += i + 2 < in.size() ? t[v & 63] : '=';
    }
    return o;
}

std::string two_part_gltf() {
    Gltf g;
    const std::vector<float> tri = {0, 0, 0, 1, 0, 0, 0, 0, -1};  // glTF axes: SL (0,0,0), (1,0,0), (0,1,0)
    const std::vector<float> up = {0, 0, 1, 0, 0, 1, 0, 0, 1};     // glTF +Z: SL -Y
    Json meshes = Json::array(), nodes = Json::array();
    for (int k = 0; k < 2; ++k) {
        Json attrs = Json::object();
        attrs.set("POSITION", g.floats(tri, "VEC3", 3));
        attrs.set("NORMAL", g.floats(up, "VEC3", 3));
        Json targets = Json::array(), names = Json::array(), weights = Json::array();
        Json grow = Json::object();
        if (k == 0) {
            grow.set("POSITION", g.sparse(3, {0}, {0, 1, 0}));  // vertex 0 up 1 m
            Json smile = Json::object();
            smile.set("POSITION", g.floats({0, 0, 0, 0, 0, 0, 0.5f, 0, 0}, "VEC3", 3));  // vertex 2 forward 0.5 m
            smile.set("NORMAL", g.floats({0, 0, 0, 0, 0, 0, 0, 1, 0}, "VEC3", 3));      // and its normal tipped up
            targets.push(std::move(grow)), targets.push(std::move(smile));
            names.push("Grow"), names.push("Smile");
            weights.push(0), weights.push(1);
        } else {
            grow.set("POSITION", g.floats({2, 0, 0, 2, 0, 0, 2, 0, 0}, "VEC3", 3));  // every vertex forward 2 m
            targets.push(std::move(grow));
            names.push("Grow");
            weights.push(0);
        }
        Json prim = Json::object(), prims = Json::array(), mesh = Json::object(), extras = Json::object();
        prim.set("attributes", std::move(attrs));
        prim.set("targets", std::move(targets));
        prims.push(std::move(prim));
        mesh.set("primitives", std::move(prims));
        mesh.set("weights", std::move(weights));
        extras.set("targetNames", std::move(names));
        mesh.set("extras", std::move(extras));
        meshes.push(std::move(mesh));
        Json n = Json::object();
        n.set("name", k == 0 ? "Body" : "Scarf");
        n.set("mesh", k);
        nodes.push(std::move(n));
    }
    Json doc = Json::object(), asset = Json::object(), buffers = Json::array(), buffer = Json::object();
    asset.set("version", "2.0");
    buffer.set("byteLength", int(g.bin.size()));
    buffer.set("uri", "data:application/octet-stream;base64," + base64(g.bin));
    buffers.push(std::move(buffer));
    doc.set("asset", std::move(asset));
    doc.set("nodes", std::move(nodes));
    doc.set("meshes", std::move(meshes));
    doc.set("accessors", std::move(g.accessors));
    doc.set("bufferViews", std::move(g.views));
    doc.set("buffers", std::move(buffers));
    return write_json(doc);
}

DaeModel load_two_part_gltf(DaeReport* rep_out = nullptr) {
    const std::string text = two_part_gltf();
    DaeModel m;
    DaeReport rep;
    std::string err;
    CHECK(load_gltf_mesh({text.begin(), text.end()}, "", skel(), m, rep, err));
    CHECK_EQ(err, std::string());
    if (rep_out) *rep_out = rep;
    return m;
}

// ---- COLLADA: one triangle near the pelvis, skinned to mPelvis (SL names, bound at SL's rest), instanced by two nodes:
// "Body" through a skin over a morph (Blender's shape keys: a target geometry "Grow" lifting vertex 0 by 0.1 m, at
// MORPH_WEIGHT 0.25), and "Scarf" through a plain skin.

std::string mat_rows(const Xform& x) {
    const Vec3 c[3] = {x.rot.rotate({1, 0, 0}), x.rot.rotate({0, 1, 0}), x.rot.rotate({0, 0, 1})};
    char b[256];
    std::snprintf(b, sizeof b, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g 0 0 0 1", c[0].x, c[1].x, c[2].x,
                  x.pos.x, c[0].y, c[1].y, c[2].y, x.pos.y, c[0].z, c[1].z, c[2].z, x.pos.z);
    return b;
}

std::string morph_dae() {
    const Skeleton& s = skel();
    const Vec3 p = s.global_pose(Pose(s.size()))[s.find("mPelvis")].pos;
    char pos[256], grow[256];
    std::snprintf(pos, sizeof pos, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", p.x, p.y, p.z, p.x + 0.1, p.y, p.z, p.x, p.y + 0.1, p.z);
    std::snprintf(grow, sizeof grow, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", p.x, p.y, p.z + 0.1, p.x + 0.1, p.y, p.z, p.x, p.y + 0.1, p.z);
    const std::string ibm = mat_rows(s.global_pose(Pose(s.size()))[s.find("mPelvis")].inverse());
    auto geometry = [&](const std::string& id, const std::string& name, const char* positions) {
        return "<geometry id=\"" + id + "\" name=\"" + name + "\"><mesh><source id=\"" + id + "-p\"><float_array id=\"" + id +
               "-pa\" count=\"9\">" + positions + "</float_array><technique_common><accessor source=\"#" + id +
               "-pa\" count=\"3\" stride=\"3\"/></technique_common></source><source id=\"" + id + "-n\"><float_array id=\"" + id +
               "-na\" count=\"3\">0 0 1</float_array><technique_common><accessor source=\"#" + id +
               "-na\" count=\"1\" stride=\"3\"/></technique_common></source><vertices id=\"" + id + "-v\"><input semantic=\"POSITION\" source=\"#" +
               id + "-p\"/></vertices><triangles count=\"1\"><input semantic=\"VERTEX\" source=\"#" + id +
               "-v\" offset=\"0\"/><input semantic=\"NORMAL\" source=\"#" + id + "-n\" offset=\"1\"/><p>0 0 1 0 2 0</p></triangles></mesh></geometry>";
    };
    auto skin = [&](const std::string& id, const std::string& source) {
        return "<controller id=\"" + id + "\"><skin source=\"#" + source + "\"><source id=\"" + id + "-j\"><Name_array id=\"" + id +
               "-ja\" count=\"1\">mPelvis</Name_array><technique_common><accessor source=\"#" + id +
               "-ja\" count=\"1\" stride=\"1\"/></technique_common></source><source id=\"" + id + "-b\"><float_array id=\"" + id +
               "-ba\" count=\"16\">" + ibm + "</float_array><technique_common><accessor source=\"#" + id +
               "-ba\" count=\"1\" stride=\"16\"/></technique_common></source><source id=\"" + id + "-w\"><float_array id=\"" + id +
               "-wa\" count=\"1\">1</float_array><technique_common><accessor source=\"#" + id +
               "-wa\" count=\"1\" stride=\"1\"/></technique_common></source><joints><input semantic=\"JOINT\" source=\"#" + id +
               "-j\"/><input semantic=\"INV_BIND_MATRIX\" source=\"#" + id + "-b\"/></joints><vertex_weights count=\"3\"><input semantic=\"JOINT\" source=\"#" +
               id + "-j\" offset=\"0\"/><input semantic=\"WEIGHT\" source=\"#" + id +
               "-w\" offset=\"1\"/><vcount>1 1 1</vcount><v>0 0 0 0 0 0</v></vertex_weights></skin></controller>";
    };
    const std::string morph =
        "<controller id=\"g-morph\"><morph source=\"#g\" method=\"NORMALIZED\"><source id=\"g-targets\"><IDREF_array id=\"g-ta\" count=\"1\">g-grow</IDREF_array>"
        "<technique_common><accessor source=\"#g-ta\" count=\"1\" stride=\"1\"/></technique_common></source><source id=\"g-weights\">"
        "<float_array id=\"g-wa\" count=\"1\">0.25</float_array><technique_common><accessor source=\"#g-wa\" count=\"1\" stride=\"1\"/>"
        "</technique_common></source><targets><input semantic=\"MORPH_TARGET\" source=\"#g-targets\"/><input semantic=\"MORPH_WEIGHT\" "
        "source=\"#g-weights\"/></targets></morph></controller>";
    return std::string("<?xml version=\"1.0\"?><COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">"
                       "<asset><unit meter=\"1\"/><up_axis>Z_UP</up_axis></asset><library_geometries>") +
           geometry("g", "Tri", pos) + geometry("g-grow", "Grow", grow) + "</library_geometries><library_controllers>" + morph +
           skin("skin-body", "g-morph") + skin("skin-scarf", "g") +
           "</library_controllers><library_visual_scenes><visual_scene id=\"Scene\"><node id=\"mPelvis\" name=\"mPelvis\" sid=\"mPelvis\" type=\"JOINT\"/>"
           "<node id=\"body\" name=\"Body\"><instance_controller url=\"#skin-body\"/></node>"
           "<node id=\"scarf\" name=\"Scarf\"><instance_controller url=\"#skin-scarf\"/></node>"
           "</visual_scene></library_visual_scenes><scene><instance_visual_scene url=\"#Scene\"/></scene></COLLADA>";
}

fs::path scratch_dir() {
    fs::path d = fs::temp_directory_path() / ("vats-shape-keys-" + std::to_string(std::random_device{}()));
    fs::create_directories(d);
    return d;
}

}  // namespace

TEST(shape_keys_gltf_parts_and_morph_targets) {
    DaeModel m = load_two_part_gltf();
    CHECK_EQ(m.parts.size(), size_t(2));
    const DaePart *body = part(m, "Body"), *scarf = part(m, "Scarf");
    CHECK(body && scarf);
    if (!body || !scarf) return;
    CHECK(body->first_vertex == 0 && body->vertex_count == 3 && body->first_index == 0 && body->index_count == 3);
    CHECK(scarf->first_vertex == 3 && scarf->vertex_count == 3 && scarf->first_index == 3 && scarf->index_count == 3);
    // Keys: Grow on both parts (an entry each), Smile on Body; names once, in the file's order; the file's values.
    CHECK_EQ(m.shape_keys.size(), size_t(3));
    CHECK(shape_key_names(m) == std::vector<std::string>({"Grow", "Smile"}));
    CHECK_NEAR(shape_key_value(m, {}, "Smile"), 1.0, 1e-12);
    CHECK_NEAR(shape_key_value(m, {}, "Grow"), 0.0, 1e-12);
    // The sparse Grow moves Body's vertex 0 only, up in SL (glTF +Y).
    const DaeShapeKey& grow = m.shape_keys[0];
    CHECK(grow.name == "Grow" && grow.vertices == std::vector<std::uint32_t>({0}));
    CHECK((Vec3{grow.dpos[0], grow.dpos[1], grow.dpos[2]} - Vec3{0, 0, 1}).length() < 1e-6);
}

TEST(shape_keys_value_moves_part_way_and_drives_every_part) {
    DaeModel m = load_two_part_gltf();
    // Smile is on at 1 (the file's weight): Body's vertex 2 sits 0.5 m forward, its normal tipped half up.
    const DaeModel shown0 = shown_model(m, {});
    CHECK((vertex(shown0, 2) - (vertex(m, 2) + Vec3{0.5, 0, 0})).length() < 1e-6);
    CHECK((normal(shown0, 2) - Vec3{0, -1, 1}.normalized()).length() < 1e-5);
    CHECK((vertex(shown0, 0) - vertex(m, 0)).length() < 1e-9);
    // Grow at 0.5: half way on both parts that have it, one value for both.
    MeshLook look;
    look.keys["Grow"] = 0.5;
    look.keys["Smile"] = 0;
    const DaeModel half = shown_model(m, look);
    CHECK((vertex(half, 0) - (vertex(m, 0) + Vec3{0, 0, 0.5})).length() < 1e-6);
    for (std::uint32_t v = 3; v < 6; ++v) CHECK((vertex(half, v) - (vertex(m, v) + Vec3{1, 0, 0})).length() < 1e-6);
    CHECK((vertex(half, 2) - vertex(m, 2)).length() < 1e-9);  // Smile off
    CHECK(half.shape_keys.empty());
    CHECK((normal(half, 2) - normal(m, 2)).length() < 1e-6);
}

TEST(shape_keys_hidden_part_is_left_out) {
    DaeModel m = load_two_part_gltf();
    MeshLook look;
    look.hidden = {"Body"};
    look.keys["Grow"] = 1;
    const DaeModel s = shown_model(m, look);
    CHECK_EQ(s.vertex_count(), 3);
    CHECK_EQ(s.triangle_count(), 1);
    CHECK(s.parts.size() == 1 && s.parts[0].name == "Scarf" && s.parts[0].first_vertex == 0 && s.parts[0].first_index == 0);
    CHECK(s.groups.size() == 1 && s.groups[0].first_vertex == 0 && s.groups[0].vertex_count == 3 && s.groups[0].first_index == 0);
    for (std::uint32_t i : s.indices) CHECK(i < 3u);
    // Scarf's own Grow still applies, to its renumbered vertices; Body's does not land on them.
    for (std::uint32_t v = 0; v < 3; ++v) CHECK((vertex(s, v) - (vertex(m, v + 3) + Vec3{2, 0, 0})).length() < 1e-6);
    CHECK(s.bounds_min.x > 1.9);
    // What a fix on the shown model is carried back through: shown vertex v came from Scarf's v + 3.
    CHECK(shown_vertex_sources(m, look) == std::vector<std::uint32_t>({3, 4, 5}));
    CHECK(shown_vertex_sources(m, {}) == std::vector<std::uint32_t>({0, 1, 2, 3, 4, 5}));
}

TEST(shape_keys_group_names) {
    size_t rest = 0;
    CHECK_EQ(shape_key_group("Body - Obese", &rest), std::string("Body"));
    CHECK_EQ(std::string("Body - Obese").substr(rest), std::string("Obese"));
    CHECK_EQ(shape_key_group("mouth -  tongueout", &rest), std::string("mouth"));
    CHECK_EQ(std::string("mouth -  tongueout").substr(rest), std::string("tongueout"));
    CHECK_EQ(shape_key_group("Clothing - Kevlar - No Mags", &rest), std::string("Clothing"));
    CHECK_EQ(std::string("Clothing - Kevlar - No Mags").substr(rest), std::string("Kevlar - No Mags"));
    CHECK_EQ(shape_key_group("vrc.v_aa", &rest), std::string("vrc"));
    CHECK_EQ(std::string("vrc.v_aa").substr(rest), std::string("v_aa"));
    CHECK_EQ(shape_key_group("Smile.001", &rest), std::string(""));  // Blender's copy suffix is no group
    CHECK_EQ(rest, size_t(0));
    CHECK_EQ(shape_key_group("Blink", nullptr), std::string(""));
}

TEST(shape_keys_collada_morph_under_skin) {
    DaeModel m;
    DaeReport rep;
    std::string err;
    CHECK(load_dae(morph_dae(), "", skel(), m, rep, err));
    CHECK_EQ(err, std::string());
    CHECK(m.rigged);
    CHECK(rep.unsupported.empty());  // the morph is read, not skipped
    CHECK_EQ(m.parts.size(), size_t(2));
    const DaePart *body = part(m, "Body"), *scarf = part(m, "Scarf");
    CHECK(body && scarf && body->vertex_count == 3 && scarf->vertex_count == 3);
    CHECK_EQ(m.shape_keys.size(), size_t(1));
    if (m.shape_keys.size() != 1 || !body) return;
    const DaeShapeKey& k = m.shape_keys[0];
    CHECK_EQ(k.name, std::string("Grow"));
    CHECK_NEAR(k.initial, 0.25, 1e-12);
    CHECK_EQ(k.vertices.size(), size_t(1));  // only vertex 0 moves
    CHECK(k.vertices[0] >= body->first_vertex && k.vertices[0] < body->first_vertex + body->vertex_count);
    CHECK((Vec3{k.dpos[0], k.dpos[1], k.dpos[2]} - Vec3{0, 0, 0.1}).length() < 1e-6);
    // At its file value: a quarter of the way.
    const DaeModel s = shown_model(m, {});
    CHECK((vertex(s, k.vertices[0]) - (vertex(m, k.vertices[0]) + Vec3{0, 0, 0.025})).length() < 1e-6);
}

// The rig export writes what is shown: a hidden part is not in the file, and the keys are baked into its vertices.
TEST(shape_keys_rig_export_leaves_hidden_parts_out_and_bakes_keys) {
    const Skeleton& s = skel();
    DaeModel m;
    DaeReport rep;
    std::string err;
    CHECK(load_dae(morph_dae(), "", s, m, rep, err));
    const DaePart* body = part(m, "Body");
    CHECK(body && m.shape_keys.size() == 1);
    if (!body || m.shape_keys.size() != 1) return;
    const std::uint32_t moved = m.shape_keys[0].vertices[0] - body->first_vertex;  // Body's vertex the key moves
    MeshLook look;
    look.hidden = {"Scarf"};
    look.keys["Grow"] = 1;
    const DaeModel shown = shown_model(m, look);
    RigPart rp;
    rp.model = &shown;
    rp.name = "export";
    std::string text;
    CHECK(write_rig_dae(s, {rp}, {}, text, err));
    CHECK_EQ(err, std::string());
    size_t triangles = 0;
    for (size_t at = text.find("<triangles"); at != std::string::npos; at = text.find("<triangles", at + 1)) ++triangles;
    CHECK_EQ(triangles, size_t(1));  // one face for the one material
    DaeModel back;
    DaeReport rb;
    CHECK(load_dae(text, "", s, back, rb, err));
    CHECK_EQ(back.vertex_count(), 3);  // Scarf's triangle is not in the file
    CHECK_EQ(back.triangle_count(), 1);
    CHECK(back.shape_keys.empty());
    // Every vertex as shown: the moved one 10 cm up, the others where Body had them.
    for (std::uint32_t v = 0; v < 3; ++v) {
        const Vec3 want = vertex(m, body->first_vertex + v) + (v == moved ? Vec3{0, 0, 0.1} : Vec3{});
        double best = 1e9;
        for (std::uint32_t w = 0; w < 3; ++w) best = std::min(best, (vertex(back, w) - want).length());
        CHECK(best < 1e-4);
    }
    // Shown whole, both triangles go.
    const DaeModel all = shown_model(m, {});
    rp.model = &all;
    CHECK(write_rig_dae(s, {rp}, {}, text, err));
    CHECK(load_dae(text, "", s, back, rb, err));
    CHECK_EQ(back.triangle_count(), 2);
}

TEST(shape_keys_turn_with_the_vertices) {
    DaeModel m;
    m.rigged = true;
    m.positions = {1, 0, 0};
    m.normals = {0, 0, 1};
    m.shape_keys.push_back({"Lean", {0}, {1, 0, 0}, {0, 1, 0}, 0});
    apply_rig_turn(m, 0, 1);  // a quarter turn about Z, as a part that could not decide takes its body's
    const DaeShapeKey& k = m.shape_keys[0];
    CHECK((Vec3{k.dpos[0], k.dpos[1], k.dpos[2]} - Vec3{0, 1, 0}).length() < 1e-6);
    CHECK((Vec3{k.dnrm[0], k.dnrm[1], k.dnrm[2]} - Vec3{-1, 0, 0}).length() < 1e-6);
}

TEST(shape_keys_look_in_project_round_trip) {
    Project p;
    p.mesh_looks["body-1"].hidden = {"Scarf", "Balaclava"};
    p.mesh_looks["body-1"].keys = {{"Body - Obese", 1.0}, {"vrc.v_aa", 0.25}};
    p.mesh_looks["run:/x/y.glb"].keys = {{"Grow", 0.5}};
    const std::string text = save_project(p);
    Project q;
    std::string err;
    CHECK(load_project(text, q, err));
    CHECK(q.mesh_looks == p.mesh_looks);
    CHECK_EQ(save_project(q), text);
}

// A body of several files: each file's mapping keeps only its own parts and keys.
TEST(shape_keys_each_file_keeps_its_own_look) {
    const DaeModel m = load_two_part_gltf();
    MeshLook body;
    body.hidden = {"Scarf", "Hands"};  // Hands: a part of another file
    body.keys = {{"Smile", 0.5}, {"Blink", 1}};
    const MeshLook own = own_look(m, body);
    CHECK(own.hidden == std::set<std::string>({"Scarf"}));
    CHECK(own.keys == (std::map<std::string, double>{{"Smile", 0.5}}));
}

// The mapping file keeps the look; a mapping with no bones (a model rigged to SL's own names) loads the model as it is.
TEST(shape_keys_look_in_mapping_file) {
    RigMap map;
    map.look.hidden = {"Scarf"};
    map.look.keys = {{"Grow", 0.75}};
    RigMap back;
    std::string err;
    CHECK(parse_rig_map_json(write_rig_map_json(map), back, err));
    CHECK(back.look == map.look);
    CHECK(back.bones.empty());

    const fs::path dir = scratch_dir();
    const std::string model = (dir / "two.gltf").string();
    std::ofstream(model, std::ios::binary) << two_part_gltf();
    DaeModel plain, m;
    DaeReport rp, r;
    CHECK(load_mesh_file(model, skel(), plain, rp, err));
    CHECK(rp.look.empty());
    std::ofstream(rig_map_path(model), std::ios::binary) << write_rig_map_json(map);
    CHECK(load_mesh_file(model, skel(), m, r, err));
    CHECK(r.look == map.look);
    CHECK(r.warnings.empty());
    CHECK_EQ(m.vertex_count(), plain.vertex_count());
    std::error_code ec;
    fs::remove_all(dir, ec);
}
