// Viewport Avatar Toolset - registering .vat (and .anim) files with the desktop.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#include "file_types.h"

#ifdef VATS_LEGACY_IMPORT
#include "vats/legacy_import.h"
#endif

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace vats {
namespace {

std::string exe_path() {
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + "vats";
}

#if defined(_WIN32) && defined(VATS_LEGACY_IMPORT)
std::wstring legacy_key() {  // HKCU\Software\Classes\<legacy project extension>
    const std::string ext = legacy_import::kProjectExtension;
    return L"Software\\Classes\\" + std::wstring(ext.begin(), ext.end());
}
#endif

#if !defined(_WIN32) && !defined(__APPLE__)
std::string data_home() {
    const char* x = SDL_getenv("XDG_DATA_HOME");
    if (x && *x) return x;
    const char* home = SDL_getenv("HOME");
    return std::string(home ? home : ".") + "/.local/share";
}

bool run(std::initializer_list<const char*> args) {
    std::vector<const char*> argv(args);
    argv.push_back(nullptr);
    SDL_Process* p = SDL_CreateProcess(argv.data(), false);
    if (!p) return false;
    int code = -1;
    SDL_WaitProcess(p, true, &code);
    SDL_DestroyProcess(p);
    return code == 0;
}

// The app icon in the hicolor sizes, for the desktop entry and, through the MIME file's <icon>, the file
// types. Installed trees keep it under share/icons; the source tree under packaging/icons.
const char* const kIconSizes[] = {"16", "32", "48", "64", "128", "256", "scalable"};

std::string icon_source(const std::string& packaging_dir, const std::string& size) {
    const bool svg = size == "scalable";
    const std::string dir = svg ? "scalable" : size + "x" + size;
    for (const std::string& p : {packaging_dir + "/../../icons/hicolor/" + dir + "/apps/viewport-avatar-toolset" + (svg ? ".svg" : ".png"),
                                 packaging_dir + "/../icons/viewport-avatar-toolset" + (svg ? ".svg" : "-" + size + ".png")})
        if (std::filesystem::exists(p)) return p;
    return "";
}

std::string icon_target(const std::string& share, const std::string& size) {
    const bool svg = size == "scalable";
    return share + "/icons/hicolor/" + (svg ? "scalable" : size + "x" + size) + "/apps/viewport-avatar-toolset" +
           (svg ? ".svg" : ".png");
}

bool copy_with_exec(const std::string& from, const std::string& to) {
    std::ifstream in(from);
    if (!in) return false;
    std::ostringstream out;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("Exec=", 0) == 0) line = "Exec=\"" + exe_path() + "\" %f";  // point at this build
#ifdef VATS_LEGACY_IMPORT
        if (line.rfind("MimeType=", 0) == 0) line += std::string(legacy_import::kMimeType) + ";";
#endif
        out << line << "\n";
    }
    std::ofstream f(to, std::ios::trunc);
    f << out.str();
    return bool(f);
}

// The MIME file (with VATS_LEGACY_IMPORT, plus the legacy project type).
bool copy_mime(const std::string& from, const std::string& to) {
    std::ifstream in(from);
    if (!in) return false;
    std::ostringstream out;
    for (std::string line; std::getline(in, line);) {
#ifdef VATS_LEGACY_IMPORT
        if (line.find("</mime-info>") != std::string::npos) out << legacy_import::kMimeEntry;
#endif
        out << line << "\n";
    }
    std::ofstream f(to, std::ios::trunc);
    f << out.str();
    return bool(f);
}
#endif

}  // namespace

#ifdef _WIN32
bool register_file_types(const std::string&, std::string& message) {
    // HKCU\Software\Classes: .vat -> ViewportAvatarToolset.Project -> open command and icon.
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" \"%1\"", icon = std::wstring(exe) + L",0";
    auto set = [](const wchar_t* key, const wchar_t* value) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return false;
        LONG r = RegSetValueExW(k, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                                DWORD((wcslen(value) + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
        return r == ERROR_SUCCESS;
    };
    bool ok = set(L"Software\\Classes\\.vat", L"ViewportAvatarToolset.Project") &&
#ifdef VATS_LEGACY_IMPORT
              set(legacy_key().c_str(), L"ViewportAvatarToolset.Project") &&
#endif
              set(L"Software\\Classes\\ViewportAvatarToolset.Project", L"VATs animation project") &&
              set(L"Software\\Classes\\ViewportAvatarToolset.Project\\DefaultIcon", icon.c_str()) &&
              set(L"Software\\Classes\\ViewportAvatarToolset.Project\\shell\\open\\command", command.c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    message = ok ? "Project files now open with VATs." : "Windows refused to register the file types.";
    return ok;
}

bool unregister_file_types(std::string& message) {
    // An extension's default value goes only while it still points at VATs (another program may have
    // taken it since); the key and what others added under it (OpenWithProgids, ShellNew) stay.
    auto remove_if_ours = [](const wchar_t* key) {
        wchar_t value[128];
        DWORD size = sizeof value;
        if (RegGetValueW(HKEY_CURRENT_USER, key, nullptr, RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS &&
            std::wstring(value) == L"ViewportAvatarToolset.Project")
            RegDeleteKeyValueW(HKEY_CURRENT_USER, key, nullptr);
    };
    remove_if_ours(L"Software\\Classes\\.vat");
#ifdef VATS_LEGACY_IMPORT
    remove_if_ours(legacy_key().c_str());
#endif
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\ViewportAvatarToolset.Project");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    message = "Removed the file association.";
    return true;
}
#elif defined(__APPLE__)
bool register_file_types(const std::string&, std::string& message) {
    message = "On macOS the app bundle declares its file types; move VATs to Applications.";
    return false;
}
bool unregister_file_types(std::string& message) {
    message = "On macOS the app bundle declares its file types.";
    return false;
}
#else
bool register_file_types(const std::string& packaging_dir, std::string& message) {
    const std::string share = data_home();
    SDL_CreateDirectory((share + "/applications").c_str());
    SDL_CreateDirectory((share + "/mime/packages").c_str());
    const std::string desktop = share + "/applications/viewport-avatar-toolset.desktop";
    const std::string mime = share + "/mime/packages/viewport-avatar-toolset.xml";
    if (!copy_with_exec(packaging_dir + "/viewport-avatar-toolset.desktop", desktop) ||
        !copy_mime(packaging_dir + "/viewport-avatar-toolset.xml", mime)) {
        message = "Could not write the desktop entry or MIME type into " + share;
        return false;
    }
    for (const char* size : kIconSizes) {  // best effort: a missing icon never fails the association
        const std::string from = icon_source(packaging_dir, size), to = icon_target(share, size);
        std::error_code ec;
        if (from.empty()) continue;
        std::filesystem::create_directories(std::filesystem::path(to).parent_path(), ec);
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    }
    run({"update-mime-database", (share + "/mime").c_str()});
    run({"update-desktop-database", (share + "/applications").c_str()});
    run({"xdg-mime", "default", "viewport-avatar-toolset.desktop", "application/x-vats-project"});
    message = ".vat files now open with VATs.";
#ifdef VATS_LEGACY_IMPORT
    run({"xdg-mime", "default", "viewport-avatar-toolset.desktop", legacy_import::kMimeType});
    message = std::string(".vat and ") + legacy_import::kProjectExtension + " files now open with VATs.";
#endif
    return true;
}

bool unregister_file_types(std::string& message) {
    const std::string share = data_home();
    SDL_RemovePath((share + "/applications/viewport-avatar-toolset.desktop").c_str());
    SDL_RemovePath((share + "/mime/packages/viewport-avatar-toolset.xml").c_str());
    for (const char* size : kIconSizes) SDL_RemovePath(icon_target(share, size).c_str());
    run({"update-mime-database", (share + "/mime").c_str()});
    run({"update-desktop-database", (share + "/applications").c_str()});
    message = "Removed the desktop entry, file types and icons.";
    return true;
}
#endif

}  // namespace vats
