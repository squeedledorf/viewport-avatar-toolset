// Prints where the built-in poses put things, to sanity-check their shapes without the app:
// fingertips relative to the wrist (hand poses, wrist frame) and joint positions relative to the
// pelvis (body poses), in cm. With an output dir it also draws each pose as a PPM (two views).
// Usage: vats_pose_preview <data/character> [out dir]
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "vats/pose_presets.h"

using namespace vats;

namespace {

struct Image {
    int w, h;
    std::vector<unsigned char> px;
    Image(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * h_ * 3, 24) {}
    void dot(int x, int y, unsigned char r, unsigned char g, unsigned char b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        unsigned char* p = &px[(size_t(y) * w + x) * 3];
        p[0] = r, p[1] = g, p[2] = b;
    }
    void line(double x0, double y0, double x1, double y1, unsigned char r, unsigned char g, unsigned char b) {
        int n = int(std::max(std::abs(x1 - x0), std::abs(y1 - y0))) + 1;
        for (int i = 0; i <= n; ++i) {
            double t = double(i) / n;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    dot(int(x0 + (x1 - x0) * t) + dx, int(y0 + (y1 - y0) * t) + dy, r, g, b);
        }
    }
    bool save(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        f << "P6\n" << w << ' ' << h << "\n255\n";
        f.write(reinterpret_cast<const char*>(px.data()), std::streamsize(px.size()));
        return bool(f);
    }
};

// Two panels side by side, each looking along one axis: (a, b) pick the horizontal and vertical axes.
void draw(Image& img, int panel, int a, int b, double scale, const Vec3& centre, const Vec3& p0, const Vec3& p1,
          bool finger) {
    double ox = img.w / 4.0 + panel * img.w / 2.0, oy = img.h / 2.0;
    auto X = [&](const Vec3& p) { return ox + (p[a] - centre[a]) * scale; };
    auto Y = [&](const Vec3& p) { return oy - (p[b] - centre[b]) * scale; };
    if (finger) img.line(X(p0), Y(p0), X(p1), Y(p1), 240, 200, 90);
    else img.line(X(p0), Y(p0), X(p1), Y(p1), 150, 170, 200);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vats_pose_preview <data/character> [out dir]\n");
        return 2;
    }
    Skeleton skel;
    std::string err;
    if (!skel.load_dir(argv[1], err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::string out = argc > 2 ? argv[2] : "";
    const char* digits[] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
    const char* body[] = {"mWristLeft", "mElbowLeft", "mWristRight", "mElbowRight", "mHead",
                          "mKneeLeft",  "mAnkleLeft", "mKneeRight",  "mAnkleRight"};
    for (auto& it : builtin_poses(skel)) {
        Pose pose(skel.size());
        for (auto& [name, e] : it.bones) pose.rot[skel.find(name)] = euler_to_quat(e);
        auto g = skel.global_pose(pose);
        bool hand = it.kind == "hand";
        std::printf("%-26s %s\n", it.id.c_str(), it.name.c_str());
        Xform frame = g[skel.find(hand ? "mWristLeft" : "mPelvis")].inverse();
        if (hand) {
            for (auto d : digits) {
                int n = skel.find(std::string("mHand") + d + "3Left");
                Vec3 tip = frame.apply(g[n].apply(skel[n].end)) * 100;
                std::printf("  %-7s tip %6.1f %6.1f %6.1f\n", d, tip.x, tip.y, tip.z);
            }
        } else {
            for (auto b : body) {
                Vec3 p = frame.apply(g[skel.find(b)].pos) * 100;
                std::printf("  %-12s %6.1f %6.1f %6.1f\n", b, p.x, p.y, p.z);
            }
        }
        if (out.empty()) continue;
        // Hands: top view (X up... looking down -Z) and side view (looking along X). Body: front and side.
        Image img(800, 400);
        Vec3 centre = hand ? Vec3{0, 0.12, 0} : Vec3{0, 0, 0.1};
        double scale = hand ? 1400 : 180;
        for (int i = 0; i < skel.joint_count(); ++i) {
            const Node& nd = skel[i];
            if (nd.end.length() < 1e-5) continue;
            bool finger = nd.name.find("mHand") == 0 && nd.name.find("Left") != std::string::npos;
            if (hand && !finger && nd.name != "mWristLeft") continue;
            if (!hand && (nd.category != Category::Body)) continue;
            Vec3 p0 = frame.apply(g[i].pos), p1 = frame.apply(g[i].apply(nd.end));
            if (hand) {
                draw(img, 0, 0, 1, scale, centre, p0, p1, finger);  // palm-down top view: X across, Y up
                draw(img, 1, 1, 2, scale, centre, p0, p1, finger);  // side view: Y across, Z up
            } else {
                draw(img, 0, 1, 2, scale, centre, p0, p1, false);  // front view
                draw(img, 1, 0, 2, scale, centre, p0, p1, false);  // side view
            }
        }
        img.save(out + "/" + it.id.substr(8) + ".ppm");
    }
    return 0;
}
