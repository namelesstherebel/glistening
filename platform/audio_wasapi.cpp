// Windows audio through WASAPI shared mode, each stream on its own thread
// (COM initialised there; the render loop never touches COM):
//   capture  loopback of the default output device (what is playing, after
//            the mixer; playback is untouched). Mix format (float32 or PCM
//            16/24/32, any channel count and rate) -> stereo 44.1 kHz.
//   playback stereo float 44.1 kHz with AUTOCONVERTPCM: Windows converts to
//            the device's rate and channel layout.
// Both follow the default device: when it changes or disappears the stream is
// reopened (loopback delivers silence meanwhile). Loopback sends no packets
// while nothing plays, so the capture fills such gaps (only gaps over 100 ms,
// never mid-stream: the device clock is not the steady clock) with silence.
// https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording
#include "platform.hpp"

#include "../audio.hpp"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>

namespace tv::sys {

namespace {

using Clock = std::chrono::steady_clock;

template <class T> void release(T *&p) {
    if (p) p->Release(), p = nullptr;
}

std::string hr(HRESULT h) {
    char b[32];
    snprintf(b, sizeof b, "0x%08lx", (unsigned long)h);
    return b;
}

// The default render endpoint and its id (to notice when the default changes).
HRESULT defaultOutput(IMMDevice *&dev, std::wstring &id) {
    IMMDeviceEnumerator *en = nullptr;
    HRESULT h = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en);
    if (SUCCEEDED(h)) h = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    release(en);
    LPWSTR s = nullptr;
    if (SUCCEEDED(h) && SUCCEEDED(dev->GetId(&s))) id = s, CoTaskMemFree(s);
    return h;
}

bool defaultChanged(const std::wstring &id) {
    IMMDevice *d = nullptr;
    std::wstring now;
    const bool changed = SUCCEEDED(defaultOutput(d, now)) && now != id;
    release(d);
    return changed;
}

// Mix-format samples as float. Returns false for formats WASAPI shared mode
// does not use (it is float32 in practice; PCM is handled for odd drivers).
struct Format {
    int channels = 0, rate = 0, bytes = 0, bits = 0;
    bool isFloat = false;
    bool parse(const WAVEFORMATEX *w) {
        channels = w->nChannels, rate = int(w->nSamplesPerSec), bits = w->wBitsPerSample, bytes = w->wBitsPerSample / 8;
        WORD tag = w->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE && w->cbSize >= 22) {
            const auto *x = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(w);
            const GUID &g = x->SubFormat;  // {000000TT-0000-0010-8000-00AA00389B71}
            static const unsigned char tail[8] = {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
            if (g.Data2 != 0 || g.Data3 != 0x10 || memcmp(g.Data4, tail, 8)) return false;
            tag = WORD(g.Data1);
        }
        isFloat = tag == WAVE_FORMAT_IEEE_FLOAT;
        if (isFloat) return bits == 32 && channels > 0 && rate > 0;
        return tag == WAVE_FORMAT_PCM && (bits == 16 || bits == 24 || bits == 32) && channels > 0 && rate > 0;
    }
    float sample(const BYTE *p) const {
        if (isFloat) {
            float f;
            memcpy(&f, p, 4);
            return f;
        }
        if (bytes == 2) return int16_t(p[0] | p[1] << 8) / 32768.f;
        if (bytes == 3) return int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) / 2147483648.f;
        return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24) / 2147483648.f;
    }
};

class LoopbackCapture : public Capture {
public:
    ~LoopbackCapture() override {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }

    std::string open() {
        std::promise<std::string> ready;
        auto first = ready.get_future();
        thread_ = std::thread([this, &ready] { run(&ready); });
        std::string err = first.get();
        if (!err.empty()) thread_.join();
        return err;
    }

    int read(float *stereo, int maxFrames) override { return int(ring_.pop(stereo, size_t(maxFrames))); }
    // A lost device is reopened (silence meanwhile), never fatal: describe() says so.
    std::string error() override { return ""; }
    std::string describe() override {
        std::lock_guard<std::mutex> l(mu_);
        return desc_ + (ring_.dropped() ? ", " + std::to_string(ring_.dropped()) + " frames dropped" : "");
    }

private:
    Ring ring_{SR};
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mu_;
    std::string desc_ = "WASAPI loopback of the default output";

    IMMDevice *dev_ = nullptr;
    IAudioClient *client_ = nullptr;
    IAudioCaptureClient *cap_ = nullptr;
    std::wstring id_;
    Format fmt_;
    Resampler rs_;

    void close() { release(cap_), release(client_), release(dev_); }

    HRESULT reopen() {
        close();
        HRESULT h = defaultOutput(dev_, id_);
        if (SUCCEEDED(h)) h = dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client_);
        WAVEFORMATEX *mix = nullptr;
        if (SUCCEEDED(h)) h = client_->GetMixFormat(&mix);
        if (SUCCEEDED(h) && !fmt_.parse(mix)) h = AUDCLNT_E_UNSUPPORTED_FORMAT;
        if (SUCCEEDED(h)) h = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000 /* 200 ms */, 0, mix, nullptr);
        if (mix) CoTaskMemFree(mix);
        if (SUCCEEDED(h)) h = client_->GetService(__uuidof(IAudioCaptureClient), (void **)&cap_);
        if (SUCCEEDED(h)) h = client_->Start();
        if (FAILED(h)) return close(), h;
        rs_ = Resampler(fmt_.rate);
        std::lock_guard<std::mutex> l(mu_);
        desc_ = "WASAPI loopback of the default output · " + std::to_string(fmt_.rate) + " Hz, " + std::to_string(fmt_.channels) + " ch " +
                (fmt_.isFloat ? "float" : "PCM") + std::to_string(fmt_.bits);
        return h;
    }

    void run(std::promise<std::string> *ready) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT h = reopen();
        ready->set_value(FAILED(h) ? "cannot open loopback capture of the default output (" + hr(h) + ")" : "");
        if (FAILED(h)) {
            if (SUCCEEDED(co)) CoUninitialize();
            return;
        }
        std::vector<float> dev, st, out;
        auto lastCheck = Clock::now(), covered = lastCheck;  // covered: audio (or silence) delivered up to here
        auto push = [&](const float *p, int n) { ring_.push(p, size_t(n)); };
        while (!stop_) {
            Sleep(10);
            bool lost = !cap_;
            UINT32 packet = 0;
            while (!lost && SUCCEEDED(h = cap_->GetNextPacketSize(&packet)) && packet) {
                BYTE *data;
                UINT32 frames;
                DWORD flags;
                if (FAILED(h = cap_->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                dev.resize(size_t(frames) * fmt_.channels), st.resize(size_t(frames) * 2), out.resize(size_t(rs_.maxOut(int(frames))) * 2);
                const size_t stride = size_t(fmt_.bytes);
                for (size_t i = 0; i < dev.size(); i++) dev[i] = flags & AUDCLNT_BUFFERFLAGS_SILENT ? 0.f : fmt_.sample(data + i * stride);
                cap_->ReleaseBuffer(frames);
                downmix(dev.data(), fmt_.channels, int(frames), st.data());
                push(out.data(), rs_.run(st.data(), int(frames), out.data()));
                covered = Clock::now();
            }
            if (FAILED(h)) lost = true;  // AUDCLNT_E_DEVICE_INVALIDATED and friends
            const auto now = Clock::now();
            if (now - lastCheck > std::chrono::seconds(2)) lastCheck = now, lost |= defaultChanged(id_);
            if (lost && FAILED(reopen())) {  // retried on the next pass; silence meanwhile
                std::lock_guard<std::mutex> l(mu_);
                desc_ = "WASAPI loopback: no default output device (retrying)";
            }
            // Nothing playing: no packets. Keep the stream continuous so the visuals decay.
            if (now - covered > std::chrono::milliseconds(100)) {
                static const float zeros[2 * 512] = {};
                for (long long n = (long long)(std::chrono::duration<double>(now - covered).count() * SR); n > 0; n -= 512)
                    push(zeros, int(std::min<long long>(512, n)));
                covered = now;
            }
        }
        close();
        if (SUCCEEDED(co)) CoUninitialize();
    }
};

class RenderPlayer : public Player {
public:
    ~RenderPlayer() override {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }
    std::string open() {
        std::promise<std::string> ready;
        auto first = ready.get_future();
        thread_ = std::thread([this, &ready] { run(&ready); });
        std::string err = first.get();
        if (!err.empty()) thread_.join();
        return err;
    }
    void write(const float *stereo, int frames) override { ring_.push(stereo, size_t(frames)); }
    std::string describe() override { return "WASAPI shared output on the default device, 44100 Hz stereo float32 (Windows converts)"; }

private:
    Ring ring_{SR / 2};
    std::thread thread_;
    std::atomic<bool> stop_{false};
    IMMDevice *dev_ = nullptr;
    IAudioClient *client_ = nullptr;
    IAudioRenderClient *render_ = nullptr;
    UINT32 frames_ = 0;
    std::wstring id_;

    void close() { release(render_), release(client_), release(dev_); }

    HRESULT reopen() {
        close();
        WAVEFORMATEXTENSIBLE f{};
        f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE, f.Format.nChannels = 2, f.Format.nSamplesPerSec = SR;
        f.Format.wBitsPerSample = 32, f.Format.nBlockAlign = 8, f.Format.nAvgBytesPerSec = SR * 8, f.Format.cbSize = 22;
        f.Samples.wValidBitsPerSample = 32, f.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        f.SubFormat = GUID{WAVE_FORMAT_IEEE_FLOAT, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
        HRESULT h = defaultOutput(dev_, id_);
        if (SUCCEEDED(h)) h = dev_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client_);
        if (SUCCEEDED(h))
            h = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    1000000 /* 100 ms */, 0, &f.Format, nullptr);
        if (SUCCEEDED(h)) h = client_->GetBufferSize(&frames_);
        if (SUCCEEDED(h)) h = client_->GetService(__uuidof(IAudioRenderClient), (void **)&render_);
        if (SUCCEEDED(h)) h = client_->Start();
        if (FAILED(h)) close();
        return h;
    }

    void run(std::promise<std::string> *ready) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HRESULT h = reopen();
        ready->set_value(FAILED(h) ? "cannot open the default output (" + hr(h) + ")" : "");
        if (FAILED(h)) {
            if (SUCCEEDED(co)) CoUninitialize();
            return;
        }
        auto lastCheck = Clock::now();
        while (!stop_) {
            Sleep(5);
            bool lost = !render_;
            UINT32 pad = 0;
            if (!lost && SUCCEEDED(h = client_->GetCurrentPadding(&pad))) {
                // Only real audio is queued: an empty ring lets the device play silence
                // without building latency in front of what arrives next.
                const UINT32 n = std::min<UINT32>(frames_ - pad, UINT32(ring_.size()));
                BYTE *data;
                if (n && SUCCEEDED(h = render_->GetBuffer(n, &data))) {
                    ring_.pop(reinterpret_cast<float *>(data), n);
                    h = render_->ReleaseBuffer(n, 0);
                }
            }
            if (FAILED(h)) lost = true;
            const auto now = Clock::now();
            if (now - lastCheck > std::chrono::seconds(2)) lastCheck = now, lost |= defaultChanged(id_);
            if (lost && FAILED(reopen())) close();
        }
        close();
        if (SUCCEEDED(co)) CoUninitialize();
    }
};

}  // namespace

std::unique_ptr<Capture> openSystemCapture(std::string &err) {
    auto c = std::make_unique<LoopbackCapture>();
    err = c->open();
    if (err.empty()) return c;
    return nullptr;
}

std::unique_ptr<Player> openPlayer(std::string &err) {
    auto p = std::make_unique<RenderPlayer>();
    err = p->open();
    if (err.empty()) return p;
    return nullptr;
}

}  // namespace tv::sys
