// Real-time music analysis: band levels, onsets, tempo + beat clock, drops,
// section changes. Pure C++, no I/O, so tests drive it with synthetic audio.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace tv {

constexpr int SR = 44100, N = 2048, HOP = 512;
constexpr double HPS = double(SR) / HOP;  // hops per second
constexpr float BPM_MIN = 60, BPM_MAX = 200, BPM_STEP = 0.25f;
constexpr int BANDS = 64;  // log-spaced spectrum bands, 30 Hz .. 16 kHz

inline void fft(std::vector<std::complex<float>> &a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / len;
        for (size_t k = 0; k < len / 2; k++) {
            const std::complex<float> w(std::cos(ang * k), std::sin(ang * k));
            for (size_t i = 0; i < n; i += len) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

inline float alpha(double tauSec) { return float(1 - std::exp(-1 / (tauSec * HPS))); }
inline float db(double power) { return float(10 * std::log10(power + 1e-10)); }

struct Features {
    double t = 0;  // audio seconds, at the analysis window centre
    float bpm = 0, conf = 0;  // conf: normalised autocorrelation at the beat period
    long beatIndex = -1;
    bool beat = false, downbeat = false, onset = false, drop = false, section = false, silent = true;
    float bass = 0, mid = 0, high = 0;  // 0..1, auto-gained per band
    float energy = 0, energySlow = 0;   // 0..1, auto-gained loudness (fast / ~4s)
    float beatPhase = 0, barPhase = 0;  // 0..1 ramps: since last beat, since last downbeat (4 beats)
    std::array<float, BANDS> spectrum{};  // 0..1 per band, tilt-compensated, one shared auto-gain
};

class Analyzer {
public:
    Analyzer() : win_(N), ring_(N), buf_(N), prevLog_(N / 2), env_(ENV_LEN), bassHist_(BASS_LEN, -100) {
        for (int i = 0; i < N; i++) win_[i] = float(0.5 - 0.5 * std::cos(2 * M_PI * i / N));
    }

    // lr: interleaved stereo. onHop(const Features&) fires once per hop (~11.6ms).
    template <class F>
    void process(const float *lr, int frames, F &&onHop) {
        for (int i = 0; i < frames; i++) {
            float m = 0.5f * (lr[2 * i] + lr[2 * i + 1]);
            ring_[pos_] = std::isfinite(m) ? std::clamp(m, -4.f, 4.f) : 0.f;
            pos_ = (pos_ + 1) % N;
            samples_++;
            if (++hopFill_ == HOP) {
                hopFill_ = 0;
                hop();
                onHop(f_);
            }
        }
    }
    const Features &last() const { return f_; }
    // The newest n (<= N) mono samples, oldest first.
    void waveform(float *out, int n) const {
        for (int i = 0; i < n; i++) out[i] = ring_[(pos_ - n + i + N) % N];
    }

private:
    static constexpr int ENV_LEN = int(6 * HPS);   // onset envelope kept for tempo
    static constexpr int BASS_LEN = int(3 * HPS);  // bass history for drop detection
    static constexpr float LATENCY = 0.0f;         // seconds; calibrated by tests

    std::vector<float> win_, ring_;
    std::vector<std::complex<float>> buf_;
    std::vector<float> prevLog_, env_, bassHist_;
    int pos_ = 0, hopFill_ = 0, envPos_ = 0, envFilled_ = 0, bassPos_ = 0;
    std::int64_t samples_ = 0;
    long hops_ = 0;
    Features f_;

    float fluxPrev1_ = 0, fluxPrev2_ = 0, fluxPeak_ = 1e-3f;
    long lastOnsetHop_ = -100;
    float powRms_ = 0, powBass_ = 0, powMid_ = 0, powHigh_ = 0, powBassFast_ = 0;
    float pkRms_ = -100, pkBass_ = -100, pkMid_ = -100, pkHigh_ = -100, pkSpec_ = -100;
    float fastVec_[3] = {}, slowVec_[3] = {};
    bool novelHigh_ = false;
    int loudHops_ = 0;  // consecutive non-silent hops; section detection warms up first
    double lastDrop_ = -1e9, lastSection_ = -1e9;

    float candBpm_ = 0;
    int candCount_ = 0;
    double nextBeat_ = -1, lastBeatT_ = -1e9;
    double barAnchorT_ = -1;  // a time on a downbeat; bars count from here

    // age 0 = newest hop; fractional ages interpolate.
    float env(double age) const {
        int a = int(age);
        float fr = float(age - a);
        auto at = [&](int k) { return env_[((envPos_ - 1 - k) % ENV_LEN + ENV_LEN) % ENV_LEN]; };
        return a + 1 < envFilled_ ? at(a) * (1 - fr) + at(a + 1) * fr : 0.f;
    }

    // Normalised level of a band against its own slowly decaying peak (AGC).
    static float level(float d, float &peak) {
        peak = std::max(d, peak - float(3.0 / HPS));  // peak decays 3 dB/s
        return std::clamp((d - (peak - 30)) / 30, 0.f, 1.f);
    }

    void hop() {
        hops_++;
        Features &f = f_;
        f.beat = f.downbeat = f.onset = f.drop = f.section = false;
        f.t = double(samples_ - N / 2) / SR - LATENCY;

        double hopPow = 0;
        for (int i = 0; i < N; i++) {
            float s = ring_[(pos_ + i) % N];
            buf_[i] = s * win_[i];
            if (i >= N - HOP) hopPow += s * s;
        }
        hopPow /= HOP;
        fft(buf_);

        // Bands at 21.5 Hz/bin: bass 43-150, mid 150-2k, high 2k-16k.
        // Flux is averaged per band, then the bands weigh equally, so a few bass
        // bins (kicks) count as much as hundreds of hi-hat bins.
        double eb = 0, em = 0, eh = 0, fb = 0, fm = 0, fh = 0;
        const float norm = 4.f / N;  // sine of amplitude A -> magnitude A
        for (int k = 2; k < 744; k++) {
            float m = std::abs(buf_[k]) * norm, p = m * m;
            float lg = std::log1p(1000 * m), up = std::max(0.f, lg - prevLog_[k]);
            prevLog_[k] = lg;
            if (k < 7) eb += p, fb += up;
            else if (k < 93) em += p, fm += up;
            else eh += p, fh += up;
        }
        const double flux = (fb / 5 + fm / 86 + fh / 651) / 3;
        spectrum(norm);

        const float aFast = alpha(0.15), aRms = alpha(0.3), aSlow = alpha(4);
        powRms_ += (float(hopPow) - powRms_) * aRms;
        powBass_ += (float(eb) - powBass_) * aFast;
        powMid_ += (float(em) - powMid_) * aFast;
        powHigh_ += (float(eh) - powHigh_) * aFast;
        powBassFast_ += (float(eb) - powBassFast_) * alpha(0.05);

        const float rmsDb = db(powRms_);
        f.silent = rmsDb < -55;
        f.bass = level(db(powBass_), pkBass_);
        f.mid = level(db(powMid_), pkMid_);
        f.high = level(db(powHigh_), pkHigh_);
        f.energy = level(rmsDb, pkRms_);
        f.energySlow += (f.energy - f.energySlow) * aSlow;

        // Onset envelope + peak picking (onset reported one hop late).
        env_[envPos_] = float(flux);
        envPos_ = (envPos_ + 1) % ENV_LEN;
        envFilled_ = std::min(envFilled_ + 1, ENV_LEN);
        fluxPeak_ = std::max(float(flux), fluxPeak_ * (1 - alpha(10)));
        float mean = 0;
        const int win = std::min(envFilled_, 20);
        for (int k = 0; k < win; k++) mean += env(k);
        mean /= std::max(win, 1);
        if (fluxPrev1_ > fluxPrev2_ && fluxPrev1_ >= flux && fluxPrev1_ > 1.4f * mean + 0.05f * fluxPeak_ &&
            hops_ - lastOnsetHop_ > 4 && !f.silent) {
            f.onset = true;
            lastOnsetHop_ = hops_;
        }
        fluxPrev2_ = fluxPrev1_;
        fluxPrev1_ = float(flux);

        if (hops_ % 43 == 0 && envFilled_ >= int(3 * HPS)) tempo();
        if (f.silent) f.conf *= 1 - alpha(1);
        beatClock();
        drops(db(powBassFast_));
        sections();
    }

    // Log bands; bands narrower than a bin interpolate. +3 dB/octave tilt so a
    // typical (pink-ish) mix reads level across the range.
    void spectrum(float norm) {
        float d[BANDS], peak = -100;
        for (int b = 0; b < BANDS; b++) {
            const double lo = 30 * std::pow(16000 / 30.0, double(b) / BANDS), hi = 30 * std::pow(16000 / 30.0, double(b + 1) / BANDS);
            const double klo = lo / (double(SR) / N), khi = hi / (double(SR) / N);
            double p = 0;
            int cnt = 0;
            for (int k = int(std::ceil(klo)); k < khi && k < N / 2; k++, cnt++) p += std::norm(buf_[k]);
            if (cnt) p /= cnt;
            else {
                const double c = 0.5 * (klo + khi);
                const int k = int(c);
                const double fr = c - k;
                p = std::norm(buf_[k]) * (1 - fr) + std::norm(buf_[k + 1]) * fr;
            }
            d[b] = db(p * norm * norm) + 3 * float(std::log2(std::sqrt(lo * hi) / 1000));
            peak = std::max(peak, d[b]);
        }
        pkSpec_ = std::max(peak, pkSpec_ - float(3.0 / HPS));
        for (int b = 0; b < BANDS; b++) f_.spectrum[b] = f_.silent ? 0 : std::clamp((d[b] - (pkSpec_ - 45)) / 45, 0.f, 1.f);
    }

    // Autocorrelation of the onset envelope over candidate periods, with a
    // log-normal tempo prior around 120 BPM to settle octave ambiguity.
    void tempo() {
        const int n = envFilled_;
        std::vector<float> e(n);
        float mean = 0;
        for (int i = 0; i < n; i++) mean += e[i] = env(n - 1 - i);  // oldest -> newest
        mean /= n;
        for (auto &v : e) v -= mean;
        auto lerp = [&](double x) {
            int a = int(x);
            float fr = float(x - a);
            return a + 1 < n ? e[a] * (1 - fr) + e[a + 1] * fr : 0.f;
        };
        auto ac = [&](double lag) {
            double s = 0;
            int cnt = 0;
            for (int i = 0; i + lag + 1 < n; i++, cnt++) s += e[i] * lerp(i + lag);
            return cnt ? s / cnt : 0.0;
        };
        const double ac0 = ac(0) + 1e-12;
        const int nc = int((BPM_MAX - BPM_MIN) / BPM_STEP) + 1;
        std::vector<double> score(nc), raw(nc);
        int best = 0;
        for (int c = 0; c < nc; c++) {
            const double bpm = BPM_MIN + c * BPM_STEP, lag = 60 * HPS / bpm;
            const double oct = std::log2(bpm / 120) / 0.9;
            raw[c] = ac(lag);
            score[c] = std::exp(-0.5 * oct * oct) * (raw[c] + 0.5 * ac(2 * lag));
            if (score[c] > score[best]) best = c;
        }
        double off = 0;  // parabolic refinement
        if (best > 0 && best < nc - 1) {
            const double a = score[best - 1], b = score[best], c = score[best + 1], d = a - 2 * b + c;
            if (d < 0) off = std::clamp(0.5 * (a - c) / d, -0.5, 0.5);
        }
        const float bpm = float(BPM_MIN + (best + off) * BPM_STEP);
        const float conf = float(std::clamp(raw[best] / ac0, 0.0, 1.0));
        f_.conf += (conf - f_.conf) * 0.5f;

        // Hysteresis: small drift is smoothed; a jump needs 3 agreeing estimates;
        // weak periodicity never moves the tempo (avoids wandering on beatless music).
        if (conf < 0.12f && f_.bpm > 0) {
        } else if (f_.bpm == 0 || std::abs(bpm - f_.bpm) < 2) {
            f_.bpm = f_.bpm == 0 ? bpm : f_.bpm * 0.7f + bpm * 0.3f;
            candCount_ = 0;
        } else if (std::abs(bpm - candBpm_) < 2 && ++candCount_ >= 3) {
            f_.bpm = bpm;
            candCount_ = 0;
        } else if (std::abs(bpm - candBpm_) >= 2) {
            candBpm_ = bpm;
            candCount_ = 1;
        }
        phase();
    }

    // Find the beat phase that best lines up with recent onsets, then nudge the clock.
    void phase() {
        const double P = 60 * HPS / f_.bpm;
        double bestPhi = 0, bestC = -1;
        for (double phi = 0; phi < P; phi += 0.25) {
            double c = 0, w = 1;
            for (double age = phi; age + 1 < envFilled_ && w > 0.2; age += P, w *= 0.85) c += w * env(age);
            if (c > bestC) bestC = c, bestPhi = phi;
        }
        const double period = 60.0 / f_.bpm, predicted = f_.t - bestPhi / HPS + period;
        if (nextBeat_ < 0) {
            nextBeat_ = predicted;
            return;
        }
        double err = predicted - nextBeat_;
        err -= period * std::round(err / period);
        nextBeat_ += 0.5 * err;
    }

    void beatClock() {
        if (f_.bpm <= 0 || nextBeat_ < 0) return;
        const double period = 60.0 / f_.bpm;
        if (f_.t - nextBeat_ > period) nextBeat_ += period * std::floor((f_.t - nextBeat_) / period);
        updatePhase(period);
        if (f_.t < nextBeat_) return;
        nextBeat_ += period;
        if (f_.silent || f_.t - lastBeatT_ < 0.5 * period) return;
        lastBeatT_ = f_.t;
        f_.beat = true;
        f_.beatIndex++;
        if (barAnchorT_ < 0) barAnchorT_ = f_.t;
        f_.downbeat = (std::lround((f_.t - barAnchorT_) / period) % 4 + 4) % 4 == 0;
        updatePhase(period);
    }
    void updatePhase(double period) {
        f_.beatPhase = float(std::clamp(1 - (nextBeat_ - f_.t) / period, 0.0, 1.0));
        if (f_.t - lastBeatT_ > 2 * period) f_.beatPhase = 0;  // no recent beat (silence): rest at 0
        const long k = barAnchorT_ < 0 ? 0 : (std::lround((lastBeatT_ - barAnchorT_) / period) % 4 + 4) % 4;
        f_.barPhase = (k + f_.beatPhase) / 4;
    }

    // A drop starts a bar. Anchoring by time (not beat count)
    // survives the beat clock re-locking by half a beat.
    void anchorBar() { barAnchorT_ = f_.t; }

    // Drop: bass arrives >=12 dB above everything in the preceding 0.5-3s.
    void drops(float bassDb) {
        bassHist_[bassPos_] = bassDb;
        bassPos_ = (bassPos_ + 1) % BASS_LEN;
        if (hops_ < BASS_LEN || f_.silent || f_.t - lastDrop_ < 8) return;
        float before = -100;
        for (int age = int(0.5 * HPS); age < BASS_LEN; age++)
            before = std::max(before, bassHist_[((bassPos_ - 1 - age) % BASS_LEN + BASS_LEN) % BASS_LEN]);
        if (bassDb - before >= 12 && f_.bass > 0.6f) {
            f_.drop = true;
            lastDrop_ = f_.t;
            anchorBar();
        }
    }

    // Section change: short-term band balance departs from the long-term one.
    void sections() {
        if (f_.silent) {
            loudHops_ = 0;
            return;
        }
        const float v[3] = {std::max(db(powBass_), -60.f), std::max(db(powMid_), -60.f), std::max(db(powHigh_), -60.f)};  // floor: near-silent bands are noise in dB
        if (loudHops_++ == 0) std::copy(v, v + 3, fastVec_), std::copy(v, v + 3, slowVec_);
        float d2 = 0;
        for (int i = 0; i < 3; i++) {
            fastVec_[i] += (v[i] - fastVec_[i]) * alpha(1.5);
            slowVec_[i] += (v[i] - slowVec_[i]) * alpha(8);
            d2 += (fastVec_[i] - slowVec_[i]) * (fastVec_[i] - slowVec_[i]);
        }
        const bool high = std::sqrt(d2) > 8;
        if (high && !novelHigh_ && loudHops_ > 4 * HPS && f_.t - lastSection_ >= 8) {
            f_.section = true;  // detected ~1s late (averages), so it does not re-anchor bars
            lastSection_ = f_.t;
        }
        novelHigh_ = high;
    }
};

// Loudness normaliser for the PCM handed to projectM, so quiet tracks still drive it.
class Agc {
public:
    void apply(float *lr, int frames) {
        if (frames <= 0) return;
        double p = 0;
        for (int i = 0; i < 2 * frames; i++) {
            if (!std::isfinite(lr[i])) lr[i] = 0;
            p += lr[i] * lr[i];
        }
        p /= 2 * frames;
        const double dt = double(frames) / SR;
        if (db(p) > -60) {
            if (slow_ < 0) slow_ = p;
            const double tau = p > slow_ ? 0.1 : 3.0;
            slow_ += (p - slow_) * (1 - std::exp(-dt / tau));
            const float target = float(std::clamp(0.2 / std::sqrt(slow_ + 1e-12), 0.5, 8.0));
            gain_ += (target - gain_) * float(1 - std::exp(-dt / 0.2));
        }
        for (int i = 0; i < 2 * frames; i++) lr[i] = std::clamp(lr[i] * gain_, -1.f, 1.f);
    }
    float gain() const { return gain_; }

private:
    double slow_ = -1;  // seeded by the first non-silent block
    float gain_ = 1;
};

}  // namespace tv
