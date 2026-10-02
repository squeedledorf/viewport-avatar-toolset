// Viewport Avatar Toolset - entry point: window, OpenGL context, Dear ImGui and the main loop.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>

#include "app.h"
#include "gl.h"
#include "icon_button.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "profile.h"
#include "sdl_host.h"
#include "vats_version.h"
#include "theme.h"

namespace {

bool exists(const std::string& path) { return SDL_GetPathInfo(path.c_str(), nullptr); }

// Where data lives: beside the executable (unpacked release: <exe dir>/data), then an installed tree
// (<prefix>/bin/vats with <prefix>/share/viewport-avatar-toolset/data), then the source tree (developer builds).
std::string find_dir(const char* installed, const char* source) {
    const std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "";
    for (std::string dir : {base + installed, base + "../share/viewport-avatar-toolset/" + installed})
        if (exists(dir)) return dir;
    return std::string(VATS_SOURCE_DIR) + "/" + source;
}

// The folders the UI reads and writes. --data-dir moves every piece of user data (IO-53); --library-dir only
// the libraries (UI-10).
vats::ui::Paths find_paths(int argc, char** argv) {
    vats::ui::Paths p;
    p.data = find_dir("data", "data");
    p.assets = find_dir("assets", "app/assets");
    const char* help_dir = std::getenv("VATS_HELP_DIR");  // tests and page writers: another folder of pages
    p.help = help_dir && *help_dir ? help_dir : find_dir("help", "docs/wiki");
    std::string data_dir;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i], v = argv[i + 1];
        if (a == "--data-dir") data_dir = v.empty() || v.back() == '/' ? v : v + '/';
        if (a == "--library-dir") p.library = v.empty() || v.back() == '/' ? v : v + '/';
    }
    if (!data_dir.empty()) {
        SDL_CreateDirectory(data_dir.c_str());
        p.user = data_dir;
        p.settings = data_dir + "settings.json";
        return p;
    }
    char* pref = SDL_GetPrefPath("", "viewport-avatar-toolset");  // creates it
    p.user = pref ? pref : "";
    SDL_free(pref);
#ifdef _WIN32
    p.settings = p.user + "settings.json";  // %APPDATA%, whatever HOME a developer shell sets
#else
    const char* xdg = SDL_getenv("XDG_CONFIG_HOME");
    const char* home = SDL_getenv("HOME");
    if ((xdg && *xdg) || home) {
        const std::string dir = (xdg && *xdg ? std::string(xdg) : std::string(home) + "/.config") + "/viewport-avatar-toolset";
        SDL_CreateDirectory(dir.c_str());
        p.settings = dir + "/settings.json";
    } else {
        p.settings = p.user + "settings.json";
    }
#endif
    return p;
}

bool g_headless = false;  // --screenshot: nobody is there to close a message box

int fail(const char* what, const std::string& why) {
    std::fprintf(stderr, "%s: %s\n", what, why.c_str());
    if (!g_headless)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Viewport Avatar Toolset", (std::string(what) + "\n\n" + why).c_str(),
                             nullptr);
    return 1;
}

}  // namespace

// --bench: median, 95th percentile and mean of each timed section, in milliseconds.
static void print_bench() {
    std::printf("%-20s %8s %8s %8s %6s\n", "section", "median", "p95", "mean", "n");
    for (auto& [name, v] : vats::Profile::ms) {
        std::sort(v.begin(), v.end());
        double sum = 0;
        for (double x : v) sum += x;
        std::printf("%-20s %8.3f %8.3f %8.3f %6zu\n", name.c_str(), v[v.size() / 2], v[v.size() * 95 / 100],
                    sum / double(v.size()), v.size());
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {  // before SDL: prints and quits without opening a window
        const std::string a = argv[i];
        if (a == "--version") return std::printf("Viewport Avatar Toolset %s\n", VATS_VERSION), 0;
        if (a == "--help" || a == "-h") {
            std::printf(
                "Usage: vats [options] [file.vat | file.anim | file.bvh]\n\n"
                "Data:     --data-dir <dir>  --library-dir <dir>\n"
                "Window:   --size <W>x<H>  --theme <name>  --preset <name>  --window <name>  --tab <name>\n"
                "          --picker <page>[/<view>]  --picker-style <name>  --filter <text>  --bone-filter <text>\n"
                "          --open-help <page>  --open-menu <menu>  --workspace <name>  --pie <ring>[/<dir>]\n"
                "Scene:    --light <name>  --backdrop  --body <id>  --reference <file.png>  --points  --physics\n"
                "          --bones <stick | hidden>\n"
                "          --target <file.vat | file.anim>  --mesh-body <file.dae | file.fbx>\n"
                "          --frame <n>  --select <bone>  --select-all  --select-group <name>\n"
                "          --select-prop <n>  --pose <slug>  --sit\n"
                "          --tool <name>  --focus  --view <name>  --distance <m>\n"
                "Import:   --import-prop <file>  --import-body <a.dae,b.dae>  --map-rig <file>  --retarget <file>\n"
                "          --rig-scratch <file>  --rig-groups <list>  --rig-marker <id=x,y,z>  --rig-weights  --rig-apply\n"
                "          --paint <spec>\n"
                "          --batch-retarget <folder>  --plan-clip <file>\n"
                "Output:   --screenshot <file.png>  --shot-rect <window>  --listing <file>  --bench <seconds>\n"
                "          --export-rig <file.dae>\n"
                "Other:    --help  --version\n\n"
                "Every option is described in Help > Command line.\n");
            return 0;
        }
    }
    for (int i = 1; i < argc; ++i) g_headless = g_headless || std::string(argv[i]) == "--screenshot";
    SDL_SetAppMetadata("Viewport Avatar Toolset", VATS_VERSION, "org.viewport-avatar-toolset");
    // Closing the window asks to save (WINDOW_CLOSE_REQUESTED); SDL_EVENT_QUIT is then only signals and logout.
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) return fail("Could not start SDL", SDL_GetError());

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    // --size WxH: that window size instead of maximised (docs screenshots).
    int win_w = 1600, win_h = 900;
    bool sized = false;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--size") sized = std::sscanf(argv[i + 1], "%dx%d", &win_w, &win_h) == 2 && win_w > 0 && win_h > 0;
    if (!sized) win_w = 1600, win_h = 900;
    SDL_Window* window = SDL_CreateWindow("Viewport Avatar Toolset", win_w, win_h,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                              (sized ? 0 : SDL_WINDOW_MAXIMIZED));
    if (!window) return fail("Could not open a window", SDL_GetError());
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) return fail("OpenGL 3.3 is not available", SDL_GetError());
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    if (const char* missing = gl::load()) return fail("OpenGL function missing", missing);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    // The folders have to be known before anything is read.
    vats::SdlHost host(window, find_paths(argc, argv));
    // Window and panel layout persists between runs.
    const std::string root = host.paths().user;
    std::string ini = root.empty() ? "" : root + "layout.ini";
    io.IniFilename = ini.empty() ? nullptr : ini.c_str();

    // UI scale in window coordinates: the display scale without the part the pixel density already covers.
    float density = SDL_GetWindowPixelDensity(window);
    float display_scale = SDL_GetWindowDisplayScale(window) / (density > 0 ? density : 1.f);
    const std::string assets = host.paths().assets;
    vats::load_fonts(assets.c_str());
    if (SDL_Surface* icon = SDL_LoadBMP((assets + "/icon.bmp").c_str())) {  // title bar and taskbar
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    std::string err;
    if (!host.init(err)) return fail("Could not start the 3D view", err);
    vats::App app(host);
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--screenshot") app.set_headless(true);  // before init: no recovery offer, no dialogs
    if (!app.init(display_scale, err)) return fail("Could not load the Second Life skeleton", err);
    // --screenshot <png>: render a few frames, save the window and quit (for tests and docs).
    std::string screenshot;
    std::string listing;  // --listing <file.gif|file.png>: listing media after the first frames (08 LM)
    std::string shot_rect;  // --shot-rect <window>: its rectangle in the screenshot, printed to stdout
    bool focus_on_start = false;
    double distance = 0, pitch = 1e9, bench_seconds = 0;  // --bench <s>: play, time each frame's sections, print, quit
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc)
            screenshot = argv[++i];
        else if (a == "--select-group" && i + 1 < argc)
            app.cli_select_group(argv[++i]) || std::fprintf(stderr, "unknown picker group %s\n", argv[i]);
        else if (a == "--select" && i + 1 < argc)
            app.select_by_name(argv[++i]);
        else if (a == "--pose" && i + 1 < argc)
            app.apply_builtin_pose(argv[++i]) || std::fprintf(stderr, "no built-in pose %s\n", argv[i]);
        else if (a == "--focus")
            focus_on_start = true;
        else if (a == "--view" && i + 1 < argc)
            app.cli_view(argv[++i]) || std::fprintf(stderr, "unknown view %s\n", argv[i]);
        else if (a == "--distance" && i + 1 < argc)
            distance = std::atof(argv[++i]);
        else if (a == "--pitch" && i + 1 < argc)
            pitch = std::atof(argv[++i]);
        else if (a == "--body" && i + 1 < argc) {
            std::string b = argv[++i] == std::string("off") ? "none" : argv[i];
            for (int k = 0; k < vats::kBodyCount; ++k)
                if (b == vats::kBodyIds[k]) app.set_body(k, false);  // this run only
        } else if (a == "--mesh-body" && i + 1 < argc) {
            app.cli_mesh_body(argv[++i]);
        } else if (a == "--physics") {
            app.cli_physics();
        } else if (a == "--points")
            app.show_points();
        else if (a == "--tool" && i + 1 < argc)
            app.set_tool(argv[++i]);
        else if (a == "--frame" && i + 1 < argc)
            app.goto_frame(std::atof(argv[++i]));
        else if ((a == "--library-dir" || a == "--data-dir" || a == "--size") && i + 1 < argc)
            ++i;  // already applied before init
        else if (a == "--theme" && i + 1 < argc)
            app.set_theme(argv[++i]) || std::fprintf(stderr, "unknown theme %s\n", argv[i]);
        else if (a == "--picker" && i + 1 < argc)
            app.cli_picker(argv[++i]) || std::fprintf(stderr, "unknown picker page %s\n", argv[i]);
        else if (a == "--picker-style" && i + 1 < argc)
            app.cli_picker_style(argv[++i]) || std::fprintf(stderr, "unknown picker style %s\n", argv[i]);
        else if (a == "--bones" && i + 1 < argc)
            app.cli_bone_style(argv[++i]) || std::fprintf(stderr, "unknown bone style %s\n", argv[i]);
        else if (a == "--window" && i + 1 < argc)
            app.show_window(argv[++i]) || std::fprintf(stderr, "unknown window %s\n", argv[i]);
        else if (a == "--shot-rect" && i + 1 < argc)
            shot_rect = argv[++i];
        else if (a == "--import-prop" && i + 1 < argc)
            app.cli_import_prop(argv[++i]);
        else if (a == "--import-body" && i + 1 < argc) {  // files joined with commas, as one body, shown
            std::vector<std::string> parts;
            std::stringstream ss(argv[++i]);
            for (std::string part; std::getline(ss, part, ',');)
                if (!part.empty()) parts.push_back(part);
            app.cli_import_body(parts);
        }
        else if (a == "--export-rig" && i + 1 < argc)  // as File > Export Rigged Mesh for SL... with its settings (08 RG)
            app.cli_export_rig(argv[++i]);
        else if (a == "--map-rig" && i + 1 < argc)  // as Rig > Map Rig to Second Life... on this model (08 RM)
            app.open_rig_map(argv[++i]);
        else if (a == "--rig-scratch" && i + 1 < argc)  // as Rig > Rig a Model from Scratch... on this model (08 RG-14)
            app.open_rig_scratch(argv[++i]);
        else if (a == "--rig-groups" && i + 1 < argc)  // its optional bones: "face,tail"
            app.cli_rig_groups(argv[++i]) || std::fprintf(stderr, "unknown rig group in %s\n", argv[i]);
        else if (a == "--rig-marker" && i + 1 < argc)  // a marker moved: "wrist_l=x,y,z"
            app.cli_rig_marker(argv[++i]) || std::fprintf(stderr, "bad --rig-marker %s\n", argv[i]);
        else if (a == "--rig-weights")  // the weights worked out now
            app.cli_rig_weights();
        else if (a == "--rig-apply")  // and Apply
            app.cli_rig_apply();
        else if (a == "--paint" && i + 1 < argc)  // 08 RG-15: one stroke "<joint> <add|subtract|smooth> <x> <y> <z> [<radius> <strength>]"
            app.cli_paint(argv[++i]) || std::fprintf(stderr, "cannot paint %s\n", argv[i]);
        else if (a == "--retarget" && i + 1 < argc)  // as File > Import Animation (Retarget)...: the dialog opens
            app.open_retarget(argv[++i]);
        else if (a == "--plan-clip" && i + 1 < argc)  // 08 PP: the Priority Planner with this context clip
            app.planner_add(argv[++i]);
        else if (a == "--batch-retarget" && i + 1 < argc)  // 07 RT-13: File > Batch Retarget Folder..., run on it
            app.batch_retarget_folder(argv[++i]);
        else if (a == "--tab" && i + 1 < argc)
            app.show_tab(argv[++i]);
        else if (a == "--filter" && i + 1 < argc)
            app.set_inventory_filter(argv[++i]);
        else if (a == "--bone-filter" && i + 1 < argc)
            app.set_bone_filter(argv[++i]);
        else if (a == "--sit")  // as Tools > Sit on Seat, at the current frame
            app.cli_sit();
        else if (a == "--open-help" && i + 1 < argc) {  // a page title or file, optionally "#heading"
            const std::string page = argv[++i];
            const size_t hash = page.find('#');
            app.open_help(page.substr(0, hash), hash == std::string::npos ? "" : page.substr(hash + 1));
        }
        else if (a == "--select-prop" && i + 1 < argc)
            app.select_prop(std::atoi(argv[++i]));
        else if (a == "--bench" && i + 1 < argc)
            bench_seconds = std::atof(argv[++i]);
        else if (a == "--select-all")
            app.select_all();
        else if (a == "--light" && i + 1 < argc)
            app.set_light(argv[++i]) || std::fprintf(stderr, "unknown light %s\n", argv[i]);
        else if (a == "--backdrop")
            app.show_backdrop();
        else if (a == "--target" && i + 1 < argc)  // a .vat or .anim as the target ghost
            app.cli_target(argv[++i]);
        else if (a == "--reference" && i + 1 < argc)
            app.cli_reference(argv[++i]);
        else if (a == "--listing" && i + 1 < argc)
            listing = argv[++i];
        else if (a == "--open-menu" && i + 1 < argc)  // a menu, or a path of menus: "Tools/Loop Tools"
            vats::force_open_menus(argv[++i]);
        else if (a == "--workspace" && i + 1 < argc)  // pose, animate, face, rig, export or all: workspaces on, that one
            app.cli_workspace(argv[++i]) || std::fprintf(stderr, "unknown workspace %s\n", argv[i]);
        else if (a == "--pie" && i + 1 < argc)  // the Tab pie open: main or more, optionally /N, /NE ... hovered
            app.cli_pie(argv[++i]) || std::fprintf(stderr, "unknown pie %s\n", argv[i]);
        else if (a == "--preset" && i + 1 < argc)
            app.set_preset(argv[++i]) || std::fprintf(stderr, "unknown preset %s\n", argv[i]);
        else if (exists(a))
            app.open_path(a);  // only existing files are opened (IO-49)
        else
            std::fprintf(stderr, "ignoring unknown argument %s\n", a.c_str());
    }
    int frames = 0;
    if (bench_seconds > 0) {
        app.start_playing();
        SDL_GL_SetSwapInterval(0);
    }
    Uint64 bench_start = 0, frame_t0 = 0;

    for (bool running = true; running;) {
        // Sleep until something happens, unless playing or dragging (render on demand).
        SDL_Event e;
        // ImGui needs a couple of frames after input (hover, clicks, key releases) before it is idle.
        // For 2 s after input it also wakes every 250 ms (tooltip delays, text cursor blink); after that it
        // blocks until the next event, so an idle window costs no CPU.
        static int settle = 0;
        static Uint64 last_input = 0;
        const bool recent = SDL_GetTicks() - last_input < 2000 || ImGui::GetIO().WantTextInput;
        bool got = app.busy() || !screenshot.empty() || settle > 0 ? SDL_PollEvent(&e)
                                                                    : SDL_WaitEventTimeout(&e, recent ? 250 : -1);
        settle = got ? 3 : std::max(settle - 1, 0);
        if (got) last_input = SDL_GetTicks();
        while (got) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT)
                app.quit_unattended();  // a signal or logout: never block on a prompt
            else if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window))
                app.request_quit();
            if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) app.open_path(e.drop.data);
            got = SDL_PollEvent(&e);
        }
        if (bench_seconds > 0 && ++frames == 60) vats::Profile::on = true, bench_start = SDL_GetTicksNS();  // after warm-up
        frame_t0 = SDL_GetTicksNS();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        // Screenshots: the pointer is wherever the display put it; no hover, no tooltip over the picture.
        if (!screenshot.empty()) ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        ImGui::NewFrame();
        running = app.frame();
        if (!listing.empty() && ImGui::GetFrameCount() > 2) app.export_listing_media(std::exchange(listing, ""));
        // --focus and --distance: on the second frame of every run, once the pose is evaluated and the view laid out
        // (frames counts only in --screenshot and --bench runs).
        if (focus_on_start && ImGui::GetFrameCount() == 2) app.focus_selection();
        if (distance > 0 && ImGui::GetFrameCount() == 2) app.set_camera_distance(distance);
        if (pitch < 1e8 && ImGui::GetFrameCount() == 2) app.set_camera_pitch(pitch);
        if (!screenshot.empty() && frames == 2) app.fit_graph();  // UI-10: the graph shows the whole clip
        if (vats::Profile::on) vats::Profile::add("app frame()", double(SDL_GetTicksNS() - frame_t0) * 1e-6);
        const Uint64 render_t0 = SDL_GetTicksNS();
        ImGui::Render();
        if (vats::Profile::on) vats::Profile::add("ImGui::Render", double(SDL_GetTicksNS() - render_t0) * 1e-6);
        if (vats::Profile::on) vats::Profile::add("vertices (k)", ImGui::GetDrawData()->TotalVtxCount * 1e-3);
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0.12f, 0.13f, 0.15f, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (vats::Profile::on) {
            const Uint64 cpu_end = SDL_GetTicksNS();
            vats::Profile::add("frame (CPU)", double(cpu_end - frame_t0) * 1e-6);
            gl::Finish();
            vats::Profile::add("GL finish", double(SDL_GetTicksNS() - cpu_end) * 1e-6);
            if (double(SDL_GetTicksNS() - bench_start) * 1e-9 >= bench_seconds) {
                print_bench();
                running = false;
            }
        }
        if (!screenshot.empty() && ++frames == 12) {
            SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
            gl::ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
            SDL_FlipSurface(s, SDL_FLIP_VERTICAL);
            if (!SDL_SavePNG(s, screenshot.c_str())) std::fprintf(stderr, "screenshot: %s\n", SDL_GetError());
            SDL_DestroySurface(s);
            if (!shot_rect.empty()) {  // in the PNG's pixels, clipped to it: "shot-rect x y w h"
                const ImGuiWindow* win = ImGui::FindWindowByName(shot_rect.c_str());
                const ImVec2 k = ImGui::GetIO().DisplayFramebufferScale;
                if (win && win->WasActive) {
                    const int x0 = std::clamp(int(win->Pos.x * k.x), 0, w), y0 = std::clamp(int(win->Pos.y * k.y), 0, h);
                    const int x1 = std::clamp(int((win->Pos.x + win->Size.x) * k.x + 0.5f), 0, w);
                    const int y1 = std::clamp(int((win->Pos.y + win->Size.y) * k.y + 0.5f), 0, h);
                    std::printf("shot-rect %d %d %d %d\n", x0, y0, x1 - x0, y1 - y0);
                } else {
                    std::fprintf(stderr, "shot-rect: no window named %s is showing\n", shot_rect.c_str());
                }
            }
            running = false;
        }
        SDL_GL_SwapWindow(window);
    }

    app.shutdown();
    host.shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
