// Auto-director: decides when to change preset (musical events) and which
// preset fits the moment (learned per-preset motion vs current energy).
#pragma once
#include "analysis.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace tv {

struct DirectorConfig {
    double minHold = 8;       // seconds before a phrase/section cut is allowed
    double sectionHold = 12;  // section changes cut only after this
    double dropHold = 4;      // drops hard-cut after this
    double maxHold = 60;      // force a cut (on a beat) after this
    double fallback = 30;     // no usable tempo: plain timer
    int phraseBeats = 64;     // cut on the first downbeat after this many beats
    float minConf = 0.25f;    // tempo confidence needed to trust beats
};

enum class Why { None, Phrase, Section, Drop, Timeout, Fallback };
inline const char *whyName(Why w) {
    static const char *n[] = {"", "phrase", "section", "drop", "timeout", "fallback"};
    return n[int(w)];
}

struct Cut {
    Why why = Why::None;
    bool hard = false;
    explicit operator bool() const { return why != Why::None; }
};

class Director {
public:
    explicit Director(DirectorConfig c = {}) : c_(c) {}
    bool locked = false;

    Cut update(const Features &f) {
        if (lastCut_ < -1e8) lastCut_ = f.t;
        if (f.section) sectionBeats_ = 8;  // a section change stays "pending" for 8 beats
        if (f.silent) return {};
        if (f.beat) beats_++, sectionBeats_ = std::max(0, sectionBeats_ - 1);
        if (locked) return {};
        const double since = f.t - lastCut_;
        Cut c;
        if (f.drop && since >= c_.dropHold) c = {Why::Drop, true};
        else if (f.conf >= c_.minConf) {
            if (f.downbeat && sectionBeats_ > 0 && since >= c_.sectionHold) c = {Why::Section, false};
            else if (f.downbeat && beats_ >= c_.phraseBeats && since >= c_.minHold) c = {Why::Phrase, false};
            else if (f.beat && since >= c_.maxHold) c = {Why::Timeout, false};
        } else if (since >= c_.fallback) c = {Why::Fallback, false};
        if (c) cut(f.t);
        return c;
    }
    void cut(double t) { lastCut_ = t, beats_ = 0, sectionBeats_ = 0; }
    int beats() const { return beats_; }
    double lastCut() const { return lastCut_; }
    const DirectorConfig &config() const { return c_; }

private:
    DirectorConfig c_;
    double lastCut_ = -1e9;
    int beats_ = 0, sectionBeats_ = 0;
};

struct PresetInfo {
    std::string name;
    float motion = 0, bright = 0;  // running means, measured while the preset plays
    int n = 0;                     // frames observed
    int flag = 0;                  // 1 favourite, -1 banned, -2 failed to load
};

class Library {
public:
    std::vector<PresetInfo> p;

    explicit Library(std::vector<std::string> names) {
        for (auto &n : names) p.push_back({n});
    }

    // TSV: name, motion, bright, n, flag. Unknown names are ignored.
    void load(const std::string &path) {
        std::unordered_map<std::string, int> idx;
        for (size_t i = 0; i < p.size(); i++) idx[p[i].name] = int(i);
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream s(line);
            std::string name;
            PresetInfo e;
            if (std::getline(s, name, '\t') && s >> e.motion >> e.bright >> e.n >> e.flag) {
                auto it = idx.find(name);
                if (it != idx.end()) e.name = name, p[it->second] = e;
            }
        }
    }
    void save(const std::string &path) const {
        std::ofstream out(path);
        for (auto &e : p)
            if (e.n || e.flag) out << e.name << '\t' << e.motion << '\t' << e.bright << '\t' << e.n << '\t' << e.flag << '\n';
    }

    void observe(int i, float motion, float bright) {
        auto &e = p[i];
        const float w = 1.f / std::min(++e.n, 600);  // running mean, then slow EMA
        e.motion += (motion - e.motion) * w;
        e.bright += (bright - e.bright) * w;
    }

    void played(int i) {
        recent_.push_back(i);
        if (recent_.size() > 40) recent_.erase(recent_.begin());
    }

    // energy 0..1: low picks calm presets, high picks busy ones. Unmeasured
    // presets get a neutral score so the library keeps exploring.
    template <class Rng>
    int pick(float energy, Rng &rng) const {
        if (p.empty()) return -1;
        std::vector<float> known;
        for (auto &e : p)
            if (e.n >= 60 && e.flag >= 0) known.push_back(e.motion);
        std::sort(known.begin(), known.end());
        std::uniform_int_distribution<int> any(0, int(p.size()) - 1);
        std::uniform_real_distribution<float> jitter(0.85f, 1.15f);
        int best = -1;
        float bestS = -1;
        for (int tries = 0; tries < 64; tries++) {
            const int i = any(rng);
            const auto &e = p[i];
            if (e.flag < 0 || std::find(recent_.begin(), recent_.end(), i) != recent_.end()) continue;
            float s = 0.55f;
            if (e.n >= 60 && known.size() > 1) {
                const float rank = float(std::lower_bound(known.begin(), known.end(), e.motion) - known.begin()) / (known.size() - 1);
                s = 1 - std::abs(rank - energy);
                if (e.bright < 0.05f) s *= 0.3f;  // mostly-black presets
            }
            if (e.flag > 0) s *= 1.4f;
            s *= jitter(rng);
            if (s > bestS) bestS = s, best = i;
        }
        if (best >= 0) return best;
        // The probes missed: prefer any usable preset not played recently, then any usable one.
        std::vector<int> fresh, usable;
        for (size_t i = 0; i < p.size(); i++)
            if (p[i].flag >= 0) {
                usable.push_back(int(i));
                if (std::find(recent_.begin(), recent_.end(), int(i)) == recent_.end()) fresh.push_back(int(i));
            }
        const auto &from = fresh.empty() ? usable : fresh;
        if (from.empty()) return -1;  // every preset is banned or broken
        return from[std::uniform_int_distribution<size_t>(0, from.size() - 1)(rng)];
    }
    // Uniformly among the usable presets; -1 if there are none.
    template <class Rng>
    int random(Rng &rng) const {
        std::vector<int> ok;
        for (size_t i = 0; i < p.size(); i++)
            if (p[i].flag >= 0) ok.push_back(int(i));
        return ok.empty() ? -1 : ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng)];
    }

private:
    std::vector<int> recent_;
};

// Which scene next: busy rating close to the music's energy, never one of the
// last few shown, with jitter so equal-energy stretches still rotate.
class ScenePicker {
public:
    std::vector<float> busy;  // per scene, 0 calm .. 1 busy
    size_t avoid = 2;         // recent scenes excluded

    template <class Rng>
    int pick(float energy, Rng &rng) const {
        std::uniform_real_distribution<float> jitter(0, 0.3f);
        int best = -1;
        float bestS = -1e9f;
        for (size_t i = 0; i < busy.size(); i++) {
            const size_t window = std::min({avoid, recent_.size(), busy.size() - 1});  // always leave one other scene
            if (std::find(recent_.end() - window, recent_.end(), int(i)) != recent_.end()) continue;
            const float s = -std::abs(busy[i] - energy) + jitter(rng);
            if (s > bestS) bestS = s, best = int(i);
        }
        return best >= 0 ? best : (recent_.empty() ? 0 : recent_.back());
    }
    void played(int i) {
        recent_.push_back(i);
        if (recent_.size() > 16) recent_.erase(recent_.begin());
    }
    int last() const { return recent_.empty() ? -1 : recent_.back(); }
    void remap(const std::vector<int> &m) {  // scene indices changed (shader reload)
        std::vector<int> r;
        for (int i : recent_)
            if (i >= 0 && i < (int)m.size() && m[i] >= 0) r.push_back(m[i]);
        recent_ = r;
    }

private:
    std::vector<int> recent_;
};

// Energy 0..1 the music asks for; drops want the busiest scenes.
inline float sceneEnergy(float energySlow, bool drop) { return drop ? 1.f : std::clamp((energySlow - 0.25f) / 0.6f, 0.f, 1.f); }

}  // namespace tv
