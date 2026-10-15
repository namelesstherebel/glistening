// Offline gates for analysis.hpp and director.hpp. Synthetic fixtures have
// known ground truth; GLISTENING_REAL=<f32le stereo 44.1k file> adds a report on real audio.
#include "../audio.hpp"
#include "../director.hpp"
#include "../music.hpp"
#include "../platform/platform.hpp"
#include "../term.hpp"
#include "fixtures.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace tv;

static int failures = 0;
static void check(bool ok, const char *what, const std::string &detail = "") {
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  — ", detail.c_str());
    failures += !ok;
}
static std::string fmt(const char *f, double a, double b = 0, double c = 0) {
    char s[160];
    snprintf(s, sizeof s, f, a, b, c);
    return s;
}

struct Run {
    std::vector<double> beats, drops, sections, downbeats;
    std::vector<Features> hops;
    Features last;
};
static Run analyse(const Audio &a) {
    Analyzer an;
    Run r;
    const int chunk = 735;  // ~60 fps worth of frames per call, like the render loop
    for (size_t off = 0; off < a.size() / 2; off += chunk) {
        const int n = int(std::min<size_t>(chunk, a.size() / 2 - off));
        an.process(&a[2 * off], n, [&](const Features &f) {
            if (f.beat) r.beats.push_back(f.t);
            if (f.downbeat) r.downbeats.push_back(f.t);
            if (f.drop) r.drops.push_back(f.t);
            if (f.section) r.sections.push_back(f.t);
            r.hops.push_back(f);
        });
    }
    r.last = an.last();
    return r;
}

// Beat timing against ground truth over [from, to).
static void beatGates(const char *name, const Run &r, double bpm, double from, double to, double phase = 0) {
    const double P = 60 / bpm;
    std::vector<double> err;
    for (double b : r.beats)
        if (b >= from && b < to) err.push_back(b - phase - P * std::round((b - phase) / P));
    std::vector<double> sorted = err;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted.empty() ? 1 : sorted[sorted.size() / 2];
    int within = 0;
    for (double e : err) within += std::abs(e) <= 0.04;
    const double expected = (to - from) / P;
    check(err.size() >= 0.9 * expected && err.size() <= 1.1 * expected + 1,
          (std::string(name) + " beat recall").c_str(), fmt("%.0f emitted / %.0f true", err.size(), expected));
    check(!err.empty() && within >= 0.9 * err.size(), (std::string(name) + " beats within ±40ms").c_str(),
          fmt("%.0f%% within, median offset %+.1f ms", 100.0 * within / std::max<size_t>(err.size(), 1), median * 1000));
}

static void testTempo(double bpm) {
    hatPrev = 0;
    auto a = render(20, [&](double t) { return groove(t, bpm, true, false); });
    auto r = analyse(a);
    char name[32];
    snprintf(name, sizeof name, "%.0f BPM", bpm);
    check(std::abs(r.last.bpm - bpm) <= 1, (std::string(name) + " tempo").c_str(), fmt("got %.2f", r.last.bpm));
    check(r.last.conf >= 0.3f, (std::string(name) + " confidence").c_str(), fmt("conf %.2f", r.last.conf));
    beatGates(name, r, bpm, 6, 20);
}

// Busier mix: swung hats with velocity, snare on 2 and 4, syncopated bass notes,
// chord pad, and off-grid noise bursts standing in for vocals.
static void testRealistic(double bpm) {
    hatPrev = 0;
    std::mt19937 vr(3);
    std::vector<float> vel(4096), burst(4096);
    for (auto &v : vel) v = std::uniform_real_distribution<float>(0.4f, 1.f)(vr);
    for (auto &b : burst) b = std::uniform_real_distribution<float>(0, 1)(vr);
    const double B = 60 / bpm;
    auto a = render(30, [&](double t) {
        const long beat = long(t / B);
        const double tb = t - beat * B, swing = 0.58 * B;  // second eighth lands late
        const double te = tb < swing ? tb : tb - swing;
        float snN = noise(), x = kick(tb) + vel[(beat * 2 + (tb >= swing)) % 4096] * hat(te);
        if (beat % 2 == 1 && tb < 0.15) x += float(std::exp(-tb / 0.05) * (0.35 * snN + 0.25 * std::sin(2 * M_PI * 190 * tb)));
        static const double notes[] = {55, 55, 65.4, 49};
        const double bt = std::fmod(t, B * 1.5);  // dotted-quarter bass rhythm
        x += float(0.25 * std::exp(-bt / 0.25) * std::sin(2 * M_PI * notes[long(t / (B * 4)) % 4] * t));
        x += float(0.04 * (std::sin(2 * M_PI * 261.6 * t) + std::sin(2 * M_PI * 329.6 * t) + std::sin(2 * M_PI * 392 * t)));
        const long slot = long(t / 0.37);  // off-grid "vocal" phrases
        if (burst[slot % 4096] > 0.6) x += 0.08f * noise() * float(std::sin(M_PI * std::fmod(t, 0.37) / 0.37));
        return 0.7f * x;
    });
    auto r = analyse(a);
    char name[48];
    snprintf(name, sizeof name, "busy mix %.0f BPM", bpm);
    check(std::abs(r.last.bpm - bpm) <= 1, (std::string(name) + " tempo").c_str(), fmt("got %.2f, conf %.2f", r.last.bpm, r.last.conf));
    beatGates(name, r, bpm, 8, 30);
}

/** Check synthetic-track analysis, invalid input handling, AGC, and the realtime CPU budget. */
static void testAnalysis() {
    testRealistic(100);
    testRealistic(126);
    for (double bpm : {95.0, 120.0, 128.0, 140.0}) testTempo(bpm);

    for (unsigned seed : {0u, 4u, 7u}) {
        std::mt19937 rng(seed);
        auto r = analyse(render(20, [&](double) { return 0.3f * std::uniform_real_distribution<float>(-1, 1)(rng); }));
        check(r.last.conf < DirectorConfig{}.minConf, "white noise has low tempo confidence", fmt("conf %.2f", r.last.conf));
        check(r.beats.empty() && std::all_of(r.hops.begin(), r.hops.end(), [](const Features &f) { return f.bpm == 0 && f.beatPhase == 0; }),
              "white noise never acquires tempo or beat phase",
              fmt("%.2f BPM, %.0f beats", r.last.bpm, r.beats.size()));
    }
    {
        std::mt19937 rng(7);
        hatPrev = 0;
        auto r = analyse(render(20, [&](double t) {
            return t < 8 ? 0.3f * std::uniform_real_distribution<float>(-1, 1)(rng) : groove(t - 8, 120, true, false);
        }));
        check(std::abs(r.last.bpm - 120) <= 1 && r.last.conf > 0.3f, "periodic music acquires tempo after noise");
        beatGates("groove after noise", r, 120, 14, 20);
    }
    {
        auto r = analyse(render(20, [](double t) {
            return 0.1f * kick(std::fmod(t, 0.5)) + 0.15f * float(std::sin(2 * M_PI * 220 * t));
        }));
        check(std::abs(r.last.bpm - 120) <= 1 && r.last.conf > 0.3f, "weak periodic kicks acquire tempo over sustained pad");
        beatGates("weak kicks", r, 120, 8, 20);
    }
    {
        hatPrev = 0;
        auto r = analyse(render(16, [](double t) { return t < 8 ? groove(t, 120, true, false) : 0.f; }));
        check(std::abs(r.last.bpm - 120) <= 1 && r.last.conf < 0.25f, "acquired tempo survives confidence loss during silence");
    }
    {
        hatPrev = 0;
        auto r = analyse(render(20, song));
        beatGates("song after drop", r, SONG_BPM, SONG_DROP + 2, 20, SONG_QUIET);
    }
    {
        auto r = analyse(render(10, [](double) { return 0.f; }));
        check(r.last.silent && r.beats.empty() && r.drops.empty(), "silence: silent, no beats, no drops",
              fmt("%.0f beats", r.beats.size()));
    }
    {
        hatPrev = 0;
        auto r = analyse(render(30, [](double t) { return groove(t, 124, true, true); }));
        check(r.drops.empty(), "steady groove: no false drops", fmt("%.0f drops", r.drops.size()));
    }
    {
        hatPrev = 0;
        const double D = 26 * 60 / 128.0;  // drop on beat 26 (12.19s), as in real tracks
        auto r = analyse(render(24, [&](double t) { return groove(t, 128, t >= D, t >= D); }));
        check(r.drops.size() == 1 && r.drops[0] >= D - 0.05 && r.drops[0] <= D + 0.4, "breakdown -> drop detected once at 12.19s",
              r.drops.empty() ? "none" : fmt("%.0f drops, first %.2fs", r.drops.size(), r.drops[0]));
        const double bar = 4 * 60 / 128.0;
        int onBar = 0, total = 0;
        for (double d : r.downbeats)
            if (d >= D + 2 && d < D + 10) total++, onBar += std::abs(std::remainder(d - D, bar)) <= 0.06;
        check(total >= 3 && onBar == total, "bars re-anchored to the drop: downbeats on its bar grid",
              fmt("%.0f of %.0f downbeats on grid", onBar, total));
        check(r.sections.size() == 1 && r.sections[0] >= D && r.sections[0] <= D + 2, "one section change, near the drop",
              r.sections.empty() ? "none" : fmt("%.0f, first %.2fs", r.sections.size(), r.sections[0]));
    }
    {
        Audio a = render(10, [](double t) { return groove(t, 120, true, true); });
        for (size_t i = 0; i < a.size(); i += 97) a[i] = i % 3 ? NAN : (i % 2 ? INFINITY : 1e30f);
        auto r = analyse(a);
        bool finite = true;
        for (auto &f : r.hops)
            finite &= std::isfinite(f.bpm) && std::isfinite(f.conf) && std::isfinite(f.energy) && std::isfinite(f.bass);
        check(finite, "NaN/Inf/huge input: all features finite");
        Audio b(2048, NAN);
        Agc agc;
        agc.apply(b.data(), 1024);
        check(std::all_of(b.begin(), b.end(), [](float v) { return v == 0; }), "AGC scrubs non-finite samples");
    }
    {
        Agc agc;
        float quietOut = 0;
        for (int i = 0; i < 400; i++) {  // ~10s of 0.01-amplitude sine in 1024-frame blocks
            Audio blk(2048);
            for (int j = 0; j < 1024; j++) blk[2 * j] = blk[2 * j + 1] = 0.01f * std::sin(2 * M_PI * 220 * (i * 1024 + j) / SR);
            agc.apply(blk.data(), 1024);
            for (float v : blk) quietOut = std::max(quietOut, std::abs(v));
        }
        check(agc.gain() > 7 && quietOut <= 1, "AGC lifts quiet input (capped at 8x)", fmt("gain %.2f", agc.gain()));
    }
    {
        auto a = render(60, [](double t) { return groove(t, 128, true, true); });
        auto t0 = std::chrono::steady_clock::now();
        analyse(a);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        // Budget is 5% of realtime: shared hosted runners vary several-fold in CPU speed and
        // contention, while a real regression in the analysis would still blow well past this.
        check(s < 3.0, "CPU: 60s of audio analysed in <3.0s (5% realtime)", fmt("%.3fs", s));
    }
}

// Synthetic feature stream: steady beats at bpm, with hooks to inject events.
static std::vector<std::pair<double, Cut>> direct(Director &d, double secs, double bpm, float conf,
                                                 const std::function<void(Features &)> &edit = nullptr) {
    std::vector<std::pair<double, Cut>> cuts;
    Features f;
    f.bpm = bpm, f.conf = conf, f.silent = false;
    double nextBeat = 0;
    for (long h = 0; h < long(secs * HPS); h++) {
        f.t = h / HPS;
        f.beat = f.downbeat = f.drop = f.section = false;
        if (f.t >= nextBeat) {
            f.beat = true, f.beatIndex++, nextBeat += 60 / bpm;
            f.downbeat = f.beatIndex % 4 == 0;
        }
        if (edit) edit(f);
        if (Cut c = d.update(f)) cuts.push_back({f.t, c});
    }
    return cuts;
}

static void testDirector() {
    {
        Director d;
        auto cuts = direct(d, 300, 120, 0.8f);
        bool ok = !cuts.empty();
        for (size_t i = 1; i < cuts.size(); i++)
            ok &= cuts[i].second.why == Why::Phrase && !cuts[i].second.hard && std::abs(cuts[i].first - cuts[i - 1].first - 32) < 0.05;
        check(ok, "steady 120 BPM: soft phrase cuts every 64 beats (32s)", fmt("%.0f cuts", cuts.size()));
    }
    {
        Director d;
        auto cuts = direct(d, 120, 120, 0.8f, [](Features &f) { f.silent = true; });
        check(cuts.empty(), "silence: no cuts");
    }
    {
        Director d;
        auto cuts = direct(d, 30, 120, 0.8f, [](Features &f) {
            f.drop = std::abs(f.t - 20) < 0.006 || std::abs(f.t - 22) < 0.006;
        });
        check(cuts.size() == 1 && cuts[0].second.why == Why::Drop && cuts[0].second.hard && std::abs(cuts[0].first - 20) < 0.02,
              "drop: immediate hard cut; second drop 2s later ignored", fmt("%.0f cuts", cuts.size()));
    }
    {
        Director d;
        auto cuts = direct(d, 95, 120, 0.0f);
        bool ok = cuts.size() == 3;
        for (auto &c : cuts) ok &= c.second.why == Why::Fallback;
        check(ok, "no tempo confidence: fallback cut every 30s", fmt("%.0f cuts", cuts.size()));
    }
    {
        Director d;
        auto cuts = direct(d, 20, 120, 0.8f, [](Features &f) { f.section = std::abs(f.t - 14.1) < 0.006; });
        check(cuts.size() == 1 && cuts[0].second.why == Why::Section && cuts[0].first >= 14.1 && cuts[0].first <= 16.1,
              "section change after 12s: cut on next downbeat", cuts.empty() ? "none" : fmt("at %.2fs", cuts[0].first));
    }
    {
        Director d;
        auto cuts = direct(d, 31, 120, 0.8f, [](Features &f) { f.section = std::abs(f.t - 5) < 0.006; });
        check(cuts.empty(), "early section change expires instead of cutting late");
    }
    {
        Director d;
        d.locked = true;
        check(direct(d, 200, 120, 0.8f).empty(), "locked: no cuts");
    }
}

static void testLibrary() {
    std::vector<std::string> names;
    for (int i = 0; i < 200; i++) names.push_back("preset " + std::to_string(i));
    Library lib(names);
    for (int i = 0; i < 200; i++)
        for (int k = 0; k < 60; k++) lib.observe(i, i / 200.f, 0.5f);
    lib.p[150].flag = -1;
    lib.p[151].flag = -2;
    std::mt19937 rng(1);
    Library empty({});
    check(empty.pick(0.5f, rng) == -1 && empty.random(rng) == -1, "empty preset library has no selection");
    bool banned = false;
    double lo = 0, hi = 0;
    for (int k = 0; k < 2000; k++) {
        int a = lib.pick(0.1f, rng), b = lib.pick(0.9f, rng);
        banned |= a == 150 || a == 151 || b == 150 || b == 151;
        lo += a / 200.0, hi += b / 200.0;
    }
    check(!banned, "banned and broken presets never picked");
    check(lo / 2000 < 0.3 && hi / 2000 > 0.7, "low energy picks calm presets, high energy picks busy ones",
          fmt("mean motion rank %.2f vs %.2f", lo / 2000, hi / 2000));
    for (int k = 0; k < 40; k++) lib.played(k);
    bool repeat = false;
    for (int k = 0; k < 500; k++) repeat |= lib.pick(0.f, rng) < 40;
    check(!repeat, "recently played presets are skipped");
    {
        Library all = lib;
        for (auto &e : all.p) e.flag = -1;
        all.p[7].flag = -2;
        bool none = all.pick(0.5f, rng) == -1 && all.random(rng) == -1;
        all.p[42].flag = 0;
        bool only = true;
        for (int k = 0; k < 50; k++) only &= all.pick(0.5f, rng) == 42 && all.random(rng) == 42;
        check(none && only, "with every preset banned or broken nothing is picked; random picks only usable ones");
    }
    {  // a big library with a handful of usable presets outside the recent window
        std::vector<std::string> many;
        for (int i = 0; i < 2000; i++) many.push_back("p" + std::to_string(i));
        Library big(many);
        for (auto &e : big.p) e.flag = -1;
        for (int i = 0; i < 45; i++) big.p[i].flag = 0;
        for (int i = 0; i < 40; i++) big.played(i);
        bool fresh = true;
        for (int k = 0; k < 300; k++) {
            const int i = big.pick(0.5f, rng);
            fresh &= i >= 40 && i < 45;
        }
        check(fresh, "when probes miss, an unplayed usable preset still beats a recent one");
    }

    const std::string path = (std::filesystem::temp_directory_path() / "glistening-test-presets.tsv").string();
    lib.p[7].flag = 1;
    lib.save(path);
    Library back(names);
    back.load(path);
    std::remove(path.c_str());
    check(back.p[7].flag == 1 && back.p[150].flag == -1 && back.p[42].n == 60 && std::abs(back.p[42].motion - 0.21f) < 1e-4,
          "library save/load round trip");
}

static void reportReal(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    Audio a;
    float buf[4096];
    size_t n;
    while ((n = fread(buf, 4, 4096, f)) > 0) a.insert(a.end(), buf, buf + n);
    fclose(f);
    auto r = analyse(a);
    printf("\nreal audio: %.1fs\n", a.size() / 2.0 / SR);
    for (auto &h : r.hops)
        if (std::fmod(h.t, 5) < 1 / HPS)
            printf("  t=%5.1fs bpm %6.2f conf %.2f energy %.2f bass %.2f%s\n", h.t, h.bpm, h.conf, h.energySlow, h.bass, h.silent ? " silent" : "");
    printf("  %zu beats, %zu drops, %zu sections\n", r.beats.size(), r.drops.size(), r.sections.size());
    double m = 0, v = 0;
    int c = 0;
    for (auto &h : r.hops)
        if (h.t > a.size() / 2.0 / SR - 20) m += h.bpm, v += h.bpm * h.bpm, c++;
    m /= c;
    const double sd = std::sqrt(std::max(0.0, v / c - m * m));
    const float conf = r.last.conf;
    check(sd < 1.5 || conf < DirectorConfig{}.minConf, "real audio: tempo stable, or confidence honestly low",
          fmt("mean %.2f, sd %.2f, conf %.2f", m, sd, conf));
    if (r.beats.size() > 8 && conf >= DirectorConfig{}.minConf) {  // beat spacing should match the reported tempo
        std::vector<double> gaps;
        for (size_t i = 1; i < r.beats.size(); i++) gaps.push_back(r.beats[i] - r.beats[i - 1]);
        std::sort(gaps.begin(), gaps.end());
        check(std::abs(gaps[gaps.size() / 2] - 60 / m) < 0.02, "real audio: median beat gap matches tempo",
              fmt("gap %.3fs vs %.3fs", gaps[gaps.size() / 2], 60 / m));
    }
}

// --- spectrum, beat/bar phase, frame-rate accents ---------------------------------
static void testMusic() {
    {
        hatPrev = 0;
        auto a = render(12, [](double t) { return groove(t, 120, true, false); });
        Listener listener;
        long detected = 0;
        listener.feed(a.data(), int(a.size() / 2), [&](const Features &f) { detected += f.beat; });
        listener.frame(0.1f);
        check(detected > 3 && listener.music.beats == detected && listener.music.beat > 0.9f,
              "stalled video counts every audio beat and preserves beat accent", fmt("%.0f detected, %.0f counted", detected, listener.music.beats));
        listener.frame(1 / 60.f);
        check(listener.music.beats == detected, "next video frame never recounts accumulated beats");
    }
    {
        auto r = analyse(render(3, [](double t) { return 0.5f * float(std::sin(2 * M_PI * 1000 * t)); }));
        const auto &sp = r.last.spectrum;
        const int peak = int(std::max_element(sp.begin(), sp.end()) - sp.begin());
        const int want = int(std::floor(std::log(1000 / 30.0) / std::log(16000 / 30.0) * BANDS));
        int loud = 0;
        for (float v : sp) loud += v > 0.5f;
        check(std::abs(peak - want) <= 1 && loud <= 4, "spectrum: 1 kHz sine peaks in its band, others quiet",
              fmt("peak band %.0f (want %.0f), %.0f bands > 0.5", peak, want, loud));
        auto q = analyse(render(3, [](double) { return 0.f; }));
        check(*std::max_element(q.last.spectrum.begin(), q.last.spectrum.end()) == 0, "spectrum: silence is all zero");
    }
    {
        hatPrev = 0;
        auto a = render(16, [](double t) { return groove(t, 120, true, false); });
        Analyzer an;
        float prevPhase = 0, prevBar = 0;
        int ramps = 0, breaks = 0, wraps = 0, downs = 0, n = 0;
        double lastT = 0;
        for (size_t off = 0; off < a.size() / 2; off += 735) {
            an.process(&a[2 * off], int(std::min<size_t>(735, a.size() / 2 - off)), [&](const Features &f) {
                if (f.t < 8) return;
                n++;
                if (f.beat) {
                    breaks += f.beatPhase > 0.15f;  // a beat restarts the ramp
                    if (f.downbeat) downs++, wraps += f.barPhase < 0.1f && prevBar > 0.85f;
                } else if (f.beatPhase + 1e-4f >= prevPhase) ramps++;
                prevPhase = f.beatPhase, prevBar = f.barPhase, lastT = f.t;
            });
        }
        check(breaks == 0 && ramps >= 0.95 * (n - 16), "beat phase ramps 0..1 between beats and restarts on each beat",
              fmt("%.0f of %.0f hops rising, %.0f late restarts", ramps, n, breaks));
        check(downs >= 3 && wraps == downs, "bar phase wraps from ~1 to 0 on downbeats", fmt("%.0f of %.0f downbeats", wraps, downs));
    }
    {
        Music m;
        Features f;
        f.silent = false, f.bpm = 120, f.beat = true;
        m.update(f, nullptr, 1 / 60.f);
        f.beat = false;
        m.update(f, nullptr, 1 / 60.f);
        const float rise = m.beat;
        for (int i = 0; i < 15; i++) m.update(f, nullptr, 1 / 60.f);  // +0.25 s: half a beat
        check(rise > 0.9f && m.beat < 0.3f, "beat accent: rises within ~30 ms, mostly gone by half a beat",
              fmt("%.2f after 33 ms, %.2f after 0.28 s", rise, m.beat));
        f.drop = true;
        m.update(f, nullptr, 1 / 60.f);
        f.drop = false;
        for (int i = 0; i < 60; i++) m.update(f, nullptr, 1 / 60.f);
        check(m.drop > 0.5f, "drop accent lasts beyond a second", fmt("%.2f after 1 s", m.drop));
        const double flow0 = m.flow;
        for (int i = 0; i < 60; i++) m.update(f, nullptr, 1 / 60.f);
        check(m.flow > flow0 + 0.3, "music clock advances", fmt("+%.2f in 1 s", m.flow - flow0));
        const double before = m.flow;
        m.update(f, nullptr, 0.125f);
        check(m.flowDelta > 0 && std::abs((m.flow - before) - m.flowDelta) < 1e-12,
              "music flowDelta equals actual update increment");
        m.update(f, nullptr, 0);
        check(m.flowDelta == 0, "zero-duration update has zero flowDelta");
    }
}

static void testScenePicker() {
    ScenePicker sp;
    sp.busy = {0.15f, 0.55f, 0.4f, 0.85f, 0.5f, 0.3f, 0.45f, 0.7f, 0.1f, 0.8f, 0.6f, 0.35f, 0.65f, 0.5f};  // the built-in ratings
    std::mt19937 rng(5);
    double lo = 0, hi = 0;
    bool repeat = false;
    std::vector<int> used(sp.busy.size());
    int last2[2] = {-1, -1};
    for (int k = 0; k < 400; k++) {
        const float e = k % 2 ? 0.05f : 0.95f;
        const int i = sp.pick(e, rng);
        repeat |= i == last2[0] || i == last2[1];
        (k % 2 ? lo : hi) += sp.busy[i];
        sp.played(i), last2[0] = last2[1], last2[1] = i;
    }
    for (int k = 0; k < 400; k++) {  // mid-rated scenes only win mid energies, so sweep the whole range
        const int i = sp.pick((k % 21) / 20.f, rng);
        used[i]++, sp.played(i);
    }
    check(lo / 200 < 0.4 && hi / 200 > 0.6, "scene director: quiet music picks calm scenes, loud picks busy",
          fmt("mean busy %.2f quiet vs %.2f loud", lo / 200, hi / 200));
    check(!repeat, "scene director never repeats one of the last two scenes");
    check(std::count(used.begin(), used.end(), 0) == 0, "scene director uses every scene across the energy range");
    check(sceneEnergy(0.2f, true) == 1 && sceneEnergy(0.25f, false) == 0, "drops ask for the busiest scenes");
}

// --- TUI: layout, keys ---------------------------------------------------------------
static UiState sampleState() {
    UiState st;
    st.scenes = {"Aurora", "Mercury", "Galaxy", "Tunnel", "Ridgeline", "Ink", "Quasicrystal", "Phosphor",
                 "Rain Glass", "Laser Cathedral", "Magnetic Field", "Loom", "Paper City", "Clockwork"};
    st.current = 3, st.milkdropAvailable = true, st.palette = "ember", st.bpm = 128, st.conf = 0.8f, st.sure = true;
    st.silent = false, st.beats = 12, st.toast = "Tunnel  · drop — a deliberately long toast that must be cut to fit any width";
    return st;
}

static void testLayout() {
    for (auto [c, r] : {std::pair{80, 24}, {200, 60}, {100, 30}, {40, 12}}) {
        Settings s;
        Ui ui(s);
        for (int mode = 0; mode < 4; mode++) {
            ui.panel = mode & 1, ui.help = mode & 2;
            for (int sel = 0; sel < 22; sel++) {  // 14 scenes, MilkDrop, 7 settings
                ui.sel = sel;
                Screen sc(c, r);
                auto boxes = ui.draw(sc, sampleState());
                bool inside = true;
                for (auto &b : boxes) inside &= b.x >= 0 && b.y >= 0 && b.x + b.w <= c && b.y + b.h <= r && b.w > 0 && b.h > 0;
                bool selShown = !ui.panel || ui.help;
                for (int y = 0; y < r && !selShown; y++)
                    for (int x = 0; x < c; x++) selShown |= sc.at(x, y).ch == "›";
                if (sc.clipped || !inside || !selShown) {
                    check(false, ("TUI fits at " + std::to_string(c) + "x" + std::to_string(r)).c_str(),
                          fmt("mode %.0f sel %.0f: %.0f clipped glyphs", mode, sel, sc.clipped) + (inside ? "" : ", box outside") +
                              (selShown ? "" : ", selection hidden"));
                    goto next;
                }
            }
        }
        check(true, ("TUI fits at " + std::to_string(c) + "x" + std::to_string(r) + " (HUD, toast, panel, help, every selection)").c_str());
    next:;
    }
    {
        Settings s;
        Ui ui(s);
        ui.panel = true, ui.sel = 3;
        ui.key("?", 14, true);
        const std::vector<std::string> labels = {"control panel", "choose a scene", "previous / next", "next scene (director)",
            "auto director on/off", "MilkDrop mode", "cycle palette", "new look / random", "sensitivity", "effect intensity",
            "HUD", "favourite / ban", "reload shaders", "info", "this help", "quit"};
        std::vector<bool> seen(labels.size());
        bool clean = true;
        for (int step = 0; step < 16; step++) {
            Screen sc(40, 12);
            ui.draw(sc, sampleState());
            std::string text;
            for (int y = 0; y < sc.rows; y++) {
                for (int x = 0; x < sc.cols; x++) text += sc.at(x, y).ch;
                text += "\n";
            }
            for (size_t i = 0; i < labels.size(); i++) seen[i] = seen[i] || text.find(labels[i]) != std::string::npos;
            clean &= !sc.clipped && ui.sel == 3;
            ui.key(step % 2 ? "down" : "j", 14, true);
        }
        ui.key("esc", 14, true);
        check(clean && std::all_of(seen.begin(), seen.end(), [](bool v) { return v; }) && ui.panel && !ui.help && ui.sel == 3,
              "40x12 help scroll reaches every entry and preserves underlying panel");
    }
    {
        Settings s;
        Ui ui(s);
        auto st = sampleState();
        st.toast.clear();
        st.palette = std::string(200, 'p');
        Screen sc(100, 24);
        ui.draw(sc, st);
        std::string row;
        int meters = 0;
        for (int x = 0; x < sc.cols; x++) {
            const auto &cell = sc.at(x, sc.rows - 1);
            row += cell.ch;
            meters += cell.fg == st.accent;
        }
        check(!sc.clipped && row.find("128 bpm") != std::string::npos && row.find("auto") != std::string::npos && meters == int(std::size(st.spectrum)),
              "long palette leaves HUD tempo, meters and director mode visible");
    }
    {
        Settings s;
        Ui ui(s);
        ui.panel = true;
        Screen sc(100, 30);
        auto boxes = ui.draw(sc, sampleState());
        const Box &pb = boxes.back();
        int names = 0;
        for (auto &n : sampleState().scenes)
            for (int y = pb.y; y < pb.y + pb.h; y++) {
                std::string row;
                for (int x = pb.x; x < pb.x + pb.w; x++) row += sc.at(x, y).ch;
                names += row.find(" " + n) != std::string::npos;
            }
        check(names == 14, "100x30 panel lists all 14 scenes", fmt("%.0f of 14", names));
    }
}

static void testKeys() {
    {
        Settings ps;
        Ui pu(ps);
        ps.phrase = 8, pu.stepPhrase(1);
        const int up8 = ps.phrase;
        ps.phrase = 256, pu.stepPhrase(1);
        const int up256 = ps.phrase;
        pu.stepPhrase(-1);
        const int down256 = ps.phrase;
        ps.phrase = 48, pu.stepPhrase(-1);
        const int down48 = ps.phrase;
        check(up8 == 16 && up256 == 256 && down256 == 128 && down48 == 32, "phrase steps by powers of two over the whole 8..256 range");
    }
    auto k = splitKeys("\x1b[A\x1b[B\x1bOC\tq\r\x1b");
    check(k == std::vector<std::string>{"up", "down", "right", "tab", "q", "enter", "esc"}, "key parser: arrows (CSI and SS3), tab, enter, lone esc");
    Settings s;
    Ui ui(s);
    ui.focus = 3;
    auto run = [&](const std::string &key) { return ui.key(key, 14, true); };
    run("tab");
    bool ok = ui.panel && ui.sel == 3;
    run("up"), run("up"), run("up"), run("up");
    ok &= ui.sel == 21;  // wrapped to the last row (HUD): 14 scenes, MilkDrop, 7 settings
    run("down");
    ok &= ui.sel == 0;
    auto a = run("enter");
    ok &= a.size() == 1 && a[0].a == Act::Scene && a[0].arg == 0;
    check(ok, "panel opens on the current scene; ↑↓ wrap; ⏎ chooses a scene");
    ui.sel = 16;  // Palette row (14 scenes + MilkDrop + director)
    const std::string before = s.palette;
    a = run("right");
    check(a.size() == 1 && a[0].a == Act::Palette && s.palette != before, "→ on Palette cycles it", s.palette);
    ui.sel = 17;
    const float g = s.gain;
    run("right");
    check(s.gain > g, "→ on Sensitivity raises it");
    ui.sel = 19, run("left");
    check(s.scale == 3, "← on Quality lowers the resolution", fmt("scale %.0f", s.scale));
    run("?");
    a = run("esc");
    bool esc = !ui.help && ui.panel && a.empty();
    a = run("esc");
    esc &= !ui.panel && a.empty();
    a = run("esc");
    esc &= a.size() == 1 && a[0].a == Act::Quit;
    check(esc, "esc closes help, then the panel, then quits");
    a = run("5");
    bool nums = a.size() == 1 && a[0].a == Act::Scene && a[0].arg == 4;
    a = run("9");
    nums &= a.size() == 1 && a[0].a == Act::Scene && a[0].arg == 8;
    nums &= ui.key("9", 8, true).empty();  // no ninth scene, no action
    a = run("a");
    nums &= a.size() == 1 && a[0].a == Act::Director && !s.director;
    run("tab"), ui.sel = 13, a = run("enter"), run("tab");  // scenes past 9 are chosen in the panel
    nums &= a.size() == 1 && a[0].a == Act::Scene && a[0].arg == 13 && !ui.panel;
    check(nums, "number keys pick scenes 1-9, the panel reaches the rest; a toggles the director");
}

// --- output encoders ------------------------------------------------------------------
static bool validUtf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) return false;
        for (int k = 1; k < n; k++)
            if ((s[i + k] & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

static void testEncoders() {
    {
        Pen p;
        std::string out;
        p.to(out, DEF, DEF, 0);
        out += "x";
        check(out == "\x1b[0mx", "pen resets inherited terminal style before first default cell");
    }
    const int w = 97, h = 61;
    std::vector<unsigned char> rgb(w * h * 3);
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = (unsigned char)(i * 7);
    const std::string f = kittyDirect(rgb.data(), w, h, 2, 80, 24, Z_TEXT);
    size_t pos = 0, chunks = 0, payload = 0;
    bool ok = true, last = false;
    while ((pos = f.find("\x1b_G", pos)) != std::string::npos) {
        const size_t semi = f.find(';', pos), end = f.find("\x1b\\", pos);
        ok &= semi < end && !last;
        const std::string ctl = f.substr(pos + 3, semi - pos - 3), data = f.substr(semi + 1, end - semi - 1);
        ok &= data.size() <= 4096 && data.size() % 4 == 0;
        ok &= chunks || (ctl.find("a=T") != std::string::npos && ctl.find("s=97") != std::string::npos && ctl.find("v=61") != std::string::npos);
        last = ctl.find("m=0") != std::string::npos;
        payload += data.size(), chunks++, pos = end + 2;
    }
    check(ok && last && pos == std::string::npos && payload == (rgb.size() + 2) / 3 * 4 && chunks == (payload + 4095) / 4096,
          "kitty in-band frame: ≤4096-byte base64 chunks, m=1 until the last, full payload", fmt("%.0f chunks", chunks));
    const std::string sh = kittyShm("/glistening-1-2", w, h, rgb.size(), 1, 80, 24, Z_CELLS);
    check(sh.find("t=s") != std::string::npos && sh.find("S=" + std::to_string(rgb.size())) != std::string::npos &&
              sh.find("z=-1073741825") != std::string::npos && sh.find(b64((const unsigned char *)"/glistening-1-2", 15)) != std::string::npos,
          "kitty shm frame names the object, its size and the z layer");

    Screen ui(40, 12);
    ui.text(3, 5, "Tunnel · ember ●", 0xffffff, DEF, BOLD);
    std::vector<unsigned char> img(40 * 24 * 3, 40);
    const std::string hb = halfBlocks(img.data(), ui);
    size_t blocks = 0, moves = 0;
    for (size_t i = 0; (i = hb.find("▀", i)) != std::string::npos; i += 3) blocks++;
    for (size_t i = 0; (i = hb.find("H", i)) != std::string::npos; i++) moves += hb[i - 1] >= '0' && hb[i - 1] <= '9';
    check(validUtf8(hb) && blocks == 40 * 12 - 13 && moves == 12 && hb.find("Tunnel") != std::string::npos && hb.find("ember") != std::string::npos,
          "half blocks: one cell per column and row, text replaces cells, valid UTF-8", fmt("%.0f blocks, %.0f row moves", blocks, moves));
    Screen a(40, 12), b(40, 12);
    a.text(1, 1, "hello", 0xff0000);
    b.text(1, 1, "help!", 0xff0000);
    const std::string first = textDiff(a, Screen(0, 0)), none = textDiff(a, a), delta = textDiff(b, a);
    check(!first.empty() && none.empty() && delta.find("p!") != std::string::npos && delta.find("hel") == std::string::npos,
          "text layer: full first draw, nothing when unchanged, only the changed span after");
}

// --- config and palettes --------------------------------------------------------------
static void testReviewFixes() {
    check(frameName("f/%099999999999999999999d.ppm", 42) == "f/" + std::string(18, '0') + "42.ppm",
          "dump pattern: absurd widths saturate at 20 digits");
    check(frameName("f/%05d.ppm", 42) == "f/00042.ppm" && frameName("f-%d.ppm", 7) == "f-7.ppm" && frameName("100%.ppm", 3) == "100%.ppm" &&
              frameName("a%s%n.ppm", 1) == "a%s%n.ppm" && frameName("x.ppm", 1) == "x.ppm",
          "--dump: only %d / %0Nd become the frame number; other % stay literal");
    std::string rest;
    auto k = splitKeys("q\x1b[", &rest);
    auto k2 = splitKeys(rest + "A", &rest);
    check(k == std::vector<std::string>{"q"} && k2 == std::vector<std::string>{"up"} && rest.empty(),
          "key parser keeps an escape sequence split across reads");
    auto k3 = splitKeys("\x1b", &rest);
    auto k4 = splitKeys(rest + "[B", &rest);
    check(k3.empty() && k4 == std::vector<std::string>{"down"} && rest.empty(), "a lone trailing ESC waits for the rest of its sequence");
    std::string peaks;
    bool peaked = true;
    for (float fps : {5.f, 30.f, 120.f}) {  // peak within one frame or the 30 ms rise, whichever is longer
        Music m;
        Features fb;
        fb.bpm = 120, fb.silent = false, fb.beat = true;
        float best = 0;
        for (int i = 0; i < std::max(1, int(std::ceil(0.03f * fps))); i++) m.update(fb, nullptr, 1 / fps), fb.beat = false, best = std::max(best, m.beat);
        peaked &= best > 0.95f;
        peaks += fmt("%.2f@%.0f ", best, fps);
    }
    check(peaked, "beat accent peaks within a frame or its rise, at 5, 30 and 120 fps", peaks);
    ScenePicker sp;
    sp.busy = {0.1f, 0.5f, 0.9f};
    sp.played(0), sp.played(2);
    sp.remap({1, -1, 0});  // scene 0 moved to 1, 1 vanished, 2 moved to 0
    check(sp.last() == 0, "scene director history follows a reload's new indices");
    ScenePicker two;
    two.busy = {0.2f, 0.8f};
    std::mt19937 r2(1);
    bool alternates = true;
    for (int k = 0, prev = -1; k < 20; k++) {
        const int i = two.pick(0.5f, r2);
        alternates &= i != prev;
        two.played(i), prev = i;
    }
    check(alternates, "with only two scenes the director still alternates");
    Settings s;
    s.parse("scale=1e300\nphrase=-1e300\n");
    check(s.scale == 4 && s.phrase == 8, "config clamps huge numbers before narrowing to int");
    Music a, b;
    Features f;
    for (int i = 0; i < 30; i++) a.update(f, nullptr, 1 / 30.f);
    for (int i = 0; i < 120; i++) b.update(f, nullptr, 1 / 120.f);
    check(a.histPos == 30 && std::abs(b.histPos - 30) <= 1, "spectrum history advances at 30 rows/s at any frame rate",
          fmt("%.0f rows at 30 fps, %.0f at 120 fps", a.histPos, b.histPos));
}

static void testSanitize() {
    Screen sc(40, 2);
    sc.text(0, 0, "a\x1b]0;pwn\x07b\xc2\x9b\x80é●", 0xffffff);
    std::string row;
    for (int x = 0; x < 40; x++) row += sc.at(x, 0).ch;
    bool clean = true;
    for (unsigned char c : row) clean &= c >= 0x20 && c != 0x7f;
    check(clean && row.find("é●") != std::string::npos && row.find("\xc2\x9b") == std::string::npos,
          "UI text: control bytes, C1 controls and broken UTF-8 become '?'");
    const std::string ours = "·—…‹›←↑→↓⏎▀▁▂▃▄▅▆▇█●★é";
    bool kept = safeText(ours) == ours;
    check(kept, "UI text: every glyph glistening draws is one cell wide and kept", safeText(ours));
    check(safeText("a漢b🎵c\xcc\x81" "d\xe2\x80\x8d" "e") == "a?b?c?d?e" && safeText("\xc0\xaf\xed\xa0\x80") == "??",
          "UI text: wide, zero-width, overlong and surrogate glyphs become one '?' cell each");
}

static void testConfig() {
    Settings s;
    s.scene = "ink", s.palette = "desktop", s.director = false, s.milkdrop = true, s.hud = false, s.gain = 1.56f;
    s.intensity = 0.7f, s.scale = 3, s.phrase = 32, s.cellbg = true;
    Settings t;
    const auto file = (std::filesystem::temp_directory_path() / "glistening-test-config-save").string();
    std::filesystem::remove(file);
    Settings loaded;
    const bool saved = s.save(file);
    check(saved && loaded.load(file) && loaded.str() == s.str(), "config save reports success and round-trips on disk");
    check(!s.save(file + "/child"), "config save reports unwritable parent path");
#ifndef _WIN32
    check(!s.save("/dev/full"), "config save reports buffered write or close failure");
#endif
    std::filesystem::remove(file);
    t.parse(s.str());
    check(t.str() == s.str(), "config round trip");
    Settings g;
    g.parse("gain=banana\nscale=99\nphrase=-4\n=\nnoequals\nintensity=nan\nscene=\n# comment\nhud=0");
    check(g.gain == 1 && g.scale == 4 && g.phrase == 8 && g.intensity == 1 && g.scene == "aurora" && !g.hud,
          "config ignores garbage and clamps values");
    Settings tail;
    tail.parse("gain=2oops\ndirector=0x\nscale=3 \nphrase=16\r");
    check(tail.gain == 1 && tail.director && tail.scale == 3 && tail.phrase == 16,
          "config ignores numbers with trailing garbage (whitespace and CR are fine)");
    Settings crlf;
    crlf.parse("scene=tunnel\r\npalette=ember \r\n");
    check(crlf.scene == "tunnel" && crlf.palette == "ember", "config strings drop CRLF and trailing spaces");
    Palette p;
    const char *toml =
        "# Tokyo Night\naccent = \"#7aa2f7\"\ncursor = \"#c0caf5\"\nforeground = \"#a9b1d6\"\nbackground = \"#1a1b26\"\n"
        "selection_foreground = \"#c0caf5\"\nselection_background = \"#7aa2f7\"\n\ncolor0 = \"#32344a\"\ncolor1 = \"#f7768e\"\n"
        "color2 = \"#9ece6a\"\ncolor3 = \"#e0af68\"\ncolor4 = \"#7aa2f7\"\ncolor5 = \"#ad8ee6\"\ncolor6 = \"#449dab\"\ncolor7 = \"#787c99\"\n";
    const bool parsed = parseOmarchy(toml, p);
    bool ordered = parsed && luma(p.c[0]) < luma(p.c[1]) && luma(p.c[1]) < luma(p.c[3]) && luma(p.c[3]) <= luma(p.c[4]) + 1;
    check(ordered, "Omarchy colors.toml -> dark-to-light 5-stop palette",
          parsed ? [&] { char b[80]; snprintf(b, sizeof b, "deep #%06x, accent #%06x, highlight #%06x", p.c[0], p.c[3], p.c[4]); return std::string(b); }() : "not parsed");
    Palette q;
    check(!parseOmarchy("[colors]\nprimary = 12\nbackground = \"#zzzzzz\"\n", q) && !parseOmarchy("", q),
          "Omarchy parser rejects files without usable colours");
    std::mt19937 rng(3);
    bool own = true;
    for (int i = 0; i < 50; i++) {
        auto v = makeVariant({"ember", "abyss"}, "auto", nullptr, rng);
        own &= v.palette == "ember" || v.palette == "abyss";
    }
    auto named = makeVariant({"ember"}, "moss", nullptr, rng), desk = makeVariant({"ember"}, "desktop", &p, rng);
    auto fallback = makeVariant({"ember"}, "desktop", nullptr, rng);
    check(own && named.palette == "moss" && desk.palette == "desktop" && fallback.palette == "ember",
          "palette choice: scene's own in auto, named when set, desktop when available");
}

// Ring, downmix and resampler: what every native capture path feeds the analysis through.
static void testAudioPlumbing() {
    {
        Ring r(4);
        float f[20], out[20];
        for (int k = 0; k < 10; k++) f[2 * k] = float(k), f[2 * k + 1] = -float(k);
        bool ok = r.push(f, 3) == 3 && r.pop(out, 2) == 2 && out[0] == 0 && out[3] == -1;
        ok &= r.push(f + 6, 3) == 3 && r.push(f + 12, 1) == 0 && r.dropped() == 1 && r.size() == 4;
        ok &= r.pop(out, 10) == 4 && out[0] == 2 && out[2] == 3 && out[6] == 5 && out[7] == -5 && r.pop(out, 1) == 0;
        check(ok, "ring: order, wrap-around, full ring drops (counted), empty pops nothing");
    }
    for (bool corrupt : {false, true}) {  // A mismatch must still drain the producer.
        Ring r(256);
        const int total = 400000;
        std::thread producer([&] {
            float fr[2 * 64];
            for (int k = 0; k < total;) {
                const int n = std::min(64, total - k);
                for (int i = 0; i < n; i++) fr[2 * i] = float(k + i), fr[2 * i + 1] = float(-(k + i));
                if (corrupt && k == 0) fr[0] = -1;
                k += int(r.push(fr, size_t(n)));  // a full ring takes fewer; retry the rest
            }
        });
        float got[2 * 100];
        bool ok = true;
        for (int next = 0; next < total;)
            for (size_t i = 0, n = r.pop(got, 100); i < n; i++, next++) ok &= got[2 * i] == float(next) && got[2 * i + 1] == float(-next);
        producer.join();
        check(ok == !corrupt, corrupt ? "ring: order mismatch detected while draining producer to completion" :
              "ring: concurrent producer and consumer see every frame once, in order");
    }
    {
        const float six[6] = {1, 2, 3, 4, 5, 6}, mono[2] = {0.25f, -0.5f}, three[6] = {0, 0, 1, 1, 2, 3}, quad[4] = {1, 2, 3, 4};
        float st[4];
        downmix(six, 6, 1, st);
        bool ok = std::fabs(st[0] - (1 + 0.7071f * 3)) < 1e-5 && std::fabs(st[1] - (2 + 0.7071f * 3)) < 1e-5;
        downmix(mono, 1, 2, st);
        ok &= st[0] == 0.25f && st[1] == 0.25f && st[2] == -0.5f && st[3] == -0.5f;
        downmix(three, 3, 2, st);
        ok &= std::fabs(st[0] - 0.7071f) < 1e-5 && std::fabs(st[1] - 0.7071f) < 1e-5 &&
              std::fabs(st[2] - (1 + 0.7071f * 3)) < 1e-5 && std::fabs(st[3] - (2 + 0.7071f * 3)) < 1e-5;
        downmix(quad, 4, 1, st);
        ok &= st[0] == 1 && st[1] == 2;
        check(ok, "downmix: 3.0 and 5.1 fold centre at -3 dB; mono duplicated; quad keeps fronts");
    }
    {  // 1 kHz at 48 kHz -> 44.1 kHz: right length, right waveform, seamless across chunks.
        const int in = 48000;
        std::vector<float> src(size_t(in) * 2);
        for (int i = 0; i < in; i++) src[2 * i] = src[2 * i + 1] = float(std::sin(2 * M_PI * 1000 * i / in));
        Resampler whole(in), parts(in);
        std::vector<float> a(size_t(whole.maxOut(in)) * 2), b;
        a.resize(size_t(whole.run(src.data(), in, a.data())) * 2);
        for (int off = 0; off < in; off += 777) {
            const int n = std::min(777, in - off);
            std::vector<float> o(size_t(parts.maxOut(n)) * 2);
            o.resize(size_t(parts.run(&src[size_t(off) * 2], n, o.data())) * 2);
            b.insert(b.end(), o.begin(), o.end());
        }
        double err = 0, seam = a.size() == b.size() ? 0 : 1;
        for (size_t i = 0; i < a.size() && i < b.size(); i++) seam = std::max(seam, double(std::fabs(a[i] - b[i])));
        for (size_t n = 2; n < a.size() / 2; n++)  // output frame n sits at input frame n*step - 1
            err = std::max(err, std::fabs(a[2 * n] - std::sin(2 * M_PI * 1000 * (n * double(in) / SR - 1) / in)));
        check(std::abs(long(a.size() / 2) - SR) <= 2 && err < 0.01 && seam < 1e-4, "resampler: 48 kHz to 44.1 kHz, accurate and chunk-seamless",
              fmt("%.0f frames, max error %.4f, chunked vs whole %.1e", double(a.size() / 2), err, seam));
        Resampler same(SR);
        float o[4];
        check(same.run(src.data(), 2, o) == 2 && o[0] == src[0] && o[2] == src[2], "resampler: 44.1 kHz passes through unchanged");
    }
}

// Child processes: arguments survive exactly (no shell, Windows quoting
// rules), exit codes are reported, and a child that won't end is stopped.
static void testProcess(const std::string &self) {
    const std::vector<std::string> tricky = {"plain", "with space", "quote\"inside", "trail\\", "back\\\\slash\"q", "", "\xc3\xbc\xe2\x82\xac", "-i"};
    std::vector<std::string> argv = {self, "--echo-args"};
    argv.insert(argv.end(), tricky.begin(), tricky.end());
    sys::Process p;
    std::string out;
    bool started = p.start(argv, true);
    char b[256];
    for (long n; started && (n = p.read(b, sizeof b)) != 0;)
        if (n > 0) out.append(b, size_t(n));
    std::string want;
    for (auto &a : tricky) want += a + "\n";
    check(started && out == want && p.wait(2000) && !p.failed(), "process: arguments arrive exactly, output read to its end", std::to_string(out.size()) + " bytes");

#ifndef _WIN32
    {
        sys::Process reuse;
        bool ok = true;
        for (int i = 0; i < 40; i++) {
            ok &= reuse.startShell("printf x", true);
            ok &= reuse.wait(2000) && reuse.open();  // waiting preserves unread output
            reuse.stop();
            ok &= !reuse.open();
        }
        std::string text;
        const bool startedAgain = reuse.startShell("printf preserved", true);
        ok &= startedAgain && reuse.wait(2000) && reuse.poll();
        for (long n; startedAgain && (n = reuse.read(b, sizeof b)) != 0;)
            if (n > 0) text.append(b, size_t(n));
        check(ok && text == "preserved" && !reuse.open(), "process stop releases unread output; reuse and wait-before-read preserve output");
    }
#endif

    sys::Process e;
    check(e.startShell("exit 3", false) && e.wait(5000) && e.failed() && e.status() == 3, "process: shell line exit status reported", std::to_string(e.status()));

    sys::Process slow;
#ifdef _WIN32
    const bool ok = slow.startShell("ping -n 30 127.0.0.1 >NUL", false);
#else
    const bool ok = slow.startShell("sleep 30", false);
#endif
    const auto t0 = std::chrono::steady_clock::now();
    slow.stop();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check(ok && secs < 3, "process: a command that never ends is stopped, bounded", fmt("%.2f s", secs));
}

int main(int argc, char **argv) {
    const std::vector<std::string> args = sys::args(argc, argv);
    if (args.size() > 1 && args[1] == "--echo-args") {  // child side of testProcess
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        for (size_t i = 2; i < args.size(); i++) fwrite(args[i].data(), 1, args[i].size(), stdout), fputc('\n', stdout);
        return 0;
    }
    if (args.size() > 1 && args[1] == "--audio-review") {
        testAudioPlumbing();
        testAnalysis();
        testLibrary();
        testMusic();
        printf("\n%s (%d failed)\n", failures ? "FAILED" : "ALL PASS", failures);
        return failures != 0;
    }
    testAudioPlumbing();
    testProcess(args[0]);
    testAnalysis();
    testDirector();
    testLibrary();
    testMusic();
    testScenePicker();
    testLayout();
    testKeys();
    testEncoders();
    testConfig();
    testReviewFixes();
    testSanitize();
    if (const char *real = getenv("GLISTENING_REAL")) reportReal(real);
    printf("\n%s (%d failed)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures != 0;
}
