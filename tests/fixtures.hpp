// Synthetic audio with known ground truth, shared by the unit tests and the render gate.
#pragma once
#include "../analysis.hpp"

#include <functional>
#include <random>
#include <vector>

namespace tv {

using Audio = std::vector<float>;  // interleaved stereo

inline Audio render(double secs, const std::function<float(double)> &fn) {
    Audio a(size_t(secs * SR) * 2);
    for (size_t i = 0; i < a.size() / 2; i++) a[2 * i] = a[2 * i + 1] = fn(double(i) / SR);
    return a;
}

inline std::mt19937 noiseRng(7);
inline float noise() { return std::uniform_real_distribution<float>(-1, 1)(noiseRng); }

inline float kick(double t) {  // pitch-swept sine, 50 Hz body
    if (t < 0 || t > 0.3) return 0;
    const double ph = 2 * M_PI * (50 * t + 70 * 0.03 * (1 - std::exp(-t / 0.03)));
    return float(0.8 * std::exp(-t / 0.12) * std::sin(ph));
}
inline float hatPrev = 0;
inline float hat(double t) {  // differentiated noise burst
    if (t < 0 || t > 0.04) return 0;
    float n = noise(), v = n - hatPrev;
    hatPrev = n;
    return float(0.12 * std::exp(-t / 0.01)) * v;
}
// Kick on every beat (optional), hats on eighths, optional sustained bass.
inline float groove(double t, double bpm, bool kicks, bool bass) {
    const double beat = 60 / bpm, tb = std::fmod(t, beat), te = std::fmod(t, beat / 2);
    return (kicks ? kick(tb) : 0) + hat(te) + (bass ? float(0.3 * std::sin(2 * M_PI * 55 * t)) : 0);
}

// Render-gate track: silence, a quiet breakdown (hats + pad), then a drop with
// kicks, bass and a moving arpeggio. 128 BPM, drop on a bar line.
constexpr double SONG_BPM = 128, SONG_QUIET = 3, SONG_DROP = 3 + 16 * 60 / 128.0;
inline float song(double t) {
    if (t < SONG_QUIET) return 0;
    const double B = 60 / SONG_BPM, elapsed = t - SONG_QUIET;
    const double tb = std::fmod(elapsed, B), te = std::fmod(elapsed, B / 2), ts = std::fmod(elapsed, B / 4);
    float x = 0.6f * hat(te) + float(0.03 * (std::sin(2 * M_PI * 220 * t) + std::sin(2 * M_PI * 277.2 * t) + std::sin(2 * M_PI * 329.6 * t)));
    if (t >= SONG_DROP) {
        static const double arp[] = {440, 554.4, 659.3, 880, 659.3, 554.4, 493.9, 587.3};
        const long step = long(elapsed / (B / 4));
        x += kick(tb) + float(0.28 * std::sin(2 * M_PI * 55 * t) * (0.6 + 0.4 * std::exp(-tb / 0.2)));
        x += float(0.12 * std::exp(-ts / 0.08) * std::sin(2 * M_PI * arp[step % 8] * t));
    }
    return 0.8f * x;
}

}  // namespace tv
