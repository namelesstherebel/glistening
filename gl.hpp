// Minimal GL 3.3 core function table, filled from the platform's context
// (EGL, CGL or WGL; see gpu.hpp), so no GL headers or import libraries are
// needed. Names live in a namespace so they never interpose libOpenGL's
// exported symbols that projectM calls.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// GL entry points use the system calling convention: __stdcall on 32-bit Windows.
#if defined(_WIN32) && !defined(_WIN64)
#define TVGL_API __stdcall
#else
#define TVGL_API
#endif

namespace tvgl {

typedef unsigned GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLboolean, GLubyte;

enum : GLenum {
    GL_NO_ERROR = 0, GL_TRIANGLES = 0x0004, GL_TRIANGLE_STRIP = 0x0005,
    GL_ONE = 1, GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303,
    GL_BLEND = 0x0BE2, GL_DEPTH_TEST = 0x0B71, GL_CULL_FACE = 0x0B44, GL_SCISSOR_TEST = 0x0C11,
    GL_FUNC_ADD = 0x8006,
    GL_UNPACK_ALIGNMENT = 0x0CF5, GL_PACK_ALIGNMENT = 0x0D05,
    GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE0 = 0x84C0,
    GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801,
    GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803,
    GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_CLAMP_TO_EDGE = 0x812F, GL_REPEAT = 0x2901,
    GL_RED = 0x1903, GL_RGB = 0x1907, GL_RGBA = 0x1908, GL_R16F = 0x822D, GL_RGBA8 = 0x8058, GL_RGBA16F = 0x881A,
    GL_UNSIGNED_BYTE = 0x1401, GL_FLOAT = 0x1406, GL_HALF_FLOAT = 0x140B,
    GL_FRAMEBUFFER = 0x8D40, GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9,
    GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_COLOR_BUFFER_BIT = 0x4000,
    GL_FRAGMENT_SHADER = 0x8B30, GL_VERTEX_SHADER = 0x8B31,
    GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82, GL_INFO_LOG_LENGTH = 0x8B84,
    GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02,
};

#define TVGL_FUNCS(X)                                                                      \
    X(const GLubyte *, glGetString, (GLenum))                                              \
    X(GLenum, glGetError, ())                                                              \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))                                  \
    X(void, glClear, (GLbitfield))                                                         \
    X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                            \
    X(void, glEnable, (GLenum))                                                            \
    X(void, glDisable, (GLenum))                                                           \
    X(void, glBlendFunc, (GLenum, GLenum))                                                 \
    X(void, glBlendEquation, (GLenum))                                                     \
    X(void, glFinish, ())                                                                  \
    X(void, glPixelStorei, (GLenum, GLint))                                                \
    X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))        \
    X(void, glGenTextures, (GLsizei, GLuint *))                                            \
    X(void, glDeleteTextures, (GLsizei, const GLuint *))                                   \
    X(void, glBindTexture, (GLenum, GLuint))                                               \
    X(void, glActiveTexture, (GLenum))                                                     \
    X(void, glTexParameteri, (GLenum, GLenum, GLint))                                      \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint *))                                        \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint *))                               \
    X(void, glBindFramebuffer, (GLenum, GLuint))                                           \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))               \
    X(GLenum, glCheckFramebufferStatus, (GLenum))                                          \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(GLuint, glCreateShader, (GLenum))                                                    \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))       \
    X(void, glCompileShader, (GLuint))                                                     \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *))                                      \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                    \
    X(void, glDeleteShader, (GLuint))                                                      \
    X(GLuint, glCreateProgram, ())                                                         \
    X(void, glAttachShader, (GLuint, GLuint))                                              \
    X(void, glLinkProgram, (GLuint))                                                       \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *))                                     \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                   \
    X(void, glDeleteProgram, (GLuint))                                                     \
    X(void, glUseProgram, (GLuint))                                                        \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar *))                               \
    X(void, glUniform1i, (GLint, GLint))                                                   \
    X(void, glUniform1f, (GLint, GLfloat))                                                 \
    X(void, glUniform2f, (GLint, GLfloat, GLfloat))                                        \
    X(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                      \
    X(void, glUniform3fv, (GLint, GLsizei, const GLfloat *))                               \
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))                               \
    X(void, glGenVertexArrays, (GLsizei, GLuint *))                                        \
    X(void, glBindVertexArray, (GLuint))                                                   \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei))                                        \
    X(void, glDrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))

#define TVGL_DECL(ret, name, args) inline ret(TVGL_API *name) args = nullptr;
TVGL_FUNCS(TVGL_DECL)
#undef TVGL_DECL

// Fills the table from `get` (the platform's proc-address lookup).
// Returns the first missing function name, or "" when all loaded.
inline std::string load(void *(*get)(const char *)) {
#define TVGL_LOAD(ret, name, args)                                   \
    if (!(name = reinterpret_cast<ret(TVGL_API *) args>(get(#name)))) return #name;
    TVGL_FUNCS(TVGL_LOAD)
#undef TVGL_LOAD
    return "";
}

}  // namespace tvgl
