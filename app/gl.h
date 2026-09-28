// Viewport Avatar Toolset - the OpenGL 3.3 functions the renderer uses, loaded through SDL.
// Copyright (C) 2026 Viewport Avatar Toolset contributors. LGPL-2.1, see LICENSE.
#pragma once

#include <SDL3/SDL_opengl.h>

namespace gl {

#define VATS_GL_FUNCTIONS(X)                                                    \
    X(decltype(&::glEnable), Enable, "glEnable")                                 \
    X(decltype(&::glDisable), Disable, "glDisable")                              \
    X(decltype(&::glBlendFunc), BlendFunc, "glBlendFunc")                        \
    X(decltype(&::glDepthMask), DepthMask, "glDepthMask")                        \
    X(decltype(&::glDepthFunc), DepthFunc, "glDepthFunc")                        \
    X(decltype(&::glViewport), Viewport, "glViewport")                           \
    X(decltype(&::glClearColor), ClearColor, "glClearColor")                     \
    X(decltype(&::glClear), Clear, "glClear")                                    \
    X(decltype(&::glFinish), Finish, "glFinish")                                \
    X(decltype(&::glDrawArrays), DrawArrays, "glDrawArrays")                     \
    X(decltype(&::glDrawElements), DrawElements, "glDrawElements")               \
    X(decltype(&::glGenTextures), GenTextures, "glGenTextures")                  \
    X(decltype(&::glBindTexture), BindTexture, "glBindTexture")                  \
    X(decltype(&::glTexImage2D), TexImage2D, "glTexImage2D")                     \
    X(decltype(&::glTexSubImage2D), TexSubImage2D, "glTexSubImage2D")            \
    X(decltype(&::glTexParameteri), TexParameteri, "glTexParameteri")            \
    X(decltype(&::glDeleteTextures), DeleteTextures, "glDeleteTextures")         \
    X(decltype(&::glCullFace), CullFace, "glCullFace")                           \
    X(decltype(&::glReadPixels), ReadPixels, "glReadPixels")                     \
    X(PFNGLCREATESHADERPROC, CreateShader, "glCreateShader")                     \
    X(PFNGLSHADERSOURCEPROC, ShaderSource, "glShaderSource")                     \
    X(PFNGLCOMPILESHADERPROC, CompileShader, "glCompileShader")                  \
    X(PFNGLGETSHADERIVPROC, GetShaderiv, "glGetShaderiv")                        \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog, "glGetShaderInfoLog")         \
    X(PFNGLDELETESHADERPROC, DeleteShader, "glDeleteShader")                     \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram, "glCreateProgram")                  \
    X(PFNGLATTACHSHADERPROC, AttachShader, "glAttachShader")                     \
    X(PFNGLLINKPROGRAMPROC, LinkProgram, "glLinkProgram")                        \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv, "glGetProgramiv")                     \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog, "glGetProgramInfoLog")      \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram, "glDeleteProgram")                  \
    X(PFNGLUSEPROGRAMPROC, UseProgram, "glUseProgram")                           \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation, "glGetUniformLocation")   \
    X(PFNGLUNIFORM1FPROC, Uniform1f, "glUniform1f")                              \
    X(PFNGLUNIFORM1IPROC, Uniform1i, "glUniform1i")                              \
    X(PFNGLUNIFORM3FPROC, Uniform3f, "glUniform3f")                              \
    X(PFNGLUNIFORM4FPROC, Uniform4f, "glUniform4f")                              \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv, "glUniformMatrix4fv")         \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays, "glGenVertexArrays")            \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray, "glBindVertexArray")            \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays, "glDeleteVertexArrays")   \
    X(PFNGLGENBUFFERSPROC, GenBuffers, "glGenBuffers")                           \
    X(PFNGLBINDBUFFERPROC, BindBuffer, "glBindBuffer")                           \
    X(PFNGLBUFFERDATAPROC, BufferData, "glBufferData")                           \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers, "glDeleteBuffers")                  \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer, "glVertexAttribPointer") \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray, "glEnableVertexAttribArray") \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers, "glGenFramebuffers")            \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer, "glBindFramebuffer")            \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers, "glDeleteFramebuffers")   \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D, "glFramebufferTexture2D") \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer, "glFramebufferRenderbuffer") \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus, "glCheckFramebufferStatus") \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers, "glGenRenderbuffers")         \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer, "glBindRenderbuffer")         \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers, "glDeleteRenderbuffers") \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, RenderbufferStorageMultisample, "glRenderbufferStorageMultisample") \
    X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer, "glBlitFramebuffer")

#define VATS_GL_DECLARE(type, name, sym) inline type name = nullptr;
VATS_GL_FUNCTIONS(VATS_GL_DECLARE)
#undef VATS_GL_DECLARE

// Loads every function; returns the name of the first missing one, or nullptr.
const char* load();

}  // namespace gl
