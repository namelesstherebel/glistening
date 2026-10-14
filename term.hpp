// Terminal output: kitty graphics frames, a truecolor half-block fallback,
// and the text layer (diffed against the previous frame).
#pragma once
#include "tui.hpp"

#include <string>
#include <vector>

namespace tv {

inline std::string b64(const unsigned char *p, size_t n) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r;
    r.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        r += t[v >> 18 & 63];
        r += t[v >> 12 & 63];
        r += i + 1 < n ? t[v >> 6 & 63] : '=';
        r += i + 2 < n ? t[v & 63] : '=';
    }
    return r;
}

// Image below text. z < -1073741824 also goes below cells with a background colour.
constexpr int Z_TEXT = -1, Z_CELLS = -1073741825;

// Frame from a POSIX shared-memory object (the terminal unlinks it).
inline std::string kittyShm(const std::string &name, int w, int h, size_t bytes, int id, int cols, int rows, int z) {
    char hdr[256];
    snprintf(hdr, sizeof hdr, "\x1b_Ga=T,f=24,t=s,s=%d,v=%d,S=%zu,i=%d,p=1,c=%d,r=%d,z=%d,C=1,q=2;", w, h, bytes, id, cols, rows, z);
    return hdr + b64((const unsigned char *)name.data(), name.size()) + "\x1b\\";
}

// Frame sent in-band, base64 in 4096-byte chunks (works over ssh).
inline std::string kittyDirect(const unsigned char *rgb, int w, int h, int id, int cols, int rows, int z) {
    const std::string data = b64(rgb, size_t(w) * h * 3);
    std::string f;
    char hdr[256];
    for (size_t off = 0; off < data.size(); off += 4096) {
        const bool more = off + 4096 < data.size();
        if (!off) snprintf(hdr, sizeof hdr, "\x1b_Ga=T,f=24,s=%d,v=%d,i=%d,p=1,c=%d,r=%d,z=%d,C=1,q=2,m=%d;", w, h, id, cols, rows, z, more);
        else snprintf(hdr, sizeof hdr, "\x1b_Gm=%d,q=2;", more);
        f += hdr + data.substr(off, 4096) + "\x1b\\";
    }
    return f;
}

inline std::string kittyDelete(int id) { return "\x1b_Ga=d,d=I,i=" + std::to_string(id) + ",q=2\x1b\\"; }

// SGR for a cell, emitting only what differs from the running state.
struct Pen {
    unsigned fg = DEF, bg = DEF;
    unsigned char attr = 0;
    void to(std::string &o, unsigned f, unsigned b, unsigned char a) {
        if (a != attr) {
            o += "\x1b[0m", fg = bg = DEF, attr = 0;
            if (a & BOLD) o += "\x1b[1m";
            if (a & DIM) o += "\x1b[2m";
            attr = a;
        }
        char s[48];
        if (f != fg) {
            if (f == DEF) o += "\x1b[39m";
            else snprintf(s, sizeof s, "\x1b[38;2;%u;%u;%um", f >> 16 & 255, f >> 8 & 255, f & 255), o += s;
            fg = f;
        }
        if (b != bg) {
            if (b == DEF) o += "\x1b[49m";
            else snprintf(s, sizeof s, "\x1b[48;2;%u;%u;%um", b >> 16 & 255, b >> 8 & 255, b & 255), o += s;
            bg = b;
        }
    }
};

inline std::string moveTo(int x, int y) { return "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H"; }

// Text layer over a kitty image: rewrite the changed span of each changed row.
inline std::string textDiff(const Screen &cur, const Screen &prev) {
    std::string o;
    Pen pen;
    const bool full = prev.cols != cur.cols || prev.rows != cur.rows;
    for (int y = 0; y < cur.rows; y++) {
        int a = cur.cols, b = -1;
        for (int x = 0; x < cur.cols; x++)
            if (full || !(cur.at(x, y) == prev.at(x, y))) a = std::min(a, x), b = x;
        if (b < 0) continue;
        o += moveTo(a, y);
        for (int x = a; x <= b; x++) {
            const Cell &c = cur.at(x, y);
            pen.to(o, c.fg, c.bg, c.attr);
            o += c.ch;
        }
    }
    if (!o.empty()) o += "\x1b[0m";
    return o;
}

// Half-block fallback: rgb is cols x (2*rows), top-down. Each cell is ▀ with the
// upper pixel as foreground and the lower as background; UI text replaces cells
// and sits on the cell's mean colour.
inline std::string halfBlocks(const unsigned char *rgb, const Screen &ui) {
    std::string o;
    o.reserve(size_t(ui.cols) * ui.rows * 24);
    Pen pen;
    auto px = [&](int x, int y) {
        const unsigned char *p = rgb + (size_t(y) * ui.cols + x) * 3;
        return unsigned(p[0]) << 16 | unsigned(p[1]) << 8 | p[2];
    };
    for (int y = 0; y < ui.rows; y++) {
        o += moveTo(0, y);
        for (int x = 0; x < ui.cols; x++) {
            const unsigned t = px(x, 2 * y), b = px(x, 2 * y + 1);
            const Cell &c = ui.at(x, y);
            if (c.blank()) {
                pen.to(o, t, b, 0);
                o += "▀";
            } else {
                pen.to(o, c.fg, c.bg != DEF ? c.bg : mixc(t, b, 0.5f), c.attr);
                o += c.ch;
            }
        }
    }
    o += "\x1b[0m";
    return o;
}

// Box-average an image down by 2 in each direction (half-block mode renders at 2x).
inline void halve(const std::vector<unsigned char> &in, int w, int h, std::vector<unsigned char> &out) {
    const int W = w / 2, H = h / 2;
    out.resize(size_t(W) * H * 3);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = 0; k < 3; k++) {
                auto at = [&](int xx, int yy) { return in[(size_t(yy) * w + xx) * 3 + k]; };
                out[(size_t(y) * W + x) * 3 + k] = (at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4;
            }
}

}  // namespace tv
