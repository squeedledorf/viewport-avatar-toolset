// The UI themes (ui/theme.h), on a headless ImGui context: the app's own themes and a host's (the viewer's skin).
#include "check.h"
#include "imgui.h"
#include "theme.h"

using namespace vats;

namespace {

float lum(ImU32 c) { return float((c >> IM_COL32_R_SHIFT) & 255) + float((c >> IM_COL32_G_SHIFT) & 255) + float((c >> IM_COL32_B_SHIFT) & 255); }
float lum(const ImVec4& c) { return 255 * (c.x + c.y + c.z); }

}  // namespace

TEST(theme_floating_title_lifted_and_accent_labels_read) {
    ImGuiContext* ctx = ImGui::CreateContext();
    // Dusk: the amber accent takes a dark label; a floating window's title bar is lighter than the panels.
    apply_theme(0, 1);
    const ImVec4* c = ImGui::GetStyle().Colors;
    CHECK(lum(c[ImGuiCol_TitleBg]) > lum(c[ImGuiCol_WindowBg]));
    CHECK(lum(c[ImGuiCol_TitleBgActive]) >= lum(c[ImGuiCol_TitleBg]));
    CHECK(lum(c[ImGuiCol_Tab]) < lum(c[ImGuiCol_TitleBg]));  // the docked strip stays darker (App::draw_dockspace)
    CHECK(lum(accent_text_colour()) < 100);
    CHECK(ImGui::GetStyle().HoverFlagsForTooltipMouse & ImGuiHoveredFlags_AllowWhenDisabled);  // disabled buttons say why

    // A host's skin with a dark accent and light panels: a light label, and still a lifted title bar.
    HostColours h{};
    h.bg = ImVec4(0.80f, 0.80f, 0.82f, 1), h.panel = ImVec4(0.86f, 0.86f, 0.88f, 1), h.frame = ImVec4(0.92f, 0.92f, 0.93f, 1);
    h.frame_hi = ImVec4(0.95f, 0.95f, 0.96f, 1), h.frame_active = ImVec4(0.97f, 0.97f, 0.98f, 1);
    h.text = ImVec4(0.1f, 0.1f, 0.1f, 1), h.text_dim = ImVec4(0.4f, 0.4f, 0.4f, 1), h.border = ImVec4(0.6f, 0.6f, 0.6f, 1);
    h.accent = ImVec4(0.10f, 0.25f, 0.55f, 1);
    apply_theme(0, 1, &h);
    CHECK(lum(accent_text_colour()) > 600);
    CHECK(lum(c[ImGuiCol_TitleBg]) > lum(c[ImGuiCol_WindowBg]));
    // The loop band is the host's accent, faint, not a fixed blue.
    const ImU32 band = loop_band();
    CHECK(((band >> IM_COL32_A_SHIFT) & 255) < 40);
    CHECK((band & ~IM_COL32_A_MASK) == (accent_colour() & ~IM_COL32_A_MASK));
    ImGui::DestroyContext(ctx);
}
