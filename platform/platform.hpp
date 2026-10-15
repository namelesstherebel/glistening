// Operating-system services glistening needs: UTF-8 arguments and paths, the
// terminal (raw input, VT output, size, resize, quit), child processes that
// print PCM, and native system-audio capture and playback. One implementation
// per OS: posix.cpp (Linux, macOS), windows.cpp, audio_macos.mm, audio_wasapi.cpp.
// Strings are UTF-8 everywhere; Windows converts at this boundary.
#pragma once
#include <csignal>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace tv::sys {

// Set by signal / console-control handlers once the main loop is running (and
// for headless runs): quit asks the loop to unwind; quitSignal is the signal
// number (Windows: SIGINT for Ctrl+C/Break, SIGTERM for close/logoff/shutdown).
extern volatile std::sig_atomic_t quitFlag, quitSignal;

std::vector<std::string> args(int argc, char **argv);  // Windows: from the wide command line
std::string env(const char *name);                     // "" when unset or empty
std::string configDir();                               // created if missing; XDG / %APPDATA%
bool readable(const std::string &path);
FILE *open(const std::string &path, const char *mode);
std::string findProgram(const std::string &name);  // path on PATH (PATHEXT on Windows), or ""
const char *osName();                              // "linux", "macos", "windows"

// ---- terminal (stdin + stdout)
struct TermSize {
    int cols, rows, w, h;  // cells; pixels (0 when the terminal does not say)
};
bool isTerminal();
// Raw, non-echoing input and VT output. Until runHandlers(), an interrupt
// restores the terminal, stops the audio command and exits (POSIX), or sets
// quitFlag (Windows). Returns "" or why the terminal can't be used.
std::string enterRaw();
void restoreInput();  // idempotent; safe after a failed enterRaw
void runHandlers();   // main loop: quit and resize become flags; SIGPIPE ignored
void quitHandlers();  // headless: interrupts only set quitFlag
// Best effort: checks quit on interrupted/would-block POSIX writes and between
// Windows chunks. Pending synchronous I/O is not guaranteed to be cancellable.
void write(const std::string &s);
std::string readInput(int waitMs);  // waits up to waitMs for the first byte; never blocks after that
TermSize size();
bool resized();  // a resize happened since the last call

// ---- POSIX shared memory for kitty's t=s medium (Linux, macOS; not Windows)
bool shmSupported();
std::string shmName(unsigned frame);  // unique per process and frame, within macOS's 31-byte limit
bool shmPut(const std::string &name, const void *data, size_t n);  // a new object holding data; nothing left on failure
bool shmExists(const std::string &name);  // the terminal unlinks each object once it has read it
void shmUnlink(const std::string &name);

// ---- a command printing raw PCM on stdout (stdin and stderr are the null device)
class Process {
public:
    Process();
    ~Process();  // stops it (bounded)
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;
    bool start(const std::vector<std::string> &argv, bool blocking);  // argv[0] from PATH; no shell
    bool startShell(const std::string &cmd, bool blocking);          // /bin/sh -c, or cmd.exe /d /s /c
    bool open() const;                                               // output not yet at its end
    long read(void *buf, size_t n);  // >0 bytes, 0 end of output (closed), -1 nothing yet
    bool poll();                     // has it exited? (non-blocking; status then known)
    bool wait(int ms);               // poll for up to ms
    bool failed() const;             // exited unsuccessfully
    int status() const;              // exit code; 128 + signal when killed (POSIX)
    void stop();  // ask it (and everything it started) to stop, then force it, bounded
    struct Impl;

private:
    std::unique_ptr<Impl> p_;
};

// ---- native audio devices (macOS, Windows). Linux uses Pulse/PipeWire through processes.
// Whatever the device format, captured audio arrives as stereo float at 44.1 kHz.
class Capture {
public:
    virtual ~Capture() = default;
    virtual int read(float *stereo, int maxFrames) = 0;  // never blocks
    virtual std::string error() = 0;                      // "" while healthy; set once the stream died
    virtual std::string describe() = 0;                   // device and native format, for diagnostics
};
std::unique_ptr<Capture> openSystemCapture(std::string &err);  // the default output's mix

// Plays stereo float 44.1 kHz on the default output; keeps up to ~0.5 s queued.
class Player {
public:
    virtual ~Player() = default;
    virtual void write(const float *stereo, int frames) = 0;  // never blocks; drops on overflow
    virtual std::string describe() = 0;
};
std::unique_ptr<Player> openPlayer(std::string &err);

}  // namespace tv::sys
