// Workspaces (ui/workspaces.h): each keeps its own dock arrangement through a switch, driven through a headless
// ImGui context; and the settings that decide which workspace a new or an existing user starts in.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>

#include "check.h"
#include "imgui_internal.h"
#include "settings.h"
#include "workspaces.h"

using namespace vats;

namespace {

const char* const kWindows[] = {"Bones", "Picker", "Inventory", "Viewport", "Properties", "Graph", "Dope Sheet", "Timeline"};

struct Ui {
    ImGuiID dock = 0;
    Workspace ws = Workspace::Pose;
    bool build = true;
    std::map<std::string, std::string> saved;
    FrontTabs front;
    Workspace switch_to = Workspace::Pose;
    bool switching = false;

    // One frame as App::frame draws it: the switch first, the dockspace, then the workspace's panels only.
    void frame(ImVec2 mouse, bool down) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        ImGui::NewFrame();
        restore_front_tabs(front);
        if (std::exchange(switching, false)) {
            build = !switch_workspace_layout(saved, ws, switch_to, front);
            ws = switch_to;
        }
        dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (std::exchange(build, false)) build_workspace_layout(dock, ws, false, nullptr);
        for (const char* p : kWindows) {
            if (!workspace_shows(ws, p, {})) continue;
            ImGui::Begin(p);
            ImGui::TextUnformatted(p);
            ImGui::End();
        }
        ImGui::Render();
    }
    void idle() {
        for (int i = 0; i < 3; ++i) frame(ImVec2(1, 1), false);
    }
    void go(Workspace w) {
        switch_to = w, switching = true;
        idle();
    }
    void drag(ImVec2 from, ImVec2 to) {
        frame(from, false);
        frame(from, true);
        for (int i = 1; i <= 20; ++i) frame(ImVec2(from.x + (to.x - from.x) * i / 20, from.y + (to.y - from.y) * i / 20), true);
        for (int i = 0; i < 3; ++i) frame(to, true);
        frame(to, false);
        idle();
    }
    static ImGuiWindow* window(const char* name) { return ImGui::FindWindowByName(name); }
    static bool shown(const char* name) {
        ImGuiWindow* w = window(name);
        return w && w->Active;
    }
};

ImGuiContext* headless_context() {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1600, 900);
    io.DeltaTime = 1.f / 60;
    unsigned char* px;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    return ctx;
}

}  // namespace

TEST(workspaces_keep_their_own_layouts) {
    ImGuiContext* ctx = headless_context();
    Ui ui;
    ui.idle();
    // Pose: no graph; Properties docked on the right.
    CHECK(!Ui::shown("Graph"));
    ImGuiWindow* props = Ui::window("Properties");
    CHECK(props->DockIsActive && props->Pos.x > 1000);

    // Pose's Properties pulled out to float.
    const ImRect tab = props->DockNode->TabBar->BarRect;
    ui.drag(ImVec2(tab.Min.x + 20, tab.GetCenter().y), ImVec2(700, 400));
    CHECK(!props->DockIsActive);

    // Animate is built fresh the first time: the graph shows, Properties docked on the right.
    ui.go(Workspace::Animate);
    CHECK(Ui::shown("Graph") && Ui::window("Graph")->DockIsActive);
    CHECK(!Ui::shown("Inventory"));
    CHECK(props->DockIsActive && props->Pos.x > 1000);

    // Back to Pose: Properties floats again, where it was left.
    ui.go(Workspace::Pose);
    CHECK(!Ui::shown("Graph"));
    CHECK(Ui::shown("Properties") && !props->DockIsActive);
    CHECK(Ui::window("Bones")->DockIsActive);

    // The tab in front in Pose's left column is still the one that was (Bones), not the last to reappear.
    CHECK(Ui::window("Bones")->DockNode && Ui::window("Bones")->DockNode->SelectedTabId == Ui::window("Bones")->TabId);

    // And Animate is as it was: nothing done in Pose reached it.
    ui.go(Workspace::Animate);
    CHECK(props->DockIsActive && props->Pos.x > 1000);
    CHECK(Ui::window("Graph")->DockIsActive);
    ImGui::DestroyContext(ctx);
}

TEST(workspaces_show_their_own_panels) {
    // A panel brought in from the menus or F3 shows in a workspace that leaves it out; All shows every panel.
    CHECK(!workspace_shows(Workspace::Pose, "Graph", {}));
    CHECK(workspace_shows(Workspace::Pose, "Graph", {"Graph"}));
    CHECK(workspace_shows(Workspace::Animate, "Graph", {}));
    for (const char* p : kWindows) CHECK(workspace_shows(Workspace::All, p, {}));
    CHECK(!workspace_shows(Workspace::All, "Export", {}));  // All keeps Export in Properties
    // The toolbar keeps one order: a workspace only leaves buttons out.
    for (int w = 0; w < kWorkspaceCount; ++w) CHECK((workspace_def(Workspace(w)).toolbar & ~unsigned(kTbAll)) == 0);
    Workspace w = Workspace::All;
    CHECK(workspace_from_id("face", w) && w == Workspace::Face);
    CHECK(!workspace_from_id("modelling", w));
}

TEST(workspaces_start_new_users_in_pose_and_show_existing_users_the_tabs_on_all) {
    const std::string dir = std::filesystem::temp_directory_path().string() + "/vats_ws_test";
    std::filesystem::create_directories(dir);
    const std::string file = dir + "/settings.json";
    std::filesystem::remove(file);

    Settings fresh;  // no settings file: a new user
    fresh.load(file);
    CHECK(fresh.workspaces && fresh.workspace == "pose");

    {
        std::ofstream f(file);
        f << "{\"preset\": \"blender\", \"theme\": \"Dusk\"}";  // saved before workspaces existed
    }
    Settings existing;
    existing.load(file);
    CHECK(existing.workspaces && existing.workspace == "all");  // the tabs on, their layout kept

    {  // the trial's default left an existing install with the tabs off: 0.2.0 turns them on, once
        std::ofstream f(file);
        f << "{\"workspaces\": false, \"workspace\": \"all\"}";
    }
    Settings trial;
    trial.load(file);
    CHECK(trial.workspaces);
    trial.workspaces = false;  // turned off after that: stays off
    trial.save(file);
    Settings off;
    off.load(file);
    CHECK(!off.workspaces);

    // Saved and read back, with a layout per workspace.
    existing.workspaces = true, existing.workspace = "animate";
    existing.workspace_layouts["pose"] = "[Window][Bones]\nPos=0,0\n";
    existing.workspace_extra["pose"] = {"Graph"};
    existing.save(file);
    Settings again;
    again.load(file);
    CHECK(again.workspaces && again.workspace == "animate");
    CHECK(again.workspace_layouts["pose"] == "[Window][Bones]\nPos=0,0\n");
    CHECK(again.workspace_extra["pose"].size() == 1 && again.workspace_extra["pose"][0] == "Graph");
    std::filesystem::remove_all(dir);
}

// Properties shows what each job needs (design review #6): export settings once (All only; Export has its panel).
TEST(properties_sections_per_workspace) {
    for (Workspace w : {Workspace::Pose, Workspace::Animate, Workspace::Face, Workspace::Rig, Workspace::Export})
        CHECK(!properties_shows(w, PropSection::Export));
    CHECK(properties_shows(Workspace::All, PropSection::Export));
    CHECK(properties_shows(Workspace::Pose, PropSection::Bone) && properties_shows(Workspace::Pose, PropSection::JointLimits));
    CHECK(properties_shows(Workspace::Animate, PropSection::Animation) &&
          properties_open_at_first(Workspace::Animate, PropSection::Animation));
    // Pose keeps the clip's settings reachable, folded.
    CHECK(properties_shows(Workspace::Pose, PropSection::Animation) &&
          !properties_open_at_first(Workspace::Pose, PropSection::Animation));
    CHECK(!properties_shows(Workspace::Rig, PropSection::Animation));
}
