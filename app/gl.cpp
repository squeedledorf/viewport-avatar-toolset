// Viewport Avatar Toolset - OpenGL function loading.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#include "gl.h"

#include <SDL3/SDL_video.h>

namespace gl {

const char* load() {
#define VATS_GL_LOAD(type, name, sym)                                 \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(sym));         \
    if (!name) return sym;
    VATS_GL_FUNCTIONS(VATS_GL_LOAD)
#undef VATS_GL_LOAD
    return nullptr;
}

}  // namespace gl
