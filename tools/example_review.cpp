// Viewport Avatar Toolset - frame strips and checks for reviewing an example animation without the app.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
//
// Usage: vats_example_review <project.vat> <out prefix> [options]
//   --body female|male    the SL default body to pose (female: the app's default)
//   --step n              every n-th frame (2); --from f, --to f: the range (the whole clip)
//   --views a,b,...       front, side (from the avatar's right, forward to the right), left, q34 (front right),
//                         q34l (front left), back (front,side,q34)
//   --tile px             tile height (260); --follow 1: keep the hips in the middle of each tile, for a walk
//                         that travels; --near r: crop to r metres round the right hand (a close-up of a grip)
//   --clip n              the project's clip n instead of the active one
// Writes <prefix>-<body>-<view>.ppm, one sheet per view (every frame drawn from its own world position, ground
// ticks every 10 cm, ankles and toes marked: left blue, right red), and <prefix>-<body>-metrics.txt, one line per
// frame: the hips, the ankles and toes (x forward and height above the ground), the lowest sole point (heel, ball
// or toe tip, as the Animation Check measures the ground: 0 is the soles at rest), and
// how many prop vertices are inside the body mesh and how deep. Prints how many frames have a prop inside the body.
// A prop vertex is inside when rays from it along +X, +Y and +Z each cross the skinned body mesh an odd number of
// times (two of the three agree); the 10 cm round each fist are skipped, where a grip belongs. See docs/wiki/STYLE.md,
// Example animations.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "vats/avatar_mesh.h"
#include "vats/clips.h"
#include "vats/fbx.h"
#include "vats/footlock.h"
#include "vats/project.h"
#include "vats/prop.h"
#include "vats/rig.h"

using namespace vats;

namespace {

std::string read_text(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct Img {
    int w, h;
    std::vector<unsigned char> px;
    std::vector<float> z;
    Img(int w_, int h_, unsigned char bg) : w(w_), h(h_), px(size_t(w_) * h_ * 3, bg), z(size_t(w_) * h_, 1e9f) {}
    void set(int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        unsigned char* p = &px[(size_t(y) * w + x) * 3];
        p[0] = static_cast<unsigned char>(r), p[1] = static_cast<unsigned char>(g), p[2] = static_cast<unsigned char>(b);
    }
    void blit(const Img& o, int ox, int oy) {
        for (int y = 0; y < o.h; ++y)
            for (int x = 0; x < o.w; ++x) {
                const unsigned char* s = &o.px[(size_t(y) * o.w + x) * 3];
                set(ox + x, oy + y, s[0], s[1], s[2]);
            }
    }
};

// The frame number in a 3 x 5 digit font.
void label(Img& im, int x, int y, const std::string& s, int sc) {
    static const char* digits[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111",
                                     "101101111001001", "111100111001111", "111100111101111", "111001001001001",
                                     "111101111101111", "111101111001111"};
    for (char c : s) {
        if (c >= '0' && c <= '9')
            for (int i = 0; i < 15; ++i)
                if (digits[c - '0'][i] == '1')
                    for (int dy = 0; dy < sc; ++dy)
                        for (int dx = 0; dx < sc; ++dx) im.set(x + (i % 3) * sc + dx, y + (i / 3) * sc + dy, 255, 230, 120);
        x += 4 * sc;
    }
}

struct View {
    std::string name;
    Vec3 dir, right;  // dir: from the camera into the scene
};

// An orthographic view from a camera turned yaw degrees round the avatar (0 = in front of it, on +X).
View make_view(const std::string& name) {
    const double yaw = name == "side" ? -90 : name == "left" ? 90 : name == "q34" ? -40 : name == "q34l" ? 40
                     : name == "back" ? 180 : 0;
    const Vec3 d{-std::cos(yaw * kDegToRad), -std::sin(yaw * kDegToRad), 0};
    return {name, d, d.cross({0, 0, 1}).normalized()};
}

struct Tri {
    Vec3 a, b, c;
    int r, g, bl;
};

// Whether a point is inside a closed-ish triangle mesh: rays along +X, +Y and +Z, two of three crossing it an odd
// number of times.
bool inside(const Vec3& q, const std::vector<float>& pos, const std::vector<std::uint32_t>& ix) {
    int odd = 0;
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        int hits = 0;
        for (size_t t = 0; t + 2 < ix.size(); t += 3) {
            const Vec3 a{pos[ix[t] * 3], pos[ix[t] * 3 + 1], pos[ix[t] * 3 + 2]};
            const Vec3 b{pos[ix[t + 1] * 3], pos[ix[t + 1] * 3 + 1], pos[ix[t + 1] * 3 + 2]};
            const Vec3 c{pos[ix[t + 2] * 3], pos[ix[t + 2] * 3 + 1], pos[ix[t + 2] * 3 + 2]};
            const double d1 = (b[u] - a[u]) * (q[v] - a[v]) - (b[v] - a[v]) * (q[u] - a[u]);
            const double d2 = (c[u] - b[u]) * (q[v] - b[v]) - (c[v] - b[v]) * (q[u] - b[u]);
            const double d3 = (a[u] - c[u]) * (q[v] - c[v]) - (a[v] - c[v]) * (q[u] - c[u]);
            if (!((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0))) continue;
            const double area = (b[u] - a[u]) * (c[v] - a[v]) - (b[v] - a[v]) * (c[u] - a[u]);
            if (std::fabs(area) < 1e-12) continue;
            if ((d2 * a[axis] + d3 * b[axis] + d1 * c[axis]) / area > q[axis]) ++hits;
        }
        odd += hits & 1;
    }
    return odd >= 2;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <project.vat> <out prefix> [--body female|male] [--step n] [--from f] "
                             "[--to f] [--views front,side,q34] [--tile px] [--follow 1] [--near r] [--clip n]\n", argv[0]);
        return 2;
    }
    const std::string vat = argv[1], out = argv[2];
    std::string bodyname = "female", views_s = "front,side,q34";
    int step = 2, tile = 260, clip_i = -1;
    double from = -1, to = -1, near_r = 0;
    bool follow = false;
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string a = argv[i], b = argv[i + 1];
        if (a == "--body") bodyname = b;
        else if (a == "--step") step = std::max(1, std::atoi(b.c_str()));
        else if (a == "--from") from = std::atof(b.c_str());
        else if (a == "--to") to = std::atof(b.c_str());
        else if (a == "--tile") tile = std::atoi(b.c_str());
        else if (a == "--views") views_s = b;
        else if (a == "--clip") clip_i = std::atoi(b.c_str());
        else if (a == "--follow") follow = b == "1";
        else if (a == "--near") near_r = std::atof(b.c_str());
    }
    const std::string character = std::string(VATS_ROOT) + "/data/character";
    Skeleton skel;
    AvatarMesh mesh;
    std::string err;
    if (!skel.load_dir(character, err) || !mesh.load(skel, character, err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    const Body body = bodyname == "male" ? Body::SLDefaultMale : Body::SLDefault;
    mesh.build(body);
    const Shape* shape = mesh.shape(body);
    Project p;
    if (!load_project(read_text(vat), p, err, vat)) return std::fprintf(stderr, "%s: %s\n", vat.c_str(), err.c_str()), 1;
    if (clip_i >= 0 && clip_i < int(p.clips.size())) set_active_clip(p, clip_i);
    const Clip& c = p.clip;
    const Rig rig(skel);
    // Starter props by their library id, from app/assets/props.
    struct PropMesh {
        Prop prop;
        DaeModel model;
    };
    std::vector<PropMesh> props;
    for (const Prop& pr : c.props) {
        const std::string slug = pr.lib_id.rfind("starter-", 0) == 0 ? pr.lib_id.substr(8) : "";
        PropMesh pm{pr, {}};
        DaeReport rep;
        if (load_mesh_file(std::string(VATS_ROOT) + "/app/assets/props/" + slug + ".dae", skel, pm.model, rep, err))
            props.push_back(std::move(pm));
        else
            std::fprintf(stderr, "prop %s: %s\n", pr.name.c_str(), err.c_str());
    }
    if (from < 0) from = 0;
    if (to < 0) to = c.end_frame;
    std::vector<double> frames;
    for (double f = from; f <= to + 1e-9; f += step) frames.push_back(f);
    std::vector<View> views;
    std::stringstream ss(views_s);
    for (std::string t; std::getline(ss, t, ',');) views.push_back(make_view(t));

    std::vector<float> pos, nrm;
    const double ground = sole_floor(skel, shape);  // the soles at rest, as the Animation Check's ground rule

    struct FrameGeo {
        std::vector<Tri> tris;
        std::vector<Vec3> pts;
        std::vector<Xform> g;
    };
    std::vector<FrameGeo> geo;
    const int ankL = skel.find("mAnkleLeft"), ankR = skel.find("mAnkleRight"), toeL = skel.find("mToeLeft"),
              toeR = skel.find("mToeRight"), pelvis = skel.find("mPelvis"), rhand = skel.find("Right Hand"),
              lhand = skel.find("Left Hand");
    FILE* mf = std::fopen((out + "-" + bodyname + "-metrics.txt").c_str(), "w");
    if (!mf) return std::fprintf(stderr, "cannot write %s-%s-metrics.txt\n", out.c_str(), bodyname.c_str()), 1;
    std::fprintf(mf, "# frame | hips x y z | left ankle x z, right ankle x z, left toe x z, right toe x z (z above the "
                     "ground) | lowest sole point (the Animation Check's ground measure) | prop vertices inside the body: count, deepest cm\n");
    int penetrating = 0;
    for (double f : frames) {
        FrameGeo fg;
        const Evaluation e = evaluate(rig, c, f, shape);
        fg.g = e.globals;
        mesh.skin(e.globals, shape, pos, nrm);
        const auto& ix = mesh.indices();
        for (size_t i = 0; i + 2 < ix.size(); i += 3) {
            Vec3 v[3];
            for (int k = 0; k < 3; ++k) v[k] = {pos[ix[i + k] * 3], pos[ix[i + k] * 3 + 1], pos[ix[i + k] * 3 + 2]};
            fg.tris.push_back({v[0], v[1], v[2], 200, 196, 205});
        }
        for (size_t i = 0; i < pos.size(); i += 3) fg.pts.push_back({pos[i], pos[i + 1], pos[i + 2]});
        const double low = sole_height(skel, e.globals);  // the Animation Check's measure
        int in = 0;
        double deepest = 0;
        const Vec3 fists[2] = {e.globals[rhand].apply(grip_hole(false)), e.globals[lhand].apply(grip_hole(true))};
        for (const PropMesh& pm : props) {
            const Xform frame = prop_frame(pm.prop, skel, e.globals);  // placed as the app places it
            std::vector<Vec3> w(pm.model.vertex_count());
            for (int v = 0; v < pm.model.vertex_count(); ++v)
                w[v] = frame.apply(prop_local(
                    pm.prop, pm.model, {pm.model.positions[v * 3], pm.model.positions[v * 3 + 1], pm.model.positions[v * 3 + 2]}));
            for (size_t i = 0; i + 2 < pm.model.indices.size(); i += 3)
                fg.tris.push_back({w[pm.model.indices[i]], w[pm.model.indices[i + 1]], w[pm.model.indices[i + 2]], 235, 150, 60});
            for (size_t i = 0; i < w.size(); ++i) {
                fg.pts.push_back(w[i]);
                if (i % 2 || (w[i] - fists[0]).length() < 0.1 || (w[i] - fists[1]).length() < 0.1 || !inside(w[i], pos, ix))
                    continue;
                ++in;
                double d = 1e9;
                for (size_t k = 0; k < pos.size(); k += 3) d = std::min(d, (Vec3{pos[k], pos[k + 1], pos[k + 2]} - w[i]).length());
                deepest = std::max(deepest, d);
            }
        }
        penetrating += in > 0;
        auto at = [&](int n) { return e.globals[n].pos; };
        std::fprintf(mf, "%5.1f | %6.3f %6.3f %6.3f | %6.3f %6.3f  %6.3f %6.3f  %6.3f %6.3f  %6.3f %6.3f | %6.3f | %d %.1f\n", f,
                     at(pelvis).x, at(pelvis).y, at(pelvis).z, at(ankL).x, at(ankL).z - ground, at(ankR).x,
                     at(ankR).z - ground, at(toeL).x, at(toeL).z - ground, at(toeR).x, at(toeR).z - ground, low - ground, in,
                     deepest * 100);
        geo.push_back(std::move(fg));
    }
    std::fprintf(mf, "# frames with a prop inside the body: %d of %zu\n", penetrating, frames.size());
    std::fclose(mf);
    std::printf("%s-%s: frames with a prop inside the body: %d of %zu\n", out.c_str(), bodyname.c_str(), penetrating,
                frames.size());

    const Vec3 light = Vec3{0.5, -0.4, 0.8}.normalized(), up{0, 0, 1};
    for (const View& v : views) {
        auto shift = [&](const FrameGeo& fg) { return follow ? fg.g[pelvis].pos.dot(v.right) : 0.0; };
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;  // one crop for every tile, so the motion reads
        for (const FrameGeo& fg : geo)
            for (const Vec3& q : fg.pts) {
                if (near_r > 0 && (q - fg.g[rhand].pos).length() > near_r) continue;
                const double sx = q.dot(v.right) - shift(fg), sy = q.dot(up);
                x0 = std::min(x0, sx), x1 = std::max(x1, sx), y0 = std::min(y0, sy), y1 = std::max(y1, sy);
            }
        if (near_r <= 0) y0 = std::min(y0, ground);
        x0 -= 0.06, x1 += 0.06, y0 -= 0.06, y1 += 0.06;
        const double scale = tile * 1.4 / (y1 - y0);  // pixels per metre
        const int tw = std::max(80, int((x1 - x0) * scale)), th = int((y1 - y0) * scale);
        const int cols = std::max(1, std::min(int(frames.size()), 2400 / tw)), rows = (int(frames.size()) + cols - 1) / cols;
        Img sheet(cols * tw, rows * th, 20);
        for (size_t fi = 0; fi < geo.size(); ++fi) {
            Img im(tw, th, 38);
            const double sh = shift(geo[fi]);
            auto project = [&](const Vec3& q, double& sx, double& sy, double& sz) {
                sx = (q.dot(v.right) - sh - x0) * scale, sy = (y1 - q.dot(up)) * scale, sz = q.dot(v.dir);
            };
            const int gy = int((y1 - ground) * scale);  // the ground line, with a tick every 10 cm of the world
            for (int x = 0; x < tw; ++x) im.set(x, gy, 90, 90, 100);
            for (double m = std::floor((x0 + sh) * 10) / 10; m < x1 + sh; m += 0.1) {
                const bool major = std::fabs(std::fmod(std::round(m * 10), 5.0)) < 0.5;
                for (int k = 0; k < (major ? 10 : 5); ++k) im.set(int((m - sh - x0) * scale), gy + k, 120, 160, 120);
            }
            for (const Tri& t : geo[fi].tris) {
                double ax, ay, az, bx, by, bz, cx, cy, cz;
                project(t.a, ax, ay, az), project(t.b, bx, by, bz), project(t.c, cx, cy, cz);
                const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
                if (std::fabs(area) < 1e-9) continue;
                const double shade = 0.35 + 0.65 * std::fabs((t.b - t.a).cross(t.c - t.a).normalized().dot(light));
                const int xa = std::max(0, int(std::floor(std::min({ax, bx, cx})))), xb = std::min(tw - 1, int(std::ceil(std::max({ax, bx, cx}))));
                const int ya = std::max(0, int(std::floor(std::min({ay, by, cy})))), yb = std::min(th - 1, int(std::ceil(std::max({ay, by, cy}))));
                for (int y = ya; y <= yb; ++y)
                    for (int x = xa; x <= xb; ++x) {
                        const double px = x + 0.5, py = y + 0.5;
                        const double w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
                        const double w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area, w2 = 1 - w0 - w1;
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                        float& zb = im.z[size_t(y) * tw + x];
                        const float depth = float(w0 * az + w1 * bz + w2 * cz);
                        if (depth >= zb) continue;
                        zb = depth;
                        im.set(x, y, int(t.r * shade), int(t.g * shade), int(t.bl * shade));
                    }
            }
            for (auto [n, r, g, b] : {std::tuple{ankL, 80, 140, 255}, std::tuple{toeL, 80, 140, 255},
                                      std::tuple{ankR, 255, 80, 80}, std::tuple{toeR, 255, 80, 80}}) {
                double sx, sy, sz;
                project(geo[fi].g[n].pos, sx, sy, sz);
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx) im.set(int(sx) + dx, int(sy) + dy, r, g, b);
            }
            label(im, 4, 4, std::to_string(int(std::lround(frames[fi]))), 3);
            for (int x = 0; x < tw; ++x) im.set(x, th - 1, 20, 20, 20);
            for (int y = 0; y < th; ++y) im.set(tw - 1, y, 20, 20, 20);
            sheet.blit(im, int(fi % cols) * tw, int(fi / cols) * th);
        }
        std::ofstream f(out + "-" + bodyname + "-" + v.name + ".ppm", std::ios::binary);
        f << "P6\n" << sheet.w << ' ' << sheet.h << "\n255\n";
        f.write(reinterpret_cast<const char*>(sheet.px.data()), std::streamsize(sheet.px.size()));
    }
    return 0;
}
