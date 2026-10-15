// Headless GL 3.3 core context, one implementation per OS:
//   Linux  platform/gpu_egl.cpp  EGL: default display with a pbuffer (projectM
//          finishes into the default framebuffer), else Mesa's surfaceless platform.
//   macOS  platform/gpu_cgl.cpp  CGL offscreen context (no window server needed).
//   Windows platform/gpu_wgl.cpp WGL on a hidden window (Mesa's opengl32.dll on GPU-less hosts).
// glistening's own scenes only use FBOs; MilkDrop mode needs the EGL pbuffer.
#pragma once
#include "gl.hpp"

#include <memory>
#include <string>

namespace tv {

struct Gpu {
    bool pbuffer = false;  // a default framebuffer projectM can finish into
    std::string renderer;  // "<GL_RENDERER> · <GL_VERSION>"

    Gpu();
    ~Gpu();
    Gpu(const Gpu &) = delete;
    Gpu &operator=(const Gpu &) = delete;
    // Creates the context, makes it current and loads tvgl. Returns "" or an error message.
    std::string init(int w, int h);
    // Resizes the pbuffer, if any; the old one stays current if the driver refuses the size.
    bool resize(int w, int h);

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    std::string finish(void *(*get)(const char *));
};

// Shared by the backends: load the table, then record what we got.
inline std::string Gpu::finish(void *(*get)(const char *)) {
    const std::string missing = tvgl::load(get);
    if (!missing.empty()) return "GL function " + missing + " not available";
    auto s = [](tvgl::GLenum e) {
        auto p = tvgl::glGetString(e);
        return p ? std::string((const char *)p) : std::string("?");
    };
    renderer = s(tvgl::GL_RENDERER) + " · " + s(tvgl::GL_VERSION);
    return "";
}

}  // namespace tv
