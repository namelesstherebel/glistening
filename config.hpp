// Settings (key=value file), curated palettes, and the Omarchy desktop palette.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace tv {

struct Palette {
    std::string name;
    unsigned c[5];  // sRGB 0xRRGGBB: deep, shadow, mid, accent, highlight
};

inline const std::vector<Palette> &palettes() {
    static const std::vector<Palette> p = {
        {"aurora", {0x02040a, 0x0f2140, 0x1b7f6a, 0x5ce6a6, 0xc9a7ff}},
        {"ember", {0x070302, 0x2a0c08, 0x8c2b12, 0xff7a2f, 0xffd9a0}},
        {"glacier", {0x02060b, 0x0d2233, 0x2e6f8e, 0x8fd3e8, 0xf0fbff}},
        {"dusk", {0x07040f, 0x251238, 0x6b2a5e, 0xe0735f, 0xffd1a6}},
        {"moss", {0x040604, 0x16251a, 0x3f6b45, 0xa9c97c, 0xeef2c8}},
        {"orchid", {0x06030a, 0x22102e, 0x6a2c7a, 0xd16fb4, 0xffd6ef}},
        {"indigo", {0x020308, 0x0e1630, 0x2a3f7a, 0xc9a24a, 0xf6e7c1}},
        {"silver", {0x030304, 0x15171b, 0x4a5059, 0xa7aeb8, 0xf2f4f7}},
        {"solar", {0x050302, 0x2b1606, 0xa3561a, 0xf2b544, 0xfff3c4}},
        {"abyss", {0x010507, 0x062a30, 0x0f6e74, 0xf08a6c, 0xffe3d6}},
        {"neon", {0x04030a, 0x170b2b, 0x3b1d6e, 0xff4fa3, 0x8ff7ff}},
        {"brass", {0x050302, 0x22160a, 0x6e4a1c, 0xd9a441, 0xfff0c8}},
        {"paper", {0x040506, 0x16191f, 0x5b5f66, 0xe8c9a0, 0xfff8ec}},
    };
    return p;
}

inline int paletteIndex(const std::string &name) {
    for (size_t i = 0; i < palettes().size(); i++)
        if (palettes()[i].name == name) return int(i);
    return -1;
}

inline void toLinear(const Palette &p, float out[15]) {
    for (int i = 0; i < 5; i++)
        for (int k = 0; k < 3; k++) out[i * 3 + k] = std::pow(((p.c[i] >> (16 - 8 * k)) & 255) / 255.f, 2.2f);
}

inline float luma(unsigned c) { return 0.2126f * (c >> 16 & 255) + 0.7152f * (c >> 8 & 255) + 0.0722f * (c & 255); }
inline unsigned mixc(unsigned a, unsigned b, float t) {
    unsigned r = 0;
    for (int s = 16; s >= 0; s -= 8) r |= unsigned(std::lround((a >> s & 255) * (1 - t) + (b >> s & 255) * t)) << s;
    return r;
}

// Omarchy theme colors.toml: `key = "#rrggbb"` lines (background, foreground, accent, color0..15).
// Anything unparseable is ignored; returns false if there is not enough to build a palette.
inline bool parseOmarchy(const std::string &toml, Palette &out) {
    std::map<std::string, unsigned> c;
    std::istringstream in(toml);
    for (std::string line; std::getline(in, line);) {
        const size_t eq = line.find('='), hash = line.find('#', eq == std::string::npos ? 0 : eq);
        if (eq == std::string::npos || hash == std::string::npos || hash + 7 > line.size()) continue;
        std::string key = line.substr(0, eq);
        key.erase(std::remove_if(key.begin(), key.end(), [](char ch) { return std::isspace((unsigned char)ch) || ch == '"'; }), key.end());
        const std::string hex = line.substr(hash + 1, 6);
        if (key.empty() || key[0] == '#' || !std::all_of(hex.begin(), hex.end(), [](char ch) { return std::isxdigit((unsigned char)ch); })) continue;
        c[key] = unsigned(std::stoul(hex, nullptr, 16));
    }
    auto get = [&](std::initializer_list<const char *> keys, unsigned &v) {
        for (auto k : keys)
            if (c.count(k)) return v = c[k], true;
        return false;
    };
    unsigned bg, fg, acc, a2, a3;
    if (!get({"background", "color0"}, bg) || !get({"accent", "color4", "color12"}, acc)) return false;
    if (!get({"foreground", "color15", "color7"}, fg)) fg = 0xf0f0f0;
    if (!get({"color5", "color13", "color6"}, a2)) a2 = acc;
    if (!get({"color6", "color14", "color2"}, a3)) a3 = acc;
    // Darker theme hue for shadow, brighter one for the accent stop.
    unsigned lo = luma(a2) < luma(a3) ? a2 : a3, hi = lo == a2 ? a3 : a2;
    out.name = "desktop";
    out.c[0] = mixc(bg, 0, 0.5f);
    out.c[1] = mixc(bg, lo, 0.35f);
    out.c[2] = mixc(acc, lo, 0.4f);
    out.c[3] = luma(hi) > luma(acc) ? hi : acc;
    out.c[4] = mixc(fg, out.c[3], 0.2f);
    if (luma(out.c[0]) > 90) out.c[0] = mixc(out.c[0], 0, 0.85f);  // light themes: keep the darks dark
    return true;
}

inline std::string homeDir() { return getenv("HOME") ? getenv("HOME") : "."; }
inline std::string omarchyPath() { return homeDir() + "/.local/state/omarchy/current/theme/colors.toml"; }

inline bool loadOmarchy(Palette &out) {
    std::ifstream in(omarchyPath());
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    return parseOmarchy(ss.str(), out);
}

// --dump OUT: "%d" or "%0Nd" in OUT is the frame number (a sequence); any other % is literal.
inline std::string frameName(const std::string &pat, unsigned frame) {
    for (size_t i = 0; i < pat.size(); i++) {
        if (pat[i] != '%') continue;
        size_t j = i + 1;
        while (j < pat.size() && std::isdigit((unsigned char)pat[j])) j++;
        if (j < pat.size() && pat[j] == 'd') {
            int width = 0;  // saturates: widths past 20 are capped anyway
            for (size_t k = i + 1; k < j; k++) width = std::min(100, width * 10 + (pat[k] - '0'));
            std::string n = std::to_string(frame);
            if ((int)n.size() < std::min(width, 20)) n.insert(0, std::min(width, 20) - n.size(), '0');
            return pat.substr(0, i) + n + pat.substr(j + 1);
        }
    }
    return pat;
}

struct Variant {
    float seed = 0, v[4] = {};
    float pal[15] = {};  // 5 linear RGB stops: deep, shadow, mid, accent, highlight
    std::string palette;
};

// New look for a scene appearance. setting: "auto" picks among the scene's own
// palettes, "desktop" uses the Omarchy theme when available, else a palette name.
template <class Rng>
Variant makeVariant(const std::vector<std::string> &scenePalettes, const std::string &setting, const Palette *desktop, Rng &rng) {
    std::uniform_real_distribution<float> u(0, 1);
    Variant v;
    v.seed = u(rng);
    for (float &x : v.v) x = u(rng);
    const Palette *p = nullptr;
    if (setting == "desktop" && desktop) p = desktop;
    else if (paletteIndex(setting) >= 0) p = &palettes()[paletteIndex(setting)];
    else {
        std::vector<int> ok;
        for (auto &n : scenePalettes)
            if (paletteIndex(n) >= 0) ok.push_back(paletteIndex(n));
        p = &palettes()[ok.empty() ? 0 : ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng)]];
    }
    toLinear(*p, v.pal);
    v.palette = p->name;
    return v;
}

struct Settings {
    std::string scene = "aurora";   // last scene shown
    std::string palette = "auto";   // auto (per scene), desktop, or a palette name
    bool director = true, milkdrop = false, hud = true, cellbg = false;
    float gain = 1, intensity = 1;  // sensitivity, effect intensity
    int scale = 2;                  // render at terminal pixels / scale
    int phrase = 64;                // beats per phrase cut

    std::string str() const {
        std::ostringstream o;
        o << "scene=" << scene << "\npalette=" << palette << "\ndirector=" << director << "\nmilkdrop=" << milkdrop
          << "\nhud=" << hud << "\ncellbg=" << cellbg << "\ngain=" << gain << "\nintensity=" << intensity << "\nscale=" << scale
          << "\nphrase=" << phrase << "\n";
        return o.str();
    }
    void parse(const std::string &text) {
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == '#') continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            while (!v.empty() && std::isspace((unsigned char)v.back())) v.pop_back();  // CRLF files, stray spaces
            char *end = nullptr;
            const double num = std::strtod(v.c_str(), &end);
            while (end && *end && std::isspace((unsigned char)*end)) end++;
            const bool isNum = end && end != v.c_str() && !*end && std::isfinite(num);  // the whole value, nothing trailing
            if (k == "scene" && !v.empty()) scene = v;
            else if (k == "palette" && !v.empty()) palette = v;
            else if (k == "director" && isNum) director = num != 0;
            else if (k == "milkdrop" && isNum) milkdrop = num != 0;
            else if (k == "hud" && isNum) hud = num != 0;
            else if (k == "cellbg" && isNum) cellbg = num != 0;
            else if (k == "gain" && isNum) gain = std::clamp(float(num), 0.25f, 4.f);
            else if (k == "intensity" && isNum) intensity = std::clamp(float(num), 0.f, 1.5f);
            else if (k == "scale" && isNum) scale = int(std::clamp(num, 1.0, 4.0));  // clamp before narrowing
            else if (k == "phrase" && isNum) phrase = int(std::clamp(num, 8.0, 256.0));
        }
    }
    bool load(const std::string &path) {  // UTF-8 path (Windows user names need not be ASCII)
        std::ifstream in(std::filesystem::u8path(path));
        if (!in) return false;
        std::stringstream ss;
        ss << in.rdbuf();
        parse(ss.str());
        return true;
    }
    bool save(const std::string &path) const {
        std::ofstream out(std::filesystem::u8path(path));
        out << str();
        out.close();  // buffered writes and close can fail after opening succeeded
        return bool(out);
    }
};

}  // namespace tv
