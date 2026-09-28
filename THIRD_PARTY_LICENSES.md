# Third-party code and data bundled with VATs

VATs itself is licensed under the GNU Lesser General Public License, version 2.1 only (`LICENSE`). It
includes or downloads the following, each under its own licence.

## Dear ImGui

- Where: downloaded at configure time from https://github.com/ocornut/imgui, tag `v1.92.9-docking`
  (`ui/CMakeLists.txt`), and compiled into the app.
- Used for: the whole editor UI.
- Licence: MIT, below.

The MIT License (MIT)

Copyright (c) 2014-2026 Omar Cornut

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without
limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

## SDL3

- Where: the system's SDL3 when CMake finds one; otherwise downloaded at configure time from
  https://github.com/libsdl-org/SDL, tag `release-3.4.16` (`app/CMakeLists.txt`), built as a shared
  library and shipped next to the app (`SDL3.dll` on Windows, `libSDL3.so.0` on Linux).
- Used for: the standalone app's window, input, file dialogs and OpenGL context.
- Licence: zlib, below.

Copyright (C) 1997-2026 Sam Lantinga

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be
held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and
to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original
   software. If you use this software in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the
   original software.
3. This notice may not be removed or altered from any source distribution.

## Inter

- Where: `app/assets/fonts/Inter-Regular.ttf` and `Inter-SemiBold.ttf`, Inter 4.0 from
  https://github.com/rsms/inter, unmodified.
- Used for: the UI text.
- Licence: SIL Open Font License 1.1. Full text in `app/assets/fonts/Inter-OFL.txt`.

Copyright (c) 2016 The Inter Project Authors.

## Lucide icons

- Where: `app/assets/fonts/lucide-icons.ttf`, a subset of the Lucide icon font from the npm package
  `lucide-static` 1.48.0 (https://lucide.dev), made by `tools/subset-icons.sh`; see
  `app/assets/fonts/ICONS.md`.
- Used for: the icons on buttons and menus.
- Licence: ISC; the icons derived from Feather are also under MIT. Full text in
  `app/assets/fonts/Lucide-ISC.txt`.

Copyright (c) 2026 Lucide Icons and Contributors; Copyright (c) 2013-present Cole Bemis (Feather).

## ufbx

- Where: `third_party/ufbx/` (`ufbx.c`, `ufbx.h`), vendored unmodified from
  https://github.com/ufbx/ufbx, release v0.23.1.
- Used for: FBX import (retargeting and mesh bodies). Built only with `-DVATS_FBX=ON` (the default).
- Licence: MIT, or public domain (Unlicense), at your choice. Full text in `third_party/ufbx/LICENSE`.

Copyright (c) 2020 Samuli Raivio. Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files, to deal in the Software without restriction, subject to the
conditions in the licence file.

## dr_wav, dr_mp3, dr_flac

- Where: `third_party/dr_libs/` (`dr_wav.h`, `dr_mp3.h`, `dr_flac.h`), vendored unmodified from
  https://github.com/mackron/dr_libs at commit dfe837763100 (2026-08-31).
- Used for: decoding the audio track (WAV, MP3, FLAC). Compiled in `core/src/audio_decoders.c`.
- Licence: public domain (Unlicense), or MIT No Attribution, at your choice. The full text is at the end of each
  header.

## stb_vorbis

- Where: `third_party/stb/stb_vorbis.c`, vendored unmodified from https://github.com/nothings/stb at commit
  2c980bb59875 (2026-08-02).
- Used for: decoding Ogg Vorbis audio.
- Licence: MIT, or public domain (Unlicense), at your choice. The full text is at the end of the file.

## stb_image

- Where: `third_party/stb/stb_image.h`, v2.30, vendored unmodified from https://github.com/nothings/stb at commit
  2c980bb59875 (2026-08-02), the same commit as stb_vorbis. Compiled in `core/src/image_decoders.c` with only its
  GIF and JPEG decoders.
- Used for: decoding the help's animated GIFs.
- Licence: MIT, or public domain (Unlicense), at your choice. The full text is at the end of the file.

## Second Life character data

- Where: `data/character/` (`avatar_skeleton.xml`, `avatar_lad.xml` and the `avatar_*.llm` body meshes),
  unmodified from the Second Life viewer, https://github.com/secondlife/viewer
  (`indra/newview/character`); see `data/character/README.md`.
- Used for: the SL skeleton, the default body shapes and the Linden body meshes.
- Licence: GNU Lesser General Public License, version 2.1 only (`LICENSE`).

Copyright (C) Linden Research, Inc.

## Starter props

- Where: `app/assets/props/*.dae`, converted from third-party models by Kenney, Quaternius, Poly by Google,
  jeremy, CreativeTrio and iPoly3D.
- Used for: the starter props in the Inventory.
- Licence: CC0 1.0, except `pen.dae`, `microphone.dae` and `guitar.dae`, which are under CC BY 3.0. Authors,
  sources and the changes made are listed in `app/assets/props/CREDITS.md`.
