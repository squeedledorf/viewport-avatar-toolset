// Find a Tool's menu crawl (ui/icon_button.h): one frame finds every item with its path, shows nothing and leaves no
// menu open; a crawl that names an item runs it. Driven through a headless ImGui context.
#include <string>

#include "check.h"
#include "icon_button.h"
#include "imgui_internal.h"

using namespace vats;

namespace {

int seamless = 0;  // what the nested item's code did

// A menu bar like the app's: Tools > Loop Tools > Make Loop Seamless, and a disabled item.
void menus() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (begin_menu_icon(nullptr, "File")) {
        menu_item_icon(nullptr, "Save");
        ImGui::EndMenu();
    }
    if (begin_menu_icon(nullptr, "Tools")) {
        if (begin_menu_icon(nullptr, "Loop Tools")) {
            if (menu_item_icon(nullptr, "Make Loop Seamless")) ++seamless;
            menu_item_icon(nullptr, "Nothing Here", nullptr, false, false);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

std::vector<MenuEntry> frame(bool crawl, const std::string& run_path = "", const std::string& run_label = "", bool* ran = nullptr) {
    ImGui::NewFrame();
    if (crawl) start_menu_crawl(run_path, run_label);
    menus();
    bool r = false;
    std::vector<MenuEntry> found;
    if (crawl) found = finish_menu_crawl(r);
    if (ran) *ran = r;
    ImGui::Render();
    return found;
}

bool visible_menu() {
    for (ImGuiWindow* w : GImGui->Windows)
        if (w->Active && !w->Hidden && (w->Flags & ImGuiWindowFlags_ChildMenu)) return true;
    return false;
}

}  // namespace

TEST(menu_crawl_finds_runs_and_hides) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1600, 900);
    io.DeltaTime = 1.f / 60;
    unsigned char* px;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    for (int i = 0; i < 2; ++i) frame(false);

    const std::vector<MenuEntry> found = frame(true);
    CHECK(!visible_menu());  // drawn, but never shown
    auto has = [&](const char* path, const char* label, bool enabled) {
        for (const MenuEntry& e : found)
            if (e.path == path && e.label == label && e.enabled == enabled) return true;
        return false;
    };
    CHECK(has("File", "Save", true));
    CHECK(has("Tools > Loop Tools", "Make Loop Seamless", true));
    CHECK(has("Tools > Loop Tools", "Nothing Here", false));
    CHECK_EQ(seamless, 0);
    frame(false);
    CHECK(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));  // and closed after

    bool ran = false;
    frame(true, "Tools > Loop Tools", "Make Loop Seamless", &ran);
    CHECK(ran);
    CHECK_EQ(seamless, 1);
    frame(true, "Tools > Loop Tools", "Nothing Here", &ran);  // disabled: not run
    CHECK(!ran);
    frame(false);
    CHECK(!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
    ImGui::DestroyContext(ctx);
}
