// Windows-only: KEY_EVENT_RECORD arrow normalization used by readInput.
// Plain structures only: no console handles, permissions or live capture.
#include "../platform/console_keys.hpp"

#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(c) ((c) ? void() : (std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c), void(++failures)))

static KEY_EVENT_RECORD key(WORD vk, wchar_t ch, bool down = true, WORD repeat = 1) {
    KEY_EVENT_RECORD k{};
    k.bKeyDown = down ? TRUE : FALSE;
    k.wRepeatCount = repeat;
    k.wVirtualKeyCode = vk;
    k.uChar.UnicodeChar = ch;
    return k;
}

int main() {
    using tv::sys::zeroCharArrowSequence;
    CHECK(zeroCharArrowSequence(key(VK_UP, 0)) == "\x1b[A");
    CHECK(zeroCharArrowSequence(key(VK_DOWN, 0)) == "\x1b[B");
    CHECK(zeroCharArrowSequence(key(VK_RIGHT, 0)) == "\x1b[C");
    CHECK(zeroCharArrowSequence(key(VK_LEFT, 0)) == "\x1b[D");

    // Repeats emit whole sequences; a zero count still counts once.
    CHECK(zeroCharArrowSequence(key(VK_LEFT, 0, true, 3)) == "\x1b[D\x1b[D\x1b[D");
    CHECK(zeroCharArrowSequence(key(VK_UP, 0, true, 0)) == "\x1b[A");

    // Key-up never acts.
    CHECK(zeroCharArrowSequence(key(VK_UP, 0, false)).empty());
    CHECK(zeroCharArrowSequence(key(VK_RIGHT, 0, false, 4)).empty());

    // Printable and non-arrow keys.
    CHECK(zeroCharArrowSequence(key('A', L'a')).empty());
    CHECK(zeroCharArrowSequence(key(VK_RETURN, L'\r')).empty());
    CHECK(zeroCharArrowSequence(key(VK_SHIFT, 0)).empty());
    CHECK(zeroCharArrowSequence(key(VK_HOME, 0)).empty());

    // Unicode VT passthrough: an arrow record that already has characters
    // (ESC [ A as separate records, or an arrow VK with a character) is not duplicated.
    CHECK(zeroCharArrowSequence(key(VK_UP, L'\x1b')).empty());
    CHECK(zeroCharArrowSequence(key(0, L'[')).empty());
    CHECK(zeroCharArrowSequence(key(VK_UP, L'A', true, 2)).empty());
    CHECK(zeroCharArrowSequence(key(0, 0xD83D)).empty());

    if (failures) return 1;
    std::puts("windows-input ok");
    return 0;
}
