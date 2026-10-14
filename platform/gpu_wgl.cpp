// Windows: a GL 3.3 core context through WGL on a hidden window (WGL needs a
// device context with a pixel format even when everything renders to FBOs).
// A legacy context is created first only to look up wglCreateContextAttribsARB.
// Hosts without a GL 3.3 driver (GPU-less VMs and CI runners, where Windows
// only offers "GDI Generic" GL 1.1) need Mesa's opengl32.dll next to the
// executable; that is reported as an error here, never as success.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../gpu.hpp"

namespace tv {

struct Gpu::Impl {
    HWND wnd = nullptr;
    HDC dc = nullptr;
    HGLRC ctx = nullptr;
};

// Extension and 1.2+ entry points come from wglGetProcAddress (which signals
// failure with 0, 1, 2, 3 or -1); GL 1.1 ones are only exported by opengl32.dll.
static void *procAddress(const char *n) {
    void *p = (void *)wglGetProcAddress(n);
    const intptr_t v = intptr_t(p);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) p = (void *)GetProcAddress(GetModuleHandleA("opengl32.dll"), n);
    return p;
}

Gpu::Gpu() : impl_(new Impl) {}

Gpu::~Gpu() {
    Impl &g = *impl_;
    if (g.ctx) wglMakeCurrent(nullptr, nullptr), wglDeleteContext(g.ctx);
    if (g.dc) ReleaseDC(g.wnd, g.dc);
    if (g.wnd) DestroyWindow(g.wnd);
}

std::string Gpu::init(int, int) {
    Impl &g = *impl_;
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = L"glistening-gl";
    RegisterClassW(&wc);  // fails harmlessly when already registered (a second Gpu)
    g.wnd = CreateWindowExW(0, wc.lpszClassName, L"glistening", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, inst, nullptr);
    if (!g.wnd) return "WGL: cannot create a hidden window (error " + std::to_string(GetLastError()) + ")";
    g.dc = GetDC(g.wnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof pfd, pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA, pfd.cColorBits = 32, pfd.cAlphaBits = 8, pfd.iLayerType = PFD_MAIN_PLANE;
    const int pf = ChoosePixelFormat(g.dc, &pfd);
    if (!pf || !SetPixelFormat(g.dc, pf, &pfd)) return "WGL: no usable pixel format";
    HGLRC legacy = wglCreateContext(g.dc);
    if (!legacy || !wglMakeCurrent(g.dc, legacy)) {
        if (legacy) wglDeleteContext(legacy);
        return "WGL: cannot create an OpenGL context (no OpenGL driver?)";
    }
    typedef HGLRC(WINAPI * CreateAttribs)(HDC, HGLRC, const int *);
    auto create = (CreateAttribs)procAddress("wglCreateContextAttribsARB");
    auto getString = (const unsigned char *(WINAPI *)(unsigned))procAddress("glGetString");
    const unsigned char *legacyRenderer = getString ? getString(0x1F01 /*GL_RENDERER*/) : nullptr;
    const std::string driver = legacyRenderer ? (const char *)legacyRenderer : "unknown";
    if (create) {
        const int attr[] = {0x2091 /*MAJOR*/, 3, 0x2092 /*MINOR*/, 3, 0x9126 /*PROFILE_MASK*/, 0x1 /*CORE*/, 0};
        g.ctx = create(g.dc, nullptr, attr);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(legacy);
    if (!g.ctx) return "WGL: no GL 3.3 core context from driver '" + driver + "' (GPU-less hosts need Mesa's opengl32.dll)";
    if (!wglMakeCurrent(g.dc, g.ctx)) return "WGL: wglMakeCurrent failed";
    return finish(procAddress);
}

bool Gpu::resize(int, int) { return true; }

}  // namespace tv
