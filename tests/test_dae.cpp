#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

#include "check.h"
#include "fixtures.h"
#include "vats/dae.h"
#include "vats/json.h"
#include "vats/prop.h"
#include "vats/xml.h"

using namespace vats;

namespace {

// ---- fixture builders (T14): small COLLADA documents assembled from parts ----

std::string doc(const std::string& asset, const std::string& libs, const std::string& nodes) {
    return R"(<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
<asset>)" + asset + "</asset>\n" + libs + R"(
<library_visual_scenes><visual_scene id="Scene">)" + nodes + R"(</visual_scene></library_visual_scenes>
<scene><instance_visual_scene url="#Scene"/></scene>
</COLLADA>
)";
}

std::string source(const std::string& id, const std::string& floats, int stride) {
    size_t n = 0;
    for (size_t i = 0; i < floats.size(); ++i)
        if (floats[i] != ' ' && (i == 0 || floats[i - 1] == ' ')) ++n;
    return "<source id=\"" + id + "\"><float_array id=\"" + id + "-a\" count=\"" + std::to_string(n) + "\">" + floats +
           "</float_array><technique_common><accessor source=\"#" + id + "-a\" count=\"" + std::to_string(n / stride) +
           "\" stride=\"" + std::to_string(stride) + "\"/></technique_common></source>";
}

// A mesh "id" whose positions are the source id-p; prims use "#id-v" as VERTEX.
std::string geometry(const std::string& id, const std::string& positions, const std::string& prims,
                     const std::string& more_sources = "") {
    return "<geometry id=\"" + id + "\"><mesh>" + source(id + "-p", positions, 3) + more_sources + "<vertices id=\"" +
           id + "-v\"><input semantic=\"POSITION\" source=\"#" + id + "-p\"/></vertices>" + prims + "</mesh></geometry>";
}

std::string geometries(const std::string& g) { return "<library_geometries>" + g + "</library_geometries>"; }

std::string vin(const std::string& id) { return "<input semantic=\"VERTEX\" source=\"#" + id + "-v\" offset=\"0\"/>"; }

const std::string kTriangle = geometries(geometry("g", "0 0 0  1 0 0  0 1 0", "<triangles count=\"1\">" + vin("g") + "<p>0 1 2</p></triangles>"));

bool load(const std::string& text, DaeModel& m, DaeReport& r, const std::string& dir = "") {
    std::string err;
    bool ok = load_dae(text, dir, skel(), m, r, err);
    if (!ok) std::fprintf(stderr, "  load_dae: %s\n", err.c_str());
    return ok;
}

Vec3 at(const std::vector<float>& v, size_t i) { return {v[i * 3], v[i * 3 + 1], v[i * 3 + 2]}; }

bool near(const Vec3& a, const Vec3& b, double tol = 1e-5) { return (a - b).length() <= tol; }

// Every triangle's geometric normal agrees with its vertex normals (winding follows the normals).
bool winding_matches_normals(const DaeModel& m) {
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        Vec3 a = at(m.positions, m.indices[t]), b = at(m.positions, m.indices[t + 1]), c = at(m.positions, m.indices[t + 2]);
        Vec3 fn = (b - a).cross(c - a);
        for (int k = 0; k < 3; ++k)
            if (fn.dot(at(m.normals, m.indices[t + k])) <= 0) return false;
    }
    return true;
}

std::string ibm_text(const Vec3& t) {
    char buf[200];
    std::snprintf(buf, sizeof buf, "1 0 0 %.9g 0 1 0 %.9g 0 0 1 %.9g 0 0 0 1 ", -t.x, -t.y, -t.z);
    return buf;
}

Vec3 rest(const char* name) { return skel().global_pose(Pose(skel().size()))[skel().find(name)].pos; }

struct Rig {
    std::string joints = "mPelvis Armature_mChest Foo ns:BELLY mHead";
    bool idref = false;
    std::string ibms;  // default: rest-pose IBMs for mPelvis and mChest only
    std::string weights = "0.1 0.2 0.3 0.05 0.25 0";
    std::string vcount = "6 1 1";
    std::string v = "0 0 1 1 0 0 2 2 3 3 4 4  0 5  3 3";
    std::string positions = "0 0 1.1  0 0 1.3  0.1 0 1.2";
    std::string asset;
    std::string bind_shape = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1";
    std::string joint_nodes;  // extra JOINT nodes in the scene
    std::string node_transform;

    std::string text() const {
        std::string ib = ibms.empty() ? ibm_text(rest("mPelvis")) + ibm_text(rest("mChest")) : ibms;
        size_t nj = 0;
        for (size_t i = 0; i < joints.size(); ++i)
            if (joints[i] != ' ' && (i == 0 || joints[i - 1] == ' ')) ++nj;
        std::string arr = idref ? "IDREF_array" : "Name_array";
        std::string ctl = R"(<library_controllers><controller id="ctl"><skin source="#g"><bind_shape_matrix>)" + bind_shape +
                          R"(</bind_shape_matrix><source id="ctl-j"><)" + arr + R"( id="ctl-ja" count=")" + std::to_string(nj) +
                          "\">" + joints + "</" + arr + R"(><technique_common><accessor source="#ctl-ja" count=")" +
                          std::to_string(nj) + R"(" stride="1"/></technique_common></source>)" + source("ctl-ibm", ib, 16) +
                          source("ctl-w", weights, 1) +
                          R"(<joints><input semantic="JOINT" source="#ctl-j"/><input semantic="INV_BIND_MATRIX" source="#ctl-ibm"/></joints>
<vertex_weights count="3"><input semantic="JOINT" source="#ctl-j" offset="0"/><input semantic="WEIGHT" source="#ctl-w" offset="1"/><vcount>)" +
                          vcount + "</vcount><v>" + v + "</v></vertex_weights></skin></controller></library_controllers>";
        std::string geo = geometries(geometry("g", positions, "<triangles count=\"1\">" + vin("g") + "<p>0 1 2</p></triangles>"));
        return doc(asset, geo + ctl,
                   "<node id=\"Armature\" type=\"JOINT\"><node id=\"jp\" name=\"mPelvis\" sid=\"J1\" type=\"JOINT\"/></node>" +
                       joint_nodes + "<node id=\"mesh\">" + node_transform +
                       "<instance_controller url=\"#ctl\"><skeleton>#Armature</skeleton></instance_controller></node>");
    }
};

std::array<int, 4> joints_of(const DaeModel& m, int v) {
    return {m.joints[v * 4], m.joints[v * 4 + 1], m.joints[v * 4 + 2], m.joints[v * 4 + 3]};
}

}  // namespace

TEST(xml_text_content) {
    XmlNode root;
    std::string err;
    CHECK(parse_xml("<a>x &amp; y<b>  \n </b> z<![CDATA[<raw>]]><!-- c --></a>", root, err));
    CHECK_EQ(root.text, std::string("x & y z<raw>"));
    CHECK(root.child("b") && root.child("b")->text.empty());
    CHECK(!root.child("c"));
}

TEST(dae_map_skin_joint) {
    const Skeleton& s = skel();
    CHECK_EQ(map_skin_joint(s, "mPelvis"), s.find("mPelvis"));
    CHECK_EQ(map_skin_joint(s, "hip"), s.find("mPelvis"));                // alias
    CHECK_EQ(map_skin_joint(s, "avatar_mSkull"), s.find("mSkull"));
    CHECK_EQ(map_skin_joint(s, "Armature_mChest"), s.find("mChest"));     // '_' prefix
    CHECK_EQ(map_skin_joint(s, "rig:mNeck"), s.find("mNeck"));            // ns: prefix
    CHECK_EQ(map_skin_joint(s, "a|b|mHead"), s.find("mHead"));            // a|b prefix
    // Names match exactly, as the viewer's uploader matches them: "Chest" is the attachment point, not the CHEST volume.
    CHECK_EQ(map_skin_joint(s, "Chest"), s.find("Chest"));
    CHECK_EQ(map_skin_joint(s, "Left Hand"), s.find("Left Hand"));        // attachment points map too
    CHECK_EQ(map_skin_joint(s, "Left_Hand"), s.find("Left Hand"));
    CHECK_EQ(map_skin_joint(s, "BELLY"), dae_volume(s, s.find_volume("BELLY")));
    CHECK_EQ(map_skin_joint(s, "x:BELLY"), dae_volume(s, s.find_volume("BELLY")));
    bool loose = false;
    CHECK_EQ(map_skin_joint(s, "lThigh", &loose), s.find("mHipLeft"));  // LL's alias, exact
    CHECK(!loose);
    // Any other case is read too, but the viewer would not read it: loose, for a warning, and not SL-named.
    CHECK_EQ(map_skin_joint(s, "belly", &loose), dae_volume(s, s.find_volume("BELLY")));
    CHECK(loose);
    CHECK_EQ(map_skin_joint(s, "Head", &loose), dae_volume(s, s.find_volume("HEAD")));
    CHECK(loose && viewer_skin_joint(s, "Head") < 0 && viewer_skin_joint(s, "L_Hand") < 0);
    CHECK_EQ(map_skin_joint(s, "mpelvis", &loose), s.find("mPelvis"));
    CHECK(loose);
    CHECK_EQ(map_skin_joint(s, "L_UPPER_ARM"), dae_volume(s, s.find_volume("L_UPPER_ARM")));
    CHECK_EQ(map_skin_joint(s, "PELVIS"), dae_volume(s, s.find_volume("PELVIS")));  // volume before SK-7
    CHECK_EQ(map_skin_joint(s, "mRoot"), dae_root(s));
    CHECK_EQ(map_skin_joint(s, "Bone.001"), -1);
    CHECK_EQ(map_skin_joint(s, "Armature_"), -1);
    CHECK_EQ(dae_index_count(s), s.size() + 1 + 26);
}

TEST(dae_units_and_up_axes) {
    DaeModel m;
    DaeReport r;
    std::string g = geometries(geometry("g", "0 0 0  100 0 0  0 100 0", "<triangles count=\"1\">" + vin("g") + "<p>0 1 2</p></triangles>"));
    std::string node = "<node id=\"n\"><instance_geometry url=\"#g\"/></node>";

    CHECK(load(doc("<unit meter=\"0.01\"/><up_axis>Y_UP</up_axis>", g, node), m, r));
    CHECK_EQ(r.up_axis, std::string("Y_UP"));
    CHECK_NEAR(r.scale, 0.01, 1e-12);
    CHECK_EQ(r.triangles, 1);
    CHECK(!r.rigged && !m.rigged && m.joints.empty());
    CHECK(near(at(m.positions, 1), {1, 0, 0}));
    CHECK(near(at(m.positions, 2), {0, 0, 1}));  // file +Y is up
    CHECK(near(at(m.normals, 0), {0, -1, 0}));   // generated; file +Z (towards the viewer) is SL -Y
    CHECK(near(m.bounds_min, {0, 0, 0}) && near(m.bounds_max, {1, 0, 1}));

    CHECK(load(doc("<up_axis>X_UP</up_axis>", g, node), m, r));
    CHECK_EQ(r.up_axis, std::string("X_UP"));
    CHECK_NEAR(r.scale, 1, 0);
    CHECK(near(at(m.positions, 1), {0, 0, 100}));   // file +X is up
    CHECK(near(at(m.positions, 2), {-100, 0, 0}));  // file +Y is SL -X

    CHECK(load(doc("<unit meter=\"-3\"/>", g, node), m, r));  // <= 0 counts as 1
    CHECK_EQ(r.up_axis, std::string("Z_UP"));
    CHECK_NEAR(r.scale, 1, 0);
    CHECK(near(at(m.positions, 2), {0, 100, 0}));
}

TEST(dae_node_transforms) {
    DaeModel m;
    DaeReport r;
    std::string nodes = R"(<node id="a"><translate>1 2 3</translate><rotate>0 0 1 90</rotate><rotate>0 0 0 45</rotate>
<scale>2 2 2</scale><instance_geometry url="#g"/>
<node id="b"><matrix>1 0 0 10 0 1 0 0 0 0 1 0 0 0 0 1</matrix><instance_geometry url="#g"/></node>
<node id="c"><lookat>0 0 0 1 0 0 0 0 1</lookat><instance_camera url="#cam"/><instance_light url="#light"/></node></node>
<node id="skel" type="JOINT"><node type="JOINT"><instance_geometry url="#g"/></node><node/></node>
<node id="d"><instance_node url="#lib"/></node>)";
    std::string lib = kTriangle + R"(<library_nodes><node id="lib"><translate>0 0 5</translate><instance_geometry url="#g"/></node></library_nodes>)";
    CHECK(load(doc("", lib, nodes), m, r));
    CHECK_EQ(r.triangles, 3);
    CHECK_EQ(r.skipped_joint_nodes, 2);
    CHECK(near(at(m.positions, 1), {1, 4, 3}));   // (1,0,0): scale 2, rotate 90 about Z, translate
    CHECK(near(at(m.positions, 4), {1, 24, 3}));  // child: +10 on X first
    CHECK(near(at(m.positions, 7), {1, 0, 5}));   // instance_node
    for (const char* u : {"lookat", "instance_camera", "instance_light"})
        CHECK(std::find(r.unsupported.begin(), r.unsupported.end(), u) != r.unsupported.end());
    CHECK(winding_matches_normals(m));
}

TEST(dae_primitives) {
    DaeModel m;
    DaeReport r;
    std::string pos = "0 0 0  1 0 0  1 1 0  0 1 0  0.5 0.5 1";
    std::string prims = "<polylist count=\"2\">" + vin("g") + "<vcount>4 3</vcount><p>0 1 2 3 0 1 4</p></polylist>" +
                        "<polygons count=\"2\">" + vin("g") + "<p>0 1 2 3</p><ph><p>0 1 2 3</p><h>4 4 4</h></ph></polygons>" +
                        "<tristrips count=\"1\">" + vin("g") + "<p>0 1 3 2</p></tristrips>" + "<trifans count=\"1\">" +
                        vin("g") + "<p>0 1 2 3</p></trifans>" + "<lines count=\"1\">" + vin("g") + "<p>0 1</p></lines>";
    CHECK(load(doc("", geometries(geometry("g", pos, prims)), "<node><instance_geometry url=\"#g\"/></node>"), m, r));
    CHECK_EQ(r.triangles, 3 + 2 + 2 + 2);
    CHECK(std::find(r.unsupported.begin(), r.unsupported.end(), "ph") != r.unsupported.end());
    CHECK(std::find(r.unsupported.begin(), r.unsupported.end(), "lines") != r.unsupported.end());
    CHECK_EQ(m.groups.size(), size_t(1));
    CHECK_EQ(m.groups[0].index_count, std::uint32_t(27));
    // The strip's two triangles face the same way (+Z), like the rest of the flat quads.
    for (size_t t = 0; t < 7 * 3; t += 3) {
        if (t == 3) continue;  // polylist triangle 3 is the apex
        Vec3 a = at(m.positions, m.indices[t]), b = at(m.positions, m.indices[t + 1]), c = at(m.positions, m.indices[t + 2]);
        if (t != 6) CHECK((b - a).cross(c - a).z > 0);
    }
}

TEST(dae_inputs_strides_and_uvs) {
    DaeModel m;
    DaeReport r;
    std::string sources = source("g-n", "0 0 1  0 0 -1", 3) + source("g-t1", "0.9 0.9  0.9 0.9", 2) +
                          source("g-t0", "0 0  1 0  0 0.25 5 5", 2);
    std::string prim = "<triangles count=\"1\">" + vin("g") +
                       R"(<input semantic="NORMAL" source="#g-n" offset="1"/><input semantic="TEXCOORD" source="#g-t1" offset="2" set="1"/>
<input semantic="TEXCOORD" source="#g-t0" offset="3" set="0"/><p>0 0 0 0  1 0 1 1  2 0 0 2</p></triangles>)";
    CHECK(load(doc("", geometries(geometry("g", "0 0 0  1 0 0  0 1 0", prim, sources)), "<node><instance_geometry url=\"#g\"/></node>"), m, r));
    CHECK_EQ(m.vertex_count(), 3);
    CHECK_NEAR(m.uvs[0], 0, 1e-6);
    CHECK_NEAR(m.uvs[1], 1, 1e-6);     // v flipped
    CHECK_NEAR(m.uvs[4], 0, 1e-6);
    CHECK_NEAR(m.uvs[5], 0.75, 1e-6);  // set 0 wins over set 1
    CHECK(near(at(m.normals, 2), {0, 0, 1}));
    CHECK(winding_matches_normals(m));

    // Normals and UVs that live on <vertices> are indexed by the VERTEX index.
    std::string g2 = "<geometry id=\"h\"><mesh>" + source("h-p", "0 0 0  1 0 0  0 1 0", 3) + source("h-n", "0 0 1  0 0 1  0 0 1", 3) +
                     R"(<vertices id="h-v"><input semantic="POSITION" source="#h-p"/><input semantic="NORMAL" source="#h-n"/></vertices>
<triangles count="1"><input semantic="VERTEX" source="#h-v" offset="0"/><p>0 1 2</p></triangles></mesh></geometry>)";
    CHECK(load(doc("", geometries(g2), "<node><instance_geometry url=\"#h\"/></node>"), m, r));
    CHECK(near(at(m.normals, 1), {0, 0, 1}));
}

TEST(dae_negative_scale_flips_winding) {
    DaeModel m;
    DaeReport r;
    std::string g = geometries(geometry("g", "0 0 0  1 0 0  0 1 0",
                                        "<triangles count=\"1\">" + vin("g") +
                                            "<input semantic=\"NORMAL\" source=\"#g-n\" offset=\"1\"/><p>0 0 1 0 2 0</p></triangles>",
                                        source("g-n", "0 0 1", 3)));
    CHECK(load(doc("", g, "<node><scale>-1 1 1</scale><instance_geometry url=\"#g\"/></node>"), m, r));
    CHECK(near(at(m.positions, m.indices[1]), {0, 1, 0}));  // winding reversed
    CHECK(near(at(m.normals, 0), {0, 0, 1}));
    CHECK(winding_matches_normals(m));

    // Non-uniform scale: normals use the inverse-transpose.
    std::string slope = geometries(geometry("s", "0 0 0  1 0 1  0 1 0",
                                            "<triangles count=\"1\">" + vin("s") +
                                                "<input semantic=\"NORMAL\" source=\"#s-n\" offset=\"1\"/><p>0 0 1 0 2 0</p></triangles>",
                                            source("s-n", "-0.70710678 0 0.70710678", 3)));
    CHECK(load(doc("", slope, "<node><scale>2 1 1</scale><instance_geometry url=\"#s\"/></node>"), m, r));
    CHECK(near(at(m.normals, 0), Vec3{-1, 0, 2}.normalized()));
    CHECK(winding_matches_normals(m));
}

TEST(dae_generated_normals) {
    DaeModel m;
    DaeReport r;
    // A quad split in two and a mirrored copy: generated normals are smooth per shared position.
    std::string g = geometries(geometry("g", "0 0 0  1 0 0  1 1 0  0 1 0",
                                        "<triangles count=\"2\">" + vin("g") + "<p>0 1 2 0 2 3</p></triangles>"));
    CHECK(load(doc("", g, "<node><instance_geometry url=\"#g\"/></node><node><scale>1 1 -1</scale><instance_geometry url=\"#g\"/></node>"), m, r));
    CHECK_EQ(m.vertex_count(), 8);
    for (int v = 0; v < 4; ++v) CHECK(near(at(m.normals, v), {0, 0, 1}));
    for (int v = 4; v < 8; ++v) CHECK(near(at(m.normals, v), {0, 0, -1}));
    CHECK(winding_matches_normals(m));
}

TEST(dae_materials_and_textures) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "vats_test_dae";
    fs::create_directories(dir);
    for (const char* f : {"wood grain.png", "tex15.png"}) std::ofstream(dir / f) << "png";

    std::string libs = R"(<library_images>
<image id="img1"><init_from>file:///C:/Users/someone/art/wood%20grain.png</init_from></image>
<image id="img2"><init_from><ref>tex15.png</ref></init_from></image>
<image id="img3"><init_from>nope.png</init_from></image></library_images>
<library_effects>
<effect id="fx1"><profile_COMMON><newparam sid="surf"><surface type="2D"><init_from>img1</init_from></surface></newparam>
<newparam sid="samp"><sampler2D><source>surf</source></sampler2D></newparam>
<technique sid="common"><phong><diffuse><texture texture="samp" texcoord="UV"/></diffuse></phong></technique></profile_COMMON></effect>
<effect id="fx2"><profile_COMMON><newparam sid="s2"><sampler2D><instance_image url="#img2"/></sampler2D></newparam>
<technique sid="t"><lambert><diffuse><texture texture="s2" texcoord="UV"/></diffuse><transparency><float>0.5</float></transparency></lambert></technique></profile_COMMON></effect>
<effect id="fx3"><profile_COMMON><technique sid="t"><blinn><diffuse><color>1 0 0 0.5</color></diffuse></blinn></technique></profile_COMMON></effect>
<effect id="fx4"><profile_COMMON><technique sid="t"><constant><diffuse><texture texture="img3" texcoord="UV"/></diffuse></constant></technique></profile_COMMON></effect>
</library_effects>
<library_materials><material id="m1"><instance_effect url="#fx1"/></material><material id="m2"><instance_effect url="#fx2"/></material>
<material id="m3"><instance_effect url="#fx3"/></material><material id="m4"><instance_effect url="#fx4"/></material></library_materials>)";
    std::string prims;
    for (const char* s : {"s1", "s2", "s3", "m4", "", "s1"})
        prims += std::string("<triangles count=\"1\" material=\"") + s + "\">" + vin("g") + "<p>0 1 2</p></triangles>";
    libs += geometries(geometry("g", "0 0 0  1 0 0  0 1 0", prims));
    std::string bind = R"(<bind_material><technique_common><instance_material symbol="s1" target="#m1"/>
<instance_material symbol="s2" target="#m2"/><instance_material symbol="s3" target="#m3"/></technique_common></bind_material>)";
    DaeModel m;
    DaeReport r;
    CHECK(load(doc("", libs, "<node><instance_geometry url=\"#g\">" + bind + "</instance_geometry></node>"), m, r, dir.string()));
    CHECK_EQ(m.materials.size(), size_t(5));
    CHECK_EQ(m.groups.size(), size_t(5));
    CHECK_EQ(m.groups[0].index_count, std::uint32_t(6));  // s1 twice
    const DaeMaterial& m1 = m.materials[0];
    CHECK_EQ(m1.name, std::string("m1"));
    CHECK_EQ(fs::path(m1.texture), dir / "wood grain.png");  // /C:/ path found by file name
    CHECK(m1.rgba[0] == 1 && m1.rgba[1] == 1 && !m1.blend);
    CHECK_EQ(fs::path(m.materials[1].texture), dir / "tex15.png");  // 1.5 instance_image + ref
    CHECK(m.materials[1].blend);
    CHECK_NEAR(m.materials[1].rgba[3], 0.5, 1e-6);
    CHECK(m.materials[2].blend && m.materials[2].rgba[0] == 1 && m.materials[2].rgba[1] == 0 && m.materials[2].texture.empty());
    CHECK(m.materials[3].texture.empty());  // bound by material id, texture missing
    CHECK_EQ(m.materials[3].name, std::string("m4"));
    CHECK_EQ(r.missing_textures, std::vector<std::string>{"nope.png"});
    const DaeMaterial& def = m.materials[4];
    CHECK(def.name.empty() && def.double_sided && def.texture.empty() && !def.blend);
    CHECK_NEAR(def.rgba[0], 0.82, 1e-6);
    fs::remove_all(dir);
}

TEST(dae_rigged_weights_and_mapping) {
    const Skeleton& s = skel();
    DaeModel m;
    DaeReport r;
    CHECK(load(Rig().text(), m, r));
    CHECK(r.rigged && m.rigged && !r.skins_as_static);
    CHECK_EQ(r.unmapped_joints, std::vector<std::string>{"Foo"});
    CHECK_EQ(r.skipped_joint_nodes, 2);
    CHECK_NEAR(r.scale, 1, 0);
    CHECK_NEAR(r.measured_scale, 1, 1e-9);
    CHECK_EQ(m.vertex_count(), 3);
    int root = dae_root(s), belly = dae_volume(s, s.find_volume("BELLY"));
    // > 4 weights: pelvis summed (0.1 + 0.1), Foo -> root, the smallest (BELLY) dropped, renormalised.
    CHECK((joints_of(m, 0) == std::array<int, 4>{root, s.find("mHead"), s.find("mPelvis"), s.find("mChest")}));
    CHECK_NEAR(m.weights[0], 0.3 / 0.95, 1e-6);
    CHECK_NEAR(m.weights[1], 0.25 / 0.95, 1e-6);
    CHECK_NEAR(m.weights[2], 0.2 / 0.95, 1e-6);
    CHECK_NEAR(m.weights[3], 0.2 / 0.95, 1e-6);
    CHECK((joints_of(m, 1) == std::array<int, 4>{root, root, root, root}));  // zero weight -> root
    CHECK_EQ(m.weights[4], 1.0f);
    CHECK_EQ(m.weights[5], 0.0f);
    CHECK((joints_of(m, 2) == std::array<int, 4>{belly, root, root, root}));  // collision volume
    CHECK_EQ(m.weights[8], 1.0f);
    // Binds: from INV_BIND for mPelvis/mChest; BELLY and mHead have none, so the SL rest pose.
    CHECK_EQ(m.binds.size(), size_t(dae_index_count(s)));
    std::vector<Xform> g = s.global_pose(Pose(s.size()));
    CHECK(near(m.binds[s.find("mChest")].pos, g[s.find("mChest")].pos));
    const CollisionVolume& bv = s.volumes()[s.find_volume("BELLY")];
    CHECK(near(m.binds[belly].pos, (g[bv.joint] * Xform{bv.rot, bv.pos}).pos));
    CHECK(near(m.binds[root].pos, {}));
    // Rest pose: nothing moves.
    std::vector<float> p, n;
    skin_prop(m, s, g, nullptr, p, n);
    for (int v = 0; v < 3; ++v) CHECK(near(at(p, v), at(m.positions, v)));
}

TEST(dae_rigged_skin_prop_follows_pose) {
    const Skeleton& s = skel();
    DaeModel m;
    DaeReport r;
    CHECK(load(Rig().text(), m, r));
    Pose pose(s.size());
    Quat q = Quat::axis_angle({0, 0, 1}, kPi / 2);
    pose.rot[s.find("mPelvis")] = q;
    std::vector<Xform> g = s.global_pose(pose);
    std::vector<float> p, n;
    skin_prop(m, s, g, nullptr, p, n);
    Vec3 piv = rest("mPelvis");
    auto turned = [&](const Vec3& v) { return piv + q.rotate(v - piv); };
    Vec3 v0 = at(m.positions, 0), v1 = at(m.positions, 1), v2 = at(m.positions, 2);
    CHECK(near(at(p, 0), v0 * (0.3 / 0.95) + turned(v0) * (0.65 / 0.95)));  // root share stays still
    CHECK(near(at(p, 1), v1));                                              // mRoot
    CHECK(near(at(p, 2), turned(v2)));                                      // BELLY rides mTorso
    CHECK(near(at(n, 2), q.rotate(at(m.normals, 2))));

    // An animated BELLY volume turns the vertex it carries about the volume's own origin.
    const CollisionVolume& bv = s.volumes()[s.find_volume("BELLY")];
    Pose vp(s.size());
    Quat qv = Quat::axis_angle({1, 0, 0}, kPi / 3);
    vp.rot[bv.node] = qv;
    std::vector<Xform> gv = s.global_pose(vp), g0 = s.global_pose(Pose(s.size()));
    skin_prop(m, s, gv, nullptr, p, n);
    Vec3 o = g0[bv.node].pos;
    Quat turn = gv[bv.node].rot * g0[bv.node].rot.conj();
    CHECK(near(at(p, 2), o + turn.rotate(v2 - o)));
    CHECK(near(at(p, 1), v1));  // mRoot still still

    // Shape: a vertex at the mHead joint lands on the shaped mHead.
    Rig head;
    Vec3 h = rest("mHead");
    char buf[200];
    std::snprintf(buf, sizeof buf, "%.9g %.9g %.9g  %.9g %.9g %.9g  %.9g %.9g %.9g", h.x, h.y, h.z, h.x + 0.1, h.y, h.z,
                  h.x, h.y + 0.1, h.z);
    head.positions = buf;
    head.joints = "mHead";
    head.ibms = ibm_text(h);
    head.weights = "1";
    head.vcount = "1 1 1";
    head.v = "0 0 0 0 0 0";
    CHECK(load(head.text(), m, r));
    const Shape* male = &s.male_shape();
    g = s.global_pose(Pose(s.size()), male);
    skin_prop(m, s, g, male, p, n);
    CHECK(near(at(p, 0), g[s.find("mHead")].pos));
    Vec3 sc = male->scale[s.find("mHead")];
    CHECK(near(at(p, 1) - at(p, 0), g[s.find("mHead")].rot.rotate(Vec3{0.1 * sc.x, 0, 0})));
}

// A mesh exported in Blender's axes (facing -Y) while its binds are in SL's (+X): Blender's COLLADA exporter
// does that for rigs carrying SL bind data. It is turned onto its skeleton, and a collision volume the body
// bound somewhere else than SL's default is moved there, so the rest pose is exact and the pose follows.
TEST(dae_blender_axes_mesh_and_volume_binds) {
    const Skeleton& s = skel();
    const auto rest = s.global_pose(Pose(s.size()));
    auto blender = [](const Vec3& sl) { return Vec3{sl.y, -sl.x, sl.z}; };
    const char* names[] = {"mPelvis", "mShoulderLeft", "mShoulderRight", "mHead", "L_UPPER_ARM"};
    const int vol = s.find_volume("L_UPPER_ARM");
    const Vec3 vol_bind = rest[s.volumes()[vol].node].pos + Vec3{0.03, 0.02, 0};  // the devkit's own place
    std::string pos, ibm, joints, vc, vv, w = "1";
    for (int j = 0; j < 5; ++j) {
        Vec3 at = j == 4 ? vol_bind : rest[s.find(names[j])].pos;
        if (j == 4) {  // a volume's bind carries its rest rotation, as SL exports write it
            const Xform inv = Xform{rest[s.volumes()[vol].node].rot, at}.inverse();
            const Vec3 x = inv.rot.rotate({1, 0, 0}), y = inv.rot.rotate({0, 1, 0}), z = inv.rot.rotate({0, 0, 1});
            char buf[300];
            std::snprintf(buf, sizeof buf, "%.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g 0 0 0 1 ", x.x, y.x, z.x,
                          inv.pos.x, x.y, y.y, z.y, inv.pos.y, x.z, y.z, z.z, inv.pos.z);
            ibm += buf;
        } else {
            ibm += ibm_text(at);
        }
        joints += std::string(j ? " " : "") + names[j];
        for (int k = 0; k < 3; ++k) {
            Vec3 v = blender(at + Vec3{k == 0 ? 0.02 : 0, k == 1 ? 0.02 : 0, k == 2 ? 0.02 : 0});
            char buf[120];
            std::snprintf(buf, sizeof buf, "%.9g %.9g %.9g  ", v.x, v.y, v.z);
            pos += buf;
            vc += "1 ";
            vv += std::to_string(j) + " 0 ";
        }
    }
    std::string idx;
    for (int i = 0; i < 15; ++i) idx += std::to_string(i) + " ";
    std::string ctl = R"(<library_controllers><controller id="ctl"><skin source="#g"><source id="ctl-j"><Name_array id="ctl-ja" count="5">)" +
                      joints + R"(</Name_array><technique_common><accessor source="#ctl-ja" count="5" stride="1"/></technique_common></source>)" +
                      source("ctl-ibm", ibm, 16) + source("ctl-w", w, 1) +
                      R"(<joints><input semantic="JOINT" source="#ctl-j"/><input semantic="INV_BIND_MATRIX" source="#ctl-ibm"/></joints>
<vertex_weights count="15"><input semantic="JOINT" source="#ctl-j" offset="0"/><input semantic="WEIGHT" source="#ctl-w" offset="1"/><vcount>)" +
                      vc + "</vcount><v>" + vv + "</v></vertex_weights></skin></controller></library_controllers>";
    std::string geo = geometries(geometry("g", pos, "<triangles count=\"5\">" + vin("g") + "<p>" + idx + "</p></triangles>"));
    DaeModel m;
    DaeReport r;
    CHECK(load(doc("", geo + ctl, "<node id=\"mesh\"><instance_controller url=\"#ctl\"/></node>"), m, r));
    CHECK(m.rigged);
    // Turned: the vertices sit at their SL joints again (the first of each three is 2 cm along +X).
    CHECK(near(at(m.positions, 3), rest[s.find("mShoulderLeft")].pos + Vec3{0.02, 0, 0}, 1e-5));

    Shape shape;
    std::vector<const DaeModel*> parts{&m};
    CHECK(shape_from_binds(s, parts, nullptr, shape));
    std::vector<float> p, n;
    const auto r0 = s.global_pose(Pose(s.size()), &shape);
    skin_prop(m, s, r0, &shape, p, n);
    for (size_t v = 0; v < 15; ++v) CHECK(near(at(p, v), at(m.positions, v), 1e-5));  // exact at rest

    const int shoulder = s.find("mShoulderLeft");
    Pose pose(s.size());
    const Quat q = Quat::axis_angle({1, 0, 0}, kPi / 2);
    pose.rot[shoulder] = q;
    const auto g = s.global_pose(pose, &shape);
    skin_prop(m, s, g, &shape, p, n);
    const Vec3 piv = r0[shoulder].pos;
    for (size_t v = 12; v < 15; ++v)  // weighted to the L_UPPER_ARM volume: rides the shoulder
        CHECK(near(at(p, v), piv + q.rotate(at(m.positions, v) - piv), 1e-5));
}

// A fitted-mesh body in SL's own frames, modelled in an A-pose (the Legacy devkits): the left arm is bound turned
// 45 degrees down, and the mesh is weighted to collision volumes as well as joints. As in SL, the inverse binds
// carry the arm onto the rest skeleton: every vertex lands at its volume's (or joint's) own offset on the T-pose
// arm, and one weighted to a volume rides that volume when it moves or scales. A hand volume stays on a wrist the
// body did not bind, and a centre volume bound a few centimetres behind SL's (the male Legacy's CHEST) is not
// turned inside out.
TEST(dae_fitted_mesh_a_pose_binds_skin_onto_rest) {
    const Skeleton& s = skel();
    const auto r0 = s.global_pose(Pose(s.size()));
    const int count = dae_index_count(s);
    auto vol = [&](const char* n) { return dae_volume(s, s.find_volume(n)); };
    auto vol_node = [&](const char* n) { return s.volumes()[s.find_volume(n)].node; };
    DaeModel m;
    m.rigged = true;
    m.binds = r0;
    m.binds.push_back({});  // mRoot
    for (auto& v : s.volumes()) m.binds.push_back(r0[v.node]);
    std::vector<bool> bound(count, false);
    for (const char* n : {"mPelvis", "mTorso", "mChest", "mCollarLeft", "mShoulderLeft", "mElbowLeft"}) bound[s.find(n)] = true;
    // The A-pose: the shoulder turned down, the elbow carried with it; each arm volume hangs off its bound joint.
    const int shoulder = s.find("mShoulderLeft"), elbow = s.find("mElbowLeft");
    const Quat a = Quat::axis_angle({1, 0, 0}, -kPi / 4);
    m.binds[shoulder] = {a, r0[shoulder].pos};
    m.binds[elbow] = m.binds[shoulder] * Xform{s[elbow].rest, s[elbow].pos};
    const int wrist = s.find("mWristLeft");  // not bound: its bind is the rest, and the A-pose hand hangs off the elbow
    const Xform wrist_a = m.binds[elbow] * Xform{s[wrist].rest, s[wrist].pos};
    for (const char* n : {"L_UPPER_ARM", "L_LOWER_ARM", "L_HAND", "BELLY", "CHEST"}) {
        const CollisionVolume& cv = s.volumes()[s.find_volume(n)];
        m.binds[vol(n)] = (cv.joint == wrist ? wrist_a : m.binds[cv.joint]) * Xform{cv.rot, cv.pos};
        bound[vol(n)] = true;
    }
    const Vec3 chest_rest = r0[vol_node("CHEST")].pos;
    m.binds[vol("CHEST")].pos.x = -chest_rest.x - 0.004;  // 3 cm behind SL's, close to SL's mirrored across the centre line
    CHECK(std::fabs(m.binds[vol("CHEST")].pos.x - chest_rest.x) > 0.01);
    // One vertex per influence, a few centimetres off it in its own frame; the last is half joint, half volume.
    struct V {
        int j0, j1;
        float w0;
        Vec3 off;
    };
    const std::vector<V> verts = {{vol("L_UPPER_ARM"), -1, 1, {0.02, 0.01, 0.04}},
                                  {vol("L_LOWER_ARM"), -1, 1, {0, 0.03, -0.035}},
                                  {vol("BELLY"), -1, 1, {0.06, 0, 0.02}},
                                  {s.find("mChest"), -1, 1, {0.05, 0.03, 0.1}},
                                  {elbow, vol("L_LOWER_ARM"), 0.5f, {0, 0.05, 0.03}},
                                  {vol("L_HAND"), -1, 1, {0.01, 0.04, -0.01}}};
    for (const V& v : verts) {
        const Vec3 p = m.binds[v.j0].apply(v.off);
        for (int i = 0; i < 3; ++i) m.positions.push_back(float(p[i])), m.normals.push_back(i == 2 ? 1.f : 0.f);
        m.joints.insert(m.joints.end(), {v.j0, v.j1 < 0 ? dae_root(s) : v.j1, dae_root(s), dae_root(s)});
        m.weights.insert(m.weights.end(), {v.w0, 1 - v.w0, 0, 0});
    }
    DaeReport rep;
    settle_rig(m, s, bound, rep);
    CHECK(rep.warnings.empty());
    auto same = [](const Quat& x, const Quat& y) { return std::fabs((x.conj() * y).w) > 1 - 1e-12; };
    CHECK(same(m.binds[shoulder].rot, a));  // the pose is kept, not taken for a bone-axis convention
    CHECK(same(m.binds[vol("CHEST")].rot, r0[vol_node("CHEST")].rot));

    Shape shape;
    std::vector<const DaeModel*> parts{&m};
    CHECK(shape_from_binds(s, parts, nullptr, shape));
    const auto g0 = s.global_pose(Pose(s.size()), &shape);
    CHECK(near(g0[elbow].pos, r0[elbow].pos, 1e-9));  // the elbow keeps SL's rest place: out along the T-pose arm
    CHECK(near(g0[vol_node("L_LOWER_ARM")].pos, r0[vol_node("L_LOWER_ARM")].pos, 1e-9));
    CHECK(near(g0[vol_node("L_HAND")].pos, r0[vol_node("L_HAND")].pos, 1e-9));
    std::vector<float> p, n;
    skin_prop(m, s, g0, &shape, p, n);
    const int on[] = {vol_node("L_UPPER_ARM"), vol_node("L_LOWER_ARM"), vol_node("BELLY"), s.find("mChest"), elbow,
                      vol_node("L_HAND")};
    for (size_t v = 0; v < verts.size(); ++v) CHECK(near(at(p, v), g0[on[v]].apply(verts[v].off), 1e-6));

    // The belly volume moves 4 cm up: its vertex moves with it, the chest's stays.
    Pose jiggle(s.size());
    jiggle.offset[vol_node("BELLY")] = {0, 0, 0.04};
    std::vector<float> q;
    skin_prop(m, s, s.global_pose(jiggle, &shape), &shape, q, n);
    CHECK(near(at(q, 2), at(p, 2) + Vec3{0, 0, 0.04}, 1e-6));
    CHECK(near(at(q, 3), at(p, 3), 1e-9));
    // Scaled (the shape scales a volume with its joint): the vertex keeps its place relative to the volume, scaled.
    const int torso = s.volumes()[s.find_volume("BELLY")].joint;
    shape.scale[torso] = {1.5, 1.5, 1.5};
    const auto gs = s.global_pose(Pose(s.size()), &shape);
    skin_prop(m, s, gs, &shape, q, n);
    const Xform b = gs[vol_node("BELLY")];
    CHECK(near(at(q, 2), b.pos + b.rot.rotate(verts[2].off * 1.5), 1e-6));
}

// The rig scale is the unit most joints agree on: an A-posed body's arms and hands (a good third of its joints, all
// nearer the origin than SL's T-pose) no longer pull the median off 1 and grow the body by 13 %.
TEST(dae_rig_scale_ignores_posed_arms) {
    std::vector<double> r(35, 1.0);
    for (int i = 0; i < 40; ++i) r.push_back(1.33 + i * 0.0015);
    double measured = 0;
    CHECK_NEAR(rig_scale_of(r, measured), 1.0, 0);
    CHECK(measured > 1.1);
    CHECK_NEAR(rig_scale_of({0.0254, 0.0256, 0.0251}, measured), 0.0254, 0);
    // No unit fits: the declared unit when the median is within a factor of 2 of it, else the median.
    CHECK_NEAR(rig_scale_of({1.2, 1.21, 1.19}, measured), 1.0, 0);
    CHECK_NEAR(rig_scale_of({1.2, 1.21, 1.19}, measured, 0.01), 1.2, 0);
    CHECK_NEAR(rig_scale_of({0.66, 0.70, 0.74}, measured, 1), 1.0, 0);
    CHECK_NEAR(rig_scale_of({0.4, 0.41, 0.39}, measured, 1), 0.4, 0);
}

// A creature rigged with its own joint positions, far from SL's (legs raised, torso stretched), and exported in
// metres: no unit fits the ratios, and the median (about 0.7) used to shrink the whole body to 70 % of what SL
// uploads. The declared metre wins, and the binds keep the file's joint positions.
TEST(dae_rig_scale_keeps_custom_proportions) {
    Rig rig;
    rig.asset = "<unit meter=\"1\"/>";
    rig.ibms = ibm_text(rest("mPelvis") * 1.35) + ibm_text(rest("mChest") * 1.5);
    DaeModel m;
    DaeReport r;
    CHECK(load(rig.text(), m, r));
    CHECK(r.measured_scale > 0.6 && r.measured_scale < 0.8);
    CHECK_NEAR(r.scale, 1, 0);
    CHECK(near(m.binds[skel().find("mPelvis")].pos, rest("mPelvis") * 1.35));
    CHECK(near(m.binds[skel().find("mChest")].pos, rest("mChest") * 1.5));
}

// Guards: a vertex with no usable weight stays where it was bound (not at the origin), and a broken bind
// never yields a non-finite position.
TEST(dae_skin_guards_never_fling) {
    const Skeleton& s = skel();
    DaeModel m;
    DaeReport r;
    CHECK(load(Rig().text(), m, r));
    m.weights[0] = m.weights[1] = m.weights[2] = m.weights[3] = 0;  // vertex 0: no weight at all
    m.binds[s.find("mChest")].pos = Vec3{NAN, 0, 0};
    std::vector<float> p, n;
    skin_prop(m, s, s.global_pose(Pose(s.size())), nullptr, p, n);
    CHECK(near(at(p, 0), at(m.positions, 0)));
    for (float x : p) CHECK(std::isfinite(x));
}

TEST(dae_rig_scale_snaps) {
    // Centimetres, with a 1 % error in the binds: measured about 0.0099, snapped to 0.01.
    // unit@meter is far from these binds (a factor of 100), so the declared-unit fallback does not apply.
    Rig rig;
    rig.asset = "<unit meter=\"0.0001\"/>";
    rig.ibms = ibm_text(rest("mPelvis") * 101) + ibm_text(rest("mChest") * 101);
    rig.positions = "0 0 110  0 0 130  10 0 120";
    DaeModel m;
    DaeReport r;
    CHECK(load(rig.text(), m, r));
    CHECK_NEAR(r.measured_scale, 1.0 / 101, 1e-9);
    CHECK_NEAR(r.scale, 0.01, 0);
    CHECK(near(at(m.positions, 2), {0.1, 0, 1.2}));
    CHECK(near(m.binds[skel().find("mPelvis")].pos, rest("mPelvis") * 1.01));

    // A 3 % error still snaps (03 P13); 6 % does not.
    rig.ibms = ibm_text(rest("mPelvis") * 103) + ibm_text(rest("mChest") * 103);
    CHECK(load(rig.text(), m, r));
    CHECK_NEAR(r.scale, 0.01, 0);
    rig.ibms = ibm_text(rest("mPelvis") * 106) + ibm_text(rest("mChest") * 106);
    CHECK(load(rig.text(), m, r));
    CHECK_NEAR(r.scale, 1.0 / 106, 1e-9);
}

TEST(dae_missing_inv_bind) {
    Rig rig;
    rig.ibms = "0";  // an INV_BIND source with no complete matrix: every joint uses the SL rest pose
    DaeModel m;
    DaeReport r;
    CHECK(load(rig.text(), m, r));
    CHECK(m.rigged);
    CHECK_NEAR(r.scale, 1, 0);
    CHECK_EQ(r.measured_scale, 0.0);
    std::vector<Xform> g = skel().global_pose(Pose(skel().size()));
    CHECK(near(m.binds[skel().find("mPelvis")].pos, g[skel().find("mPelvis")].pos));
}

TEST(dae_idref_and_sid_joints) {
    Rig rig;
    rig.idref = true;
    rig.joints = "jp jc jx jb jh";
    rig.joint_nodes = R"(<node id="rig2" type="JOINT"><node id="jc" sid="rig_mChest" type="JOINT"/><node id="jx" name="Bone" type="JOINT"/>
<node id="jb" name="BELLY" type="JOINT"/><node id="jh" name="mHead" type="JOINT"/></node>)";
    DaeModel m;
    DaeReport r;
    CHECK(load(rig.text(), m, r));
    CHECK_EQ(r.unmapped_joints, std::vector<std::string>{"Bone"});  // reported by node name
    CHECK_EQ(r.skipped_joint_nodes, 7);
    CHECK_EQ(joints_of(m, 0)[1], skel().find("mHead"));

    // A Name_array holding sids (step 5): J1 is the sid of the node named mPelvis.
    rig = Rig();
    rig.joints = "J1 mChest Nope ns:BELLY mHead";
    CHECK(load(rig.text(), m, r));
    CHECK_EQ(r.unmapped_joints, std::vector<std::string>{"Nope"});
    CHECK_EQ(joints_of(m, 0)[2], skel().find("mPelvis"));
}

TEST(dae_unmapped_skin_is_static) {
    Rig rig;
    rig.joints = "Foo Bar Baz Qux Quux";
    rig.bind_shape = "1 0 0 0 0 1 0 0 0 0 1 1 0 0 0 1";
    rig.node_transform = "<translate>1 0 0</translate>";
    DaeModel m;
    DaeReport r;
    CHECK(load(rig.text(), m, r));
    CHECK(!m.rigged && !r.rigged && r.skins_as_static);
    CHECK(m.joints.empty() && m.binds.empty());
    CHECK_EQ(r.unmapped_joints.size(), size_t(5));
    CHECK(near(at(m.positions, 0), {1, 0, 2.1}));  // node transform x bind shape
    std::vector<float> p, n;
    skin_prop(m, skel(), skel().global_pose(Pose(skel().size())), nullptr, p, n);
    CHECK(p == m.positions);
}

TEST(dae_bad_indices_never_crash) {
    DaeModel m;
    DaeReport r;
    std::string prims = "<triangles count=\"3\">" + vin("g") + "<p>0 1 2  0 1 99  0 1 -5  0 1</p></triangles>" +
                        "<polylist count=\"2\">" + vin("g") + "<vcount>3 1000</vcount><p>0 1 2 0</p></polylist>" +
                        "<triangles count=\"1\">" + vin("g") +
                        "<input semantic=\"NORMAL\" source=\"#g-n\" offset=\"1\"/><p>0 0 1 7 2 0</p></triangles>" +
                        "<triangles count=\"1\">" + vin("g") + "<p>99999999999999999999 0 1</p></triangles>";
    std::string g = geometries(geometry("g", "0 0 0  1 0 0  0 1 0", prims, source("g-n", "0 0 1", 3)));
    CHECK(load(doc("", g, "<node><instance_geometry url=\"#g\"/><instance_geometry url=\"#missing\"/></node>"), m, r));
    CHECK_EQ(r.triangles, 2);
    CHECK(!r.warnings.empty());

    // Out-of-range joint and weight indices in the skin are skipped.
    Rig rig;
    rig.v = "0 0 9 1 0 77 -1 2 3 3 4 4  0 5  3 3";
    CHECK(load(rig.text(), m, r));
    CHECK_EQ(joints_of(m, 0)[0], dae_root(skel()));  // -1 = bind shape -> root, 0.3
    rig.vcount = "1000000 5";
    CHECK(load(rig.text(), m, r));
}

TEST(dae_malformed_files_fail_with_reason) {
    auto fails = [](const std::string& text, const char* expect) {
        DaeModel m;
        DaeReport r;
        std::string err;
        bool ok = load_dae(text, "", skel(), m, r, err);
        CHECK(!ok);
        if (err.find(expect) == std::string::npos) check::fail(__FILE__, __LINE__, "err = " + err);
    };
    fails("", "no root element");
    fails("<COLLADA><asset>", "missing closing tag");
    fails("<foo/>", "not a COLLADA document");
    fails("<COLLADA/>", "no visual scene");
    fails(doc("", "", "<node/>"), "no triangles");
    fails(doc("", geometries(geometry("g", "0 0 x 1 0 0 0 1 0", "<triangles>" + vin("g") + "<p>0 1 2</p></triangles>")),
              "<node><instance_geometry url=\"#g\"/></node>"),
          "unreadable source #g-p");
}

TEST(dae_fuzz_mutations_never_crash) {
    Rig rig;
    std::string seeds[] = {rig.text(), doc("<up_axis>Y_UP</up_axis>", kTriangle, "<node><instance_geometry url=\"#g\"/></node>")};
    std::mt19937 rng(1234);
    const char alphabet[] = "0123456789 -.eE<>/\"#_:|x";
    int loaded = 0;
    std::vector<Xform> g = skel().global_pose(Pose(skel().size()));
    for (int iter = 0; iter < 3000; ++iter) {
        std::string t = seeds[iter % 2];
        int edits = 1 + static_cast<int>(rng() % 4);
        for (int e = 0; e < edits && !t.empty(); ++e) {
            size_t at = rng() % t.size();
            switch (rng() % 4) {
                case 0: t[at] = alphabet[rng() % (sizeof alphabet - 1)]; break;
                case 1: t.erase(at, 1 + rng() % 8); break;
                case 2: t.insert(at, t.substr(rng() % t.size(), 1 + rng() % 16)); break;
                default: t.insert(at, std::to_string(static_cast<int>(rng()) % 100000)); break;
            }
        }
        DaeModel m;
        DaeReport r;
        std::string err;
        if (load_dae(t, "", skel(), m, r, err)) {
            ++loaded;
            for (std::uint32_t i : m.indices) CHECK(i < static_cast<std::uint32_t>(m.vertex_count()));
            CHECK(m.uvs.size() == m.positions.size() / 3 * 2 && m.normals.size() == m.positions.size());
            std::vector<float> p, n;
            skin_prop(m, skel(), g, &skel().male_shape(), p, n);
        } else {
            CHECK(!err.empty());
        }
    }
    CHECK(loaded > 100);
}

TEST(prop_json_round_trip) {
    Json in;
    std::string err;
    CHECK(parse_json(R"([{"path": "props/Chair Big.dae", "bone": "mPelvis", "pos": [1, 2, 3], "rot": [0, 90, 0],
        "scale": 2, "rigged": true, "future": {"x": 1}},
        {"path": "C:\\art\\cup.dae", "name": "Cup", "point": "Right Hand", "visible": false, "lib_id": "abc",
         "scale": [1, 2, 3]}])", in, err));
    std::vector<Prop> props;
    CHECK(props_from_json(in, props, err));
    CHECK_EQ(props.size(), size_t(2));
    CHECK_EQ(props[0].name, std::string("Chair Big"));  // file stem
    CHECK((props[0].scale == Vec3{2, 2, 2}));
    CHECK((props[0].pos == Vec3{1, 2, 3}) && (props[0].rot == Vec3{0, 90, 0}));
    CHECK(props[0].rigged && props[0].visible && props[0].point.empty());
    CHECK(props[0].extra.find("future"));
    CHECK_EQ(props[1].name, std::string("Cup"));
    CHECK(!props[1].visible && props[1].lib_id == "abc" && props[1].point == "Right Hand");

    Json out = props_to_json(props);
    std::vector<Prop> again;
    CHECK(props_from_json(out, again, err));
    CHECK_EQ(write_json(props_to_json(again)), write_json(out));
    CHECK(out.arr[0].find("future") && out.arr[0].find("visible"));

    Json bad;
    CHECK(parse_json(R"([{"path": "a.dae"}, {"pos": [1, 2]}])", bad, err));
    std::vector<Prop> keep = props;
    CHECK(!props_from_json(bad, keep, err));
    CHECK_EQ(err, std::string("props[1].pos: expected 3 numbers"));
    CHECK_EQ(keep.size(), size_t(2));
}

TEST(prop_paths_relative_and_absolute) {
    // An absolute path needs a drive on Windows; stored paths always use forward slashes.
#ifdef _WIN32
    const std::string R = "C:";
#else
    const std::string R;
#endif
    auto same = [](const std::string& a, const std::string& b) { return std::filesystem::path(a) == std::filesystem::path(b); };
    CHECK_EQ(prop_path_to_stored(R + "/home/o/proj/props/a.dae", R + "/home/o/proj"), std::string("props/a.dae"));
    CHECK_EQ(prop_path_to_stored(R + "/home/o/meshes/a.dae", R + "/home/o/proj/"), std::string("../meshes/a.dae"));
    CHECK_EQ(prop_path_to_stored(R + "/home/o/meshes/a.dae", ""), R + "/home/o/meshes/a.dae");
    CHECK(same(prop_path_from_stored("../meshes/a.dae", R + "/home/o/proj"), R + "/home/o/meshes/a.dae"));
    CHECK(same(prop_path_from_stored(R + "/abs/a.dae", R + "/home/o/proj"), R + "/abs/a.dae"));
    CHECK(prop_path_from_stored("", R + "/home/o/proj").empty());  // no file, not the folder (an audio track with a BPM only)
    CHECK(prop_path_to_stored("", R + "/home/o/proj").empty());
}

TEST(path_inside_resolves_before_comparing) {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "vats_path_inside_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "help" / "examples");
    fs::create_directories(base / "helpers");
    const std::string help = (base / "help").string(), ex = (base / "help" / "examples" / "a.vat").string();
    CHECK(path_inside(ex, help));
    CHECK(path_inside(ex, help + "/"));
    CHECK(path_inside(help, help));
    CHECK(path_inside((base / "helpers" / ".." / "help" / "examples" / "a.vat").string(), help));
    CHECK(!path_inside((base / "helpers" / "a.vat").string(), help));  // a shared prefix is not inside
    CHECK(!path_inside((base / "help" / ".." / "b.vat").string(), help));
    CHECK(!path_inside(ex, ""));
    CHECK(!path_inside("", help));
#ifndef _WIN32  // links need no privilege here
    fs::create_directory_symlink(base / "help", base / "link", ec);
    CHECK(!ec);
    CHECK(path_inside((base / "link" / "examples" / "a.vat").string(), help));
    CHECK(path_inside(ex, (base / "link").string()));
#endif
    const fs::path cwd = fs::current_path();
    fs::current_path(base, ec);
    CHECK(!ec && path_inside("help/examples/a.vat", help));
    fs::current_path(cwd, ec);
    fs::remove_all(base, ec);
}

TEST(prop_library_formats) {
    std::vector<PropLibraryItem> items;
    std::string err;
    CHECK(load_prop_library(R"({"format": "vats-prop-library", "items": [
        {"id": "1700000000_4242", "name": "Chair", "path": "/m/chair.dae", "rigged": false, "bone": "",
         "point": "Right Hand", "pos": [0, 0, 0.1], "rot": [0, 0, 90], "scale": 1.5}]})", items, err));
    CHECK_EQ(items.size(), size_t(1));
    CHECK_EQ(items[0].id, std::string("1700000000_4242"));
    CHECK_EQ(items[0].prop.point, std::string("Right Hand"));
    CHECK((items[0].prop.scale == Vec3{1.5, 1.5, 1.5}));

    items.push_back({new_library_id(), {}});
    items.back().prop.path = "/m/cup.dae";
    items.back().prop.name = "Cup";
    CHECK_EQ(items.back().id.size(), size_t(36));
    CHECK_EQ(items.back().id[14], '4');
    std::string text = save_prop_library(items);
    CHECK(text.find("\"vats-prop-library\"") != std::string::npos);
    CHECK(text.find("visible") == std::string::npos);
    std::vector<PropLibraryItem> again;
    CHECK(load_prop_library(text, again, err));
    CHECK_EQ(save_prop_library(again), text);

    CHECK(!load_prop_library(R"({"format": "vats-pose-library", "items": []})", again, err));
    CHECK(!load_prop_library(R"({"format": "vats-prop-library", "items": [3]})", again, err));
    CHECK_EQ(err, std::string("items[0]: expected an object"));
}

// The starter props (app/assets/props/props.json): each file loads as a small static mesh with its faces
// turned outwards, the slugs are unique, and each category is one run (the Inventory grid heads each run).
TEST(starter_props_load) {
    const std::string dir = VATS_ASSETS_DIR "/props/";
    std::ifstream f(dir + "props.json", std::ios::binary);
    CHECK(bool(f));
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()), err;
    Json doc;
    std::vector<Prop> props;
    CHECK(parse_json(text, doc, err) && props_from_json(doc, props, err));
    CHECK(props.size() > 30);
    std::vector<std::string> slugs, categories;
    for (const Prop& p : props) {
        const Json *slug = p.extra.find("slug"), *file = p.extra.find("file"), *cat = p.extra.find("category");
        CHECK(slug && slug->is_string() && file && file->is_string() && cat && cat->is_string());
        CHECK(std::find(slugs.begin(), slugs.end(), slug->str) == slugs.end());
        slugs.push_back(slug->str);
        if (categories.empty() || categories.back() != cat->str) {
            CHECK(std::find(categories.begin(), categories.end(), cat->str) == categories.end());
            categories.push_back(cat->str);
        }
        std::ifstream d(dir + file->str, std::ios::binary);
        std::string xml((std::istreambuf_iterator<char>(d)), std::istreambuf_iterator<char>());
        DaeModel m;
        DaeReport r;
        CHECK(load(xml, m, r, dir));
        CHECK(!m.rigged && m.triangle_count() > 0 && r.warnings.empty());
        Vec3 size = m.bounds_max - m.bounds_min;
        double longest = std::max({size.x, size.y, size.z});
        CHECK(longest > 0.05 && longest < 3.0);
        double volume = 0;  // signed, times 6: positive when the faces point outwards
        for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
            volume += at(m.positions, m.indices[t]).dot(at(m.positions, m.indices[t + 1]).cross(at(m.positions, m.indices[t + 2])));
        if (volume <= 0) std::fprintf(stderr, "  %s: faces point inwards\n", file->str.c_str());
        CHECK(volume > 0);
    }
}

// Prop placement (vats::prop_frame, prop_local, prop_pos_for), the one set of maths the app, vats_example_review
// and the examples share: the mesh is placed by the centre of its bounding box, not its origin. A grip offset set
// with prop_pos_for puts the chosen model point in the fist's hole, and the starter props' grips hold where they
// should: the Sword by the hilt just below the guard, not by the blade (a tool that skipped the centring once put
// the fist 33 cm up the sword).
TEST(prop_grip_placement) {
    const std::vector<Xform> g = skel().global_pose(Pose(skel().size()));
    const int rhand = skel().find("Right Hand");
    CHECK(rhand >= 0);
    // Where a model point of a prop ends up, and the model point that sits in the Right Hand's hole.
    auto world = [&](const Prop& p, const DaeModel& m, const Vec3& v) {
        return prop_frame(p, skel(), g).apply(prop_local(p, m, v));
    };
    auto in_hole = [&](const Prop& p, const DaeModel& m) {
        const Vec3 l = prop_frame(p, skel(), g).inverse().apply(g[rhand].apply(grip_hole(false)));
        return (m.bounds_min + m.bounds_max) * 0.5 + Vec3{l.x / p.scale.x, l.y / p.scale.y, l.z / p.scale.z};
    };
    // A 1 m rod from z -0.2 to 0.8: its origin is 0.3 m below the middle of its box.
    DaeModel rod;
    rod.bounds_min = {-0.01, -0.01, -0.2}, rod.bounds_max = {0.01, 0.01, 0.8};
    Prop p;
    p.point = "Right Hand", p.rot = {0, 90, 0}, p.scale = {1, 1, 2};
    CHECK(near(prop_local(p, rod, {0, 0, 0.3}), {0, 0, 0}));  // the box's centre is the placed point
    p.pos = prop_pos_for(p, rod, {0, 0, 0}, grip_hole(false));
    CHECK(near(world(p, rod, {0, 0, 0}), g[rhand].apply(grip_hole(false))));
    CHECK(near(in_hole(p, rod), {0, 0, 0}));
    CHECK(near(world(p, rod, {0, 0, 0.1}) - world(p, rod, {0, 0, 0}), g[rhand].rot.rotate({0.2, 0, 0})));  // scaled
    Prop w = p;  // no parent: the world frame
    w.point.clear();
    CHECK(near(prop_frame(w, skel(), g, Xform{{}, {1, 2, 3}}).pos, Vec3{1, 2, 3} + p.pos));

    // The starter props at their props.json grips.
    const std::string dir = VATS_ASSETS_DIR "/props/";
    std::ifstream f(dir + "props.json", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()), err;
    Json doc;
    std::vector<Prop> props;
    CHECK(parse_json(text, doc, err) && props_from_json(doc, props, err));
    auto starter = [&](const std::string& slug, DaeModel& m) {
        for (Prop q : props)
            if (q.extra.find("slug")->str == slug) {
                std::ifstream d(dir + q.extra.find("file")->str, std::ios::binary);
                std::string xml((std::istreambuf_iterator<char>(d)), std::istreambuf_iterator<char>());
                DaeReport r;
                CHECK(load(xml, m, r, dir));
                q.point = q.extra.find("suggested_point")->str;
                return q;
            }
        CHECK(false);
        return Prop{};
    };
    DaeModel m;
    // Sword: the grip runs from z -0.12 to the guard at 0.105; the fist's middle is 5.5 cm below the guard, on the axis.
    const Vec3 h = in_hole(starter("sword", m), m);
    CHECK(std::fabs(h.z - 0.05) < 0.005 && std::hypot(h.x, h.y) < 0.002);
    // Pistol and shotgun: the model's origin is the grip; the rifle's pistol grip is 1 cm behind and 2 cm below it.
    CHECK(in_hole(starter("pistol", m), m).length() < 0.002);
    CHECK(in_hole(starter("shotgun", m), m).length() < 0.002);
    CHECK(near(in_hole(starter("rifle", m), m), {-0.01, 0, -0.02}, 0.002));
}
