// Headless render gate: every scene on silence and on a synthetic track with a
// drop, checked for life, reaction, beat accents, finiteness and flash safety.
// --sheets DIR also writes the contact-sheet moments as PPM.
#include "../director.hpp"
#include "../engine.hpp"
#include "../gpu.hpp"
#include "fixtures.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace tv;

static const std::vector<Src> kShaders = {
#include <shaders.inc>  // on the include path only (see viz.cpp)
};

static int failures = 0;
static void check(bool ok, const std::string &what, const std::string &detail) {
    printf("%s  %s  — %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    failures += !ok;
}

static bool writeOutput(const std::string &path, const void *data, size_t bytes, int w = 0, int h = 0) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        fprintf(stderr, "cannot open '%s': %s\n", path.c_str(), strerror(errno));
        return false;
    }
    bool ok = !w || fprintf(f, "P6\n%d %d\n255\n", w, h) > 0;
    if (ok) ok = fwrite(data, 1, bytes, f) == bytes;
    int error = ok ? 0 : errno;
    if (fclose(f) != 0) ok = false, error = errno;
    if (!ok) fprintf(stderr, "cannot write '%s': %s\n", path.c_str(), strerror(error ? error : EIO));
    return ok;
}

static void ppm(const std::string &path, const std::vector<unsigned char> &rgb, int w, int h) {
    failures += !writeOutput(path, rgb.data(), rgb.size(), w, h);
}

// Small shader probes use the embedded production sources, without bloom or tonemapping.
static const std::string &source(const std::string &path) {
    for (const auto &s : kShaders) if (s.path == path) return s.text;
    static const std::string empty;
    check(false, "shader probe source", path);
    return empty;
}

static Program probeProgram(const std::string &vertex, const std::string &fragment) {
    Program p;
    p.id = glCreateProgram();
    for (auto stage : {std::make_pair(GL_VERTEX_SHADER, vertex), std::make_pair(GL_FRAGMENT_SHADER, fragment)}) {
        const GLuint sh = glCreateShader(stage.first);
        const std::string text = "#version 330 core\n" + stage.second;
        const char *data = text.c_str();
        glShaderSource(sh, 1, &data, nullptr);
        glCompileShader(sh);
        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[4096] = {};
            glGetShaderInfoLog(sh, sizeof log, nullptr, log);
            check(false, "shader probe compiles", log);
        }
        glAttachShader(p.id, sh);
        glDeleteShader(sh);
    }
    glLinkProgram(p.id);
    GLint ok = 0;
    glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    check(ok != 0, "shader probe links", "production source");
    return p;
}

static std::vector<float> probePixels(Program &p, Target &t) {
    t.bind();
    glUseProgram(p.id);
    glUniform2f(p("uRes"), float(t.w), float(t.h));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::vector<float> pixels(size_t(t.w) * t.h * 4);
    glReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_FLOAT, pixels.data());
    return pixels;
}

static decltype(glDeleteTextures) deleteTexturesDriver;
static int deletedTextures = 0;
static void TVGL_API countDeletedTextures(GLsizei count, const GLuint *textures) {
    deletedTextures += count;
    deleteTexturesDriver(count, textures);
}

static int regressions(Engine &eng) {
    const double pi = std::acos(-1.0);
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    Target t;
    if (!t.make(128, 64, GL_RGBA16F)) return 1;
    Program mix = probeProgram(source("shaders/quad.vert"), source("shaders/mix.glsl") +
        "\nout vec4 o; void main() { o = vec4(vec3(mixMask(vUv)), 1); }\n");
    bool endpoints = true, monotone = true, progression = true;
    for (float seed : {0.f, 0.31f, 0.99f}) {
        std::vector<float> prev;
        for (float progress : {0.f, 0.001f, 0.25f, 0.5f, 0.77f, 0.9f, 0.99f, 1.f}) {
            glUseProgram(mix.id);
            glUniform1f(mix("uMix"), progress);
            glUniform1f(mix("uSeed"), seed);
            auto pixels = probePixels(mix, t);
            for (size_t i = 0; i < pixels.size(); i += 4) {
                const float v = pixels[i];
                if (progress == 0 || progress == 1) endpoints &= v == progress;
                if (!prev.empty()) monotone &= std::isfinite(v) && v >= prev[i] && v >= 0 && v <= 1;
                if (progress > 0 && progress <= 0.9f) progression &= v > 0 && v < 1;
            }
            prev = std::move(pixels);
        }
    }
    check(endpoints && monotone && progression, "dissolve endpoints and monotone interior", "three noise seeds; progress 0..1 including 0.77 and 0.9");
    glDeleteProgram(mix.id);
    t.free();
    if (!t.make(4096, 1, GL_RGBA16F)) return 1;
    Program sprite = probeProgram(
        "uniform float uHalf; out vec2 vLocal; out float vHalf; out vec4 vColor;\n"
        "void main() { vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "vLocal=vec2((p.x-0.5)*16.0,0); vHalf=uHalf; vColor=vec4(1); gl_Position=vec4(p*2-1,0,1); }\n",
        source("shaders/sprite.frag"));
    auto capsule = [&](float half) {
        glUseProgram(sprite.id);
        glUniform1f(sprite("uHalf"), half);
        return probePixels(sprite, t);
    };
    const auto below = capsule(0.04999f), above = capsule(0.05001f), dot = capsule(0);
    float thresholdDelta = 0, pointDelta = 0;
    double energyError = 0;
    for (size_t i = 0; i < above.size(); i += 4) thresholdDelta = std::max(thresholdDelta, std::abs(above[i] - below[i]));
    for (float half : {0.0005f, 0.0011f, 0.05f, 0.1f, 0.5f, 1.f, 2.f}) {
        const auto pixels = capsule(half);
        double sum = 0;
        for (size_t i = 0; i < pixels.size(); i += 4) sum += pixels[i] * 16 / t.w;
        const double expected = std::max(std::sqrt(pi / 2), 2.0 * half);
        energyError = std::max(energyError, std::abs(sum / expected - 1));
        if (half < 0.002f)
            for (size_t i = 0; i < pixels.size(); i += 4) pointDelta = std::max(pointDelta, std::abs(pixels[i] - dot[i]));
    }
    check(thresholdDelta < 0.001f && pointDelta < 0.002f && energyError < 0.003, "sprite point limit and segment continuity preserve energy",
          "threshold delta " + std::to_string(thresholdDelta) + ", point delta " + std::to_string(pointDelta) + ", energy error " + std::to_string(energyError));
    glDeleteProgram(sprite.id);
    t.free();

    Target orphan;
    glGenTextures(1, &orphan.tex);
    deleteTexturesDriver = glDeleteTextures;
    glDeleteTextures = countDeletedTextures;
    orphan.free();
    glDeleteTextures = deleteTexturesDriver;
    check(deletedTextures == 1 && !orphan.tex && !orphan.fbo, "texture released without framebuffer", "allocation-failure cleanup");
    Music m;
    bool trigger = waveformTrigger(m.wave) == 0.03f;
    std::fill(m.wave.begin(), m.wave.end(), 0.5f);
    trigger &= waveformTrigger(m.wave) == 0.03f;
    for (int i = 0; i < Music::WAVE; i++) m.wave[i] = std::sin((float(i) / (Music::WAVE - 1) - 0.2f) * 4 * pi);
    const float crossing = waveformTrigger(m.wave);
    trigger &= crossing >= 0.2f && crossing <= 0.209f;
    check(trigger, "phosphor trigger preserves crossing and fallback", "silence, DC, and rising sinusoid at 0.2");

    if (!eng.resize(160, 90)) return 1;
    std::mt19937 rng(51);
    auto frame = [&](const char *scene, const Music &music, Variant variant) {
        eng.show(eng.find(scene), variant, 0, 0);
        eng.exposure = 1, eng.meanLuma = 1;
        eng.render(music, 1 / 30.f);
        std::vector<unsigned char> rgb;
        eng.read(rgb);
        return rgb;
    };
    for (float symmetry : {0.25f, 0.75f}) {
        Variant v = makeVariant(eng.scenes[eng.find("quasicrystal")].palettes, "first", nullptr, rng);
        v.v[2] = symmetry;
        m.beats = 4095, m.beatPhase = 1;
        const auto a = frame("quasicrystal", m, v);
        m.beats = 4096, m.beatPhase = 0;
        const auto b = frame("quasicrystal", m, v);
        check(a == b, "quasicrystal continuous at 4096 beats", symmetry < 0.5f ? "five-fold" : "seven-fold");
        m.beats = 1119, m.beatPhase = 1;
        const auto c = frame("quasicrystal", m, v);
        m.beats = 1120, m.beatPhase = 0;
        const auto d = frame("quasicrystal", m, v);
        int maxDiff = 0;
        for (size_t i = 0; i < c.size(); i++) maxDiff = std::max(maxDiff, std::abs(int(c[i]) - d[i]));
        check(maxDiff <= 1, "quasicrystal full-turn phrase wrap", std::to_string(maxDiff) + " maximum channel difference");
    }
    Variant galaxy = makeVariant(eng.scenes[eng.find("galaxy")].palettes, "first", nullptr, rng);
    m = {};
    eng.intensity = 0;
    const auto dim = frame("galaxy", m, galaxy);
    eng.intensity = 1;
    const auto bright = frame("galaxy", m, galaxy);
    check(Engine::lumaOf(bright) > Engine::lumaOf(dim) + 0.001f, "galaxy stars honor intensity", "fixed camera and waveform; particle intensity 0 versus 1");
    Variant rain = makeVariant(eng.scenes[eng.find("rainglass")].palettes, "first", nullptr, rng);
    m = {}, m.silent = false;
    const auto noBeats = frame("rainglass", m, rain);
    m.beats = 123;
    const auto stopped = frame("rainglass", m, rain);
    m.beat = 1;
    const auto phaseZero = frame("rainglass", m, rain);
    check(noBeats == stopped && phaseZero != stopped, "rain beat rings stop without freezing", "inactive phase zero has no beat rings; valid phase zero retains them");
    const auto temp = std::filesystem::temp_directory_path() /
        ("glistening-render-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(temp);
    const std::vector<unsigned char> rgb = {1, 2, 3};
    const auto valid = (temp / "image.ppm").string();
    const bool saved = writeOutput(valid, rgb.data(), rgb.size(), 1, 1);
    bool io = saved && std::filesystem::file_size(valid) == 14;
    io &= !writeOutput((temp / "missing" / "image.ppm").string(), rgb.data(), rgb.size(), 1, 1);
    io &= !writeOutput(temp.string(), rgb.data(), rgb.size(), 1, 1);
    if (std::filesystem::exists("/dev/full")) io &= !writeOutput("/dev/full", rgb.data(), rgb.size(), 1, 1);
    check(io, "PPM output reports open and buffered-close failures", "writable positive control; missing parent, directory, and full device where available");
    std::filesystem::remove_all(temp);
    printf("%s\n", failures ? "VISUAL REGRESSIONS FAILED" : "VISUAL REGRESSIONS PASS");
    return failures != 0;
}

/** Run scene assertion gates or the requested benchmark, transition, and image-export modes. */
int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IOLBF, 4096);  // line-buffered so progress shows before a hang
    int W = 616, H = 314;
    bool benchMode = false;
    std::string sheets, only, palette = "auto";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sheets") && i + 1 < argc) sheets = argv[++i];
        else if (!strcmp(argv[i], "--scene") && i + 1 < argc) only = argv[++i];
        else if (!strcmp(argv[i], "--palette") && i + 1 < argc) palette = argv[++i];
        else if (!strcmp(argv[i], "--bench")) benchMode = true;
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &W, &H);
        else if (!strcmp(argv[i], "--song") && i + 1 < argc) {  // write the fixture track as f32le stereo
            hatPrev = 0;
            Audio a = render(30, song);
            return !writeOutput(argv[++i], a.data(), sizeof(float) * a.size());
        }
    }
    std::string transA, transB;
    for (int i = 1; i + 2 < argc; i++)
        if (!strcmp(argv[i], "--transition")) transA = argv[i + 1], transB = argv[i + 2];
    if (!sheets.empty()) W = 960, H = 540;
    Gpu gpu;
    std::string err = gpu.init(W, H);
    if (!err.empty()) {  // a host without GL 3.3 cannot pass the render gate
        printf("FAIL  render gate: no GL (%s)\n\nRENDER GATE FAILED (no GL)\n", err.c_str());
        return 1;
    }
    // Apple's software renderer is far too slow at full size for CI; scale assertion runs only.
    const int requestedW = W, requestedH = H;
    const bool softwareAssertions = sheets.empty() && transA.empty() && !benchMode &&
        gpu.renderer.find("Apple Software Renderer") != std::string::npos;
    if (softwareAssertions) W = std::max(1, int(W * 0.3)), H = std::max(1, int(H * 0.3));
    printf("render gate on %s: requested %dx%d, effective %dx%d\n", gpu.renderer.c_str(), requestedW, requestedH, W, H);
    Engine eng;
    err = eng.load(kShaders);
    check(err.empty(), "all shaders compile", err.empty() ? "ok" : err);
    {
        static const char *builtIn[] = {"aurora", "mercury", "galaxy", "tunnel", "ridgeline", "ink", "quasicrystal",
                                        "phosphor", "rainglass", "cathedral", "magnetic", "loom", "papercity", "clockwork"};
        bool ok = eng.scenes.size() >= std::size(builtIn);
        for (size_t i = 0; ok && i < std::size(builtIn); i++) ok = eng.scenes[i].id == builtIn[i];
        check(ok, "all 14 built-in scenes load, in display order", std::to_string(eng.scenes.size()) + " scenes");
    }
    if (!eng.resize(W, H)) return printf("FAIL  render targets at %dx%d\n", W, H), 1;
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--regressions")) return regressions(eng);
    if (!sheets.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(sheets), ec);
        if (ec) return fprintf(stderr, "cannot create sheets directory '%s': %s\n", sheets.c_str(), ec.message().c_str()), 1;
    }
    if (!only.empty() && eng.find(only) < 0) {
        printf("FAIL  unknown scene '%s'\n", only.c_str());
        return 1;
    }

    const double secs = SONG_DROP + 7.5, fps = 30, B = 60 / SONG_BPM;
    hatPrev = 0;
    const Audio audio = render(secs, song);
    const int hop = int(SR / fps);
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--bench")) {  // median render ms per scene (GPU-synced), then render+readback
            Listener L;
            for (int k = 0; k < int(SONG_DROP * fps) + 30; k++) L.feed(&audio[size_t(k) * hop * 2], hop, [](const Features &) {}), L.frame(float(1 / fps));
            printf("%-13s %9s %9s\n", "scene", "render", "+readback");
            for (size_t si = 0; si < eng.scenes.size(); si++) {
                std::mt19937 rng(5);
                eng.show(int(si), makeVariant(eng.scenes[si].palettes, "first", nullptr, rng), 0, 0);
                std::vector<double> a, b;
                std::vector<unsigned char> rgb;
                for (int k = 0; k < 40; k++) {
                    auto t0 = std::chrono::steady_clock::now();
                    eng.render(L.music, float(1 / fps));
                    glFinish();
                    auto t1 = std::chrono::steady_clock::now();
                    eng.read(rgb);
                    auto t2 = std::chrono::steady_clock::now();
                    if (k >= 5) a.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count()), b.push_back(std::chrono::duration<double, std::milli>(t2 - t0).count());
                }
                std::sort(a.begin(), a.end()), std::sort(b.begin(), b.end());
                printf("%-13s %7.1fms %7.1fms\n", eng.scenes[si].id.c_str(), a[a.size() / 2], b[b.size() / 2]);
            }
            return failures != 0;
        }
    if (!transA.empty()) {  // frames through a 2.5 s dissolve, for review
        std::mt19937 rng(9);
        Listener L;
        std::vector<unsigned char> rgb;
        const int a = eng.find(transA), b = eng.find(transB);
        if (a < 0 || b < 0) return printf("FAIL  unknown scene '%s'\n", (a < 0 ? transA : transB).c_str()), 1;
        eng.show(a, makeVariant(eng.scenes[a].palettes, "auto", nullptr, rng), 0, 0);
        for (int k = 0; k < int(15 * fps); k++) {
            L.feed(&audio[size_t(k) * hop * 2], hop, [](const Features &) {});
            L.frame(float(1 / fps));
            if (k == int(12 * fps)) eng.show(b, makeVariant(eng.scenes[b].palettes, "auto", nullptr, rng), 2.5f, L.music.time);
            eng.render(L.music, float(1 / fps));
            eng.read(rgb);
            const int into = k - int(12 * fps);
            if (into == 15 || into == 37 || into == 60) ppm((sheets.empty() ? std::string(".") : sheets) + "/fade-" + std::to_string(into) + ".ppm", rgb, W, H);
        }
        return failures != 0;
    }
    printf("\n%-13s %6s %6s %7s %7s %6s %6s %7s %8s\n", "scene", "luma", "sd", "dQuiet", "dLoud", "react", "beat", "flash", "ms/frame");

    for (size_t si = 0; si < eng.scenes.size(); si++) {
        const SceneDef &sd = eng.scenes[si];
        if (!only.empty() && int(si) != eng.find(only)) continue;  // ids, names, any case
        if (softwareAssertions) {
            // Ink advects a feedback texture: coarse grids damp its measured motion.
            // Keep the requested grid for this inexpensive scene and preserve its assertions.
            const int sceneW = sd.id == "ink" ? requestedW : W, sceneH = sd.id == "ink" ? requestedH : H;
            if (!eng.resize(sceneW, sceneH)) return printf("FAIL  %s render targets at %dx%d\n", sd.id.c_str(), sceneW, sceneH), 1;
            printf("%s render targets: %dx%d\n", sd.id.c_str(), sceneW, sceneH);
        }
        std::mt19937 rng(1234 + si);
        eng.show(int(si), makeVariant(sd.palettes, palette == "first" ? sd.palettes[0] : palette, nullptr, rng), 0, 0);
        eng.exposure = 1, eng.meanLuma = 0;
        Listener L;
        std::vector<unsigned char> rgb, prev;
        double dq = 0, dl = 0, ms = 0, lumaLoud = 0, sdLoud = 0, maxRise = 0;
        int nq = 0, nl = 0, nms = 0;
        bool finite = true;
        double bins[8] = {}, binN[8] = {};
        const int frames = int(secs * fps);
        std::vector<std::pair<int, int>> moments = {{int(2.5 * fps), 0}, {int(9 * fps), 1}, {int((SONG_DROP + 0.2) * fps), 2}};
        int beatShot = -1;
        for (int k = 0; k < frames; k++) {
            const double t = (k + 1) / fps;  // the frame shows audio up to the end of its hop
            L.feed(&audio[size_t(k) * hop * 2], hop, [](const Features &) {});
            L.frame(float(1 / fps));
            auto t0 = std::chrono::steady_clock::now();
            eng.render(L.music, float(1 / fps));
            prev.swap(rgb);
            eng.read(rgb);
            const double frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (k % 30 == 15) finite &= eng.sceneFinite();
            double diff = 0, sum = 0, sum2 = 0;
            const size_t n = rgb.size();
            for (size_t i = 0; i < n; i += 5) {
                const double v = rgb[i] / 255.0;
                sum += v, sum2 += v * v;
                if (prev.size() == n) diff += std::abs(rgb[i] - prev[i]) / 255.0;
            }
            const double cnt = double((n + 4) / 5), mean = sum / cnt, sdv = std::sqrt(std::max(0.0, sum2 / cnt - mean * mean));
            diff /= cnt;
            static double lastLuma = 0;
            if (k > 0) maxRise = std::max(maxRise, double(eng.meanLuma) - lastLuma);
            lastLuma = eng.meanLuma;
            if (t > 1 && t < SONG_QUIET) dq += diff, nq++;
            if (t > SONG_DROP + 1.5) {
                dl += diff, nl++, lumaLoud += eng.meanLuma, sdLoud += sdv, ms += frameMs, nms++;
                const double ph = std::fmod(t - SONG_QUIET, B) / B;
                bins[int(ph * 8) % 8] += diff, binN[int(ph * 8) % 8]++;
                if (beatShot < 0 && t > SONG_DROP + 4 && ph > 0.08 && ph < 0.2) beatShot = k, moments.push_back({k, 3});
            }
            if (!sheets.empty())
                for (auto &m : moments)
                    if (m.first == k) ppm(sheets + "/" + sd.id + "-" + std::to_string(m.second) + ".ppm", rgb, W, H);
        }
        dq /= std::max(nq, 1), dl /= std::max(nl, 1), lumaLoud /= std::max(nl, 1), sdLoud /= std::max(nl, 1), ms /= std::max(nms, 1);
        double all = 0, onBeat = (bins[0] / binN[0] + bins[1] / binN[1]) / 2;
        for (int b = 0; b < 8; b++) all += bins[b] / binN[b] / 8;
        const double react = dl / std::max(dq, 1e-4), beat = onBeat / std::max(all, 1e-9);
        if (getenv("VIZ_BINS")) { for (int b = 0; b < 8; b++) printf(" %.4f", bins[b] / binN[b]); printf("\n"); }
        printf("%-13s %6.3f %6.3f %7.4f %7.4f %6.1f %6.2f %7.3f %8.1f\n", sd.id.c_str(), lumaLoud, sdLoud, dq, dl, react, beat, maxRise, ms);
        const std::string id = sd.id + ": ";
        check(lumaLoud > 0.04, id + "not black", "mean luma " + std::to_string(lumaLoud));
        check(sdLoud > 0.035, id + "not flat", "pixel sd " + std::to_string(sdLoud));
        check(finite, id + "HDR buffer finite (no NaN/Inf)", finite ? "ok" : "non-finite values");
        check(dl > 0.004 && react > 1.5, id + "reacts to music", "loud/quiet frame change " + std::to_string(react));
        check(beat > 1.1, id + "beat accents align with beats", "on-beat/mean change " + std::to_string(beat));
        check(maxRise < 0.08, id + "no flashes", "max frame luma rise " + std::to_string(maxRise));
    }
    printf("\n%s (%d failed)\n", failures ? "RENDER GATE FAILED" : "RENDER GATE PASS", failures);
    return failures != 0;
}
