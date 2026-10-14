// Windows: the console in VT mode (Windows 10 1809+ conhost, Windows Terminal,
// or any ConPTY host), CreateProcessW children in a kill-on-close job, and
// UTF-8 at every boundary (wide command line, environment and paths).
// No POSIX layer: native Win32 only. Audio devices live in audio_wasapi.cpp.
#include "platform.hpp"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>

namespace tv::sys {

volatile std::sig_atomic_t quitFlag = 0, quitSignal = 0;

static std::wstring wide(const std::string &s) {
    if (s.empty()) return {};
    std::wstring w(size_t(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], int(w.size()));
    return w;
}
static std::string utf8(const wchar_t *w, int n = -1) {
    if (!w || !n) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, &s[0], len, nullptr, nullptr);
    if (n < 0 && !s.empty()) s.pop_back();  // the terminator
    return s;
}

std::vector<std::string> args(int argc, char **argv) {
    int n = 0;
    wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w) return std::vector<std::string>(argv, argv + argc);
    std::vector<std::string> a;
    for (int i = 0; i < n; i++) a.push_back(utf8(w[i]));
    LocalFree(w);
    return a;
}

std::string env(const char *name) {
    const wchar_t *v = _wgetenv(wide(name).c_str());
    return v && *v ? utf8(v) : "";
}

std::string configDir() {
    std::string d = env("APPDATA");
    if (d.empty()) d = env("USERPROFILE");
    if (d.empty()) d = ".";
    d += "\\glistening";
    CreateDirectoryW(wide(d).c_str(), nullptr);
    return d;
}

FILE *open(const std::string &path, const char *mode) { return _wfopen(wide(path).c_str(), wide(mode).c_str()); }
bool readable(const std::string &path) {
    FILE *f = open(path, "rb");
    return f && fclose(f) == 0;
}

static bool isFile(const std::wstring &p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string findProgram(const std::string &name) {
    const std::wstring n = wide(name);
    std::vector<std::wstring> exts{L""};
    if (n.find(L'.') == std::wstring::npos || n.rfind(L'.') < n.find_last_of(L"\\/") + 1) {  // no extension: try PATHEXT
        const std::wstring pe = wide(env("PATHEXT").empty() ? ".COM;.EXE;.BAT;.CMD" : env("PATHEXT"));
        exts.clear();
        for (size_t a = 0; a <= pe.size();) {
            size_t b = pe.find(L';', a);
            if (b == std::wstring::npos) b = pe.size();
            if (b > a) exts.push_back(pe.substr(a, b - a));
            a = b + 1;
        }
    }
    auto tryDir = [&](const std::wstring &dir) -> std::string {
        for (auto &e : exts) {
            const std::wstring p = (dir.empty() ? L"" : dir + L"\\") + n + e;
            if (isFile(p)) return utf8(p.c_str());
        }
        return "";
    };
    if (n.find_first_of(L"\\/:") != std::wstring::npos) return tryDir(L"");
    const std::wstring path = wide(env("PATH"));
    for (size_t a = 0; a <= path.size();) {
        size_t b = path.find(L';', a);
        if (b == std::wstring::npos) b = path.size();
        std::wstring dir = path.substr(a, b - a);
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') dir = dir.substr(1, dir.size() - 2);
        if (!dir.empty()) {
            const std::string hit = tryDir(dir);
            if (!hit.empty()) return hit;
        }
        a = b + 1;
    }
    return "";
}

const char *osName() { return "windows"; }

// ---- terminal

static HANDLE in() { return GetStdHandle(STD_INPUT_HANDLE); }
static HANDLE outh() { return GetStdHandle(STD_OUTPUT_HANDLE); }
static DWORD g_inMode = 0, g_outMode = 0;
static UINT g_outCP = 0, g_inCP = 0;
static bool g_raw = false;
static volatile LONG g_resizedEvent = 0;

bool isTerminal() {
    DWORD m;
    return GetConsoleMode(in(), &m) && GetConsoleMode(outh(), &m);
}

// Ctrl+C / Ctrl+Break ask the loop to quit (SIGINT). Closing the window,
// logoff and shutdown (SIGTERM) end the process once this returns, so wait
// here while the main loop unwinds; the job object stops any audio command
// even if that wait runs out.
static BOOL WINAPI onCtrl(DWORD type) {
    const bool interrupt = type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
    quitSignal = interrupt ? SIGINT : SIGTERM, quitFlag = 1;
    if (!interrupt) Sleep(4000);
    return TRUE;
}

std::string enterRaw() {
    HANDLE i = in(), o = outh();
    if (!GetConsoleMode(i, &g_inMode) || !GetConsoleMode(o, &g_outMode)) return "not a Windows console";
    g_outCP = GetConsoleOutputCP(), g_inCP = GetConsoleCP();
    DWORD om = g_outMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(o, om | DISABLE_NEWLINE_AUTO_RETURN) && !SetConsoleMode(o, om))
        return "this console has no VT support (needs Windows 10 1809+, Windows Terminal, or another VT terminal)";
    // Keys arrive as VT sequences; no line editing or echo; Ctrl+C still raises CTRL_C_EVENT.
    // Quick Edit off: a stray click would otherwise freeze output until Esc.
    DWORD im = (g_inMode | ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS) &
               ~DWORD(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT);
    if (!SetConsoleMode(i, im)) {
        SetConsoleMode(o, g_outMode);
        return "cannot switch the console to VT input";
    }
    SetConsoleOutputCP(CP_UTF8), SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(onCtrl, TRUE);
    g_raw = true;
    return "";
}

void restoreInput() {
    if (!g_raw) return;
    SetConsoleMode(in(), g_inMode), SetConsoleMode(outh(), g_outMode);
    SetConsoleOutputCP(g_outCP), SetConsoleCP(g_inCP);
    g_raw = false;
}

void runHandlers() { SetConsoleCtrlHandler(onCtrl, TRUE); }
void quitHandlers() { SetConsoleCtrlHandler(onCtrl, TRUE); }

void write(const std::string &s) {
    HANDLE o = outh();
    for (size_t off = 0; off < s.size();) {
        DWORD n = 0;
        if (!WriteFile(o, s.data() + off, DWORD(std::min<size_t>(s.size() - off, 1 << 20)), &n, nullptr) || !n) return;
        off += n;
        if (quitFlag && off < s.size()) return;  // a stalled console must not hold up shutdown
    }
}

// Console input records: key-down characters (VT sequences included, thanks
// to ENABLE_VIRTUAL_TERMINAL_INPUT) as UTF-8; buffer-size records flag a resize.
std::string readInput(int waitMs) {
    HANDLE i = in();
    std::string s;
    if (waitMs > 0 && WaitForSingleObject(i, DWORD(waitMs)) != WAIT_OBJECT_0) return s;
    static wchar_t high = 0;  // a surrogate pair may span two records
    DWORD n = 0;
    while (GetNumberOfConsoleInputEvents(i, &n) && n) {
        INPUT_RECORD r[64];
        DWORD got = 0;
        if (!ReadConsoleInputW(i, r, DWORD(std::min<DWORD>(n, 64)), &got) || !got) break;
        for (DWORD k = 0; k < got; k++) {
            if (r[k].EventType == WINDOW_BUFFER_SIZE_EVENT) InterlockedExchange(&g_resizedEvent, 1);
            if (r[k].EventType != KEY_EVENT || !r[k].Event.KeyEvent.bKeyDown) continue;
            const wchar_t c = r[k].Event.KeyEvent.uChar.UnicodeChar;
            if (!c) continue;
            if (c >= 0xD800 && c < 0xDC00) {
                high = c;
                continue;
            }
            const wchar_t pair[2] = {high, c};
            const bool low = c >= 0xDC00 && c < 0xE000;
            const std::string u = low && high ? utf8(pair, 2) : utf8(&c, 1);
            high = 0;
            for (WORD rep = 0; rep < std::max<WORD>(1, r[k].Event.KeyEvent.wRepeatCount); rep++) s += u;
        }
    }
    return s;
}

TermSize size() {
    CONSOLE_SCREEN_BUFFER_INFO b;
    if (!GetConsoleScreenBufferInfo(outh(), &b)) return {80, 24, 0, 0};
    // Pixels are unknown here (ConPTY hides the font): kitty mode estimates from cells.
    return {b.srWindow.Right - b.srWindow.Left + 1, b.srWindow.Bottom - b.srWindow.Top + 1, 0, 0};
}

// Buffer-size events are not delivered for every window resize in every host,
// so the size itself is compared too (cheap: one console call per frame).
bool resized() {
    static TermSize last = size();
    const TermSize now = size();
    const bool changed = InterlockedExchange(&g_resizedEvent, 0) || now.cols != last.cols || now.rows != last.rows;
    last = now;
    return changed;
}

bool shmSupported() { return false; }
std::string shmName(unsigned) { return ""; }
bool shmPut(const std::string &, const void *, size_t) { return false; }
bool shmExists(const std::string &) { return false; }
void shmUnlink(const std::string &) {}

// ---- processes

struct Process::Impl {
    HANDLE proc = nullptr, job = nullptr, out = nullptr;
    bool blocking = false, exited = false;
    DWORD code = 0;
};

Process::Process() : p_(new Impl) {}
Process::~Process() { stop(); }

// One argument as CommandLineToArgvW / the C runtime will split it back.
static std::wstring quoteArg(const std::wstring &a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring q = L"\"";
    for (size_t i = 0;; i++) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') i++, bs++;
        if (i == a.size()) {
            q.append(bs * 2, L'\\');
            break;
        }
        if (a[i] == L'"') q.append(bs * 2 + 1, L'\\'), q += L'"';
        else q.append(bs, L'\\'), q += a[i];
    }
    return q + L"\"";
}

// Stdout on a pipe, stdin/stderr on NUL, only those two handles inherited, no
// console of its own (so the console's Ctrl+C is ours alone), and a job
// object that kills the whole tree when glistening exits or crashes.
static bool spawn(Process::Impl &p, const std::wstring &app, std::wstring cmdline, bool blocking) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 20)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> attrs(size);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
    HANDLE inherit[2] = {wr, nul};
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul, si.StartupInfo.hStdOutput = wr, si.StartupInfo.hStdError = nul;
    PROCESS_INFORMATION pi{};
    bool ok = nul != INVALID_HANDLE_VALUE && InitializeProcThreadAttributeList(list, 1, 0, &size) &&
              UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr);
    if (ok) {
        si.lpAttributeList = list;
        ok = CreateProcessW(app.c_str(), &cmdline[0], nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr,
                            &si.StartupInfo, &pi);
        DeleteProcThreadAttributeList(list);
    }
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) return CloseHandle(rd), false;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim{};
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof lim) || !AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, 1);  // never leave an unsupervised child behind
        CloseHandle(pi.hThread), CloseHandle(pi.hProcess), CloseHandle(rd);
        if (job) CloseHandle(job);
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    p.proc = pi.hProcess, p.job = job, p.out = rd, p.blocking = blocking, p.exited = false;
    return true;
}

bool Process::start(const std::vector<std::string> &argv, bool blocking) {
    if (argv.empty() || p_->proc) return false;
    const std::string path = findProgram(argv[0]);
    if (path.empty()) return false;
    std::wstring cmd = quoteArg(wide(path));
    for (size_t i = 1; i < argv.size(); i++) cmd += L" " + quoteArg(wide(argv[i]));
    return spawn(*p_, wide(path), cmd, blocking);
}

// cmd.exe /s /c "LINE": the outer quotes are stripped and LINE runs as typed.
bool Process::startShell(const std::string &line, bool blocking) {
    if (p_->proc) return false;
    std::string comspec = env("ComSpec");
    if (comspec.empty()) comspec = findProgram("cmd.exe");
    if (comspec.empty()) return false;
    return spawn(*p_, wide(comspec), quoteArg(wide(comspec)) + L" /d /s /c \"" + wide(line) + L"\"", blocking);
}

bool Process::open() const { return p_->out != nullptr; }

long Process::read(void *buf, size_t n) {
    Impl &p = *p_;
    if (!p.out) return 0;
    DWORD avail = DWORD(n);
    if (!p.blocking) {
        if (!PeekNamedPipe(p.out, nullptr, 0, nullptr, &avail, nullptr)) avail = DWORD(-1);  // broken: the writer is gone
        else if (!avail) return -1;
    }
    DWORD got = 0;
    if (avail != DWORD(-1) && ReadFile(p.out, buf, DWORD(std::min<size_t>(n, avail)), &got, nullptr)) return got ? long(got) : -1;
    CloseHandle(p.out), p.out = nullptr;  // ERROR_BROKEN_PIPE: end of output
    return 0;
}

bool Process::poll() {
    Impl &p = *p_;
    if (!p.exited && p.proc && WaitForSingleObject(p.proc, 0) == WAIT_OBJECT_0) GetExitCodeProcess(p.proc, &p.code), p.exited = true;
    return p.exited;
}

bool Process::wait(int ms) {
    if (p_->proc && !p_->exited) WaitForSingleObject(p_->proc, DWORD(ms));
    return poll();
}

bool Process::failed() const { return p_->exited && p_->code != 0; }
int Process::status() const { return int(p_->code); }

// Closing the pipe makes a well-behaved writer exit on its own; after half a
// second the job (the command and anything it started) is terminated.
void Process::stop() {
    Impl &p = *p_;
    if (p.out) CloseHandle(p.out), p.out = nullptr;
    if (!p.proc) return;
    if (!poll()) WaitForSingleObject(p.proc, 500);
    if (!poll()) TerminateJobObject(p.job, 1), WaitForSingleObject(p.proc, 1000), poll();
    if (p.job) TerminateJobObject(p.job, 1), CloseHandle(p.job);  // whatever a shell line left running
    CloseHandle(p.proc);
    p.proc = p.job = nullptr;
}

}  // namespace tv::sys
