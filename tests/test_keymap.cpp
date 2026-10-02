// The user's own shortcuts over a preset (ui/keymap.h) and their place in settings.json.
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "check.h"
#include "keymap.h"
#include "settings.h"

using namespace vats;

TEST(keymap_chord_names_round_trip) {
    const ImGuiKeyChord ctrl_shift_k = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_K;
    CHECK(chord_name(ctrl_shift_k) == "Ctrl+Shift+K");
    CHECK(chord_name(ImGuiKey_Keypad5) == "Keypad5");
    CHECK(chord_name(0).empty());
    ImGuiKeyChord k = 1;
    CHECK(parse_chord("Ctrl+Shift+K", k) && k == ctrl_shift_k);
    CHECK(parse_chord("shift+ctrl+k", k) && k == ctrl_shift_k);  // any case, any modifier order
    CHECK(parse_chord("Alt+Period", k) && k == (ImGuiMod_Alt | ImGuiKey_Period));
    CHECK(parse_chord("", k) && k == 0);
    for (ImGuiKeyChord c : {ImGuiKeyChord(ImGuiKey_F12), ImGuiMod_Super | ImGuiKey_Space, ImGuiMod_Ctrl | ImGuiKey_KeypadDecimal,
                            ImGuiMod_Alt | ImGuiKey_LeftBracket}) {
        CHECK(parse_chord(chord_name(c), k) && k == c);
    }
    CHECK(!parse_chord("Ctrl+Nope", k));
    CHECK(!parse_chord("Hyper+K", k));
    CHECK(!parse_chord("LeftCtrl", k));    // a modifier alone is no shortcut
    CHECK(!parse_chord("MouseLeft", k));   // nor a mouse button
    CHECK(!parse_chord("GamepadStart", k));
}

TEST(keymap_overrides_apply_over_the_preset) {
    const KeyPair preset = {ImGuiKey_Keypad5, 0};
    KeyOverrides o;
    CHECK(effective_keys(o, "view_ortho", preset) == preset);
    set_keys(o, "view_ortho", {ImGuiMod_Ctrl | ImGuiKey_5, 0}, preset);
    CHECK(o.at("view_ortho")[0] == "Ctrl+5" && o.at("view_ortho")[1].empty());
    CHECK((effective_keys(o, "view_ortho", preset) == KeyPair{ImGuiMod_Ctrl | ImGuiKey_5, 0}));
    CHECK(effective_keys(o, "other", preset) == preset);
    // Only the second slot kept: it moves to the first, which the menus show.
    set_keys(o, "play", {0, ImGuiKey_P}, {ImGuiKey_Space, 0});
    CHECK((effective_keys(o, "play", {ImGuiKey_Space, 0}) == KeyPair{ImGuiKey_P, 0}));
    o["tween"] = {"", "Shift+E"};  // the same from a hand-edited file
    CHECK((effective_keys(o, "tween", {0, 0}) == KeyPair{ImGuiMod_Shift | ImGuiKey_E, 0}));
    set_keys(o, "copy", {ImGuiKey_C, ImGuiKey_C}, {0, 0});  // one key in both slots is kept once
    CHECK(o.at("copy")[0] == "C" && o.at("copy")[1].empty());
    // No keys at all is an override too; the preset's own keys are none.
    set_keys(o, "key", {0, 0}, {ImGuiKey_S, 0});
    CHECK((effective_keys(o, "key", {ImGuiKey_S, 0}) == KeyPair{0, 0}));
    // Setting the preset's keys again drops the override.
    set_keys(o, "view_ortho", preset, preset);
    CHECK(!o.count("view_ortho"));
    // A typo in a hand-edited file keeps the preset's keys.
    o["frame_all"] = {"Ctrl+Nope", ""};
    CHECK(effective_keys(o, "frame_all", {ImGuiKey_A, 0}) == (KeyPair{ImGuiKey_A, 0}));
}

TEST(keymap_conflicts_name_the_other_action) {
    const std::vector<std::pair<std::string, KeyPair>> b = {
        {"save", {ImGuiMod_Ctrl | ImGuiKey_S, 0}},
        {"redo", {ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z}},
        {"key", {ImGuiKey_S, 0}}};
    CHECK((find_conflict(b, "key", ImGuiMod_Ctrl | ImGuiKey_S) == std::pair<std::string, int>{"save", 0}));
    CHECK((find_conflict(b, "undo", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) == std::pair<std::string, int>{"redo", 1}));
    CHECK(find_conflict(b, "save", ImGuiMod_Ctrl | ImGuiKey_S).second == -1);  // its own key is no conflict
    CHECK(find_conflict(b, "key", ImGuiMod_Shift | ImGuiKey_S).second == -1);  // modifiers make a different key
    CHECK(find_conflict(b, "key", 0).second == -1);                            // clearing never conflicts
}

TEST(keymap_numpad_keys) {
    CHECK(needs_numpad(ImGuiKey_Keypad5));
    CHECK(needs_numpad(ImGuiMod_Ctrl | ImGuiKey_Keypad1));
    CHECK(needs_numpad(ImGuiKey_KeypadDecimal));
    CHECK(!needs_numpad(ImGuiKey_5));
    CHECK(!needs_numpad(0));
}

TEST(keymap_overrides_saved_in_settings) {
    // In the temp folder: the working folder may not be writable (spec 09 section 0b runs the tests from /).
    const std::string file = (std::filesystem::temp_directory_path() / "vats_keymap_settings_test.json").string();
    Settings s;
    s.key_overrides["view_ortho"] = {"Ctrl+5", ""};
    s.key_overrides["play"] = {"P", "Alt+V"};
    s.save(file);
    Settings t;
    t.load(file);
    CHECK(t.key_overrides == s.key_overrides);
    std::remove(file.c_str());
}

TEST(keymap_offered_presets) {
    const auto app = offered_presets(false), viewer = offered_presets(true);
    CHECK(app.size() == 4 && app.front() == Preset::Industry && app.back() == Preset::SecondLife);
    CHECK(viewer.size() == 1 && viewer[0] == Preset::SecondLife);  // one preset: Preferences shows no picker
    CHECK(std::string(preset_label(Preset::SecondLife)) == "Second Life");
}

TEST(settings_bone_style_glyph_loads_as_stick) {
    const std::string file = (std::filesystem::temp_directory_path() / "vats_bone_style_test.json").string();
    {
        std::ofstream f(file, std::ios::trunc);
        f << "{\"bone_style\": \"glyph\"}\n";
    }
    Settings s;
    s.load(file);
    CHECK(s.bone_style == "stick");

    {
        std::ofstream f(file, std::ios::trunc);
        f << "{\"bone_style\": \"hidden\"}\n";
    }
    Settings h;
    h.load(file);
    CHECK(h.bone_style == "hidden");

    {
        std::ofstream f(file, std::ios::trunc);
        f << "{\"bone_style\": \"stick\"}\n";
    }
    Settings st;
    st.load(file);
    CHECK(st.bone_style == "stick");

    {
        std::ofstream f(file, std::ios::trunc);
        f << "{\"bone_style\": \"unknown_style\"}\n";
    }
    Settings un;
    un.load(file);
    CHECK(un.bone_style.empty());

    std::remove(file.c_str());
}

TEST(settings_default_preset) {
    // 1. Fresh settings defaults to Second Life
    Settings fresh;
    CHECK(fresh.preset == Preset::SecondLife);

    // 2. Loading nonexistent file preserves Second Life
    fresh.load("/nonexistent_path/settings.json");
    CHECK(fresh.preset == Preset::SecondLife);

    // 3. Existing settings file with no preset key preserves Industry
    const std::string file = (std::filesystem::temp_directory_path() / "vats_preset_test.json").string();
    {
        std::ofstream f(file, std::ios::trunc);
        f << "{\"theme\": \"Dusk\"}\n";
    }
    Settings old_cfg;
    old_cfg.load(file);
    CHECK(old_cfg.preset == Preset::Industry);

    // 4. Existing file with explicit preset loads that preset
    {
        std::ofstream f(file, std::ios::trunc);
        f << "{\"preset\": \"blender\"}\n";
    }
    Settings blender_cfg;
    blender_cfg.load(file);
    CHECK(blender_cfg.preset == Preset::Blender);

    {
        std::ofstream f(file, std::ios::trunc);
        f << "{\"preset\": \"secondlife\"}\n";
    }
    Settings sl_cfg;
    sl_cfg.load(file);
    CHECK(sl_cfg.preset == Preset::SecondLife);

    std::remove(file.c_str());
}
