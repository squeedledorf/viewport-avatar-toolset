// Viewport Avatar Toolset - OpenGL drawing for the 3D view.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#include "renderer.h"

#include "gl.h"

namespace vats {
namespace {

const char* kBackdropVs = R"(#version 330 core
out vec2 uv;
void main() {
    uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
})";

const char* kBackdropFs = R"(#version 330 core
in vec2 uv;
uniform vec3 top, bottom;
out vec4 color;
void main() {
    vec3 c = mix(bottom, top, smoothstep(0.0, 1.0, uv.y));
    float v = 1.0 - 0.35 * dot(uv - 0.5, uv - 0.5);  // gentle vignette
    color = vec4(c * v, 1.0);
})";

// A large ground quad; the grid, forward arrow and contact shadow are drawn procedurally.
const char* kGroundVs = R"(#version 330 core
uniform mat4 view_proj;
uniform vec3 focus;
out vec2 world;
void main() {
    vec2 corner = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1) * 2.0 - 1.0;
    world = corner * 40.0;
    gl_Position = view_proj * vec4(world, focus.z, 1.0);
})";

const char* kGroundFs = R"(#version 330 core
in vec2 world;
uniform vec3 eye, grid_colour;
uniform vec3 focus;
out vec4 color;

float grid(vec2 p, float spacing) {
    vec2 g = abs(fract(p / spacing - 0.5) - 0.5) / fwidth(p / spacing);
    return 1.0 - min(min(g.x, g.y), 1.0);
}
float segment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}
void main() {
    float dist = length(world - eye.xy);
    float fade = 1.0 - smoothstep(4.0, 14.0 + eye.z * 2.0, dist);
    float minor = grid(world, 0.25) * 0.22, major = grid(world, 1.0) * 0.45;
    vec2 axis = abs(world) / fwidth(world);
    float axes = (1.0 - min(min(axis.x, axis.y), 1.0)) * 0.7;
    float a = max(max(minor, major), axes) * fade * 0.55;
    vec3 c = grid_colour;

    // Forward arrow: the avatar faces +X.
    vec2 p = world;
    float arrow = min(segment(p, vec2(0.30, 0.0), vec2(0.60, 0.0)),
                      min(segment(p, vec2(0.60, 0.0), vec2(0.52, 0.06)), segment(p, vec2(0.60, 0.0), vec2(0.52, -0.06))));
    float aw = 1.0 - smoothstep(0.0, fwidth(p.x) * 1.5, arrow - 0.004);
    c = mix(c, vec3(0.95, 0.45, 0.32), aw);
    a = max(a, aw * 0.85);

    // Soft contact shadow under the avatar.
    float shadow = exp(-dot(world - focus.xy, world - focus.xy) / (2.0 * 0.22 * 0.22)) * 0.45;
    color = vec4(mix(c, vec3(0.0), shadow / max(a + shadow, 1e-4)), clamp(a + shadow, 0.0, 1.0));
})";

const char* kLitVs = R"(#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
layout(location = 2) in vec4 col;
uniform mat4 view_proj;
out vec3 v_pos, v_nrm;
out vec4 v_col;
void main() {
    v_pos = pos;
    v_nrm = nrm;
    v_col = col;
    gl_Position = view_proj * vec4(pos, 1.0);
})";

const char* kLitFs = R"(#version 330 core
in vec3 v_pos, v_nrm;
in vec4 v_col;
uniform vec3 eye, sky, ground;
uniform vec3 key, key_colour, amb;  // the Light menu's preset (08 LT-1); the studio light: amb 0.75, key_colour 1
uniform float gloss;
out vec4 color;
void main() {
    vec3 n = normalize(v_nrm);
    vec3 v = normalize(eye - v_pos);
    if (dot(n, v) < 0.0) n = -n;  // lit from the viewer's side for thin or inverted faces
    vec3 fill = normalize(vec3(-0.6, -0.5, 0.35));
    vec3 ambient = mix(ground, sky, n.z * 0.5 + 0.5);
    float wrap = max((dot(n, key) + 0.25) / 1.25, 0.0);
    vec3 diffuse = key_colour * wrap * 0.85 + amb / 0.75 * max(dot(n, fill), 0.0) * 0.25;
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.35;
    vec3 spec = key_colour * pow(max(dot(n, normalize(key + v)), 0.0), 40.0) * gloss;
    vec3 c = v_col.rgb * (ambient * amb + diffuse) + vec3(rim * 0.6) + spec;
    color = vec4(c, v_col.a);
})";

// The reference picture (08 RF): four corners as uniforms, drawn as a strip bl, br, tl, tr.
const char* kImageVs = R"(#version 330 core
uniform mat4 view_proj;
uniform vec3 c0, c1, c2, c3;
out vec2 uv;
void main() {
    int i = gl_VertexID;
    vec3 p = i == 0 ? c0 : i == 1 ? c1 : i == 2 ? c3 : c2;
    uv = vec2(float(i & 1), i < 2 ? 1.0 : 0.0);  // the texture's first row is the picture's top
    gl_Position = view_proj * vec4(p, 1.0);
})";

const char* kImageFs = R"(#version 330 core
in vec2 uv;
uniform sampler2D tex;
uniform float opacity;
out vec4 color;
void main() {
    vec4 t = texture(tex, uv);
    color = vec4(t.rgb, t.a * opacity);
})";

unsigned compile(unsigned type, const char* src, std::string& err) {
    unsigned s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    int ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        err = log;
        gl::DeleteShader(s);
        return 0;
    }
    return s;
}

unsigned program(const char* vs, const char* fs, std::string& err) {
    unsigned v = compile(GL_VERTEX_SHADER, vs, err);
    unsigned f = v ? compile(GL_FRAGMENT_SHADER, fs, err) : 0;
    if (!f) return 0;
    unsigned p = gl::CreateProgram();
    gl::AttachShader(p, v);
    gl::AttachShader(p, f);
    gl::LinkProgram(p);
    gl::DeleteShader(v);
    gl::DeleteShader(f);
    int ok = 0;
    gl::GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetProgramInfoLog(p, sizeof log, nullptr, log);
        err = log;
        return 0;
    }
    return p;
}

void set3(unsigned prog, const char* name, float x, float y, float z) {
    gl::Uniform3f(gl::GetUniformLocation(prog, name), x, y, z);
}
void set3(unsigned prog, const char* name, const Rgb& c) { set3(prog, name, c.r, c.g, c.b); }

}  // namespace

bool Renderer::init(std::string& err) {
    backdrop_prog_ = program(kBackdropVs, kBackdropFs, err);
    ground_prog_ = backdrop_prog_ ? program(kGroundVs, kGroundFs, err) : 0;
    lit_prog_ = ground_prog_ ? program(kLitVs, kLitFs, err) : 0;
    image_prog_ = lit_prog_ ? program(kImageVs, kImageFs, err) : 0;
    if (!image_prog_) return false;
    gl::GenVertexArrays(1, &empty_vao_);
    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::GenBuffers(1, &ibo_);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, p)));
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, n)));
    gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, c)));
    for (unsigned i = 0; i < 3; ++i) gl::EnableVertexAttribArray(i);
    gl::BindVertexArray(0);
    return true;
}

void Renderer::shutdown() {
    gl::DeleteFramebuffers(1, &fbo_);
    gl::DeleteFramebuffers(1, &resolve_fbo_);
    gl::DeleteRenderbuffers(1, &color_rb_);
    gl::DeleteRenderbuffers(1, &depth_rb_);
    gl::DeleteTextures(1, &resolve_tex_);
    gl::DeleteBuffers(1, &vbo_);
    gl::DeleteBuffers(1, &ibo_);
    gl::DeleteVertexArrays(1, &vao_);
    gl::DeleteVertexArrays(1, &empty_vao_);
    for (unsigned p : {backdrop_prog_, ground_prog_, lit_prog_, image_prog_}) gl::DeleteProgram(p);
}

void Renderer::ensure_targets(int w, int h) {
    if (w == w_ && h == h_ && fbo_) return;
    w_ = w;
    h_ = h;
    if (!fbo_) {
        gl::GenFramebuffers(1, &fbo_);
        gl::GenFramebuffers(1, &resolve_fbo_);
        gl::GenRenderbuffers(1, &color_rb_);
        gl::GenRenderbuffers(1, &depth_rb_);
        gl::GenTextures(1, &resolve_tex_);
    }
    gl::BindRenderbuffer(GL_RENDERBUFFER, color_rb_);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, w, h);
    gl::BindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
    gl::RenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, w, h);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rb_);
    gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);

    gl::BindTexture(GL_TEXTURE_2D, resolve_tex_);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::BindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_tex_, 0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::begin(int width, int height, const Camera& cam, const SceneColours& colours, const Mat4* projection) {
    ensure_targets(width, height);
    colours_ = colours;
    eye_ = cam.eye();
    // Shading looks from the eye; in ortho from far back along the view, since a close zoom puts the eye inside the body.
    shade_eye_ = cam.ortho ? cam.target - cam.forward() * 1000.0 : eye_;
    view_proj_ = (projection ? *projection : cam.projection(double(width) / height)) * cam.view();

    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo_);
    gl::Viewport(0, 0, width, height);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl::Disable(GL_DEPTH_TEST);
    gl::Disable(GL_CULL_FACE);
    gl::UseProgram(backdrop_prog_);
    set3(backdrop_prog_, "top", colours.backdrop_top);
    set3(backdrop_prog_, "bottom", colours.backdrop_bottom);
    gl::BindVertexArray(empty_vao_);
    gl::DrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::draw_ground(const Vec3& focus) {
    gl::Enable(GL_DEPTH_TEST);
    gl::DepthMask(GL_FALSE);
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(ground_prog_);
    gl::UniformMatrix4fv(gl::GetUniformLocation(ground_prog_, "view_proj"), 1, GL_FALSE, view_proj_.m);
    set3(ground_prog_, "eye", float(eye_.x), float(eye_.y), float(eye_.z));
    set3(ground_prog_, "grid_colour", colours_.grid);
    set3(ground_prog_, "focus", float(focus.x), float(focus.y), float(focus.z));  // z: the body's floor
    gl::BindVertexArray(empty_vao_);
    gl::DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl::DepthMask(GL_TRUE);
    gl::Disable(GL_BLEND);
}

void Renderer::draw_triangles(const std::vector<Vertex>& verts, const std::vector<std::uint32_t>& indices,
                              bool depth_test, float gloss, bool translucent) {
    if (verts.empty()) return;
    depth_test ? gl::Enable(GL_DEPTH_TEST) : gl::Disable(GL_DEPTH_TEST);
    if (translucent) {
        gl::Enable(GL_CULL_FACE);
        gl::CullFace(GL_BACK);
        gl::DepthMask(GL_FALSE);
    }
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(lit_prog_);
    gl::UniformMatrix4fv(gl::GetUniformLocation(lit_prog_, "view_proj"), 1, GL_FALSE, view_proj_.m);
    set3(lit_prog_, "eye", float(shade_eye_.x), float(shade_eye_.y), float(shade_eye_.z));
    set3(lit_prog_, "sky", colours_.sky);
    set3(lit_prog_, "ground", colours_.ground);
    set3(lit_prog_, "key", light_.key[0], light_.key[1], light_.key[2]);
    set3(lit_prog_, "key_colour", light_.colour);
    set3(lit_prog_, "amb", light_.ambient);
    gl::Uniform1f(gl::GetUniformLocation(lit_prog_, "gloss"), gloss);
    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER, static_cast<long>(verts.size() * sizeof(Vertex)), verts.data(), GL_STREAM_DRAW);
    if (indices.empty()) {
        gl::DrawArrays(GL_TRIANGLES, 0, static_cast<int>(verts.size()));
    } else {
        gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long>(indices.size() * 4), indices.data(), GL_STREAM_DRAW);
        gl::DrawElements(GL_TRIANGLES, static_cast<int>(indices.size()), GL_UNSIGNED_INT, nullptr);
    }
    gl::BindVertexArray(0);
    gl::Disable(GL_BLEND);
    if (translucent) {
        gl::Disable(GL_CULL_FACE);
        gl::DepthMask(GL_TRUE);
    }
}

void Renderer::draw_image(unsigned texture, const std::array<Vec3, 4>& c, float opacity, bool backdrop) {
    backdrop ? gl::Disable(GL_DEPTH_TEST) : gl::Enable(GL_DEPTH_TEST);
    gl::DepthMask(GL_FALSE);
    gl::Enable(GL_BLEND);
    gl::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(image_prog_);
    const Mat4 identity;
    gl::UniformMatrix4fv(gl::GetUniformLocation(image_prog_, "view_proj"), 1, GL_FALSE, backdrop ? identity.m : view_proj_.m);
    const char* names[] = {"c0", "c1", "c2", "c3"};
    for (int i = 0; i < 4; ++i) set3(image_prog_, names[i], float(c[i].x), float(c[i].y), float(c[i].z));
    gl::Uniform1f(gl::GetUniformLocation(image_prog_, "opacity"), opacity);
    gl::BindTexture(GL_TEXTURE_2D, texture);
    gl::BindVertexArray(empty_vao_);
    gl::DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl::BindVertexArray(0);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    gl::Disable(GL_BLEND);
    gl::DepthMask(GL_TRUE);
}

void Renderer::end() {
    gl::BindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    gl::BindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_);
    gl::BlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::Disable(GL_DEPTH_TEST);
}

void Renderer::set_light(const LightPreset* p) { light_ = p ? *p : kStudio; }

}  // namespace vats
