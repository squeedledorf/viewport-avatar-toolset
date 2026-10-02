// Viewport Avatar Toolset - an unrigged test body made in code: one closed surface round a soft union of capsules (a
// humanoid in a T-pose or an A-pose, optionally with a tail), for rigging from scratch (spec 08 RG-14). The joints it
// was built round are known, so a test can check where the markers and joints land. CC0.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE. The mesh it makes is CC0.
//
// Header-only: tests/test_auto_rig.cpp and tools/make_test_body.cpp (--blob) share it.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "vats/dae.h"

namespace vats::blob {

struct Capsule {
    Vec3 a, b;
    double ra, rb;  // radius at a and at b
};

struct Body {
    std::map<std::string, Vec3> joints;  // marker id -> where the body was built round it (facing +X, Y left, Z up)
    std::vector<Capsule> capsules;
};

// A humanoid 1.75 m tall. a_pose: the arms 40 degrees down from level. tail: a tail out behind the hips. ears: a cat's,
// pointing up from the top of the head.
inline Body humanoid(bool a_pose = false, bool tail = false, bool ears = false) {
    Body b;
    auto& j = b.joints;
    const double arm = a_pose ? 40 * kPi / 180 : 0;
    j["neck"] = {-0.01, 0, 1.47};
    j["chin"] = {0.07, 0, 1.52};
    j["groin"] = {0.06, 0, 0.88};
    for (const int s : {1, -1}) {
        const char* side = s > 0 ? "_l" : "_r";
        const Vec3 shoulder{-0.01, 0.18 * s, 1.40};
        auto along = [&](double d) { return shoulder + Vec3{0, std::cos(arm) * d * s, -std::sin(arm) * d}; };
        j[std::string("shoulder") + side] = shoulder;
        j[std::string("elbow") + side] = along(0.27);
        j[std::string("wrist") + side] = along(0.51);
        j[std::string("hand_tip") + side] = along(0.69);
        j[std::string("hip") + side] = {0.0, 0.095 * s, 0.93};
        j[std::string("knee") + side] = {0.02, 0.09 * s, 0.50};
        j[std::string("ankle") + side] = {-0.01, 0.09 * s, 0.085};
        j[std::string("toe") + side] = {0.16, 0.09 * s, 0.03};
    }
    if (tail) j["tail_tip"] = {-0.62, 0, 0.62};
    if (ears) j["ear_l"] = {-0.01, 0.07, 1.82}, j["ear_r"] = {-0.01, -0.07, 1.82};
    auto cap = [&](Vec3 a, Vec3 c, double ra, double rb) { b.capsules.push_back({a, c, ra, rb}); };
    // Trunk: hips, belly, chest (wide across), neck, head.
    cap({-0.01, -0.08, 0.95}, {-0.01, 0.08, 0.95}, 0.115, 0.115);
    cap({0.0, 0, 0.98}, {0.0, 0, 1.18}, 0.12, 0.11);
    cap({-0.01, -0.09, 1.30}, {-0.01, 0.09, 1.30}, 0.11, 0.11);
    cap({0.0, 0, 1.18}, {-0.01, 0, 1.30}, 0.11, 0.12);
    cap({-0.01, 0, 1.36}, {-0.01, 0, 1.53}, 0.05, 0.045);
    cap({0.02, 0, 1.58}, {0.01, 0, 1.66}, 0.095, 0.09);
    cap({0.06, 0, 1.53}, {0.03, 0, 1.58}, 0.04, 0.06);  // jaw and chin
    for (const int s : {1, -1}) {
        const char* side = s > 0 ? "_l" : "_r";
        auto at = [&](const char* name) { return j[std::string(name) + side]; };
        cap({-0.01, 0.10 * s, 1.36}, at("shoulder"), 0.07, 0.055);  // the shoulder's cap
        cap(at("shoulder"), at("elbow"), 0.05, 0.042);
        cap(at("elbow"), at("wrist"), 0.042, 0.03);
        const Vec3 w = at("wrist"), t = at("hand_tip"), d = (t - w).normalized();
        cap(w + d * 0.04, w + d * 0.11, 0.038, 0.036);  // the palm, wider than the wrist
        cap(w + d * 0.11, t, 0.033, 0.018);
        cap(at("hip"), at("knee"), 0.085, 0.058);
        cap(at("knee"), at("ankle"), 0.055, 0.036);
        cap(at("ankle") + Vec3{-0.03, 0, -0.035}, at("toe"), 0.04, 0.03);
    }
    if (tail) {
        cap({-0.1, 0, 0.95}, {-0.3, 0, 0.88}, 0.05, 0.04);
        cap({-0.3, 0, 0.88}, {-0.47, 0, 0.76}, 0.04, 0.028);
        cap({-0.47, 0, 0.76}, j["tail_tip"], 0.028, 0.012);
    }
    if (ears)
        for (const char* e : {"ear_l", "ear_r"}) cap({0.01, j[e].y * 0.7, 1.68}, j[e], 0.035, 0.008);
    return b;
}

inline double capsule_distance(const Capsule& c, const Vec3& p) {
    const Vec3 d = c.b - c.a;
    const double t = std::clamp((p - c.a).dot(d) / d.dot(d), 0.0, 1.0);
    return (p - (c.a + d * t)).length() - (c.ra + (c.rb - c.ra) * t);
}

// The soft union's field: negative inside.
inline double field(const Body& b, const Vec3& p) {
    const double k = 0.025;
    double f = 1e9;
    for (const Capsule& c : b.capsules) {
        const double g = capsule_distance(c, p);
        const double h = std::clamp(0.5 + 0.5 * (g - f) / k, 0.0, 1.0);
        f = g * (1 - h) + f * h - k * h * (1 - h);  // polynomial smooth minimum
    }
    return f;
}

// The surface by marching tetrahedra on a grid of the given cell size: one closed, consistently wound mesh, as a
// static DaeModel with one part named "Body".
inline DaeModel mesh(const Body& b, double cell = 0.02) {
    Vec3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
    for (const Capsule& c : b.capsules)
        for (const Vec3& p : {c.a, c.b})
            for (int a = 0; a < 3; ++a) lo[a] = std::min(lo[a], p[a] - 0.15), hi[a] = std::max(hi[a], p[a] + 0.15);
    const int nx = int((hi.x - lo.x) / cell) + 2, ny = int((hi.y - lo.y) / cell) + 2, nz = int((hi.z - lo.z) / cell) + 2;
    auto id = [&](int x, int y, int z) { return (std::int64_t(z) * ny + y) * nx + x; };
    auto point = [&](std::int64_t i) {
        const int x = int(i % nx), y = int((i / nx) % ny), z = int(i / (std::int64_t(nx) * ny));
        return Vec3{lo.x + x * cell, lo.y + y * cell, lo.z + z * cell};
    };
    std::vector<float> f(size_t(nx) * size_t(ny) * size_t(nz));
    for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x) f[size_t(id(x, y, z))] = float(field(b, point(id(x, y, z))));
    DaeModel m;
    std::unordered_map<std::uint64_t, std::uint32_t> on_edge;
    auto vertex = [&](std::int64_t i, std::int64_t j) {
        if (i > j) std::swap(i, j);
        const std::uint64_t key = (std::uint64_t(i) << 32) | std::uint64_t(j);
        const auto it = on_edge.find(key);
        if (it != on_edge.end()) return it->second;
        const double fi = f[size_t(i)], fj = f[size_t(j)], t = fi / (fi - fj);
        const Vec3 p = point(i) + (point(j) - point(i)) * t;
        // The field's gradient for the normal.
        const double e = cell * 0.25;
        const Vec3 n = Vec3{field(b, p + Vec3{e, 0, 0}) - field(b, p - Vec3{e, 0, 0}),
                            field(b, p + Vec3{0, e, 0}) - field(b, p - Vec3{0, e, 0}),
                            field(b, p + Vec3{0, 0, e}) - field(b, p - Vec3{0, 0, e})}.normalized();
        const std::uint32_t v = std::uint32_t(m.positions.size() / 3);
        m.positions.insert(m.positions.end(), {float(p.x), float(p.y), float(p.z)});
        m.normals.insert(m.normals.end(), {float(n.x), float(n.y), float(n.z)});
        m.uvs.insert(m.uvs.end(), {0.f, 0.f});
        on_edge.emplace(key, v);
        return v;
    };
    auto tri = [&](std::uint32_t a, std::uint32_t c, std::uint32_t d) {
        if (a == c || c == d || d == a) return;
        // Wound so the face's normal agrees with the field's gradient (outwards).
        auto at = [&](std::uint32_t v) { return Vec3{m.positions[v * 3], m.positions[v * 3 + 1], m.positions[v * 3 + 2]}; };
        auto nrm = [&](std::uint32_t v) { return Vec3{m.normals[v * 3], m.normals[v * 3 + 1], m.normals[v * 3 + 2]}; };
        const Vec3 face = (at(c) - at(a)).cross(at(d) - at(a));
        if (face.dot(nrm(a) + nrm(c) + nrm(d)) < 0) std::swap(c, d);
        m.indices.insert(m.indices.end(), {a, c, d});
    };
    static const int corner[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    static const int tets[6][4] = {{0, 5, 1, 6}, {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6}, {0, 7, 4, 6}, {0, 4, 5, 6}};
    for (int z = 0; z + 1 < nz; ++z)
        for (int y = 0; y + 1 < ny; ++y)
            for (int x = 0; x + 1 < nx; ++x)
                for (const auto& t : tets) {
                    std::int64_t v[4];
                    std::vector<int> in, out;
                    for (int k = 0; k < 4; ++k) {
                        v[k] = id(x + corner[t[k]][0], y + corner[t[k]][1], z + corner[t[k]][2]);
                        (f[size_t(v[k])] < 0 ? in : out).push_back(k);
                    }
                    if (in.empty() || out.empty()) continue;
                    if (in.size() == 1 || out.size() == 1) {
                        const auto& one = in.size() == 1 ? in : out;
                        const auto& rest = in.size() == 1 ? out : in;
                        tri(vertex(v[one[0]], v[rest[0]]), vertex(v[one[0]], v[rest[1]]), vertex(v[one[0]], v[rest[2]]));
                    } else {
                        const std::uint32_t a = vertex(v[in[0]], v[out[0]]), c = vertex(v[in[0]], v[out[1]]),
                                            d = vertex(v[in[1]], v[out[1]]), e = vertex(v[in[1]], v[out[0]]);
                        tri(a, c, d), tri(a, d, e);
                    }
                }
    m.bounds_min = {1e9, 1e9, 1e9}, m.bounds_max = {-1e9, -1e9, -1e9};
    for (size_t i = 0; i < m.positions.size(); ++i) {
        const int a = int(i % 3);
        m.bounds_min[a] = std::min(m.bounds_min[a], double(m.positions[i])), m.bounds_max[a] = std::max(m.bounds_max[a], double(m.positions[i]));
    }
    const std::uint32_t nv = std::uint32_t(m.positions.size() / 3), ni = std::uint32_t(m.indices.size());
    m.materials.push_back({});
    m.groups.push_back({0, 0, nv, 0, ni});
    m.parts.push_back({"Body", 0, nv, 0, ni});
    return m;
}

// The mesh as an unrigged COLLADA file (Z up, metres): a node and a geometry per part (positions and normals), named as
// the part is.
inline std::string static_dae(const DaeModel& m) {
    auto floats = [](const std::vector<float>& v, size_t from, size_t count) {
        std::string s;
        char b[32];
        for (size_t i = from; i < from + count && i < v.size(); ++i) std::snprintf(b, sizeof b, "%.6g ", v[i]), s += b;
        return s;
    };
    std::vector<DaePart> parts = m.parts;
    if (parts.empty()) parts.push_back({"Body", 0, std::uint32_t(m.positions.size() / 3), 0, std::uint32_t(m.indices.size())});
    std::string geo, nodes;
    for (size_t k = 0; k < parts.size(); ++k) {
        const DaePart& p = parts[k];
        const std::string id = "g" + std::to_string(k), nv = std::to_string(p.vertex_count), nf = std::to_string(p.vertex_count * 3);
        std::string idx;
        for (std::uint32_t i = p.first_index; i < p.first_index + p.index_count; ++i) idx += std::to_string(m.indices[i] - p.first_vertex) + " ";
        const std::string acc = "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/>"
                                "<param name=\"Z\" type=\"float\"/></accessor></technique_common></source>";
        geo += "<geometry id=\"" + id + "\" name=\"" + p.name + "\"><mesh>"
               "<source id=\"" + id + "-p\"><float_array id=\"" + id + "-pa\" count=\"" + nf + "\">" +
               floats(m.positions, p.first_vertex * 3, p.vertex_count * 3) + "</float_array><technique_common><accessor source=\"#" + id +
               "-pa\" count=\"" + nv + acc +
               "<source id=\"" + id + "-n\"><float_array id=\"" + id + "-na\" count=\"" + nf + "\">" +
               floats(m.normals, p.first_vertex * 3, p.vertex_count * 3) + "</float_array><technique_common><accessor source=\"#" + id +
               "-na\" count=\"" + nv + acc +
               "<vertices id=\"" + id + "-v\"><input semantic=\"POSITION\" source=\"#" + id + "-p\"/><input semantic=\"NORMAL\" source=\"#" +
               id + "-n\"/></vertices><triangles count=\"" + std::to_string(p.index_count / 3) +
               "\"><input semantic=\"VERTEX\" source=\"#" + id + "-v\" offset=\"0\"/><p>" + idx + "</p></triangles></mesh></geometry>";
        nodes += "<node id=\"n" + std::to_string(k) + "\" name=\"" + p.name + "\"><instance_geometry url=\"#" + id + "\"/></node>";
    }
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" "
           "version=\"1.4.1\">\n<asset><unit name=\"meter\" meter=\"1\"/><up_axis>Z_UP</up_axis></asset>\n<library_geometries>" +
           geo + "</library_geometries>\n<library_visual_scenes><visual_scene id=\"Scene\">" + nodes +
           "</visual_scene></library_visual_scenes>\n<scene><instance_visual_scene url=\"#Scene\"/></scene>\n</COLLADA>\n";
}

}  // namespace vats::blob
