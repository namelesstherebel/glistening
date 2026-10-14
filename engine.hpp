// glistening's own renderer: GLSL scenes with feedback and optional instanced
// particles, crossfades, bloom + post, flash limiting, RGB readback.
#pragma once
#include "config.hpp"
#include "gl.hpp"
#include "music.hpp"

#include <cerrno>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace tv {
using namespace tvgl;

struct Src {
    std::string path, text;
};

struct Rect {
    float x0, y0, x1, y1;  // 0..1, top-left origin
};

struct Program {
    GLuint id = 0;
    std::map<std::string, GLint> loc;
    GLint operator()(const char *n) {
        auto it = loc.find(n);
        return it != loc.end() ? it->second : (loc[n] = glGetUniformLocation(id, n));
    }
};

struct SceneDef {
    std::string id, name, desc;
    float busy = 0.5f, bloom = 0.6f;
    int particles = 0;
    std::vector<std::string> palettes;
    Program prog, part;
    bool ok = false;
};

struct Target {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    // False if the driver can't allocate it (too big, out of memory).
    bool make(int W, int H, GLenum ifmt) {
        if (W == w && H == h && fbo) return true;
        free();
        while (glGetError() != GL_NO_ERROR) {}
        w = W, h = H;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, GL_RGBA, ifmt == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (glGetError() != GL_NO_ERROR || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return free(), false;
        clear();
        return true;
    }
    void clear() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
    }
    void free() {
        if (fbo) glDeleteFramebuffers(1, &fbo), glDeleteTextures(1, &tex);
        fbo = tex = 0, w = h = 0;
    }
};

// "// @key value" lines at the top of a scene file.
inline std::string meta(const std::string &src, const std::string &key) {
    std::istringstream in(src);
    std::string line, tag = "// @" + key + " ";
    while (std::getline(in, line))
        if (line.compare(0, tag.size(), tag) == 0) {
            std::string v = line.substr(tag.size());
            while (!v.empty() && isspace((unsigned char)v.back())) v.pop_back();  // CRLF files leave a '\r'
            return v;
        }
    return "";
}

class Engine {
public:
    std::vector<SceneDef> scenes;
    std::vector<int> remap;  // after load(): old scene index -> new index (-1 if gone)
    float intensity = 1;  // user effect intensity, 0..1.5
    float quality = 1;    // 0.25..1, scenes scale step/particle counts by it
    float exposure = 1;   // flash limiter output
    float meanLuma = 0;   // of the last read frame, 0..1

    // Compiles everything; returns "" or the errors. Broken scenes are skipped.
    std::string load(const std::vector<Src> &srcs) {
        std::map<std::string, std::string> f;
        for (auto &s : srcs) f[s.path] = s.text;
        std::string errs;
        if (!vao_) glGenVertexArrays(1, &vao_);
        auto need = [&](const char *p) -> const std::string & {
            static const std::string empty;
            if (!f.count(p)) errs += std::string("missing ") + p + "\n";
            return f.count(p) ? f[p] : empty;
        };
        const std::string common = need("scenes/common.glsl"), quad = need("shaders/quad.vert");
        auto build = [&](Program &p, const std::string &vs, const std::string &fs, const std::string &name) {
            std::string e;
            GLuint id = link(vs, fs, e);
            if (!id) {
                errs += name + ": " + e;
                return false;
            }
            if (p.id) glDeleteProgram(p.id);
            p.id = id, p.loc.clear();
            return true;
        };
        const std::string vsQuad = "#version 330 core\n" + quad;
        auto engineFrag = [&](const char *path) { return "#version 330 core\n" + need("shaders/mix.glsl") + "#line 1\n" + need(path); };
        build(down_, vsQuad, engineFrag("shaders/down.frag"), "down");
        build(blur_, vsQuad, engineFrag("shaders/blur.frag"), "blur");
        build(post_, vsQuad, engineFrag("shaders/post.frag"), "post");

        std::vector<SceneDef> next;
        for (auto &entry : f) {
            const std::string &path = entry.first, &text = entry.second;  // not a structured binding: lambdas below capture these (C++17)
            if (path.compare(0, 7, "scenes/") || path.size() < 6 || path.compare(path.size() - 5, 5, ".frag")) continue;
            SceneDef d;
            d.id = path.substr(7, path.size() - 12);
            for (auto &old : scenes)
                if (old.id == d.id) d.prog = old.prog, d.part = old.part, d.particles = old.particles;  // keep running programs if a reload breaks
            d.name = meta(text, "name");
            if (d.name.empty()) d.name = d.id;
            d.desc = meta(text, "desc");
            auto num = [&](const char *key, float &v, float hi) {  // malformed values are reported, never thrown
                const std::string m = meta(text, key);
                char *end = nullptr;
                const float x = m.empty() ? 0 : std::strtof(m.c_str(), &end);
                if (m.empty()) return;
                if (end == m.c_str() || *end || !std::isfinite(x)) errs += path + ": bad @" + key + " '" + m + "'\n";
                else v = std::clamp(x, 0.f, hi);
            };
            num("busy", d.busy, 1.f), num("bloom", d.bloom, 4.f);  // busy is matched against energy 0..1
            std::istringstream pl(meta(text, "palettes"));
            for (std::string p; std::getline(pl, p, ',');) d.palettes.push_back(p);
            const std::string head = "#version 330 core\n" + common + "#line 1\n";
            d.ok = build(d.prog, vsQuad, head + text, path);
            const std::string vpath = "scenes/" + d.id + ".vert";
            if (d.ok && !f.count(vpath) && d.part.id) {  // the particle pass was removed
                glDeleteProgram(d.part.id);
                d.part = {}, d.particles = 0;
            }
            if (d.ok && f.count(vpath)) {
                const std::string pm = meta(f[vpath], "particles");
                char *end = nullptr;
                errno = 0;
                const long n = std::strtol(pm.c_str(), &end, 10);  // checked, then clamped while still long
                const bool countOk = !pm.empty() && end != pm.c_str() && !*end && errno != ERANGE;
                if (!countOk) errs += vpath + ": bad @particles '" + pm + "'\n";
                d.ok = build(d.part, "#version 330 core\n#define VERTEX\n" + common + "#line 1\n" + f[vpath],
                             head + need("shaders/sprite.frag"), vpath);
                if (d.ok && countOk) d.particles = int(std::clamp(n, 0L, long(1 << 20)));  // the count belongs to the program that runs
            }
            if (d.ok || d.prog.id) d.ok = true;
            if (d.ok) next.push_back(d);  // a scene that never compiled is not offered at all
        }
        std::sort(next.begin(), next.end(), [](auto &a, auto &b) { return before(a.id, b.id); });
        // Reloads can add or drop scenes: tell callers where each old index went, and
        // keep the running slots on the same scene.
        remap.assign(scenes.size(), -1);
        for (size_t i = 0; i < scenes.size(); i++)
            for (size_t j = 0; j < next.size(); j++)
                if (next[j].id == scenes[i].id) remap[i] = int(j);
        for (auto &s : slots_)
            if (s.scene >= 0) s.scene = s.scene < (int)remap.size() && remap[s.scene] >= 0 ? remap[s.scene] : -2;  // gone: none (-1 is MilkDrop)
        for (size_t i = 0; i < scenes.size(); i++)  // programs of scenes that are gone
            if (remap[i] < 0) {
                if (scenes[i].prog.id) glDeleteProgram(scenes[i].prog.id);
                if (scenes[i].part.id) glDeleteProgram(scenes[i].part.id);
            }
        scenes = next;
        return errs;
    }

    // Display order: the built-in scenes, then custom scenes by id (a total order,
    // so every listing numbers scenes the same way).
    static bool before(const std::string &a, const std::string &b) {
        auto strip = [](std::string id) { return id.size() > 5 && id.compare(id.size() - 5, 5, ".frag") == 0 ? id.substr(0, id.size() - 5) : id; };
        const std::string x = strip(a), y = strip(b);
        return std::make_pair(order(x), x) < std::make_pair(order(y), y);
    }
    static int order(std::string id) {  // display order; unknown scenes go last
        if (id.size() > 5 && id.compare(id.size() - 5, 5, ".frag") == 0) id.resize(id.size() - 5);
        static const char *ids[] = {"aurora", "mercury", "galaxy", "tunnel", "ridgeline", "ink", "quasicrystal", "phosphor",
                                    "rainglass", "cathedral", "magnetic", "loom", "papercity", "clockwork"};
        for (int i = 0; i < int(std::size(ids)); i++)
            if (id == ids[i]) return i;
        return 100 + int(id[0]);
    }
    bool ready() const { return down_.id && blur_.id && post_.id; }  // the mandatory passes compiled
    int find(const std::string &id) const {
        for (size_t i = 0; i < scenes.size(); i++)
            if (lower(scenes[i].id) == lower(id) || lower(scenes[i].name) == lower(id)) return int(i);
        return -1;
    }
    static std::string lower(std::string s) {
        for (auto &c : s) c = char(std::tolower((unsigned char)c));
        return s;
    }

    // False if any render target could not be allocated at this size.
    bool resize(int w, int h) {
        w_ = w, h_ = h;
        bool ok = true;
        for (auto &s : slots_)
            for (auto &t : s.ping) ok &= t.make(w, h, GL_RGBA16F);
        ok &= down_t.make(std::max(1, w / 4), std::max(1, h / 4), GL_RGBA16F);
        ok &= blur_t.make(std::max(1, w / 4), std::max(1, h / 4), GL_RGBA16F);
        ok &= out_.make(w, h, GL_RGBA8);
        if (!specTex_) {
            specTex_ = tex1(BANDS, 1), waveTex_ = tex1(Music::WAVE, 1), histTex_ = tex1(BANDS, Music::HIST);
        }
        return ok;
    }
    int width() const { return w_; }
    int height() const { return h_; }

    // Switch to scene i (-1: external frame from the default framebuffer, i.e. projectM).
    void show(int i, const Variant &v, float fadeSecs, double now) {
        std::swap(slots_[0], slots_[1]);
        Slot &s = slots_[0];
        s.scene = i, s.var = v, s.start = now, s.flow0 = -1, s.fresh = true;
        fade_ = fadeSecs > 0 && slots_[1].scene != -2 ? 0 : 1;
        fadeLen_ = std::max(fadeSecs, 0.01f);
        fadeSeed_ = v.seed;
        for (auto &t : s.ping) t.make(w_, h_, GL_RGBA16F);
    }
    int current() const { return slots_[0].scene; }
    bool fading() const { return fade_ < 1; }
    const Variant &variant() const { return slots_[0].var; }

    void render(const Music &m, float dt, const std::vector<Rect> &panels = {}) {
        glDisable(GL_DEPTH_TEST), glDisable(GL_CULL_FACE), glDisable(GL_SCISSOR_TEST), glDisable(GL_BLEND);
        glBindVertexArray(vao_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        upload(specTex_, BANDS, 1, m.spectrum.data());
        upload(waveTex_, Music::WAVE, 1, m.wave.data());
        upload(histTex_, BANDS, Music::HIST, m.history.data());

        fade_ = std::min(1.f, fade_ + dt / fadeLen_);
        const bool both = fade_ < 1 && slots_[1].scene != -2;
        for (int k = 0; k < (both ? 2 : 1); k++) drawSlot(slots_[k], m, dt);
        if (!both) slots_[1].scene = -2;
        const float mixv = both ? smooth(fade_) : 1;
        const GLuint A = slots_[0].ping[slots_[0].cur].tex, B = slots_[1].ping[slots_[1].cur].tex;
        const float extA = slots_[0].scene == -1, extB = slots_[1].scene == -1;

        // Bloom source: quarter-res blur of the (mixed) scene.
        auto bindMix = [&](Program &p) {
            glUseProgram(p.id);
            bindTex(0, A, p("uA")), bindTex(1, B, p("uB"));
            glUniform1f(p("uMix"), mixv);
            glUniform1f(p("uSeed"), fadeSeed_);
            glUniform1f(p("uExtA"), extA), glUniform1f(p("uExtB"), extB);
            glUniform2f(p("uRes"), float(w_), float(h_));
        };
        down_t.bind();
        bindMix(down_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        for (int pass = 0; pass < 4; pass++) {  // H, V, H, V: wide and smooth
            const bool v = pass & 1;
            (v ? down_t : blur_t).bind();
            glUseProgram(blur_.id);
            bindTex(0, v ? blur_t.tex : down_t.tex, blur_("uSrc"));
            glUniform2f(blur_("uDir"), v ? 0 : 1.5f / down_t.w, v ? 1.5f / down_t.h : 0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        auto bloomOf = [&](const Slot &s) { return s.scene >= 0 && s.scene < (int)scenes.size() ? scenes[s.scene].bloom : 0.15f; };
        pp_.dt = std::clamp(dt, 0.001f, 1.f);
        pp_.mix = mixv, pp_.glow = (bloomOf(slots_[1]) * (1 - mixv) + bloomOf(slots_[0]) * mixv) * std::min(intensity, 1.2f);
        pp_.drop = m.drop * intensity, pp_.time = float(m.time), pp_.extA = extA, pp_.extB = extB, pp_.panels = panels;
        post();
    }

    void post() {
        out_.bind();
        glUseProgram(post_.id);
        bindTex(0, slots_[0].ping[slots_[0].cur].tex, post_("uA")), bindTex(1, slots_[1].ping[slots_[1].cur].tex, post_("uB"));
        bindTex(2, down_t.tex, post_("uBloom"));
        glUniform1f(post_("uMix"), pp_.mix), glUniform1f(post_("uSeed"), fadeSeed_);
        glUniform1f(post_("uExtA"), pp_.extA), glUniform1f(post_("uExtB"), pp_.extB);
        glUniform2f(post_("uRes"), float(w_), float(h_));
        glUniform1f(post_("uGlow"), pp_.glow);
        glUniform1f(post_("uDrop"), pp_.drop);
        glUniform1f(post_("uExposure"), exposure);
        glUniform1f(post_("uTime"), pp_.time);
        float r[32] = {};
        const int n = std::min<int>(pp_.panels.size(), 8);
        for (int i = 0; i < n; i++) {
            const Rect &q = pp_.panels[i];
            r[i * 4] = q.x0, r[i * 4 + 1] = q.y0, r[i * 4 + 2] = q.x1, r[i * 4 + 3] = q.y1;
        }
        glUniform4fv(post_("uRects"), 8, r);
        glUniform1i(post_("uRectN"), n);
        glUniform3fv(post_("uTint"), 1, slots_[0].var.pal);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Top-down RGB of the last render; also runs the flash limiter.
    // Photosensitivity: mean luminance may not rise more than 0.05 per 1/30 s. Too
    // bright -> lower the exposure and redo the (cheap) post pass; it eases back after.
    void read(std::vector<unsigned char> &rgb) {
        rgb.resize(size_t(w_) * h_ * 3);
        float luma = 0;
        for (int tries = 0; tries < 4; tries++) {
            glBindFramebuffer(GL_FRAMEBUFFER, out_.fbo);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w_, h_, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
            luma = lumaOf(rgb);
            const float limit = meanLuma + 0.05f * std::min(1.f, pp_.dt * 30);  // 0.05 per 1/30 s, at any frame rate
            if (luma <= limit || luma < 0.03f || tries == 3) break;
            exposure *= std::clamp(std::pow(limit / luma, 2.6f), 0.2f, 0.95f);
            post();
        }
        const float limit = meanLuma + 0.05f * std::min(1.f, pp_.dt * 30);
        if (luma > limit && luma >= 0.03f) {  // still too bright after the retries: scale the image itself
            const float k = limit / luma;
            for (auto &c : rgb) c = (unsigned char)(c * k);
            luma = lumaOf(rgb);
        }
        exposure += (1 - exposure) * (1 - std::pow(0.95f, pp_.dt * 30));
        meanLuma = luma;
    }
    static float lumaOf(const std::vector<unsigned char> &rgb) {
        double sum = 0;
        size_t cnt = 0;
        for (size_t i = 0; i + 2 < rgb.size(); i += 3, cnt++) sum += 0.2126 * rgb[i] + 0.7152 * rgb[i + 1] + 0.0722 * rgb[i + 2];  // every pixel: no lattice to alias with
        return float(sum / (255.0 * std::max<size_t>(cnt, 1)));
    }

    // Debug: every channel of the current scene's HDR buffer finite and >= 0.
    bool sceneFinite() {
        const Slot &s = slots_[0];
        std::vector<float> f(size_t(w_) * h_ * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, s.ping[s.cur].fbo);
        glReadPixels(0, 0, w_, h_, GL_RGBA, GL_FLOAT, f.data());
        for (size_t i = 0; i < f.size(); i++)
            if (i % 4 != 3 && !(std::isfinite(f[i]) && f[i] >= -1e-3f)) return false;
        return true;
    }

    void bindDefault() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w_, h_);
    }

    // Copy the default framebuffer (projectM's output) into the external slot.
    void captureExternal() {
        for (auto &s : slots_) {
            if (s.scene != -1) continue;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.ping[s.cur].fbo);
            glBlitFramebuffer(0, 0, w_, h_, 0, 0, w_, h_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

private:
    struct Slot {
        int scene = -2;  // -2 none, -1 external
        Variant var;
        Target ping[2];
        int cur = 0;
        double start = 0, flow0 = -1;
        bool fresh = true;
    };
    Slot slots_[2];
    Program down_, blur_, post_;
    Target down_t, blur_t, out_;
    GLuint vao_ = 0, specTex_ = 0, waveTex_ = 0, histTex_ = 0;
    int w_ = 0, h_ = 0;
    float fade_ = 1, fadeLen_ = 1, fadeSeed_ = 0;
    struct {
        float mix = 1, glow = 0, drop = 0, time = 0, extA = 0, extB = 0, dt = 1 / 30.f;
        std::vector<Rect> panels;
    } pp_;  // post-pass inputs, kept so the flash limiter can redo it

    static float smooth(float x) { return x * x * (3 - 2 * x); }

    void drawSlot(Slot &s, const Music &m, float dt) {
        if (s.scene < 0 || s.scene >= (int)scenes.size() || !scenes[s.scene].ok) return;
        SceneDef &d = scenes[s.scene];
        if (s.fresh) s.ping[0].clear(), s.ping[1].clear(), s.fresh = false;
        if (s.flow0 < 0) s.flow0 = m.flow - 20 * s.var.v[3];  // start mid-motion
        Target &dst = s.ping[1 - s.cur];
        dst.bind();
        auto set = [&](Program &p) {
            glUseProgram(p.id);
            glUniform2f(p("uRes"), float(w_), float(h_));
            glUniform1f(p("uTime"), float(m.time - s.start));
            glUniform1f(p("uDt"), std::clamp(dt, 0.001f, 1.f));  // full elapsed time: decays stay frame-rate independent
            glUniform1f(p("uFlow"), float(m.flow - s.flow0));
            glUniform1f(p("uBass"), m.bass), glUniform1f(p("uMid"), m.mid), glUniform1f(p("uHigh"), m.high);
            glUniform1f(p("uEnergy"), m.energy), glUniform1f(p("uEnergySlow"), m.energySlow);
            glUniform1f(p("uBeat"), m.beat), glUniform1f(p("uDownbeat"), m.downbeat), glUniform1f(p("uDrop"), m.drop);
            glUniform1f(p("uBeatPhase"), m.beatPhase), glUniform1f(p("uBarPhase"), m.barPhase);
            glUniform1f(p("uBpm"), m.bpm), glUniform1f(p("uConf"), m.conf);
            glUniform1f(p("uBeats"), float(m.beats % 4096));
            glUniform1f(p("uIntensity"), intensity), glUniform1f(p("uQuality"), quality);
            glUniform1f(p("uSeed"), s.var.seed);
            glUniform4f(p("uVariant"), s.var.v[0], s.var.v[1], s.var.v[2], s.var.v[3]);
            glUniform3fv(p("uPal"), 5, s.var.pal);
            glUniform1f(p("uHistRow"), float(m.histRow));
            glUniform1f(p("uHistPos"), float(m.histPos % 100000));
            bindTex(0, s.ping[s.cur].tex, p("uPrev"));
            bindTex(1, specTex_, p("uSpectrum"));
            bindTex(2, waveTex_, p("uWave"));
            bindTex(3, histTex_, p("uHistory"));
        };
        set(d.prog);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (d.part.id && d.particles > 0) {
            set(d.part);
            const int n = std::max(1, int(d.particles * (0.35f + 0.65f * quality)));
            glUniform1f(d.part("uCount"), float(n));
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, n);
            glDisable(GL_BLEND);
        }
        s.cur = 1 - s.cur;
    }

    static void bindTex(int unit, GLuint tex, GLint loc) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, tex);
        if (loc >= 0) glUniform1i(loc, unit);
    }
    static GLuint tex1(int w, int h) {
        GLuint t;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        return t;
    }
    static void upload(GLuint t, int w, int h, const float *data) {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_FLOAT, data);
    }
    static GLuint shader(GLenum type, const std::string &src, std::string &err) {
        GLuint s = glCreateShader(type);
        const char *p = src.c_str();
        glShaderSource(s, 1, &p, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096] = {};
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            err += log;
            glDeleteShader(s);
            return 0;
        }
        return s;
    }
    static GLuint link(const std::string &vs, const std::string &fs, std::string &err) {
        GLuint v = shader(GL_VERTEX_SHADER, vs, err), f = v ? shader(GL_FRAGMENT_SHADER, fs, err) : 0;
        if (!v || !f) {
            if (v) glDeleteShader(v);
            return 0;
        }
        GLuint p = glCreateProgram();
        glAttachShader(p, v), glAttachShader(p, f);
        glLinkProgram(p);
        glDeleteShader(v), glDeleteShader(f);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096] = {};
            glGetProgramInfoLog(p, sizeof log, nullptr, log);
            err += log;
            glDeleteProgram(p);
            return 0;
        }
        return p;
    }
};

// Shader sources from a directory (hot reload): files there override the embedded
// ones by path, and new scenes/*.frag|*.vert in it are added.
inline std::vector<Src> readSources(const std::string &root, const std::vector<Src> &embedded) {
    namespace fs = std::filesystem;
    auto slurp = [](const std::string &p, std::string &text) {
        std::ifstream in(fs::u8path(p));
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        return text = ss.str(), true;
    };
    std::vector<Src> out;
    for (auto &e : embedded) {
        std::string t;
        out.push_back(slurp(root + "/" + e.path, t) ? Src{e.path, t} : e);
    }
    std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(root + "/scenes"), ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().u8string(), path = "scenes/" + n;
        const bool shader = n.size() > 5 && (n.compare(n.size() - 5, 5, ".frag") == 0 || n.compare(n.size() - 5, 5, ".vert") == 0);
        std::string t;
        if (shader && std::none_of(out.begin(), out.end(), [&](const Src &s) { return s.path == path; }) && slurp(root + "/" + path, t))
            out.push_back({path, t});
    }
    return out;
}

}  // namespace tv
