// Viewport Avatar Toolset - the standalone app's host (see sdl_host.h).
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#include "sdl_host.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "file_types.h"
#include "gl.h"

namespace vats {
namespace {

// SDL keeps the filters until it answers, and may answer on another thread.
struct DialogCall {
    std::vector<ui::FileFilter> filters;
    std::vector<SDL_DialogFileFilter> sdl;
    ui::FilesChosen done;
    DialogCall(const std::vector<ui::FileFilter>& f, ui::FilesChosen d) : filters(f), done(std::move(d)) {
        for (const ui::FileFilter& x : filters) sdl.push_back({x.name.c_str(), x.patterns.c_str()});
    }
};

void SDLCALL dialog_done(void* user, const char* const* files, int) {
    std::unique_ptr<DialogCall> call(static_cast<DialogCall*>(user));
    std::vector<std::string> chosen;
    for (int i = 0; files && files[i]; ++i) chosen.push_back(files[i]);
    call->done(std::move(chosen));
}

std::string find_packaging_dir() {
    const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    for (std::string dir : {base + "packaging", base + "../share/viewport-avatar-toolset/packaging"})
        if (SDL_GetPathInfo(dir.c_str(), nullptr)) return dir;
    return std::string(VATS_SOURCE_DIR) + "/packaging/linux";
}

}  // namespace

void SdlHost::shutdown() {
    if (audio_) SDL_DestroyAudioStream(audio_), audio_ = nullptr;
    if (thumb_state_ > 0) thumb_.shutdown();
    if (face_state_ > 0) face_.shutdown();
    if (picker_state_ > 0) picker_.shutdown();
    thumb_state_ = face_state_ = picker_state_ = 0;
    view_.shutdown();
}

// --- 3D scene ---

bool SdlHost::scene_begin(ui::SceneTarget target, int width, int height, const Camera& cam, const SceneColours& colours,
                          const Mat4* projection) {
    if (target == ui::SceneTarget::View) {
        current_ = &view_;
        view_.begin(width, height, cam, colours, projection);
        return true;
    }
    // The face cam and the Picker have targets of their own, so thumbnails never overwrite them.
    const bool face = target == ui::SceneTarget::FaceCam, picker = target == ui::SceneTarget::Picker;
    int& state = face ? face_state_ : picker ? picker_state_ : thumb_state_;
    Renderer& r = face ? face_ : picker ? picker_ : thumb_;
    if (state == 0) {
        std::string err;
        state = r.init(err) ? 1 : -1;
    }
    if (state < 0) return false;
    current_ = &r;
    if (&r == &thumb_) thumb_w_ = width, thumb_h_ = height;
    r.begin(width, height, cam, colours, projection);
    gl::ClearColor(0, 0, 0, 0);  // over the backdrop begin() drew
    gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}

ImTextureID SdlHost::scene_end() {
    current_->end();
    return ImTextureID(current_->texture());
}

// Reads the thumbnail renderer's picture back as straight alpha, top row first.
bool SdlHost::thumbnail_pixels(std::vector<std::uint8_t>& px, int& width, int& height) {
    if (thumb_state_ <= 0) return false;
    width = thumb_w_, height = thumb_h_;
    const size_t row = size_t(thumb_w_) * 4;
    px.resize(row * thumb_h_);
    unsigned fbo = 0;
    gl::GenFramebuffers(1, &fbo);
    gl::BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, thumb_.texture(), 0);
    gl::ReadPixels(0, 0, thumb_w_, thumb_h_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl::DeleteFramebuffers(1, &fbo);
    // The MSAA resolve against a clear background is premultiplied; PNG wants straight alpha.
    for (size_t i = 0; i < px.size(); i += 4) {
        std::uint8_t* q = &px[i];
        if (q[3] && q[3] < 255)
            for (int c = 0; c < 3; ++c) q[c] = std::uint8_t(std::min(255, q[c] * 255 / q[3]));
    }
    for (int y = 0; y < thumb_h_ / 2; ++y)  // GL's rows run bottom-up
        std::swap_ranges(px.begin() + long(y * row), px.begin() + long((y + 1) * row), px.begin() + long((thumb_h_ - 1 - y) * row));
    return true;
}

bool SdlHost::save_thumbnail_png(const std::string& png) {
    std::vector<std::uint8_t> px;
    int w = 0, h = 0;
    if (!thumbnail_pixels(px, w, h)) return false;
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, px.data(), w * 4);
    if (!s) return false;
    bool ok = SDL_SavePNG(s, png.c_str());
    SDL_DestroySurface(s);
    return ok;
}

ImTextureID SdlHost::load_texture(const std::string& png) {
    SDL_Surface* loaded = SDL_LoadPNG(png.c_str());
    ImTextureID tex{};
    if (SDL_Surface* s = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888) : nullptr) {
        tex = make_texture(static_cast<const std::uint8_t*>(s->pixels), s->w, s->h);  // pitch = w * 4
        SDL_DestroySurface(s);
    }
    SDL_DestroySurface(loaded);
    return tex;
}

ImTextureID SdlHost::make_texture(const std::uint8_t* rgba, int width, int height) {
    unsigned tex = 0;
    gl::GenTextures(1, &tex);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    return ImTextureID(tex);
}

bool SdlHost::update_texture(ImTextureID texture, const std::uint8_t* rgba, int width, int height) {
    if (!texture) return false;
    gl::BindTexture(GL_TEXTURE_2D, unsigned(texture));
    gl::TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    gl::BindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void SdlHost::free_texture(ImTextureID texture) {
    unsigned tex = unsigned(texture);
    gl::DeleteTextures(1, &tex);
}

// --- Camera ---

Projector SdlHost::projector(ImVec2 origin, ImVec2 size) {
    Projector p;
    p.view_proj = camera_.projection(double(size.x) / size.y) * camera_.view();
    p.x0 = origin.x, p.y0 = origin.y, p.w = size.x, p.h = size.y;
    return p;
}

// --- Dialogs ---

void SdlHost::open_file_dialog(const std::vector<ui::FileFilter>& filters, bool multiple, ui::FilesChosen done) {
    auto* call = new DialogCall(filters, std::move(done));
    SDL_ShowOpenFileDialog(dialog_done, call, window_, call->sdl.data(), int(call->sdl.size()), nullptr, multiple);
}

void SdlHost::save_file_dialog(const std::vector<ui::FileFilter>& filters, const std::string& suggested, ui::FilesChosen done) {
    auto* call = new DialogCall(filters, std::move(done));
    SDL_ShowSaveFileDialog(dialog_done, call, window_, call->sdl.data(), int(call->sdl.size()),
                           suggested.empty() ? nullptr : suggested.c_str());
}

void SdlHost::open_folder_dialog(const std::string& start, ui::FilesChosen done) {
    auto* call = new DialogCall({}, std::move(done));
    SDL_ShowOpenFolderDialog(dialog_done, call, window_, start.empty() ? nullptr : start.c_str(), false);
}

// A native box, answered before this returns.
void SdlHost::ask(const std::string& title, const std::string& text, const std::vector<std::string>& buttons,
                  std::function<void(int)> done) {
    std::vector<SDL_MessageBoxButtonData> data;
    for (size_t i = 0; i < buttons.size(); ++i) {
        SDL_MessageBoxButtonFlags flags = 0;
        if (i == 0) flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (i + 1 == buttons.size()) flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        data.push_back({flags, int(i), buttons[i].c_str()});
    }
    SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING, window_, title.c_str(), text.c_str(), int(data.size()), data.data(), nullptr};
    int choice = int(buttons.size()) - 1;  // closed or failed: the last button (Cancel)
    SDL_ShowMessageBox(&box, &choice);
    done(choice < 0 ? int(buttons.size()) - 1 : choice);  // -1: closed
}

bool SdlHost::associate_file_types(bool install, std::string& message) {
    return install ? register_file_types(find_packaging_dir(), message) : unregister_file_types(message);
}

// --- Audio ---

bool SdlHost::audio_start(int rate, int channels, float gain) {
    if (!audio_ || audio_rate_ != rate || audio_channels_ != channels) {
        if (audio_) SDL_DestroyAudioStream(audio_), audio_ = nullptr;
        if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
        SDL_AudioSpec spec{SDL_AUDIO_F32, channels, rate};
        audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!audio_) return false;
        audio_rate_ = rate, audio_channels_ = channels;
        SDL_ResumeAudioStreamDevice(audio_);
    }
    SDL_ClearAudioStream(audio_);
    SDL_SetAudioStreamGain(audio_, gain);
    return true;
}

void SdlHost::audio_queue(const float* samples, std::size_t count) {
    if (audio_) SDL_PutAudioStreamData(audio_, samples, int(count * sizeof(float)));
}

void SdlHost::audio_stop() {
    if (audio_) SDL_ClearAudioStream(audio_);
}

// --- Waking the main loop, which sleeps while idle ---

void SdlHost::wake(double seconds) {
    static const SDL_TimerCallback push = [](void*, SDL_TimerID, Uint32) -> Uint32 {
        SDL_Event e{};
        e.type = SDL_EVENT_USER;
        SDL_PushEvent(&e);
        return 0;
    };
    if (seconds <= 0) push(nullptr, 0, 0);
    else SDL_AddTimer(Uint32(std::lround(seconds * 1000)), push, nullptr);
}

}  // namespace vats
