// Viewport Avatar Toolset - OpenGL drawing for the 3D view.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "scene.h"
#include "view_math.h"

namespace vats {

class Renderer {
public:
    bool init(std::string& err);
    void shutdown();

    // Renders into an offscreen 4x MSAA target of this size; texture() is the resolved image.
    // projection: overrides the camera's own (thumbnails use a 30 degree lens, VP-90).
    void begin(int width, int height, const Camera& cam, const SceneColours& colours, const Mat4* projection = nullptr);
    // ground_focus: where the contact shadow sits (the avatar's pelvis over the ground).
    void draw_ground(const Vec3& ground_focus);
    // translucent: back faces culled (counter-clockwise front) and no depth writes, for see-through overlays.
    void draw_triangles(const std::vector<Vertex>& verts, const std::vector<std::uint32_t>& indices, bool depth_test,
                        float gloss = 0.f, bool translucent = false);
    // A texture on a quad (08 RF): corners bottom-left, bottom-right, top-right, top-left of the picture; backdrop =
    // clip-space corners, no depth; else scene corners, depth-tested, writing no depth.
    void draw_image(unsigned texture, const std::array<Vec3, 4>& corners, float opacity, bool backdrop);
    void end();
    // The Light menu's preset (08 LT-1); null = the studio light.
    void set_light(const LightPreset* preset);

    unsigned texture() const { return resolve_tex_; }
    const Mat4& view_proj() const { return view_proj_; }

private:
    void ensure_targets(int w, int h);

    unsigned fbo_ = 0, color_rb_ = 0, depth_rb_ = 0, resolve_fbo_ = 0, resolve_tex_ = 0;
    int w_ = 0, h_ = 0;
    unsigned backdrop_prog_ = 0, ground_prog_ = 0, lit_prog_ = 0, image_prog_ = 0;
    unsigned vao_ = 0, vbo_ = 0, ibo_ = 0, empty_vao_ = 0;
    Mat4 view_proj_;
    Vec3 eye_, shade_eye_;
    SceneColours colours_;
    static constexpr LightPreset kStudio{"", {0.5514f, 0.4511f, 0.7018f}, {1, 1, 1}, {0.75f, 0.75f, 0.75f}, false};
    LightPreset light_ = kStudio;
};

}  // namespace vats
