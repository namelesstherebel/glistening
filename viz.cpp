// glistening: a music visualizer for the terminal. Scenes are rendered headless
// with OpenGL (EGL, CGL or WGL), streamed as kitty-graphics frames (or
// truecolor half blocks), and driven by glistening's own analysis and auto-director.
// Everything OS-specific is behind platform/platform.hpp.
#include "audio.hpp"
#include "director.hpp"
#include "engine.hpp"
#include "gpu.hpp"
#include "milkdrop.hpp"
#include "platform/platform.hpp"
#include "term.hpp"
#include "tests/fixtures.hpp"  // the render gate's synthetic track, for --test-signal and --self-test

#include <cerrno>
#include <cmath>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef GLISTENING_VERSION
#define GLISTENING_VERSION "0.1.0"  // packaging may override with -DGLISTENING_VERSION=...
#endif

#if GLISTENING_PROJECTM
#define GLISTENING_HAS_PM true
#else
#define GLISTENING_HAS_PM false
#endif

using namespace tv;
using Clock = std::chrono::steady_clock;

// Built from scenes/ and shaders/ by the build (Makefile, cmake/embed-shaders.cmake).
// Angle brackets: only the include path is searched, so a CMake build never
// picks up a stale in-tree copy left by make.
static const std::vector<Src> kShaders = {
#include <shaders.inc>
};

static bool g_screen = false;
static auto &g_quit = sys::quitFlag;

static std::string env(const char *k, const char *def) {
    const std::string v = sys::env(k);
    return v.empty() ? def : v;
}

static void out(const std::string &s) { sys::write(s); }

static void restore() {
    if (g_screen) out("\x1b_Ga=d,d=A,q=2\x1b\\\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l"), g_screen = false;
    sys::restoreInput();
}

static const char *USAGE =
    "usage: glistening [options]\n"
    "  --scene NAME      start on a scene (see --list-scenes), or 'milkdrop'\n"
    "  --file AUDIO      visualize a file instead of the system output (decoded by ffmpeg)\n"
    "  --mute            with --file: don't play it\n"
    "  --test-signal     visualize a built-in synthetic track (no audio device; ends by itself)\n"
    "  --milkdrop        start in MilkDrop mode (projectM presets)\n"
    "  --no-director     stay on one scene (auto director off)\n"
    "  --list-scenes     list the scenes and exit\n"
    "  --dump OUT.ppm    render headless and write a frame (OUT with %d: every frame)\n"
    "  --seconds S       stop after S seconds (--dump default 5)\n"
    "  --size WxH        --dump: image size (default 960x540)\n"
    "  --self-test       check rendering, analysis and terminal encoding offline, and exit\n"
    "  --check-audio     capture system output for --seconds (default 3) with a quiet test tone, report\n"
    "  --version         print the version and backends, and exit\n"
    "  --help            this help\n"
    "keys: tab panel, ? help, 1-9 scenes, space/→ next, ← previous, a auto, m MilkDrop,\n"
    "      c palette, + - sensitivity, [ ] intensity, h HUD, r new look, q quit\n";

struct Options {
    std::string scene, file, dump;
    bool milkdrop = false, noDirector = false, list = false, help = false, mute = false;
    bool testSignal = false, selfTest = false, checkAudio = false, version = false, secondsGiven = false;
    double seconds = 5;
    int w = 960, h = 540;
};

static bool parse(const std::vector<std::string> &argv, Options &o, std::string &err) {
    const int argc = int(argv.size());
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&](std::string &dst) {
            if (i + 1 >= argc) return err = a + " needs a value", false;
            return dst = argv[++i], true;
        };
        std::string v;
        if (a == "--scene") { if (!val(o.scene)) return false; }
        else if (a == "--file") { if (!val(o.file)) return false; }
        else if (a == "--dump") { if (!val(o.dump)) return false; }
        else if (a == "--seconds") {
            if (!val(v)) return false;
            char *end = nullptr;
            o.seconds = strtod(v.c_str(), &end);
            if (end == v.c_str() || *end || !std::isfinite(o.seconds) || o.seconds <= 0 || o.seconds > 86400)
                return err = "bad --seconds " + v + " (want 0 < S <= 86400)", false;
            o.secondsGiven = true;
        }
        else if (a == "--size") {
            if (!val(v)) return false;
            auto dim = [](const char *p, char **end) {  // overflow-safe: anything out of range fails below
                errno = 0;
                const long x = strtol(p, end, 10);
                return errno || *end == p || x < 16 || x > 8192 ? -1 : int(x);
            };
            char *e1 = nullptr, *e2 = nullptr;
            const int w = dim(v.c_str(), &e1), h = w > 0 && *e1 == 'x' ? dim(e1 + 1, &e2) : -1;
            if (w < 0 || h < 0 || *e2) return err = "bad --size " + v + " (16..8192 each)", false;
            o.w = w, o.h = h;
        } else if (a == "--milkdrop") o.milkdrop = true;
        else if (a == "--no-director") o.noDirector = true;
        else if (a == "--list-scenes") o.list = true;
        else if (a == "--mute") o.mute = true;
        else if (a == "--test-signal") o.testSignal = true;
        else if (a == "--self-test") o.selfTest = true;
        else if (a == "--check-audio") o.checkAudio = true;
        else if (a == "--version" || a == "-V") o.version = true;
        else if (a == "--help" || a == "-h") o.help = true;
        else return err = "unknown option " + a, false;
    }
    if (o.testSignal && !o.file.empty()) return err = "--test-signal and --file are exclusive", false;
    return true;
}

using Term = sys::TermSize;
static Term measure() { return sys::size(); }

// Kitty graphics? VIZ_OUTPUT forces it; otherwise ask the terminal (a=q), with a
// DA1 query behind it so terminals that ignore graphics still answer quickly.
static bool detectKitty() {
    const std::string o = env("VIZ_OUTPUT", "");
    if (o == "kitty") return true;
    if (o == "blocks") return false;
    out("\x1b_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\x1b\\\x1b[c");
    std::string r;
    const auto until = Clock::now() + std::chrono::milliseconds(500);
    while (Clock::now() < until) {
        r += sys::readInput(20);
        const size_t da = r.find("\x1b[?");
        if (da != std::string::npos && r.find('c', da) != std::string::npos) break;
    }
    return r.find("i=31;OK") != std::string::npos;
}

// The audio, as stereo f32 frames at SR in buf: from a command printing f32le
// stereo 44.1 kHz (ffmpeg, parec, VIZ_PCM_CMD), a native system capture
// (macOS, Windows), or the built-in test track.
struct Pcm {
    sys::Process proc;
    std::unique_ptr<sys::Capture> cap;
    bool synthetic = false, live = true;  // test track: paced by the clock, or exactly what is asked (offline)
    double synthLen = SONG_DROP + 8;      // seconds of test track: quiet intro, breakdown, drop
    long synthPos = 0;
    Clock::time_point synthStart = Clock::now();
    bool eof = false;
    std::string capErr;
    std::vector<float> buf = std::vector<float>(2 * SR);  // a second of stereo: any frame interval fits
    size_t bytes = 0;
    bool started() const { return proc.open() || cap || synthetic; }
    // Whole stereo frames read so far (at most `want`; a blocking command or an
    // offline test track delivers exactly that).
    int read(int want) {
        const size_t cap0 = std::min(buf.size() * 4, size_t(want) * 8);
        if (synthetic) {
            long end = std::min(long(synthLen * SR), synthPos + long(cap0 - bytes) / 8);
            if (live) end = std::min(end, long(std::chrono::duration<double>(Clock::now() - synthStart).count() * SR));
            for (; synthPos < end; synthPos++, bytes += 8) buf[bytes / 4] = buf[bytes / 4 + 1] = song(double(synthPos) / SR);
            if (synthPos >= long(synthLen * SR)) eof = true;
        } else if (cap) {
            if (!eof && !(capErr = cap->error()).empty()) eof = true;
            bytes += size_t(cap->read(buf.data() + bytes / 4, int((cap0 - bytes) / 8))) * 8;
        } else {
            char *b = reinterpret_cast<char *>(buf.data());
            for (int k = 0; k < 64 && bytes < cap0; k++) {
                const long n = proc.read(b + bytes, cap0 - bytes);
                if (n == 0) eof = true, proc.wait(100);
                if (n <= 0) break;
                bytes += size_t(n);
            }
        }
        return int(bytes / 8);
    }
    // Has the source ended? (A command that lingers after closing its output is polled again later.)
    bool poll() { return cap || synthetic ? eof : proc.poll(); }
    bool wait(int ms) { return cap || synthetic ? eof : proc.wait(ms); }
    bool failed() const { return cap ? !capErr.empty() : !synthetic && proc.failed(); }
    void consume(int frames) {
        char *b = reinterpret_cast<char *>(buf.data());
        memmove(b, b + frames * 8, bytes - frames * 8);
        bytes -= frames * 8;
    }
};

static unsigned srgb(const float *lin) {
    unsigned c = 0;
    for (int k = 0; k < 3; k++) c = c << 8 | unsigned(std::lround(std::pow(std::clamp(lin[k], 0.f, 1.f), 1 / 2.2f) * 255));
    return c;
}

static bool ppm(const std::string &path, const std::vector<unsigned char> &rgb, int w, int h) {
    FILE *f = sys::open(path, "wb");
    bool ok = f && fprintf(f, "P6\n%d %d\n255\n", w, h) > 0 && fwrite(rgb.data(), 1, rgb.size(), f) == rgb.size();
    if (f) ok &= fclose(f) == 0;
    if (!ok) fprintf(stderr, "glistening: cannot write %s\n", path.c_str());
    return ok;
}

// Backends compiled into this binary, for --version and diagnostics.
static const char *backends() {
#if defined(_WIN32)
    return "WGL, WASAPI loopback + playback, console VT";
#elif defined(__APPLE__)
    return "CGL, Core Audio process tap + AudioQueue playback, termios";
#else
    return "EGL, Pulse/PipeWire via parec and ffmpeg, termios";
#endif
}

/** Verify packaged rendering, audio conversion/analysis, and terminal encoding on synthetic input.
 * Uses real GL and every embedded scene without touching an audio device or terminal.
 * Missing GL or a failed check returns a nonzero status.
 */
static int selfTest() {
    setvbuf(stdout, nullptr, _IOLBF, 4096);
    int failures = 0;
    auto check = [&](bool ok, const std::string &what, const std::string &detail) {
        printf("%s  %s  — %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
        failures += !ok;
    };
    printf("gListening %s self-test (%s; %s)\n", GLISTENING_VERSION, sys::osName(), backends());

    {  // The test track as a 48 kHz 5.1 device would deliver it, through downmix, resampler and ring.
        const int inRate = 48000, ch = 6, chunk = 480;
        const double secs = SONG_DROP + 8;
        Resampler rs(inRate);
        Ring ring(SR / 2);
        std::vector<float> dev(size_t(chunk) * ch), st(size_t(chunk) * 2), conv(size_t(rs.maxOut(chunk)) * 2), audio, pop(2048 * 2);
        hatPrev = 0;
        for (long i = 0; i < long(secs * inRate); i += chunk) {
            for (int j = 0; j < chunk; j++) {
                const float v = song(double(i + j) / inRate);
                for (int c = 0; c < ch; c++) dev[size_t(j) * ch + c] = c < 2 ? v : 0.f;  // front pair only: the check is exact
            }
            downmix(dev.data(), ch, chunk, st.data());
            ring.push(conv.data(), size_t(rs.run(st.data(), chunk, conv.data())));
            for (size_t n; (n = ring.pop(pop.data(), 2048)) > 0;) audio.insert(audio.end(), pop.begin(), pop.begin() + long(n) * 2);
        }
        const long frames = long(audio.size() / 2), expect = long(secs * SR);
        check(std::labs(frames - expect) <= 2 && ring.dropped() == 0, "48 kHz 5.1 device audio converts to stereo 44.1 kHz",
              std::to_string(frames) + " frames for " + std::to_string(expect));
        Listener L;
        const int hop = SR / 30;
        for (long k = 0; (k + 1) * hop <= frames; k++) L.feed(&audio[size_t(k) * hop * 2], hop, [](const Features &) {}), L.frame(1 / 30.f);
        check(std::fabs(L.music.bpm - SONG_BPM) < 1.5f, "analysis locks to the converted track's tempo",
              "bpm " + std::to_string(L.music.bpm) + " (track " + std::to_string(int(SONG_BPM)) + ")");
    }

    {  // Terminal encoders: a kitty frame decodes to its size; half blocks cover every cell.
        std::vector<unsigned char> rgb(4 * 4 * 3, 200);
        const std::string k = kittyDirect(rgb.data(), 4, 4, 1, 2, 2, Z_TEXT);
        const size_t semi = k.find(';'), end = k.find("\x1b\\", semi);
        check(k.compare(0, 6, "\x1b_Ga=T") == 0 && semi != std::string::npos && end - semi - 1 == b64(rgb.data(), rgb.size()).size(),
              "kitty graphics frame", std::to_string(k.size()) + " bytes");
        Screen text(4, 2);
        std::vector<unsigned char> small(4 * 4 * 3, 90);
        const std::string hb = halfBlocks(small.data(), text);
        size_t cells = 0;
        for (size_t p = 0; (p = hb.find("▀", p)) != std::string::npos; p += 3) cells++;
        check(cells == 8, "half-block frame", std::to_string(cells) + " of 8 cells");
    }

    {  // GL: every embedded scene compiles, renders, moves and stays finite on the track's drop.
        int W = 320, H = 180;
        Gpu gpu;
        const std::string gerr = gpu.init(W, H);
        check(gerr.empty(), "OpenGL 3.3 core context", gerr.empty() ? gpu.renderer : gerr);
        if (gerr.empty()) {
            // Match the render gate's software-only size; keep every scene and assertion.
            if (gpu.renderer.find("Apple Software Renderer") != std::string::npos) W = 96, H = 54;
            Engine eng;
            const std::string serr = eng.load(kShaders);
            check(serr.empty() && eng.ready(), "embedded shaders compile", serr.empty() ? "ok" : serr.substr(0, serr.find('\n')));
            check(eng.scenes.size() >= 14, "built-in scenes", std::to_string(eng.scenes.size()));
            check(eng.resize(W, H), "render targets", std::to_string(W) + "x" + std::to_string(H));
            hatPrev = 0;
            const Audio a = render(SONG_DROP + 2.5, song);
            const int hop = SR / 30, from = int(SONG_DROP * SR) / hop;
            for (size_t si = 0; si < eng.scenes.size(); si++) {
                std::mt19937 rng(77 + unsigned(si));
                eng.show(int(si), makeVariant(eng.scenes[si].palettes, "first", nullptr, rng), 0, 0);
                Listener L;
                std::vector<unsigned char> rgb, prev;
                double change = 0;
                for (int k = from - 30; k < from + 60; k++) {
                    L.feed(&a[size_t(k) * hop * 2], hop, [](const Features &) {});
                    L.frame(1 / 30.f);
                    eng.render(L.music, 1 / 30.f);
                    prev.swap(rgb);
                    eng.read(rgb);
                    if (prev.size() == rgb.size())
                        for (size_t i = 0; i < rgb.size(); i += 7) change += std::abs(rgb[i] - prev[i]) / 255.0;
                }
                const bool finite = eng.sceneFinite();
                check(finite && eng.meanLuma > 0.01f && change > 1, "scene " + eng.scenes[si].id + " renders",
                      "luma " + std::to_string(eng.meanLuma) + ", motion " + std::to_string(change) + (finite ? "" : ", NON-FINITE"));
            }
        }
    }
    printf("\n%s (%d failed)\n", failures ? "SELF-TEST FAILED" : "SELF-TEST PASS", failures);
    return failures ? 1 : 0;
}

// Goertzel power of one frequency in a mono signal at SR.
static double tonePower(const std::vector<float> &x, double hz) {
    const double w = 2 * M_PI * hz / SR, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0;
    for (float v : x) {
        const double s0 = v + c * s1 - s2;
        s2 = s1, s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / std::max<size_t>(1, x.size());
}

// --check-audio: does system-output capture deliver what actually plays? A
// quiet 440 Hz tone goes to the default output (native player; on Linux,
// ffmpeg's Pulse output) while the capture runs. Exit 0: tone heard. 3: audio
// arrives but the tone was not in it (output muted or on another device, or
// capture permission denied: macOS then delivers silence). 1: no capture.
static int checkAudio(double secs) {
    Pcm pcm;
    std::string err;
    const std::string cmd = sys::env("VIZ_PCM_CMD");
    sys::Process tone;  // Linux: ffmpeg playing the tone
    bool toned = false;
    std::unique_ptr<sys::Player> player;
#if defined(__linux__)
    if (!cmd.empty()) pcm.proc.startShell(cmd, false);
    else pcm.proc.start({"parec", "--device=@DEFAULT_MONITOR@", "--format=float32le", "--channels=2", "--rate=44100", "--latency-msec=20"}, false);
    if (!pcm.started()) err = cmd.empty() ? "parec not found or failed to start (install pipewire-pulse / libpulse)" : "cannot start VIZ_PCM_CMD";
    char sine[64];
    snprintf(sine, sizeof sine, "sine=frequency=440:duration=%.2f", secs + 0.5);
    if (pcm.started() && !(toned = tone.start({"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", sine, "-af", "volume=0.1", "-f", "pulse", "glistening check"}, false)))
        printf("note: ffmpeg not found, so no test tone is played; only the stream itself is checked\n");
#else
    if (!cmd.empty()) {
        if (!pcm.proc.startShell(cmd, false)) err = "cannot start VIZ_PCM_CMD";
    } else pcm.cap = sys::openSystemCapture(err);
    std::string perr;
    if (pcm.started() && !(player = sys::openPlayer(perr))) printf("note: no playback (%s), so no test tone is played\n", perr.c_str());
#endif
    if (!pcm.started()) return fprintf(stderr, "glistening: system audio capture unavailable: %s\n", err.c_str()), 1;
    if (pcm.cap) printf("capture: %s\n", pcm.cap->describe().c_str());
    if (player) printf("playback: %s\n", player->describe().c_str());
    toned |= bool(player);
    printf("listening for %.1f s%s\n", secs, toned ? " while playing a quiet 440 Hz tone" : "");
    std::vector<float> mono, chunk(2 * SR / 50);
    long toneFrames = 0;
    float peak = 0;
    const auto t0 = Clock::now();
    for (double t = 0; t < secs && !g_quit; t = std::chrono::duration<double>(Clock::now() - t0).count()) {
        while (player && toneFrames < long((t + 0.15) * SR)) {  // keep ~150 ms queued
            for (size_t i = 0; i < chunk.size() / 2; i++)
                chunk[2 * i] = chunk[2 * i + 1] = 0.1f * float(std::sin(2 * M_PI * 440 * double(toneFrames + long(i)) / SR));
            player->write(chunk.data(), int(chunk.size() / 2)), toneFrames += long(chunk.size() / 2);
        }
        const int got = pcm.read(SR);
        for (int i = 0; i < got; i++) {
            const float m = 0.5f * (pcm.buf[2 * i] + pcm.buf[2 * i + 1]);
            mono.push_back(m), peak = std::max(peak, std::fabs(m));
        }
        pcm.consume(got);
        if (pcm.eof) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    player.reset();
    tone.stop();
    const long expect = long(secs * SR);
    const double on = tonePower(mono, 440), off = 0.5 * (tonePower(mono, 330) + tonePower(mono, 587)) + 1e-12;
    printf("received %ld of ~%ld frames, peak %.4f, 440 Hz to neighbours %.1fx\n", long(mono.size()), expect, peak, on / off);
    if (pcm.failed() || mono.size() < size_t(expect / 2)) {
        fprintf(stderr, "glistening: capture is not delivering audio%s%s\n", pcm.capErr.empty() ? "" : ": ", pcm.capErr.c_str());
        return 1;
    }
    if (toned && on / off > 10) return printf("CHECK-AUDIO PASS: the test tone was captured\n"), 0;
    if (!toned && peak > 0) return printf("CHECK-AUDIO PASS: non-silent audio captured (no test tone to match)\n"), 0;
    printf("CHECK-AUDIO INCONCLUSIVE: audio arrives but %s\n",
           peak == 0 ? "is all silence (nothing heard: output muted, or capture permission denied)" : "the test tone was not in it");
    return 3;
}

int main(int argc, char **argv) {
    Options o;
    std::string err;
    if (!parse(sys::args(argc, argv), o, err)) {
        fprintf(stderr, "glistening: %s\n%s", err.c_str(), USAGE);
        return 2;
    }
    if (o.help) return printf("%s", USAGE), 0;
    if (o.version) return printf("gListening %s (%s; %s)\n", GLISTENING_VERSION, sys::osName(), backends()), 0;
    if (o.selfTest) return selfTest();
    if (o.checkAudio) {
        sys::quitHandlers();
        return checkAudio(o.secondsGiven ? o.seconds : 3);
    }
    if (o.list) {
        std::vector<const Src *> sc;
        const std::string dir = sys::env("VIZ_SHADERS");  // the same sources the engine would load
        const std::vector<Src> src = !dir.empty() ? readSources(dir, kShaders) : kShaders;
        Gpu gpu;
        if (!dir.empty() && gpu.init(64, 64).empty()) {  // overrides may not compile: number exactly what the engine will offer
            Engine e;
            const std::string errs = e.load(src);
            if (!errs.empty()) {
                fprintf(stderr, "glistening: shader errors (scenes that never compiled are not listed):\n");
                std::istringstream lines(errs);
                for (std::string l; std::getline(lines, l);) fprintf(stderr, "  %s\n", safeText(l).c_str());
            }
            for (size_t i = 0; i < e.scenes.size(); i++)
                printf("%zu  %-13s calm..busy %.2f  %s\n", i + 1, safeText(e.scenes[i].name).c_str(), e.scenes[i].busy, safeText(e.scenes[i].desc).c_str());
            printf("0  %-13s %s\n", "MilkDrop", GLISTENING_HAS_PM ? "projectM presets (--milkdrop)" : "not built (needs projectM 3.x)");
            return 0;
        }
        for (auto &s : src)
            if (s.path.compare(0, 7, "scenes/") == 0 && s.path.size() > 5 && s.path.compare(s.path.size() - 5, 5, ".frag") == 0) sc.push_back(&s);
        std::sort(sc.begin(), sc.end(), [](auto a, auto b) { return Engine::before(a->path.substr(7), b->path.substr(7)); });
        for (size_t i = 0; i < sc.size(); i++)
            printf("%zu  %-13s calm..busy %.2f  %s\n", i + 1, safeText(meta(sc[i]->text, "name")).c_str(), atof(meta(sc[i]->text, "busy").c_str()),
                   safeText(meta(sc[i]->text, "desc")).c_str());
        printf("0  %-13s %s\n", "MilkDrop", GLISTENING_HAS_PM ? "projectM presets (--milkdrop)" : "not built (needs projectM 3.x)");
        return 0;
    }

    const std::string cfgDir = sys::configDir(), cfgPath = cfgDir + "/config";
    Settings s;
    if (!s.load(cfgPath))  // first run under this name: keep the settings saved as termviz
        s.load(cfgDir.substr(0, cfgDir.find_last_of("/\\") + 1) + "termviz/config");
    if (!o.scene.empty()) s.scene = o.scene, s.milkdrop = Engine::lower(o.scene) == "milkdrop";
    if (o.milkdrop) s.milkdrop = true;
    if (o.noDirector) s.director = false;
    const bool dump = !o.dump.empty();
    const int fps = std::clamp(atoi(env("VIZ_FPS", "30").c_str()), 5, 120);

    if (!dump && !sys::isTerminal()) {
        fprintf(stderr, "glistening: needs a terminal (stdin and stdout); use --dump for headless rendering\n");
        return 1;
    }
    if (!dump && env("TERM", "") == "dumb") {
        fprintf(stderr, "glistening: TERM=dumb: this terminal can't show graphics or colour; use --dump for headless rendering\n");
        return 1;
    }

    // Audio source: a command (argv, or a shell line), the native system capture, or the test track.
    std::vector<std::string> pcmArgv;
    std::string pcmCmd = sys::env("VIZ_PCM_CMD");
    bool nativeCapture = false, nativePlayback = false;
    const bool finite = !o.file.empty() || o.testSignal;  // the source ends by itself: so does glistening
    const bool offline = dump && finite;                  // exactly one frame's audio per frame
    if (!o.file.empty()) {
        if (sys::findProgram("ffmpeg").empty()) {
            fprintf(stderr, "glistening: --file needs ffmpeg on PATH\n");
            return 1;
        }
        if (!sys::readable(o.file)) {
            fprintf(stderr, "glistening: cannot read %s\n", o.file.c_str());
            return 1;
        }
        // "file:" keeps a name that looks like a protocol or option a local file.
        pcmArgv = {"ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error"};
        if (!dump) pcmArgv.push_back("-re");
        pcmArgv.push_back("-i"), pcmArgv.push_back("file:" + o.file);
        for (const char *a : {"-map", "0:a:0", "-f", "f32le", "-ac", "2", "-ar", "44100", "pipe:1"}) pcmArgv.push_back(a);
#if defined(__linux__)
        if (!dump && !o.mute)
            for (const char *a : {"-map", "0:a:0", "-f", "pulse", "glistening"}) pcmArgv.push_back(a);
#else
        nativePlayback = !dump && !o.mute;
#endif
        pcmCmd.clear();
    } else if (o.testSignal) {
        pcmCmd.clear();
    } else if (pcmCmd.empty()) {
#if defined(__linux__)
        if (sys::findProgram("parec").empty()) {
            fprintf(stderr, "glistening: no audio source: parec not found (PipeWire: install pipewire-pulse / libpulse),\n"
                            "         or use --file AUDIO, or set VIZ_PCM_CMD to a command printing f32le stereo 44.1k\n");
            return 1;
        }
        pcmArgv = {"parec", "--device=@DEFAULT_MONITOR@", "--format=float32le", "--channels=2", "--rate=44100", "--latency-msec=20"};
#else
        nativeCapture = true;
#endif
    }
    std::string pcmWhat = pcmCmd;
    for (auto &a : pcmArgv) pcmWhat += (pcmWhat.empty() ? "" : " ") + a;
    if (nativeCapture) pcmWhat = "system audio capture";
    if (o.testSignal) pcmWhat = "test signal";

    // Terminal: raw input first (the graphics query needs it), then the output mode.
    bool kitty = false;
    if (dump) sys::quitHandlers();  // headless: quit through the main loop so the audio command is stopped
    if (!dump) {
        err = sys::enterRaw();
        if (!err.empty()) {
            sys::restoreInput();
            fprintf(stderr, "glistening: %s\n", err.c_str());
            return 1;
        }
        atexit(restore);
        kitty = detectKitty();
    }
    // Shared memory needs the terminal on this machine and POSIX shm (not Windows).
    bool shm = kitty && sys::shmSupported() && env("VIZ_MEDIUM", getenv("SSH_CONNECTION") ? "direct" : "shm") == "shm";
    if (getenv("VIZ_CELLBG")) s.cellbg = atoi(getenv("VIZ_CELLBG")) != 0;

    Term term = measure();
    auto renderSize = [&](int &w, int &h) {
        if (dump) w = o.w, h = o.h;
        else if (!kitty) w = term.cols * 2, h = term.rows * 4;  // half blocks, 2x supersampled
        else {
            const int pw = term.w ? term.w : term.cols * 10, ph = term.h ? term.h : term.rows * 20;
            w = std::max(64, pw / s.scale), h = std::max(36, ph / s.scale);
        }
    };
    int W, H;
    renderSize(W, H);
    int liveScale = s.scale;  // the quality the render targets were last allocated for

    Gpu gpu;
    err = gpu.init(W, H);
    if (!err.empty()) {
        restore();
        fprintf(stderr, "glistening: no OpenGL 3.3: %s\n", err.c_str());
        return 1;
    }
    Engine eng;
    const std::string shaderDir = sys::env("VIZ_SHADERS");  // hot reload source (R key)
    err = eng.load(!shaderDir.empty() ? readSources(shaderDir, kShaders) : kShaders);
    if (!eng.ready() || eng.scenes.empty()) {
        restore();
        fprintf(stderr, "glistening: shaders failed to compile:\n%s", err.c_str());
        return 1;
    }
    auto applyQuality = [&] { eng.quality = s.scale <= 2 ? 1.f : s.scale == 3 ? 0.7f : 0.5f; };
    applyQuality();
    eng.intensity = s.intensity;
    if (!eng.resize(W, H)) {
        restore();
        fprintf(stderr, "glistening: the GPU could not allocate %dx%d render targets (try a smaller --size)\n", W, H);
        return 1;
    }

    int startScene = eng.find(s.scene);
    if (startScene < 0 && !s.milkdrop) {
        if (!o.scene.empty()) {
            restore();
            fprintf(stderr, "glistening: unknown scene '%s' (try --list-scenes)\n", o.scene.c_str());
            return 2;
        }
        startScene = 0;
    }

    Pcm pcm;
    std::unique_ptr<sys::Player> player;  // file playback where ffmpeg has no native output (macOS, Windows)
    if (o.testSignal) pcm.synthetic = true, pcm.live = !offline;
    else if (nativeCapture) pcm.cap = sys::openSystemCapture(err);
    else if (!pcmArgv.empty()) pcm.proc.start(pcmArgv, offline);
    else pcm.proc.startShell(pcmCmd, offline);
    if (!pcm.started()) {
        restore();
        if (nativeCapture)
            fprintf(stderr, "glistening: cannot capture system audio: %s\n"
                            "         use --file AUDIO, or set VIZ_PCM_CMD to a command printing f32le stereo 44.1k\n", err.c_str());
        else fprintf(stderr, "glistening: cannot start audio source: %s\n", pcmWhat.c_str());
        return 1;
    }
    if (nativePlayback && !(player = sys::openPlayer(err))) {
        restore();
        fprintf(stderr, "glistening: cannot play %s: %s (use --mute to only visualize it)\n", o.file.c_str(), err.c_str());
        return 1;
    }

    // Palettes and the scene director.
    Palette desktop;
    const bool haveDesktop = loadOmarchy(desktop);
    std::mt19937 rng(getenv("VIZ_SEED") ? unsigned(atoi(getenv("VIZ_SEED"))) : std::random_device{}());  // VIZ_SEED: reproducible looks
    ScenePicker picker;
    for (auto &d : eng.scenes) picker.busy.push_back(d.busy);
    auto dcfg = [&] {
        DirectorConfig c;
        c.phraseBeats = s.phrase;
        return c;
    };
    Director dir(dcfg());
    dir.locked = !s.director;
    Listener L;
    Cut pendingCut;
    MilkDrop md;
    std::string mdErr;
    bool mdTried = false;
    std::vector<int> sceneHistory;

    std::string toast;
    auto toastUntil = Clock::now();
    auto say = [&](const std::string &m, double secs = 2.5) {
        toast = m;
        toastUntil = Clock::now() + std::chrono::milliseconds(int(secs * 1000));
    };
    auto fadeSecs = [&] { return L.music.bpm > 0 ? std::clamp(4 * 60 / L.music.bpm, 1.5f, 4.f) : 2.5f; };
    auto variantFor = [&](int i) {
        return makeVariant(i >= 0 ? eng.scenes[i].palettes : std::vector<std::string>{"silver"}, s.palette, haveDesktop ? &desktop : nullptr, rng);
    };
    auto showScene = [&](int i, float fade, const std::string &why, bool remember = true) {
        if (i < 0 || i >= (int)eng.scenes.size()) return;
        eng.show(i, variantFor(i), fade, L.music.time);
        picker.played(i);
        if (remember && (sceneHistory.empty() || sceneHistory.back() != i)) sceneHistory.push_back(i);  // re-picking the same scene adds nothing
        if (sceneHistory.size() > 64) sceneHistory.erase(sceneHistory.begin());
        dir.cut(L.feat.t);
        s.scene = eng.scenes[i].id, s.milkdrop = false;
        say(eng.scenes[i].name + (why.empty() ? "" : "  · " + why));
    };
    auto mood = [&](bool drop) { return drop ? 0.95f : std::clamp((L.music.energySlow - 0.5f) / 0.5f, 0.f, 1.f); };
    auto presetAt = Clock::now();  // projectM blends presets itself (3 s); don't learn during it
    auto showPreset = [&](int i, bool hard, const std::string &why, bool remember = true) {
        if (i < 0) {  // every preset banned or broken: back to the scenes
            const int back = std::max(0, eng.find(s.scene));
            showScene(back, 1.5f, "", sceneHistory.empty() || sceneHistory.back() != back);
            return say("no usable MilkDrop presets (all banned or broken)", 4);
        }
        md.select(i, hard, remember);
        presetAt = Clock::now();
        dir.cut(L.feat.t);
        say(md.name(i) + (why.empty() ? "" : "  · " + why));
    };
    auto enterMilkDrop = [&]() {
        if (!mdTried) {
            mdTried = true;
            if (!gpu.pbuffer) mdErr = "needs an EGL pbuffer display (none here)";
            else {
                mdErr = md.init(eng.width(), eng.height(), fps, cfgDir + "/presets.tsv");
            }
        }
        if (!md.ready()) return say("MilkDrop unavailable: " + mdErr, 4), false;
        eng.show(-1, variantFor(-1), dump ? 0 : 1.5f, L.music.time);
        s.milkdrop = true;
        showPreset(md.lib->pick(mood(false), rng), true, "MilkDrop");
        if (!s.milkdrop) mdErr = "no usable presets (all banned or broken)";
        return s.milkdrop;
    };
    if (s.milkdrop && !enterMilkDrop()) {
        if (o.milkdrop || Engine::lower(o.scene) == "milkdrop") {  // asked for on the command line: an error
            restore();
            fprintf(stderr, "glistening: MilkDrop unavailable: %s\n", mdErr.c_str());
            return 1;
        }
        s.milkdrop = false, startScene = std::max(startScene, 0);  // only a saved preference: fall back
    }
    if (!s.milkdrop) showScene(startScene, 0, "");

    Ui ui(s);
    if (dump && getenv("VIZ_DUMP_UI")) ui.panel = std::string(getenv("VIZ_DUMP_UI")).find("panel") != std::string::npos,
                                        ui.help = std::string(getenv("VIZ_DUMP_UI")).find("help") != std::string::npos;
    bool dirty = false, needResize = false;
    auto lastSave = Clock::now();

    auto applyAction = [&](const Action &a) {
        const int curScene = eng.current();
        switch (a.a) {
            case Act::None: break;
            case Act::Quit: g_quit = 1; break;
            case Act::Scene: showScene(a.arg, 1.2f, ""); break;
            case Act::Next:
                if (s.milkdrop) showPreset(md.lib->pick(mood(false), rng), false, "");
                else showScene(picker.pick(sceneEnergy(L.music.energySlow, false), rng), 1.2f, "next");
                break;
            case Act::Prev:
                if (s.milkdrop) {  // banned or broken presets stay gone
                    if (!md.history.empty()) md.history.pop_back();
                    while (!md.history.empty() && md.lib->p[md.history.back()].flag < 0) md.history.pop_back();
                    if (!md.history.empty()) showPreset(md.history.back(), true, "previous", false);
                    else md.history.push_back(md.cur);
                }
                else if (!s.milkdrop && sceneHistory.size() > 1) sceneHistory.pop_back(), showScene(sceneHistory.back(), 1.2f, "previous", false);
                break;
            case Act::Director: dir.locked = !s.director, say(s.director ? "auto director on" : "auto director off: staying here"); break;
            case Act::MilkDrop:
                if (s.milkdrop) {
                    const int back = std::max(0, eng.find(s.scene));
                    showScene(back, 1.5f, "", sceneHistory.empty() || sceneHistory.back() != back);
                }
                else enterMilkDrop();
                break;
            case Act::Palette:
                if (curScene >= 0) eng.show(curScene, variantFor(curScene), 1.0f, L.music.time);
                say("palette · " + s.palette + (s.palette == "desktop" && !haveDesktop ? " (no Omarchy theme found)" : ""));
                break;
            case Act::Gain: say("sensitivity " + std::to_string(int(std::lround(s.gain * 100))) + "%"); break;
            case Act::Intensity: eng.intensity = s.intensity, say("intensity " + std::to_string(int(std::lround(s.intensity * 100))) + "%"); break;
            case Act::Quality:
                applyQuality(), needResize = true;
                say(std::string("quality · ") + (s.scale == 1 ? "full resolution" : "1/" + std::to_string(s.scale) + " resolution"));
                break;
            case Act::Phrase: dir = Director(dcfg()), dir.locked = !s.director, say("phrase · " + std::to_string(s.phrase) + " beats"); break;
            case Act::Hud: break;
            case Act::Reroll:
                if (s.milkdrop) showPreset(md.lib->random(rng), true, "random");
                else if (curScene >= 0) eng.show(curScene, variantFor(curScene), 1.5f, L.music.time), say(eng.scenes[curScene].name + "  · new look");
                break;
            case Act::Favourite:
                if (!s.milkdrop) say("favourites are for MilkDrop presets");
                else md.lib->p[md.cur].flag = md.lib->p[md.cur].flag == 1 ? 0 : 1, md.save(), say(md.lib->p[md.cur].flag == 1 ? "★ favourite" : "unfavourited");
                break;
            case Act::Ban:
                if (!s.milkdrop) say("bans are for MilkDrop presets");
                else md.lib->p[md.cur].flag = -1, md.save(), showPreset(md.lib->pick(mood(false), rng), true, "banned last");
                break;
            case Act::Reload: {
                std::string root = !shaderDir.empty() ? shaderDir : ".";
                std::string e = eng.load(readSources(root, kShaders));
                picker.remap(eng.remap);
                picker.busy.clear();
                for (auto &d : eng.scenes) picker.busy.push_back(d.busy);
                std::vector<int> h;
                for (int i : sceneHistory)
                    if (i >= 0 && i < (int)eng.remap.size() && eng.remap[i] >= 0) h.push_back(eng.remap[i]);
                sceneHistory = h;
                if (!s.milkdrop && eng.current() < 0) {  // the scene on screen was removed
                    while (!sceneHistory.empty() && sceneHistory.back() >= (int)eng.scenes.size()) sceneHistory.pop_back();
                    showScene(sceneHistory.empty() ? 0 : sceneHistory.back(), 0, "", false);
                }
                say(e.empty() ? "shaders reloaded from " + root : "shader error: " + e.substr(0, e.find('\n')), e.empty() ? 2.5 : 8);
                break;
            }
            case Act::Info:
                if (s.milkdrop) say(md.name(md.cur), 4);
                else if (curScene >= 0) say(eng.scenes[curScene].name + " — " + eng.scenes[curScene].desc + "  · " + eng.variant().palette, 5);
                break;
        }
        if (a.a != Act::None && a.a != Act::Info && a.a != Act::Reload) dirty = true;
    };

    if (!dump) {
        sys::runHandlers();
        const Term now = measure();  // the window may have changed during startup
        if (now.cols != term.cols || now.rows != term.rows || now.w != term.w || now.h != term.h) needResize = true;
        out("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
        g_screen = true;
    }

    std::vector<unsigned char> px, small, prevFrame;
    std::vector<std::pair<std::string, Clock::time_point>> shmNames;  // sent, not yet seen unlinked by the terminal
    int shmStale = 0;
    Screen prevText(0, 0);
    unsigned frame = 0;
    int curId = 1;
    const auto start = Clock::now();
    auto last = start;
    double eofAt = -1;
    bool eofChecked = false;
    auto sourceFailed = [&] {
        if (pcm.cap) fprintf(stderr, "glistening: system audio capture failed: %s\n", pcm.capErr.c_str());
        else if (!o.file.empty()) fprintf(stderr, "glistening: ffmpeg could not decode %s (exit status %d)\n", o.file.c_str(), pcm.proc.status());
        else fprintf(stderr, "glistening: audio source failed (exit status %d): %s\n", pcm.proc.status(), pcmWhat.c_str());
    };
    const int dumpFrames = std::max(1, int(o.seconds * fps));
    const bool sequence = frameName(o.dump, 0) != o.dump;
    // Offline: frame k covers samples [k*SR/fps, (k+1)*SR/fps), so rates that
    // don't divide 44.1 kHz (120 fps) alternate hop sizes instead of drifting.
    auto hopAt = [&](unsigned k) { return int((uint64_t(k) + 1) * SR / fps - uint64_t(k) * SR / fps); };
    Screen lastText(0, 0);  // dump mode: the previous frame's text layer
    // One dumped image: px plus, for screenshots (tools/screenshot.py), the text layer.
    auto writeFrame = [&](const std::string &name, const Screen &text) {
        if (!ppm(name, px, W, H)) return false;
        if (!getenv("VIZ_DUMP_UI")) return true;
        FILE *f = sys::open(name + ".cells", "w");
        bool ok = f && fprintf(f, "%d %d\n", text.cols, text.rows) > 0;
        for (int y = 0; ok && y < text.rows; y++)
            for (int x = 0; ok && x < text.cols; x++) {
                const Cell &c = text.at(x, y);
                if (!c.blank()) ok = fprintf(f, "%d %d %u %u %d %s\n", x, y, c.fg, c.bg, c.attr, c.ch.c_str()) > 0;
            }
        if (f) ok &= fclose(f) == 0;
        if (!ok) fprintf(stderr, "glistening: cannot write %s.cells\n", name.c_str());
        return ok;
    };

    while (!g_quit) {
        const auto t0 = Clock::now();
        const float dt = dump ? 1.f / fps : std::clamp(std::chrono::duration<float>(t0 - last).count(), 0.001f, 1.f);  // full elapsed time (low fps, stalls)
        last = t0;

        if (!dump) {
            static std::string partial;  // escape sequence split across reads (e.g. over ssh)
            static Clock::time_point partialAt;
            const std::string in = partial + sys::readInput(0);
            std::vector<std::string> keys;
            if (in == "\x1b" && !partial.empty() && t0 - partialAt > std::chrono::milliseconds(60))
                keys = {"esc"}, partial.clear();  // nothing followed: a real Esc
            else {
                const std::string before = partial;
                keys = splitKeys(in, &partial);
                if (!partial.empty() && partial != before) partialAt = t0;
            }
            for (auto &k : keys)
                for (auto &a : ui.key(k, int(eng.scenes.size()), md.ready() || (!mdTried && gpu.pbuffer && GLISTENING_HAS_PM)))
                    applyAction(a);
        }
        if ((sys::resized() || needResize) && !dump) {
            needResize = false;
            const Term oldTerm = term;
            const int oldW = W, oldH = H;
            term = measure();
            renderSize(W, H);
            bool ok = gpu.resize(W, H);  // the old surface stays current if the new one is refused
            bool back = true;
            if (ok && !(ok = eng.resize(W, H))) back = gpu.resize(oldW, oldH);
            if (!ok) {  // keep everything consistent with the old size
                term = oldTerm, W = oldW, H = oldH, say("resize: the GPU refused that size; keeping the old one", 4);
                if (s.scale != liveScale) s.scale = liveScale, applyQuality(), dirty = true;  // a rejected quality change is undone
                if (!back || !eng.resize(W, H)) {  // not even the old size fits any more: stop cleanly
                    for (auto &n : shmNames) sys::shmUnlink(n.first);
                    restore();
                    fprintf(stderr, "glistening: the GPU could not reallocate render targets after a resize\n");
                    return 1;
                }
            }
            liveScale = s.scale;
            if (md.ready()) md.resize(W, H);
            out(std::string("\x1b_Ga=d,d=A,q=2\x1b\\\x1b[0m\x1b[2J"));
            prevText = Screen(0, 0);
        }

        // Audio: everything available (live), or exactly one frame's worth (offline file).
        const int got = pcm.read(offline ? hopAt(frame) : 1 << 20);
        if (sys::quitSignal) break;  // interrupted (possibly mid-read): don't render a partial frame
        if (got > 0) {
            if (player) player->write(pcm.buf.data(), got);
            L.feed(pcm.buf.data(), got, [&](const Features &f) {
                if (Cut c = dir.update(f)) pendingCut = c;
            });
            if (md.ready() && s.milkdrop) {
                static Agc agc;
                std::vector<float> tmp(pcm.buf.begin(), pcm.buf.begin() + got * 2);
                agc.apply(tmp.data(), got);
                md.pcm(tmp.data(), got);
            }
            pcm.consume(got);
        } else if (pcm.eof && !offline) {  // the source is gone: feed silence so everything decays
            static std::vector<float> quiet;
            const int n = hopAt(frame);
            quiet.assign(size_t(n) * 2, 0.f);
            L.feed(quiet.data(), n, [&](const Features &f) {
                if (Cut c = dir.update(f)) pendingCut = c;
            });
        }
        if (pcm.eof && eofAt < 0) eofAt = L.music.time, say(!finite ? "audio source ended" : o.testSignal ? "end of test signal" : "end of file", 4);
        if (pcm.eof && !eofChecked && pcm.poll()) {  // its exit status, as soon as it is known
            eofChecked = true;
            pcm.proc.stop();  // whatever the command left behind goes now, while its group id is still its own
            if (pcm.failed() && (finite || dump)) return restore(), sourceFailed(), 1;  // nobody can see a toast
            if (pcm.failed()) say(pcm.cap ? "system audio capture stopped: " + pcm.capErr : "audio source failed (is PipeWire/Pulse running?)", 4);
        }
        if (finite && eofAt >= 0 && L.music.time - eofAt > 1.5) g_quit = 1;
        if (!dump && o.secondsGiven && t0 - start > std::chrono::duration<double>(o.seconds)) g_quit = 1;
        // A file that ends exactly on a frame boundary: the frame already
        // rendered (still in px) was the last one with audio. Don't add a silent one.
        if (offline && pcm.eof && got == 0 && frame > 0) {
            if (!sequence && !writeFrame(o.dump, lastText)) return 1;
            break;
        }

        if (pendingCut) {
            const bool drop = pendingCut.why == Why::Drop;
            if (s.milkdrop) showPreset(md.lib->pick(mood(drop), rng), pendingCut.hard, whyName(pendingCut.why));
            else showScene(picker.pick(sceneEnergy(L.music.energySlow, drop), rng), pendingCut.hard ? 0 : fadeSecs(), whyName(pendingCut.why));
            pendingCut = {};
        }
        L.frame(dt, s.gain);

        if (s.milkdrop && md.ready()) {
            eng.bindDefault();  // projectM draws into whatever is bound; it must be the pbuffer
            md.render();
            if (md.checkFailed()) showPreset(md.lib->pick(mood(false), rng), true, "skipped broken preset");
            eng.captureExternal();
        }

        // UI layout first: its boxes get a frosted backdrop in the image.
        Screen text = dump ? Screen(std::max(20, o.w / 10), std::max(8, o.h / 20)) : Screen(term.cols, term.rows);
        UiState st;
        for (auto &d : eng.scenes) st.scenes.push_back(d.name);
        st.current = s.milkdrop ? -1 : eng.current();
        st.milkdropAvailable = md.ready() || (!mdTried && gpu.pbuffer && GLISTENING_HAS_PM);
        st.preset = s.milkdrop ? md.name(md.cur) : "";
        st.palette = eng.variant().palette;
        const Music &m = L.music;
        st.bpm = m.bpm, st.conf = m.conf, st.bass = m.bass, st.mid = m.mid, st.high = m.high, st.energy = m.energy;
        for (int b = 0; b < 8; b++) {
            float v = 0;
            for (int k = 0; k < BANDS / 8; k++) v = std::max(v, m.spectrum[b * BANDS / 8 + k]);
            st.spectrum[b] = v * v;
        }
        st.barPhase = m.barPhase, st.silent = m.silent, st.sure = m.conf >= dir.config().minConf, st.beats = dir.beats();
        if (t0 < toastUntil) st.toast = toast;
        unsigned acc = srgb(eng.variant().pal + 12);
        st.accent = luma(acc) < 150 ? mixc(acc, 0xffffff, 0.45f) : acc;
        ui.focus = st.current < 0 ? int(st.scenes.size()) : st.current;
        if (dump) ui.sel = ui.focus;
        const auto boxes = ui.draw(text, st);
        std::vector<Rect> rects;
        for (auto &b : boxes)
            rects.push_back({b.x / float(text.cols), b.y / float(text.rows), (b.x + b.w) / float(text.cols), (b.y + b.h) / float(text.rows)});
        if (s.cellbg && kitty)
            for (auto &b : boxes)
                for (int y = b.y; y < b.y + b.h && y < text.rows; y++)
                    for (int x = b.x; x < b.x + b.w && x < text.cols; x++)
                        if (text.at(x, y).bg == DEF) text.at(x, y).bg = 0x111216;

        eng.render(m, dt, rects);
        eng.read(px);

        // MilkDrop learns how busy each preset looks (mean frame-to-frame change).
        if (s.milkdrop && md.ready() && prevFrame.size() == px.size() && !m.silent && !eng.fading() &&
            t0 - presetAt > std::chrono::seconds(4)) {
            long diff = 0, sum = 0, n = 0;
            for (size_t i = 0; i < px.size(); i += 13, n++) diff += std::abs(px[i] - prevFrame[i]), sum += px[i];
            md.lib->observe(md.cur, diff / (255.f * n) / (0.25f + m.energy), sum / (255.f * n));
        }
        if (s.milkdrop) prevFrame = px;

        if (dump) {
            const bool lastFrame = int(frame) + 1 >= dumpFrames || (pcm.eof && finite);
            if (sequence) {
                if (!writeFrame(frameName(o.dump, frame), text)) return 1;
            } else if (lastFrame) {
                if (!writeFrame(o.dump, text)) return 1;
            } else lastText = text;
            frame++;
            if (lastFrame) break;
            if (!offline) std::this_thread::sleep_until(t0 + std::chrono::microseconds(1000000 / fps));
            continue;
        }

        std::string f = "\x1b[?2026h";
        if (kitty) {
            const int id = curId == 1 ? 2 : 1, z = s.cellbg ? Z_CELLS : Z_TEXT;
            f += "\x1b[H";
            bool sent = false;
            if (shm) {
                const std::string name = sys::shmName(frame);
                if (sys::shmPut(name, px.data(), px.size())) {
                    shmNames.push_back({name, t0});
                    f += kittyShm(name, W, H, px.size(), id, term.cols, term.rows, z);
                    sent = true;
                }
            }
            if (!sent) f += kittyDirect(px.data(), W, H, id, term.cols, term.rows, z);
            f += kittyDelete(curId);
            curId = id;
            f += textDiff(text, prevText);
        } else {
            halve(px, W, H, small);
            f += halfBlocks(small.data(), text);
        }
        f += "\x1b[?2026l";
        out(f);
        prevText = text;
        // The terminal unlinks each object once it has read it. Forget those; an object
        // still there after 5 s is a stalled or non-conforming terminal: reclaim it, and
        // after a few of those stop using shared memory.
        for (size_t i = 0; i < shmNames.size();) {
            if (!sys::shmExists(shmNames[i].first)) { shmNames.erase(shmNames.begin() + i); continue; }
            if (t0 - shmNames[i].second < std::chrono::seconds(5)) { i++; continue; }
            sys::shmUnlink(shmNames[i].first), shmNames.erase(shmNames.begin() + i);
            if (++shmStale == 3 && shm) shm = false, say("terminal isn't reading shared memory; using direct transfer", 4);
        }
        frame++;

        if (dirty && t0 - lastSave > std::chrono::seconds(1)) s.save(cfgPath), dirty = false, lastSave = t0;
        const auto budget = std::chrono::microseconds(1000000 / fps);
        const auto spent = Clock::now() - t0;
        if (spent < budget) std::this_thread::sleep_for(budget - spent);
    }

    for (auto &n : shmNames) sys::shmUnlink(n.first);
    md.save();
    if (!dump) s.save(cfgPath);
    if (dump && sys::quitSignal) return restore(), fprintf(stderr, "glistening: interrupted\n"), 128 + int(sys::quitSignal);  // ~Pcm stops the source
    // A headless run whose source ended must know how it ended before reporting success.
    if (dump && pcm.eof && !eofChecked) {
        if (!pcm.wait(500)) return restore(), fprintf(stderr, "glistening: audio source closed its output but kept running: %s\n", pcmWhat.c_str()), 1;
        if (pcm.failed()) return restore(), sourceFailed(), 1;
    }
    restore();
    const double secs = std::chrono::duration<double>(Clock::now() - start).count();
    if (!dump) fprintf(stderr, "glistening: %u frames in %.1fs (%.1f fps) at %dx%d, %s\n", frame, secs, frame / secs, W, H, kitty ? "kitty graphics" : "half blocks");
    return 0;
}
