// macOS: an offscreen CGL core-profile context (macOS gives 4.1 for a 3.2
// core request; "#version 330 core" shaders compile there). No window server
// or drawable is needed: glistening renders into FBOs. Accelerated renderers
// first; VIZ_CGL=software forces Apple's software renderer, which is also
// tried when no accelerated one is available (VMs, some CI hosts).
#define GL_SILENCE_DEPRECATION
#include "../gpu.hpp"

#include <OpenGL/OpenGL.h>

#include <cstdlib>
#include <dlfcn.h>

namespace tv {

struct Gpu::Impl {
    CGLContextObj ctx = nullptr;
};

// Every GL 3.3 core entry point is exported by the OpenGL framework itself.
static void *procAddress(const char *n) {
    static void *lib = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    return lib ? dlsym(lib, n) : nullptr;
}

Gpu::Gpu() : impl_(new Impl) {}

Gpu::~Gpu() {
    if (!impl_->ctx) return;
    CGLSetCurrentContext(nullptr);
    CGLDestroyContext(impl_->ctx);
}

std::string Gpu::init(int, int) {
    const char *want = getenv("VIZ_CGL");
    const bool softwareOnly = want && std::string(want) == "software";
    std::string err;
    for (bool software : {false, true}) {
        if (!software && softwareOnly) continue;
        CGLPixelFormatAttribute a[16];
        int k = 0;
        a[k++] = kCGLPFAOpenGLProfile, a[k++] = (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core;
        a[k++] = kCGLPFAColorSize, a[k++] = (CGLPixelFormatAttribute)24;
        a[k++] = kCGLPFAAlphaSize, a[k++] = (CGLPixelFormatAttribute)8;
        if (software) a[k++] = kCGLPFARendererID, a[k++] = (CGLPixelFormatAttribute)kCGLRendererGenericFloatID;
        else a[k++] = kCGLPFAAccelerated, a[k++] = kCGLPFAAllowOfflineRenderers;
        a[k] = (CGLPixelFormatAttribute)0;
        CGLPixelFormatObj pf = nullptr;
        GLint n = 0;
        CGLError e = CGLChoosePixelFormat(a, &pf, &n);
        if (e == kCGLNoError && pf) {
            e = CGLCreateContext(pf, nullptr, &impl_->ctx);
            CGLDestroyPixelFormat(pf);
        } else if (e == kCGLNoError) {
            e = kCGLBadPixelFormat;
        }
        if (e == kCGLNoError && (e = CGLSetCurrentContext(impl_->ctx)) == kCGLNoError) return finish(procAddress);
        if (impl_->ctx) CGLDestroyContext(impl_->ctx), impl_->ctx = nullptr;
        err += std::string(err.empty() ? "" : "; ") + (software ? "software" : "accelerated") + " renderer: " + CGLErrorString(e);
    }
    return "no OpenGL 3.2+ core context via CGL (" + err + ")";
}

bool Gpu::resize(int, int) { return true; }

}  // namespace tv
