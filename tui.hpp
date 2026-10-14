// Terminal UI drawn as text over the image: HUD, control panel, help, toasts.
// Pure: draws into a Screen grid and turns keys into actions, so it is testable.
#pragma once
#include "config.hpp"

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <cwchar>
#ifdef __APPLE__
#include <xlocale.h>  // newlocale, uselocale
#endif
#include <string>
#include <vector>

namespace tv {

constexpr unsigned DEF = 0xff000000;  // default terminal colour
enum Attr : unsigned char { BOLD = 1, DIM = 2 };

struct Cell {
    std::string ch = " ";
    unsigned fg = DEF, bg = DEF;
    unsigned char attr = 0;
    bool operator==(const Cell &o) const { return ch == o.ch && fg == o.fg && bg == o.bg && attr == o.attr; }
    bool blank() const { return ch == " " && bg == DEF; }
};

// UTF-8 code points; every glyph glistening draws is one cell wide.
inline int u8len(const std::string &s) {
    int n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}
inline std::string u8cut(const std::string &s, int n) {  // first n code points
    size_t i = 0;
    for (int k = 0; i < s.size(); i++)
        if ((s[i] & 0xC0) != 0x80 && k++ == n) break;
    return s.substr(0, i);
}
inline std::string fit(const std::string &s, int w) {  // truncate with an ellipsis
    if (w <= 0) return "";
    return u8len(s) <= w ? s : u8cut(s, w - 1) + "…";
}
inline std::string pad(const std::string &s, int w) { return fit(s, w) + std::string(std::max(0, w - u8len(fit(s, w))), ' '); }

// Terminal cells a code point takes: the C.UTF-8 wcwidth when the locale
// exists (POSIX; Windows has neither wcwidth nor 32-bit wchar_t), else a table of the common wide (CJK, Hangul, emoji) and zero-width
// (combining, joiners, variation selectors) ranges.
inline int cellWidth(unsigned cp) {
#ifndef _WIN32
    static const locale_t utf8 = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
    if (utf8) {
        const locale_t old = uselocale(utf8);
        const int w = wcwidth(wchar_t(cp));
        uselocale(old);
        return w;
    }
#endif
    static const unsigned zero[][2] = {{0x300, 0x36F}, {0x483, 0x489}, {0x591, 0x5C7}, {0x610, 0x61A}, {0x64B, 0x65F},
                                       {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x2028, 0x202E},
                                       {0x2060, 0x206F}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F},
                                       {0xFEFF, 0xFEFF}, {0xE0000, 0xE01EF}};
    static const unsigned wide[][2] = {{0x1100, 0x115F}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23F3},
                                       {0x2614, 0x2615}, {0x2648, 0x2653}, {0x26A1, 0x26A1}, {0x26BD, 0x26BE},
                                       {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C},
                                       {0x2B1B, 0x2B1C}, {0x2B50, 0x2B55}, {0x2E80, 0x303E}, {0x3041, 0x33FF},
                                       {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xA960, 0xA97F},
                                       {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE6F},
                                       {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x1B000, 0x1B2FF}, {0x1F004, 0x1F004},
                                       {0x1F200, 0x1F251}, {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F900, 0x1F9FF},
                                       {0x1FA70, 0x1FAFF}, {0x20000, 0x3FFFD}};
    for (auto &r : zero)
        if (cp >= r[0] && cp <= r[1]) return 0;
    for (auto &r : wide)
        if (cp >= r[0] && cp <= r[1]) return 2;
    return 1;
}

// One glyph as it may reach the terminal: C0/C1 controls, DEL, broken UTF-8
// and anything not exactly one cell wide become '?', so names from shader
// files or preset paths can't inject escapes or shift the layout.
inline std::string safe(const std::string &g) {
    const unsigned char c = g[0];
    if (g.size() == 1) return c < 0x20 || c >= 0x7f ? "?" : g;
    if ((c & 0xC0) == 0x80 || c < 0xC2 || c >= 0xF5) return "?";
    const size_t want = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    if (g.size() != want) return "?";
    unsigned cp = c & (want == 2 ? 0x1F : want == 3 ? 0x0F : 0x07);
    for (size_t i = 1; i < want; i++) cp = cp << 6 | ((unsigned char)g[i] & 0x3F);
    if (cp < 0xA0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) return "?";  // C1 controls, surrogates, overlongs
    if ((want == 3 && cp < 0x800) || (want == 4 && cp < 0x10000)) return "?";
    return cellWidth(cp) == 1 ? g : "?";
}
// A whole string through safe(), glyph by glyph (for text printed outside a Screen).
inline std::string safeText(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        size_t n = 1;
        while (i + n < s.size() && (s[i + n] & 0xC0) == 0x80) n++;
        out += safe(s.substr(i, n)), i += n;
    }
    return out;
}

struct Screen {
    int cols, rows;
    std::vector<Cell> cells;
    int clipped = 0;  // glyphs that fell outside the grid (layout bugs)
    Screen(int c = 80, int r = 24) : cols(c), rows(r), cells(size_t(c) * r) {}
    Cell &at(int x, int y) { return cells[size_t(y) * cols + x]; }
    const Cell &at(int x, int y) const { return cells[size_t(y) * cols + x]; }
    int text(int x, int y, const std::string &s, unsigned fg, unsigned bg = DEF, unsigned char attr = 0) {
        for (size_t i = 0; i < s.size();) {
            size_t n = 1;
            while (i + n < s.size() && (s[i + n] & 0xC0) == 0x80) n++;
            if (x >= 0 && x < cols && y >= 0 && y < rows) at(x, y) = {safe(s.substr(i, n)), fg, bg, attr};
            else clipped++;
            i += n, x++;
        }
        return x;
    }
    void fill(int x, int y, int w, int h, unsigned bg) {
        for (int j = y; j < y + h; j++)
            for (int i = x; i < x + w; i++)
                if (i >= 0 && i < cols && j >= 0 && j < rows) at(i, j) = {" ", DEF, bg, 0};
                else clipped++;
    }
};

struct Box {
    int x, y, w, h;  // cells
};

// What the UI shows; filled in by the main loop every frame.
struct UiState {
    std::vector<std::string> scenes;  // names, in key order 1..n
    int current = 0;                  // scene index, -1 = MilkDrop
    bool milkdropAvailable = false;
    std::string preset;  // MilkDrop preset name
    std::string palette;
    float bpm = 0, conf = 0, bass = 0, mid = 0, high = 0, energy = 0, barPhase = 0;
    float spectrum[8] = {};  // 0..1, low to high
    bool silent = true, sure = false, beatLit = false;
    int beats = 0;
    std::string toast;
    unsigned accent = 0xd6c7ff;  // sRGB, from the palette
};

enum class Act { None, Quit, Scene, Next, Prev, Director, MilkDrop, Palette, Gain, Intensity, Quality, Phrase, Hud, Reroll, Favourite, Ban, Reload, Info };
struct Action {
    Act a = Act::None;
    int arg = 0;
};

// Raw bytes from the terminal -> key names ("up", "tab", "enter", "esc", or the character).
// An unfinished escape sequence at the end is left in *rest for the next read.
inline std::vector<std::string> splitKeys(const std::string &b, std::string *rest = nullptr) {
    if (rest) rest->clear();
    std::vector<std::string> k;
    for (size_t i = 0; i < b.size();) {
        if (b[i] == 27) {
            if (i + 1 < b.size() && (b[i + 1] == '[' || b[i + 1] == 'O')) {
                size_t j = i + 2;
                while (j < b.size() && !(b[j] >= 0x40 && b[j] <= 0x7e)) j++;
                if (j >= b.size()) {
                    if (rest) *rest = b.substr(i);
                    break;
                }
                const char f = b[j];
                k.push_back(f == 'A' ? "up" : f == 'B' ? "down" : f == 'C' ? "right" : f == 'D' ? "left" : f == 'Z' ? "btab" : "?seq");
                i = j + 1;
            } else if (i + 1 == b.size() && rest) {
                *rest = "\x1b";  // maybe the start of a sequence; the caller decides after a moment
                break;
            } else k.push_back("esc"), i++;
        } else if (b[i] == '\t') k.push_back("tab"), i++;
        else if (b[i] == '\r' || b[i] == '\n') k.push_back("enter"), i++;
        else if (b[i] == 127 || b[i] == 8) k.push_back("bs"), i++;
        else k.push_back(std::string(1, b[i])), i++;
    }
    return k;
}

inline const std::vector<std::string> &paletteChoices() {
    static std::vector<std::string> v = [] {
        std::vector<std::string> r = {"auto", "desktop"};
        for (auto &p : palettes()) r.push_back(p.name);
        return r;
    }();
    return v;
}

class Ui {
public:
    bool panel = false, help = false;
    int sel = 0;    // panel row
    int focus = 0;  // row of the current scene; the panel opens there
    Settings &s;
    explicit Ui(Settings &st) : s(st) {}

    enum Row { SceneRow, MilkRow, DirectorRow, PaletteRow, GainRow, IntensityRow, QualityRow, PhraseRow, HudRow };
    struct Item {
        Row row;
        int scene;
    };
    std::vector<Item> items(int nScenes, bool milk) const {
        std::vector<Item> v;
        for (int i = 0; i < nScenes; i++) v.push_back({SceneRow, i});
        if (milk) v.push_back({MilkRow, -1});
        for (Row r : {DirectorRow, PaletteRow, GainRow, IntensityRow, QualityRow, PhraseRow, HudRow}) v.push_back({r, 0});
        return v;
    }

    // One key -> actions. Settings changed here are already applied to s.
    std::vector<Action> key(const std::string &k, int nScenes, bool milk) {
        std::vector<Action> out;
        auto act = [&](Act a, int arg = 0) { out.push_back({a, arg}); };
        const auto it = items(nScenes, milk);
        sel = std::clamp(sel, 0, int(it.size()) - 1);
        if (k == "esc") {
            if (help) help = false;
            else if (panel) panel = false;
            else act(Act::Quit);
        } else if (k == "q") act(Act::Quit);
        else if (k == "tab") {
            panel = !panel, help = false;
            if (panel) sel = std::clamp(focus, 0, int(it.size()) - 1);
        }
        else if (k == "?") help = !help;
        else if (panel && (k == "up" || k == "k")) sel = (sel + int(it.size()) - 1) % int(it.size());
        else if (panel && (k == "down" || k == "j")) sel = (sel + 1) % int(it.size());
        else if (panel && (k == "left" || k == "right" || k == "enter" || k == " ")) {
            const int d = k == "left" ? -1 : 1;
            switch (it[sel].row) {
                case SceneRow: if (k == "enter" || k == " ") act(Act::Scene, it[sel].scene); break;
                case MilkRow: if (k == "enter" || k == " ") act(Act::MilkDrop, 1); break;
                case DirectorRow: s.director = !s.director, act(Act::Director); break;
                case PaletteRow: cyclePalette(d), act(Act::Palette); break;
                case GainRow: stepGain(d), act(Act::Gain); break;
                case IntensityRow: stepIntensity(d), act(Act::Intensity); break;
                case QualityRow: s.scale = std::clamp(s.scale - d, 1, 4), act(Act::Quality); break;
                case PhraseRow: stepPhrase(d), act(Act::Phrase); break;
                case HudRow: s.hud = !s.hud, act(Act::Hud); break;
            }
        } else if (k.size() == 1 && k[0] >= '1' && k[0] <= '9' && k[0] - '1' < nScenes) act(Act::Scene, k[0] - '1');
        else if (k == "m" || (k == "0" && milk)) act(Act::MilkDrop, -1);
        else if (k == "right" || k == "n") act(Act::Next);
        else if (k == " ") act(Act::Next);
        else if (k == "left" || k == "p") act(Act::Prev);
        else if (k == "a" || k == "l") s.director = !s.director, act(Act::Director);
        else if (k == "c") cyclePalette(1), act(Act::Palette);
        else if (k == "+" || k == "=") stepGain(1), act(Act::Gain);
        else if (k == "-" || k == "_") stepGain(-1), act(Act::Gain);
        else if (k == "]") stepIntensity(1), act(Act::Intensity);
        else if (k == "[") stepIntensity(-1), act(Act::Intensity);
        else if (k == "h") s.hud = !s.hud, act(Act::Hud);
        else if (k == "r") act(Act::Reroll);
        else if (k == "f") act(Act::Favourite);
        else if (k == "b") act(Act::Ban);
        else if (k == "R") act(Act::Reload);
        else if (k == "i") act(Act::Info);
        return out;
    }

    void cyclePalette(int d) {
        auto &c = paletteChoices();
        int i = int(std::find(c.begin(), c.end(), s.palette) - c.begin());
        if (i >= int(c.size())) i = 0;
        s.palette = c[(i + d + c.size()) % c.size()];
    }
    void stepGain(int d) { s.gain = std::clamp(std::round(s.gain * (d > 0 ? 1.25f : 0.8f) * 100) / 100, 0.25f, 4.f); }
    void stepIntensity(int d) { s.intensity = std::clamp(std::round((s.intensity + 0.1f * d) * 10) / 10, 0.f, 1.5f); }
    void stepPhrase(int d) {  // next power of two in that direction, across the whole valid 8..256
        static const int v[] = {8, 16, 32, 64, 128, 256};
        if (d > 0) {
            for (int x : v)
                if (x > s.phrase) return void(s.phrase = x);
        } else
            for (int k = 5; k >= 0; k--)
                if (v[k] < s.phrase) return void(s.phrase = v[k]);
    }

    // Draw everything; returns the boxes that want a frosted backdrop.
    std::vector<Box> draw(Screen &sc, const UiState &st) const {
        std::vector<Box> boxes;
        const int bottom = s.hud ? sc.rows - 1 : sc.rows;
        if (s.hud && sc.rows >= 3) boxes.push_back(hud(sc, st));
        if (!st.toast.empty() && sc.rows >= 4) boxes.push_back(toast(sc, st.toast, bottom));
        if (panel && sc.cols >= 24 && sc.rows >= 8) boxes.push_back(drawPanel(sc, st, bottom));
        if (help && sc.cols >= 30 && sc.rows >= 8) boxes.push_back(drawHelp(sc, st));
        return boxes;
    }

    static constexpr unsigned FG = 0xe9e9ec, MUTED = 0x9a9ea8, FAINT = 0x6b6f78;

private:
    static std::string meter(float v) {
        static const char *b[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        return b[std::clamp(int(v * 7.99f), 0, 7)];
    }
    static std::string qualityName(int scale) {
        static const char *n[] = {"", "full", "1/2", "1/3", "1/4"};
        return n[std::clamp(scale, 1, 4)];
    }

    std::string current(const UiState &st) const {
        if (st.current < 0) return "MilkDrop";
        return st.current < (int)st.scenes.size() ? st.scenes[st.current] : "?";
    }

    // Bottom row: scene · palette | tempo + bar | meters | director | hints.
    Box hud(Screen &sc, const UiState &st) const {
        const int y = sc.rows - 1, W = sc.cols - 2;
        std::string name = current(st), pal = st.current < 0 ? st.preset : st.palette;
        char tempo[48];
        if (st.silent) snprintf(tempo, sizeof tempo, "silence");
        else if (st.sure) snprintf(tempo, sizeof tempo, "%.0f bpm", st.bpm);
        else snprintf(tempo, sizeof tempo, "no clear beat");
        std::string dots;
        const int bar = int(st.barPhase * 4) % 4;
        for (int i = 0; i < 4; i++) dots += st.sure && !st.silent && i == bar ? "●" : "·";
        std::string meters;
        for (float v : st.spectrum) meters += meter(v);
        std::string mode = s.director ? "auto" : "manual";
        if (s.director && st.sure) mode += " " + std::to_string(st.beats) + "/" + std::to_string(s.phrase);
        const std::string hint = "tab menu  ? help";

        // Lay out by priority: name, tempo, meters, mode, palette, hint.
        int x = 1;
        x = sc.text(x, y, " " + fit(name, std::min(20, W - 2)), FG, DEF, BOLD);
        const int room = W - (x - 1);
        int used = 0;
        auto fits = [&](const std::string &s2) { return used + u8len(s2) + 3 <= room; };
        std::vector<std::pair<std::string, unsigned>> parts;
        if (!pal.empty() && room > 50) parts.push_back({fit(pal, 18), MUTED});
        if (fits(std::string(tempo) + " " + dots)) parts.push_back({std::string(tempo) + " " + dots, st.sure ? FG : MUTED}), used += u8len(parts.back().first) + 3;
        if (fits(meters)) parts.push_back({meters, st.accent}), used += 11;
        if (fits(mode)) parts.push_back({mode, MUTED}), used += u8len(mode) + 3;
        for (auto &[t, c] : parts) {
            if (x + 3 + u8len(t) > sc.cols - 1) break;
            x = sc.text(x, y, "  ", FG);
            x = sc.text(x, y, t, c);
        }
        if (sc.cols - 2 - u8len(hint) > x + 2) sc.text(sc.cols - 2 - u8len(hint), y, hint, FAINT);
        return {0, y, sc.cols, 1};
    }

    Box toast(Screen &sc, const std::string &t, int bottom) const {
        const std::string s2 = fit(t, sc.cols - 6);
        const int y = bottom - 2;
        sc.text(2, y, " " + s2 + " ", FG, DEF, BOLD);
        return {1, y, u8len(s2) + 4, 1};
    }

    Box drawPanel(Screen &sc, const UiState &st, int bottom) const {
        const auto it = items(int(st.scenes.size()), st.milkdropAvailable);
        const int sel = std::clamp(this->sel, 0, int(it.size()) - 1);
        const int W = std::min(36, sc.cols - 2);
        const int maxH = bottom - 1;  // a one-row margin top and bottom
        struct Line {
            int item;  // -1: heading/blank
            std::string text;
        };
        std::vector<Line> L;
        const bool roomy = int(it.size()) + 9 <= maxH;
        if (roomy) L.push_back({-1, "glistening"}), L.push_back({-1, ""});
        L.push_back({-1, "SCENES"});
        for (size_t i = 0; i < it.size(); i++) {
            if (it[i].row == DirectorRow) {
                if (roomy) L.push_back({-1, ""});
                L.push_back({-1, "SETTINGS"});
            }
            L.push_back({int(i), ""});
        }
        if (roomy) L.push_back({-1, ""});
        L.push_back({-2, W >= 34 ? "↑↓ move  ←→ change  ⏎ choose" : "↑↓ ←→ ⏎"});
        int H = std::min(int(L.size()) + 2, maxH);
        // Scroll the item lines so the selection stays visible when space is short.
        int first = 0, visible = H - 2;
        int selLine = 0;
        for (size_t i = 0; i < L.size(); i++)
            if (L[i].item == sel) selLine = int(i);
        if (int(L.size()) > visible) first = std::clamp(selLine - visible / 2, 0, int(L.size()) - visible);
        const int x0 = sc.cols - W - 1, y0 = 1;
        for (int r = 0; r < visible && first + r < int(L.size()); r++) {
            const Line &l = L[first + r];
            const int y = y0 + 1 + r, x = x0 + 2, w = W - 4;
            if (l.item == -2) sc.text(x, y, fit(l.text, w), FAINT);
            else if (l.item < 0) sc.text(x, y, fit(l.text, w), l.text == "glistening" ? FG : MUTED, DEF, l.text == "glistening" ? BOLD : 0);
            else drawItem(sc, x, y, w, it[l.item], l.item == sel, st);
        }
        return {x0, y0, W, H};
    }

    void drawItem(Screen &sc, int x, int y, int w, const Item &item, bool selected, const UiState &st) const {
        const unsigned fg = selected ? st.accent : FG;
        const unsigned char at = selected ? BOLD : 0;
        sc.text(x, y, selected ? "›" : " ", st.accent, DEF, BOLD);
        std::string label, value;
        bool live = false;
        char b[32];
        switch (item.row) {
            case SceneRow:
                label = (item.scene < 9 ? std::to_string(item.scene + 1) : " ") + "  " + st.scenes[item.scene];  // only 1-9 have number keys
                live = st.current == item.scene;
                break;
            case MilkRow: label = "0  MilkDrop"; live = st.current < 0; break;
            case DirectorRow: label = "Auto director", value = s.director ? "on" : "off"; break;
            case PaletteRow: label = "Palette", value = s.palette; break;
            case GainRow: snprintf(b, sizeof b, "%d%%", int(std::lround(s.gain * 100))), label = "Sensitivity", value = b; break;
            case IntensityRow: snprintf(b, sizeof b, "%d%%", int(std::lround(s.intensity * 100))), label = "Intensity", value = b; break;
            case QualityRow: label = "Quality", value = qualityName(s.scale); break;
            case PhraseRow: label = "Phrase", value = std::to_string(s.phrase) + " beats"; break;
            case HudRow: label = "HUD", value = s.hud ? "on" : "off"; break;
        }
        const int inner = w - 2;
        if (item.row == SceneRow || item.row == MilkRow) {
            sc.text(x + 2, y, fit(label, inner - 2), fg, DEF, at);
            if (live) sc.text(x + w - 1, y, "●", st.accent);
        } else {
            const bool arrows = selected && item.row != DirectorRow && item.row != HudRow;
            const std::string v = arrows ? "‹ " + value + " ›" : value;
            const int vw = std::min(u8len(v), inner / 2);
            sc.text(x + 2, y, fit(label, inner - vw - 1), fg, DEF, at);
            sc.text(x + w - vw, y, fit(v, vw), selected ? st.accent : MUTED, DEF, at);
        }
    }

    Box drawHelp(Screen &sc, const UiState &st) const {
        static const char *keys[][2] = {
            {"tab", "control panel"}, {"1-9", "choose a scene (tab: all)"}, {"← →", "previous / next scene"},
            {"space", "next scene (director)"}, {"a", "auto director on/off"}, {"m", "MilkDrop mode"},
            {"c", "cycle palette"}, {"r", "new look / random preset"}, {"+ -", "sensitivity"},
            {"[ ]", "effect intensity"}, {"h", "HUD"}, {"f b", "favourite / ban preset"},
            {"R", "reload shaders"}, {"i", "info"}, {"?", "this help"}, {"q", "quit"},
        };
        const int n = sizeof keys / sizeof keys[0];
        const bool two = sc.cols >= 74;
        const int colW = 34, rowsN = two ? (n + 1) / 2 : n;
        const int W = two ? 2 * colW + 4 : std::min(colW + 4, sc.cols - 2);
        int H = std::min(rowsN + 4, sc.rows - 2);
        const int x0 = (sc.cols - W) / 2, y0 = std::max(1, (sc.rows - H) / 2);
        sc.fill(x0, y0, W, H, DEF);  // covers whatever is underneath (the panel)
        sc.text(x0 + 2, y0 + 1, "Keys", FG, DEF, BOLD);
        const int shown = H - 4;
        for (int i = 0; i < n; i++) {
            const int c = two ? i / rowsN : 0, r = two ? i % rowsN : i;
            if (r >= shown) continue;
            const int x = x0 + 2 + c * (colW + 1), y = y0 + 3 + r;
            sc.text(x, y, pad(keys[i][0], 6), st.accent, DEF, BOLD);
            sc.text(x + 7, y, fit(keys[i][1], std::min(colW, W - 4) - 7), FG);
        }
        return {x0, y0, W, H};
    }
};

}  // namespace tv
