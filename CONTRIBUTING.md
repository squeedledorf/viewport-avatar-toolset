# Contributing to Viewport Avatar Toolset

Thanks for helping. Bug reports, fixes, help-page corrections and new features are all welcome. For
anything bigger than a small fix, open an issue first so the approach can be agreed before you write it.

By contributing you agree that your work is licensed under the project's licence, the GNU Lesser General
Public License, version 2.1 only (see `LICENSE`).

## Building

VATs is C++20 with CMake 3.20 or newer. The core library has no dependencies beyond the vendored code in
`third_party/`. The editor UI uses Dear ImGui (docking branch), which CMake downloads at configure time.
The standalone app uses SDL3: CMake uses the system's SDL3 when it finds one, otherwise it downloads and
builds the pinned release and installs it next to the app.

### Linux

You need a C++20 compiler (GCC 11 or newer, or Clang 14 or newer), CMake, Ninja (optional) and git. If
your distribution has no SDL3 package, install the headers SDL needs to build: on Debian and Ubuntu,
`libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev
libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libdbus-1-dev
libudev-dev libasound2-dev libpulse-dev`.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/tests/vats_tests          # or: ctest --test-dir build
build/app/vats                  # runs from the build tree
cmake --install build --prefix ~/.local/opt/vats   # optional
```

### Windows

Install Visual Studio 2022 (or its Build Tools) with the **Desktop development with C++** workload, which
includes CMake and Ninja, and git. In an **x64 Native Tools Command Prompt for VS 2022**:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON
cmake --build build
build\tests\vats_tests.exe
cmake --install build --prefix C:\vats
```

The app is a GUI program (no console window). `cmake --install` puts `vats.exe` and `SDL3.dll` in `bin\`
and the data, fonts, props and help pages in `share\viewport-avatar-toolset\`.

### CMake options

| Option | Default | What it does |
|---|---|---|
| `VATS_BUILD_TESTS` | `ON` | Builds `vats_tests` (the core tests; `ctest` runs them) |
| `VATS_BUILD_UI` | `ON` | Builds `vats_ui`, the editor UI library (Dear ImGui, no SDL) |
| `VATS_BUILD_APP` | `ON` | Builds the standalone app, `vats` (needs SDL3 and `vats_ui`) |
| `VATS_FBX` | `ON` | Reads FBX files through the vendored ufbx |

To build without network access, point CMake at local copies:
`-DFETCHCONTENT_SOURCE_DIR_IMGUI=<imgui checkout> -DFETCHCONTENT_SOURCE_DIR_SDL3=<SDL checkout>
-DFETCHCONTENT_FULLY_DISCONNECTED=ON`. The pinned versions are the `GIT_TAG` lines in `ui/CMakeLists.txt`
(Dear ImGui) and `app/CMakeLists.txt` (SDL3).

## Tests

`vats_tests` must pass on every change, and the build must stay free of warnings. Continuous integration
builds with GCC and Clang on Linux (warnings as errors) and MSVC on Windows, and runs the tests; see
`.github/workflows/ci.yml`. Locally:

```sh
cmake -S . -B build-check -G Ninja -DCMAKE_CXX_FLAGS="-Werror -Wno-error=free-nonheap-object"
cmake --build build-check && build-check/tests/vats_tests
```

Add a test for new core behaviour in `tests/` (plain `TEST(name) { CHECK(...); }`, see `tests/check.h`),
and add the file to `tests/CMakeLists.txt`. `.anim` encoding must match the SL viewer bit for bit, which
is why `core/src/anim_file.cpp` and the tests are built without fused multiply-add; keep it that way.

For UI changes, a screenshot helps review. The app can take one without a display server's dialogs:
`vats --screenshot out.png` (see the **Command line** help page for the options that set up a scene).

## Where code goes

- **Logic in `core/`, UI in `ui/`.** `core/` (`libvats`, namespace `vats`) is the animation model, file
  formats, IK, retargeting and everything else that can be tested without a window. It has no UI,
  rendering or platform dependencies. If something can be computed, it belongs in the core, with a test.
- **`ui/`** (`vats_ui`) is every panel, menu, tool window and dialog, drawn with Dear ImGui. It talks to
  the program it runs in only through `ui::Host` (`ui/host.h`) and never includes SDL: the build fails if
  it does (`ui/no_sdl/`).
- **`app/`** is the standalone program: the SDL3 window, OpenGL renderer and the `ui::Host` for them.
- **`third_party/`** is vendored code, unmodified. Update it only as a whole release, and record the
  version in `THIRD_PARTY_LICENSES.md`.

## Code style

- Follow the file you are editing. Four-space indents, no tabs, lines up to about 120 columns, braces on
  the same line, `snake_case` functions and variables, `PascalCase` types, `kConstant` constants and a
  trailing underscore on private members.
- Every source file starts with a one-line description and the licence line:

  ```cpp
  // Viewport Avatar Toolset - what this file is for.
  // Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
  ```

- Comments say why, in plain full sentences. Keep them short and true to the code.
- Standard library first; no new dependencies without discussing it in an issue.

## Help pages

The in-app help is the Markdown wiki in `docs/wiki/`; the app ships it and the Help window renders it.
Follow the style guide in `docs/wiki/STYLE.md` (one topic per page, reference first, UI names exactly as
the app shows them). The test `wiki_shipped_pages` checks that every link and section resolves, so run
the tests after editing the help. When a change alters what the user sees, update the matching help page
in the same pull request.

## Pull requests

- One topic per pull request, with a clear description of what changed and why.
- The tests pass and the build has no new warnings.
- New behaviour has a test (core) or a screenshot (UI), and the help pages are up to date.
