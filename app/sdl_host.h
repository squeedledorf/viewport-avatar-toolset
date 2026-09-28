// Viewport Avatar Toolset - the standalone app's host: SDL3 window, dialogs and audio, and its own OpenGL
// scene renderer, behind the interface the shared UI talks to (ui/host.h).
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#pragma once

#include <SDL3/SDL.h>

#include "host.h"
#include "renderer.h"

namespace vats {

class SdlHost final : public ui::Host {
public:
    SdlHost(SDL_Window* window, ui::Paths paths) : window_(window), paths_(std::move(paths)) {}
    bool init(std::string& err) { return view_.init(err); }  // needs the GL context current
    void shutdown();                                         // before the GL context goes

    const ui::Paths& paths() const override { return paths_; }

    bool scene_begin(ui::SceneTarget target, int width, int height, const Camera& cam, const SceneColours& colours,
                     const Mat4* projection) override;
    void scene_ground(const Vec3& focus) override { current_->draw_ground(focus); }
    void set_light(const LightPreset* preset) override { view_.set_light(preset); }  // thumbnails keep the studio light
    void scene_triangles(const std::vector<Vertex>& verts, const std::vector<std::uint32_t>& indices, bool depth_test,
                         float gloss, bool translucent) override {
        current_->draw_triangles(verts, indices, depth_test, gloss, translucent);
    }
    ImTextureID scene_end() override;
    bool save_thumbnail_png(const std::string& path) override;
    bool thumbnail_pixels(std::vector<std::uint8_t>& rgba, int& width, int& height) override;
    bool scene_image(ImTextureID texture, const std::array<Vec3, 4>& corners, float opacity, bool backdrop) override {
        current_->draw_image(unsigned(texture), corners, opacity, backdrop);
        return true;
    }
    ImTextureID load_texture(const std::string& png) override;
    ImTextureID make_texture(const std::uint8_t* rgba, int width, int height) override;
    bool update_texture(ImTextureID texture, const std::uint8_t* rgba, int width, int height) override;
    void free_texture(ImTextureID texture) override;

    void drive_avatar(const Skeleton&, const Pose&, const Clip&, double) override {}  // drawn by the UI's scene calls

    Camera& camera() override { return camera_; }
    bool set_orthographic(bool on) override { return camera_.ortho = on, true; }  // Camera::projection draws it
    Projector projector(ImVec2 origin, ImVec2 size) override;

    void open_file_dialog(const std::vector<ui::FileFilter>& filters, bool multiple, ui::FilesChosen done) override;
    void save_file_dialog(const std::vector<ui::FileFilter>& filters, const std::string& suggested, ui::FilesChosen done) override;
    void open_folder_dialog(const std::string& start, ui::FilesChosen done) override;
    void ask(const std::string& title, const std::string& text, const std::vector<std::string>& buttons,
             std::function<void(int)> done) override;
    bool associate_file_types(bool install, std::string& message) override;

    bool audio_start(int rate, int channels, float gain) override;
    void audio_queue(const float* samples, std::size_t count) override;
    void audio_stop() override;

    std::uint64_t ticks_ns() const override { return SDL_GetTicksNS(); }
    void wake(double seconds) override;

    void open_url(const std::string& url) override { SDL_OpenURL(url.c_str()); }
    void set_title(const std::string& title) override { SDL_SetWindowTitle(window_, title.c_str()); }
    CommandRunner command_runner() override { return system_runner(); }

private:
    SDL_Window* window_;
    ui::Paths paths_;
    Camera camera_;
    Renderer view_, thumb_, face_, picker_;  // the offscreen targets (thumbnails, face cam, Picker) never touch the view's
    int thumb_state_ = 0, face_state_ = 0, picker_state_ = 0;  // 0 not started, 1 ready, -1 failed
    int thumb_w_ = 0, thumb_h_ = 0;
    Renderer* current_ = &view_;
    SDL_AudioStream* audio_ = nullptr;
    int audio_rate_ = 0, audio_channels_ = 0;
};

}  // namespace vats
