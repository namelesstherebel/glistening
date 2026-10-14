// Audio plumbing shared by the native capture and playback backends: a
// lock-free single-producer/single-consumer stereo ring, and conversion of
// any device format to the stereo 44.1 kHz float stream the analysis expects.
#pragma once
#include "analysis.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace tv {

// Stereo float frames from one thread (an audio callback) to another (the
// render loop). Neither side blocks or allocates after construction: a full
// ring drops the newest frames (counted), an empty one returns what it has.
class Ring {
public:
    explicit Ring(size_t frames) : buf_(2 * (frames + 1)), cap_(frames + 1) {}
    size_t push(const float *stereo, size_t frames) {
        const size_t w = w_.load(std::memory_order_relaxed), r = r_.load(std::memory_order_acquire);
        const size_t room = (r + cap_ - w - 1) % cap_, n = std::min(frames, room);
        for (size_t i = 0; i < n; i++) {
            const size_t at = (w + i) % cap_;
            buf_[2 * at] = stereo[2 * i], buf_[2 * at + 1] = stereo[2 * i + 1];
        }
        w_.store((w + n) % cap_, std::memory_order_release);
        if (n < frames) dropped_.fetch_add(frames - n, std::memory_order_relaxed);
        return n;
    }
    size_t pop(float *stereo, size_t frames) {
        const size_t r = r_.load(std::memory_order_relaxed), w = w_.load(std::memory_order_acquire);
        const size_t n = std::min(frames, (w + cap_ - r) % cap_);
        for (size_t i = 0; i < n; i++) {
            const size_t at = (r + i) % cap_;
            stereo[2 * i] = buf_[2 * at], stereo[2 * i + 1] = buf_[2 * at + 1];
        }
        r_.store((r + n) % cap_, std::memory_order_release);
        return n;
    }
    size_t size() const { return (w_.load(std::memory_order_acquire) + cap_ - r_.load(std::memory_order_acquire)) % cap_; }
    size_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    std::vector<float> buf_;
    size_t cap_;
    std::atomic<size_t> w_{0}, r_{0}, dropped_{0};
};

// Device frames (any channel count, interleaved float) to stereo frames:
// mono is duplicated; front left/right are kept and a centre channel (5.1 /
// 7.1 order: L R C ...) is folded in at -3 dB so dialogue and vocals count.
inline void downmix(const float *in, int channels, int frames, float *stereo) {
    for (int i = 0; i < frames; i++) {
        const float *f = in + size_t(i) * channels;
        float l = f[0], r = channels > 1 ? f[1] : f[0];
        if (channels >= 6) l += 0.7071f * f[2], r += 0.7071f * f[2];
        stereo[2 * i] = l, stereo[2 * i + 1] = r;
    }
}

// Stereo at any rate to stereo at SR, by linear interpolation. Good enough for
// analysis and visuals (slight top-octave roll-off); not meant for listening.
// Streaming: state carries across calls, so chunk boundaries are seamless.
class Resampler {
public:
    explicit Resampler(double inRate = SR) : step_(inRate / SR) {}
    double rate() const { return step_ * SR; }
    // Output frames that `frames` more input can produce, at most.
    int maxOut(int frames) const { return int(std::ceil((frames + 1) / step_)) + 1; }
    // Appends to out (room for maxOut(frames) frames); returns frames written.
    int run(const float *in, int frames, float *out) {
        if (frames <= 0) return 0;
        if (step_ == 1.0) return std::copy(in, in + 2 * size_t(frames), out), frames;
        int n = 0;
        auto at = [&](int k, int c) { return k == 0 ? prev_[c] : in[2 * size_t(k - 1) + c]; };  // k = 0 is the last frame of the previous call
        while (pos_ < frames) {
            const int k = int(pos_);
            const float f = float(pos_ - k);
            for (int c = 0; c < 2; c++) out[2 * n + c] = at(k, c) + (at(k + 1, c) - at(k, c)) * f;
            n++, pos_ += step_;
        }
        pos_ -= frames;
        prev_[0] = in[2 * size_t(frames) - 2], prev_[1] = in[2 * size_t(frames) - 1];
        return n;
    }

private:
    double step_, pos_ = 0;
    float prev_[2] = {0, 0};
};

}  // namespace tv
