// Private Windows console helper: ReadConsoleInputW KEY_EVENT records for the
// arrow keys. ENABLE_VIRTUAL_TERMINAL_INPUT is documented to convert keys to VT
// sequences for ReadFile/ReadConsole, not for ReadConsoleInputW, so an arrow can
// arrive as a record with UnicodeChar == 0 and only a virtual-key code.
#pragma once

#include <windows.h>

#include <string>

namespace tv::sys {

// The complete CSI sequence for a zero-character, key-down arrow record, once
// per wRepeatCount (minimum 1). Everything else (key-up, printable or Unicode
// records, which already carry their own VT text, other keys) yields "".
inline std::string zeroCharArrowSequence(const KEY_EVENT_RECORD &k) {
    if (!k.bKeyDown || k.uChar.UnicodeChar) return {};
    const char *seq = nullptr;
    switch (k.wVirtualKeyCode) {
    case VK_UP: seq = "\x1b[A"; break;
    case VK_DOWN: seq = "\x1b[B"; break;
    case VK_RIGHT: seq = "\x1b[C"; break;
    case VK_LEFT: seq = "\x1b[D"; break;
    default: return {};
    }
    std::string s;
    for (WORD rep = 0; rep < (k.wRepeatCount ? k.wRepeatCount : 1); rep++) s += seq;
    return s;
}

}  // namespace tv::sys
