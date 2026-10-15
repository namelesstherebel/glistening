// Linux and macOS: termios terminal, fork/exec PCM commands in their own
// process group, XDG-style config. Native audio devices live in
// audio_macos.mm; on Linux, capture and playback go through Pulse/PipeWire
// commands (parec, ffmpeg -f pulse), so the device API reports that.
#include "platform.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace tv::sys {

volatile std::sig_atomic_t quitFlag = 0, quitSignal = 0;

static volatile std::sig_atomic_t g_resized = 0;
static volatile std::sig_atomic_t g_child = 0;  // the audio command's process group, for the startup handler
static termios g_tio;
static volatile std::sig_atomic_t g_raw = 0;

std::vector<std::string> args(int argc, char **argv) { return std::vector<std::string>(argv, argv + argc); }

std::string env(const char *name) {
    const char *v = getenv(name);
    return v && *v ? v : "";
}

std::string configDir() {
    std::string d = env("XDG_CONFIG_HOME");
    if (d.empty()) d = (env("HOME").empty() ? std::string(".") : env("HOME")) + "/.config";
    mkdir(d.c_str(), 0755);
    d += "/glistening";
    mkdir(d.c_str(), 0755);
    return d;
}

bool readable(const std::string &path) { return access(path.c_str(), R_OK) == 0; }
FILE *open(const std::string &path, const char *mode) { return fopen(path.c_str(), mode); }

std::string findProgram(const std::string &name) {
    if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0 ? name : "";
    const std::string path = env("PATH");
    for (size_t a = 0; a <= path.size();) {
        size_t b = path.find(':', a);
        if (b == std::string::npos) b = path.size();
        const std::string p = (b > a ? path.substr(a, b - a) : std::string(".")) + "/" + name;
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(p.c_str(), X_OK) == 0) return p;
        a = b + 1;
    }
    return "";
}

const char *osName() {
#ifdef __APPLE__
    return "macos";
#else
    return "linux";
#endif
}

// ---- terminal

bool isTerminal() { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

// No SA_RESTART: a quit signal must interrupt a blocking read (offline dumps) so the loop can unwind.
static void onSignal(int sig, void (*h)(int)) {
    struct sigaction sa {};
    sa.sa_handler = h;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
}
static void onQuit(int s) { quitFlag = 1, quitSignal = s; }

// Until the main loop runs, a signal must still give the terminal back, and
// stop the audio command (its own group: it won't get the terminal's signal).
// Bounded, async-signal-safe.
static void startupSignal(int s) {
    if (g_raw) tcsetattr(STDIN_FILENO, TCSANOW, &g_tio);
    static const char reset[] = "\x1b[0m\x1b[?7h\x1b[?25h";
    if (::write(STDOUT_FILENO, reset, sizeof reset - 1) < 0) {}
    if (g_child > 0) {
        kill(-g_child, SIGTERM);
        const timespec tick{0, 10000000};
        for (int k = 0; k < 50; k++) {
            while (waitpid(-g_child, nullptr, WNOHANG) > 0) {}
            if (kill(-g_child, 0) < 0 && errno == ESRCH) break;
            nanosleep(&tick, nullptr);
        }
        kill(-g_child, SIGKILL);
    }
    _exit(128 + s);
}

std::string enterRaw() {
    if (tcgetattr(STDIN_FILENO, &g_tio) < 0) return std::string("cannot read terminal settings: ") + strerror(errno);
    termios raw = g_tio;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0, raw.c_cc[VTIME] = 0;
    raw.c_cc[VQUIT] = raw.c_cc[VSUSP] = _POSIX_VDISABLE;  // preserve Ctrl-C; other signal keys become ordinary input
#ifdef VDSUSP
    raw.c_cc[VDSUSP] = _POSIX_VDISABLE;  // macOS delayed suspend (Ctrl-Y) also raises SIGTSTP
#endif
    for (int sig : {SIGINT, SIGTERM, SIGHUP}) signal(sig, startupSignal);
    // Publish restoration state before changing the terminal: a startup signal
    // during tcsetattr must restore even if the change already took effect.
    g_raw = 1;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
        const int error = errno;
        restoreInput();
        return std::string("cannot set raw mode: ") + strerror(error);
    }
    return "";
}

void restoreInput() {
    if (g_raw) tcsetattr(STDIN_FILENO, TCSANOW, &g_tio), g_raw = 0;
}

void runHandlers() {
    for (int sig : {SIGINT, SIGTERM, SIGHUP}) onSignal(sig, onQuit);
    signal(SIGWINCH, [](int) { g_resized = 1; });
    signal(SIGPIPE, SIG_IGN);
}

void quitHandlers() {
    for (int sig : {SIGINT, SIGTERM, SIGHUP}) onSignal(sig, onQuit);
}

void write(const std::string &s) {
    for (size_t off = 0; off < s.size();) {
        ssize_t n = ::write(STDOUT_FILENO, s.data() + off, s.size() - off);
        if (n < 0) {
            if (errno == EINTR) {
                if (quitFlag) return;  // a stuck terminal must not hold up shutdown
                continue;
            }
            if (errno == EAGAIN) {
                if (quitFlag) return;  // nor a full buffer
                pollfd p{STDOUT_FILENO, POLLOUT, 0};
                ::poll(&p, 1, 50);
                continue;
            }
            return;
        }
        off += size_t(n);
    }
}

std::string readInput(int waitMs) {
    std::string in;
    if (waitMs > 0) {
        pollfd p{STDIN_FILENO, POLLIN, 0};
        if (::poll(&p, 1, waitMs) <= 0) return in;
    }
    char b[256];
    ssize_t n;
    while ((n = ::read(STDIN_FILENO, b, sizeof b)) > 0) in.append(b, size_t(n));
    return in;
}

TermSize size() {
    winsize ws{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    return {ws.ws_col ? ws.ws_col : 80, ws.ws_row ? ws.ws_row : 24, ws.ws_xpixel, ws.ws_ypixel};
}

bool resized() {
    if (!g_resized) return false;
    g_resized = 0;
    return true;
}

// ---- shared memory

bool shmSupported() { return true; }

std::string shmName(unsigned frame) { return "/glistening-" + std::to_string(getpid()) + "-" + std::to_string(frame); }

bool shmPut(const std::string &name, const void *data, size_t n) {
    const int fd = shm_open(name.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) return false;
    bool ok = false;
    if (ftruncate(fd, off_t(n)) == 0) {
        void *mp = mmap(nullptr, n, PROT_WRITE, MAP_SHARED, fd, 0);
        if (mp != MAP_FAILED) memcpy(mp, data, n), munmap(mp, n), ok = true;
    }
    close(fd);
    if (!ok) shm_unlink(name.c_str());  // never leave a half-made object behind
    return ok;
}

bool shmExists(const std::string &name) {
    const int fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (fd < 0) return false;
    close(fd);
    return true;
}

void shmUnlink(const std::string &name) { shm_unlink(name.c_str()); }

// ---- processes

struct Process::Impl {
    pid_t pid = -1, pgid = -1;  // the command (until reaped) and its process group (until stopped)
    int fd = -1;
    bool exited = false;
    int st = 0;  // wait status once exited
};

Process::Process() : p_(new Impl) {}
Process::~Process() { stop(); }

// fork + exec with stdout on a pipe, stdin and stderr on /dev/null (the
// terminal belongs to glistening), in its own process group so a pipeline is
// stopped as a whole. Only async-signal-safe calls between fork and exec:
// the program path is resolved before forking.
static bool spawn(Process::Impl &p, const char *path, char *const argv[], bool blocking) {
    int pfd[2], ready[2];
    if (pipe(pfd) < 0) return false;
    if (pipe(ready) < 0) return close(pfd[0]), close(pfd[1]), false;
    for (int f : {pfd[0], pfd[1], ready[0], ready[1]}) fcntl(f, F_SETFD, FD_CLOEXEC);
    sigset_t blocked, previous;
    sigemptyset(&blocked);
    for (int sig : {SIGINT, SIGTERM, SIGHUP}) sigaddset(&blocked, sig);
    if (sigprocmask(SIG_BLOCK, &blocked, &previous) < 0) {
        close(pfd[0]), close(pfd[1]), close(ready[0]), close(ready[1]);
        return false;
    }
    const pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        // Do not run the parent's terminal/process cleanup inside the child.
        for (int sig : {SIGINT, SIGTERM, SIGHUP}) signal(sig, SIG_DFL);
        close(ready[1]);
        // Other parent threads may receive a startup signal despite this thread's
        // mask. Do not exec unless the parent published our group first: its
        // death closes ready and makes us exit without starting the command.
        char go;
        ssize_t n;
        do { n = ::read(ready[0], &go, 1); } while (n < 0 && errno == EINTR);
        close(ready[0]);
        if (n != 1) _exit(127);
        sigprocmask(SIG_SETMASK, &previous, nullptr);
        const int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) dup2(null, STDIN_FILENO), dup2(null, STDERR_FILENO);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[0]), close(pfd[1]);
        if (null > STDERR_FILENO) close(null);
        execv(path, argv);
        _exit(127);
    }
    close(pfd[1]), close(ready[0]);
    if (pid < 0) {
        close(pfd[0]), close(ready[1]);
        sigprocmask(SIG_SETMASK, &previous, nullptr);
        return false;
    }
    setpgid(pid, pid);
    p.pid = p.pgid = pid, g_child = pid;
    p.fd = pfd[0], p.exited = false;
    // A child killed before the handshake must not raise SIGPIPE in the parent.
    sigset_t pipeSignal, oldMask, pending;
    sigemptyset(&pipeSignal), sigaddset(&pipeSignal, SIGPIPE);
    sigprocmask(SIG_BLOCK, &pipeSignal, &oldMask);
    sigpending(&pending);
    const char go = 1;
    ssize_t sent;
    do { sent = ::write(ready[1], &go, 1); } while (sent < 0 && errno == EINTR);
    if (sent < 0 && errno == EPIPE && !sigismember(&pending, SIGPIPE)) {
        sigset_t generated;
        sigpending(&generated);
        int sig;
        if (sigismember(&generated, SIGPIPE)) sigwait(&pipeSignal, &sig);  // consume only the SIGPIPE this write generated
    }
    close(ready[1]);
    sigprocmask(SIG_SETMASK, &oldMask, nullptr);
    sigprocmask(SIG_SETMASK, &previous, nullptr);
    if (sent != 1) {
        kill(-pid, SIGKILL);
        while (waitpid(pid, &p.st, 0) < 0 && errno == EINTR) {}
        close(p.fd);
        p.fd = -1, p.pid = p.pgid = -1, p.exited = true, g_child = 0;
        return false;
    }
    if (!blocking) fcntl(p.fd, F_SETFL, fcntl(p.fd, F_GETFL) | O_NONBLOCK);
    return true;
}

bool Process::start(const std::vector<std::string> &argv, bool blocking) {
    if (argv.empty() || p_->pgid > 0) return false;
    const std::string path = findProgram(argv[0]);
    if (path.empty()) return false;
    std::vector<char *> a;
    for (auto &s : argv) a.push_back(const_cast<char *>(s.c_str()));
    a.push_back(nullptr);
    return spawn(*p_, path.c_str(), a.data(), blocking);
}

bool Process::startShell(const std::string &cmd, bool blocking) {
    if (p_->pgid > 0) return false;
    char sh[] = "sh", c[] = "-c";
    char *const a[] = {sh, c, const_cast<char *>(cmd.c_str()), nullptr};
    return spawn(*p_, "/bin/sh", a, blocking);
}

bool Process::open() const { return p_->fd >= 0; }

long Process::read(void *buf, size_t n) {
    if (p_->fd < 0) return 0;
    const ssize_t r = ::read(p_->fd, buf, n);
    if (r > 0) return long(r);
    if (r < 0 && (errno == EAGAIN || errno == EINTR)) return -1;
    close(p_->fd), p_->fd = -1;  // end of output (or a broken pipe: the same to us)
    return 0;
}

bool Process::poll() {
    if (!p_->exited && p_->pid > 0 && waitpid(p_->pid, &p_->st, WNOHANG) == p_->pid) p_->exited = true, p_->pid = -1;
    return p_->exited;
}

bool Process::wait(int ms) {
    for (int k = 0; k < ms / 2 && !poll(); k++) usleep(2000);
    return poll();
}

bool Process::failed() const { return p_->exited && (!WIFEXITED(p_->st) || WEXITSTATUS(p_->st) != 0); }
int Process::status() const { return WIFEXITED(p_->st) ? WEXITSTATUS(p_->st) : 128 + WTERMSIG(p_->st); }

// The command may never end on its own (or ignore a closed pipe): ask the
// whole group to stop, give it half a second, then kill it. A pipeline member
// may outlive the shell that started it. Afterwards the group id is forgotten,
// so a recycled id is never signalled.
void Process::stop() {
    Impl &p = *p_;
    if (p.fd >= 0) close(p.fd), p.fd = -1;
    if (p.pgid <= 0) return;
    auto groupGone = [&] { return kill(-p.pgid, 0) < 0 && errno == ESRCH; };
    if (p.pid > 0 || !groupGone()) {
        kill(-p.pgid, SIGTERM);
        for (int k = 0; k < 50; k++) {
            if (p.pid > 0 && waitpid(p.pid, &p.st, WNOHANG) == p.pid) p.pid = -1, p.exited = true;
            if (p.pid <= 0 && groupGone()) break;
            usleep(10000);
        }
        if (p.pid > 0 || !groupGone()) kill(-p.pgid, SIGKILL);
        if (p.pid > 0) waitpid(p.pid, &p.st, 0), p.pid = -1, p.exited = true;
    }
    p.pgid = -1, g_child = 0;
}

#ifndef __APPLE__
std::unique_ptr<Capture> openSystemCapture(std::string &err) {
    err = "Linux captures the output monitor through parec (Pulse/PipeWire)";
    return nullptr;
}
std::unique_ptr<Player> openPlayer(std::string &err) {
    err = "Linux plays files through ffmpeg's Pulse output";
    return nullptr;
}
#endif

}  // namespace tv::sys
