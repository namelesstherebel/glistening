// Frame-rate view of the analysis, eased for visuals: smoothed levels, shaped
// beat/downbeat/drop accents, a music-driven clock, spectrum + history, waveform.
#pragma once
#include "analysis.hpp"

#include <vector>

namespace tv {

struct Music {
    static constexpr int WAVE = 1024, HIST = 96;  // waveform samples; spectrum history rows

    float bass = 0, mid = 0, high = 0, energy = 0, energySlow = 0;
    float beat = 0, downbeat = 0, drop = 0;  // accents: ~30 ms rise, tempo-scaled fall
    float beatPhase = 0, barPhase = 0, bpm = 0, conf = 0;
    bool silent = true;
    double time = 0, flow = 0, flowDelta = 0;  // flow: integral of a speed that follows the music
    std::array<float, BANDS> spectrum{};
    std::vector<float> wave = std::vector<float>(WAVE), history = std::vector<float>(BANDS * HIST);
    int histRow = 0;
    long histPos = 0;  // rows pushed so far
    long beats = 0;

    // f: latest hop with OR-ed accents. beatCount: accumulated events, or -1 to count f.beat.
    // gain: user sensitivity.
    void update(const Features &f, const float *mono, float dt, float gain = 1, long beatCount = -1) {
        time += dt;
        const float period = f.bpm > 0 ? 60 / f.bpm : 0.5f;
        // An event lands somewhere inside this frame: age it by at most its rise, so the
        // accent peaks on the frame it arrives even at low frame rates.
        sinceBeat_ += dt, sinceDown_ += dt, sinceDrop_ += dt;
        if (f.beat) sinceBeat_ = std::min(dt, 0.03f);
        beats += beatCount < 0 ? long(f.beat) : beatCount;
        if (f.downbeat) sinceDown_ = std::min(dt, 0.04f);
        if (f.drop) sinceDrop_ = std::min(dt, 0.08f);
        beat = env(sinceBeat_, 0.03f, std::max(0.08f, 0.3f * period));
        downbeat = env(sinceDown_, 0.04f, std::max(0.2f, 0.9f * period));
        drop = env(sinceDrop_, 0.08f, 2.5f);

        auto follow = [&](float &v, float target, float up, float down) {
            target = std::clamp(target * gain, 0.f, 1.f);
            v += (target - v) * (1 - std::exp(-dt / (target > v ? up : down)));
        };
        follow(bass, f.silent ? 0 : f.bass, 0.03f, 0.22f);
        follow(mid, f.silent ? 0 : f.mid, 0.05f, 0.3f);
        follow(high, f.silent ? 0 : f.high, 0.02f, 0.15f);
        follow(energy, f.silent ? 0 : f.energy, 0.05f, 0.4f);
        follow(energySlow, f.silent ? 0 : f.energySlow, 0.5f, 1.5f);
        for (int b = 0; b < BANDS; b++) follow(spectrum[b], f.spectrum[b], 0.02f, 0.18f);
        beatPhase = f.beatPhase, barPhase = f.barPhase, bpm = f.bpm, conf = f.conf, silent = f.silent;

        flowDelta = dt * (0.35 + 0.8 * energySlow + 0.5 * bass + 0.4 * beat);
        flow += flowDelta;

        // History rows at a fixed 30 Hz, so its ~3 s span holds at any frame rate.
        for (histDue_ += dt; histDue_ >= 0.999f / 30; histDue_ -= 1 / 30.f) {
            histRow = (histRow + 1) % HIST, histPos++;
            std::copy(spectrum.begin(), spectrum.end(), history.begin() + histRow * BANDS);
        }

        if (mono) {  // waveform normalised by a slow peak follower
            float pk = 0;
            for (int i = 0; i < WAVE; i++) pk = std::max(pk, std::abs(mono[i]));
            wavePeak_ = std::max(pk, wavePeak_ * std::exp(-dt / 2.f));
            const float g = 0.9f / std::max(wavePeak_, 0.02f);
            for (int i = 0; i < WAVE; i++) wave[i] = std::clamp(mono[i] * g, -1.f, 1.f);
        }
    }

private:
    float sinceBeat_ = 99, sinceDown_ = 99, sinceDrop_ = 99, wavePeak_ = 0, histDue_ = 0;
    static float env(float t, float rise, float fall) {
        return t < rise ? t / rise : std::exp(-(t - rise) / fall);
    }
};

// Analyzer + Music: feed PCM as it arrives, then step once per video frame.
struct Listener {
    Analyzer an;
    Features feat;  // latest hop, beat/downbeat/drop/section OR-ed since the last frame
    Music music;
    std::vector<float> mono = std::vector<float>(Music::WAVE);

    template <class F>
    void feed(const float *lr, int frames, F &&onHop) {
        an.process(lr, frames, [&](const Features &f) {
            const bool beat = feat.beat || f.beat, down = feat.downbeat || f.downbeat, drop = feat.drop || f.drop, sec = feat.section || f.section;
            pendingBeats_ += f.beat;
            feat = f;
            feat.beat = beat, feat.downbeat = down, feat.drop = drop, feat.section = sec;
            onHop(f);
        });
    }
    void frame(float dt, float gain = 1) {
        an.waveform(mono.data(), Music::WAVE);
        music.update(feat, mono.data(), dt, gain, pendingBeats_);
        pendingBeats_ = 0;
        feat.beat = feat.downbeat = feat.drop = feat.section = false;
    }

private:
    long pendingBeats_ = 0;
};

}  // namespace tv
