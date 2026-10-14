// Linux: headless GL through EGL. Prefers the default display with a pbuffer
// (projectM finishes into the default framebuffer); falls back to Mesa's
// surfaceless platform (render nodes, or llvmpipe without a GPU).
#include "../gpu.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

namespace tv {

struct Gpu::Impl {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLConfig cfg = nullptr;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface surf = EGL_NO_SURFACE;
};

static std::string hex(unsigned v) {
    char b[16];
    snprintf(b, sizeof b, "%x", v);
    return b;
}

// eglGetProcAddress, with a fallback for drivers whose lookup skips core 1.x entry points.
static void *procAddress(const char *n) {
    static void *lib = nullptr;
    if (void *p = (void *)eglGetProcAddress(n)) return p;
    if (!lib) lib = dlopen("libOpenGL.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) lib = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    return lib ? dlsym(lib, n) : nullptr;
}

static bool tryDisplay(Gpu::Impl &g, EGLDisplay d, bool withPbuffer, int w, int h, std::string &err) {
    const char *what = withPbuffer ? "pbuffer" : "surfaceless context";
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, nullptr, nullptr)) {
        err = std::string(what) + ": eglInitialize failed";
        return false;
    }
    eglBindAPI(EGL_OPENGL_API);
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, withPbuffer ? EGL_PBUFFER_BIT : 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                              EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, withPbuffer ? 16 : 0, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(d, cfgAttr, &g.cfg, 1, &n) || !n) {
        err = std::string(what) + ": no OpenGL config";
        eglTerminate(d);
        return false;
    }
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                              EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    g.ctx = eglCreateContext(d, g.cfg, EGL_NO_CONTEXT, ctxAttr);
    if (g.ctx == EGL_NO_CONTEXT) {
        err = std::string(what) + ": no GL 3.3 core context (EGL error 0x" + hex(eglGetError()) + ")";
        eglTerminate(d);
        return false;
    }
    if (withPbuffer) {
        const EGLint a[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
        g.surf = eglCreatePbufferSurface(d, g.cfg, a);
        if (g.surf == EGL_NO_SURFACE) {  // no default framebuffer: not the pbuffer path at all
            err = std::string(what) + ": eglCreatePbufferSurface failed";
            eglDestroyContext(d, g.ctx), g.ctx = EGL_NO_CONTEXT;
            eglTerminate(d);
            return false;
        }
    }
    if (!eglMakeCurrent(d, g.surf, g.surf, g.ctx)) {
        err = std::string(what) + ": eglMakeCurrent failed";
        eglDestroyContext(d, g.ctx);
        if (g.surf != EGL_NO_SURFACE) eglDestroySurface(d, g.surf);
        g.ctx = EGL_NO_CONTEXT, g.surf = EGL_NO_SURFACE;
        eglTerminate(d);
        return false;
    }
    g.dpy = d;
    return true;
}

Gpu::Gpu() : impl_(new Impl) {}

Gpu::~Gpu() {
    Impl &g = *impl_;
    if (g.dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(g.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g.surf != EGL_NO_SURFACE) eglDestroySurface(g.dpy, g.surf);
    if (g.ctx != EGL_NO_CONTEXT) eglDestroyContext(g.dpy, g.ctx);
    eglTerminate(g.dpy);
}

std::string Gpu::init(int w, int h) {
    const char *want = getenv("VIZ_EGL");  // "default" or "surfaceless" forces one path
    std::string err;
    if (!want || std::string(want) != "surfaceless") {
        if (tryDisplay(*impl_, eglGetDisplay(EGL_DEFAULT_DISPLAY), true, w, h, err)) return pbuffer = true, finish(procAddress);
        if (want && std::string(want) == "default") return err;
    }
    auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!getPlatformDisplay) return err.empty() ? "no EGL platform support" : err;
    std::string err2;
    for (bool pb : {true, false})  // Mesa offers pbuffers here too; without one MilkDrop is off
        if (tryDisplay(*impl_, getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr), pb, w, h, err2))
            return pbuffer = pb, finish(procAddress);
    return (err.empty() ? "" : err + "; ") + err2;
}

bool Gpu::resize(int w, int h) {
    Impl &g = *impl_;
    if (!pbuffer) return true;
    const EGLint a[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
    EGLSurface s = eglCreatePbufferSurface(g.dpy, g.cfg, a);
    if (s == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(g.dpy, s, s, g.ctx)) {
        eglDestroySurface(g.dpy, s);
        eglMakeCurrent(g.dpy, g.surf, g.surf, g.ctx);
        return false;
    }
    eglDestroySurface(g.dpy, g.surf);
    g.surf = s;
    return true;
}

}  // namespace tv
