// Terminal smoke test: runs glistening on the built-in test track inside a
// pseudo-terminal (forkpty, or ConPTY on Windows) at 80x24, resizes it to
// 100x30 after 1 s, presses q after 2.5 s, and checks it drew frames, followed
// the resize, quit on the key, and gave the terminal back.
//   ptysmoke GLISTENING blocks|kitty
// Quasicrystal keeps this a terminal protocol test on CPU-only GL hosts; the
// render gate checks every scene separately. No audio device, display or real
// terminal needed; GL is (as for glistening).
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // ConPTY: Windows 10 1809+
#endif
#include <windows.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif
#endif

using Clock = std::chrono::steady_clock;
static int failures = 0;
static void check(bool ok, const std::string &what) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    failures += !ok;
}

// Runs the command with VIZ_OUTPUT=mode, resizes, sends "q"; returns its exit
// code (-1: still running after 60 s, killed) and everything it wrote.
static int run(const std::string &exe, const std::string &mode, std::string &out, double &secs);

/** Verify a terminal output protocol, resize handling, quit input, and screen restoration. */
int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[2], "blocks") && strcmp(argv[2], "kitty"))) return fprintf(stderr, "usage: ptysmoke GLISTENING blocks|kitty\n"), 2;
    const std::string mode = argv[2];
    std::string out;
    double secs = 0;
    const int code = run(argv[1], mode, out, secs);
    printf("glistening exited %d after %.1f s, %zu bytes of output\n", code, secs, out.size());
    check(code == 0, "exits cleanly");
    check(code >= 0 && secs < 30, "quits on the q key (well before --seconds 40)");
#ifdef _WIN32
    // ConPTY re-renders the screen and swallows APC (kitty) sequences: only the text survives.
    if (mode == "blocks") check(out.find("\xe2\x96\x80") != std::string::npos, "draws half-block frames");
#else
    check(out.find("\x1b[?1049h") != std::string::npos, "enters the alternate screen");
    if (mode == "blocks") {
        check(out.find("\xe2\x96\x80") != std::string::npos, "draws half-block frames");
        check(out.find("\x1b[30;1H") != std::string::npos, "redraws all 30 rows after the resize");
    } else {
        check(out.find("\x1b_Ga=T,f=24,s=") != std::string::npos, "sends kitty graphics frames");
        check(out.find("c=100,r=30,") != std::string::npos, "re-renders at the new size after the resize");
    }
    const size_t leave = out.rfind("\x1b[?1049l");
    check(leave != std::string::npos && out.find("\x1b[?25h", out.rfind("\x1b[?2026l")) != std::string::npos, "restores the screen and cursor");
#endif
    printf("\n%s (%d failed)\n", failures ? "TERMINAL SMOKE FAILED" : "TERMINAL SMOKE PASS", failures);
    return failures != 0;
}

#ifdef _WIN32
static std::wstring wide(const std::string &s) {
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], int(w.size()));
    return w;
}

/** Launch the terminal fixture, resize it, send quit, and capture output through child exit. */
static int run(const std::string &exe, const std::string &mode, std::string &out, double &secs) {
    HANDLE inR, inW, outR, outW;
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
    if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) return -2;
    HPCON pc;
    if (FAILED(CreatePseudoConsole(COORD{80, 24}, inR, outW, 0, &pc))) return fprintf(stderr, "CreatePseudoConsole failed\n"), -2;
    CloseHandle(inR), CloseHandle(outW);
    std::thread reader([&] {  // ConPTY blocks if its output is not drained
        char b[4096];
        DWORD n;
        while (ReadFile(outR, b, sizeof b, &n, nullptr) && n) out.append(b, n);
    });
    SetEnvironmentVariableW(L"VIZ_OUTPUT", wide(mode).c_str());
    SetEnvironmentVariableW(L"VIZ_MEDIUM", L"direct");
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    // Null standard handles attach to ConPTY instead of inheriting CI output pipes.
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, size);
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size);
    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof pc, nullptr, nullptr);
    std::wstring cmd = L"\"" + wide(exe) + L"\" --test-signal --seconds 40 --no-director --scene quasicrystal";
    PROCESS_INFORMATION pi{};
    const auto t0 = Clock::now();
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi))
        return fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError()), ClosePseudoConsole(pc), reader.join(), -2;
    Sleep(1000);
    ResizePseudoConsole(pc, COORD{100, 30});
    Sleep(1500);
    DWORD w;
    WriteFile(inW, "q", 1, &w, nullptr);
    DWORD code = 0;
    const bool done = WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0;
    secs = std::chrono::duration<double>(Clock::now() - t0).count();
    if (!done) TerminateProcess(pi.hProcess, 1);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
    ClosePseudoConsole(pc);  // ends the reader
    reader.join();
    CloseHandle(inW), CloseHandle(outR);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    return done ? int(code) : -1;
}
#else
/** Launch the terminal fixture, resize it, send quit, and capture output through child exit. */
static int run(const std::string &exe, const std::string &mode, std::string &out, double &secs) {
    winsize ws{24, 80, 800, 480};
    int fd;
    const auto t0 = Clock::now();
    const pid_t pid = forkpty(&fd, nullptr, nullptr, &ws);
    if (pid < 0) return perror("forkpty"), -2;
    if (pid == 0) {
        setenv("VIZ_OUTPUT", mode.c_str(), 1);
        setenv("VIZ_MEDIUM", "direct", 1);
        setenv("TERM", "xterm-256color", 1);
        execl(exe.c_str(), exe.c_str(), "--test-signal", "--seconds", "40", "--no-director", "--scene", "quasicrystal", (char *)nullptr);
        _exit(127);
    }
    bool sent = false, grown = false;
    int status = 0;
    for (;;) {
        const double t = std::chrono::duration<double>(Clock::now() - t0).count();
        if (!grown && t > 1) {  // the kernel sends the child SIGWINCH
            const winsize big{30, 100, 1000, 600};
            grown = ioctl(fd, TIOCSWINSZ, &big) == 0;
        }
        if (!sent && t > 2.5) sent = write(fd, "q", 1) == 1;
        if (t > 60) {
            kill(pid, SIGKILL), waitpid(pid, &status, 0), secs = t;
            return -1;
        }
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, 50) > 0) {
            char b[65536];
            const ssize_t n = read(fd, b, sizeof b);
            if (n > 0) {
                out.append(b, size_t(n));
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;  // EIO: the terminal's last user is gone
        }
        if (waitpid(pid, &status, WNOHANG) == pid) {  // drain what is left, then stop
            char b[65536];
            for (ssize_t n; poll(&p, 1, 100) > 0 && (n = read(fd, b, sizeof b)) > 0;) out.append(b, size_t(n));
            secs = std::chrono::duration<double>(Clock::now() - t0).count();
            close(fd);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
    }
    waitpid(pid, &status, 0);
    secs = std::chrono::duration<double>(Clock::now() - t0).count();
    close(fd);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
#endif
