// The default dock layout (ui/dock_layout.h) and docking a window back in the viewer's passthrough dockspace,
// driven through a headless ImGui context.
#include <initializer_list>
#include <utility>

#include "check.h"
#include "dock_layout.h"
#include "imgui_internal.h"

using namespace vats;

namespace {

const char* const kPanels[] = {"Bones", "Picker", "Inventory", "Properties", "Graph", "Dope Sheet", "Timeline"};

struct Ui {
    ImGuiID dock = 0;
    bool reset = true;

    // One frame as the viewer draws it: a passthrough centre, the panels, and the 3D view's drop window
    // (draw_viewport) while the view takes the drag.
    void frame(ImVec2 mouse, bool down) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        ImGui::NewFrame();
        dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
        if (std::exchange(reset, false)) build_default_layout(dock, true, nullptr);
        for (const char* p : kPanels) {
            ImGui::Begin(p);
            ImGui::TextUnformatted(p);
            ImGui::End();
        }
        if (is_view_drop(ImGui::GetDragDropPayload())) {
            const ImGuiDockNode* c = ImGui::DockBuilderGetCentralNode(dock);
            ImGui::SetNextWindowPos(c->Pos);
            ImGui::SetNextWindowSize(c->Size);
            ImGui::Begin("##world_drop", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking);
            ImGui::End();
        }
        ImGui::Render();
    }
    void drag(ImVec2 from, ImVec2 to) {
        frame(from, false);
        frame(from, true);
        for (int i = 1; i <= 20; ++i) frame(ImVec2(from.x + (to.x - from.x) * i / 20, from.y + (to.y - from.y) * i / 20), true);
        for (int i = 0; i < 3; ++i) frame(to, true);  // the drop preview settles
        frame(to, false);
        for (int i = 0; i < 2; ++i) frame(ImVec2(1, 1), false);
    }
    static ImGuiWindow* window(const char* name) { return ImGui::FindWindowByName(name); }
};

}  // namespace

TEST(dock_layout_undock_redock_and_reset) {
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

    Ui ui;
    for (int i = 0; i < 3; ++i) ui.frame(ImVec2(1, 1), false);
    for (const char* p : kPanels) CHECK(Ui::window(p)->DockIsActive);
    ImGuiWindow* props = Ui::window("Properties");
    const ImGuiID right = props->DockNode->ID;
    CHECK(props->Pos.x > 1000);  // on the right

    // Undock Properties by its tab: its node empties and goes, the centre takes the right side.
    const ImRect tab = props->DockNode->TabBar->BarRect;
    ui.drag(ImVec2(tab.Min.x + 20, tab.GetCenter().y), ImVec2(700, 400));
    CHECK(!props->DockIsActive);
    const ImGuiDockNode* root = ImGui::DockBuilderGetNode(ui.dock);
    CHECK(ImGui::DockBuilderGetCentralNode(ui.dock)->Size.x + 400 > root->Size.x - 300);

    // Its title bar dragged to the right edge's outer drop target, over the passthrough centre, docks it again.
    ui.drag(ImVec2(props->Pos.x + 30, props->Pos.y + 8), ImVec2(root->Pos.x + root->Size.x - 20, root->Pos.y + root->Size.y / 2));
    CHECK(props->DockIsActive);
    CHECK(props->Pos.x > 1000);

    // Floated again, then Reset Layout: every panel docked, Properties in its first-run node.
    ui.drag(ImVec2(props->DockNode->TabBar->BarRect.Min.x + 20, props->DockNode->TabBar->BarRect.GetCenter().y), ImVec2(700, 400));
    CHECK(!props->DockIsActive);
    ui.reset = true;
    for (int i = 0; i < 3; ++i) ui.frame(ImVec2(1, 1), false);
    for (const char* p : kPanels) CHECK(Ui::window(p)->DockIsActive);
    CHECK(props->DockNode && props->DockNode->ID == right);
    ImGui::DestroyContext(ctx);
}
