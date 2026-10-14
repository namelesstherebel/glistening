// macOS audio: system output capture through a Core Audio process tap
// (macOS 14.2+), file playback through an AudioQueue.
//
// Capture follows Apple's "Capturing system audio with Core Audio taps": a
// private, unmuted stereo tap of every process (so playback stays audible),
// added to a private aggregate device that holds nothing but the tap (so no
// microphone or other input can ever be mixed in), read by an IOProc. The
// first use asks the user for permission (NSAudioCaptureUsageDescription in
// the embedded Info.plist); without it the tap delivers silence, which
// --check-audio reports. Manual retain/release: no ARC flag needed to build.
#include "platform.hpp"

#include "../audio.hpp"

#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <chrono>
#include <cstring>

#if defined(__MAC_14_2) && __MAC_OS_X_VERSION_MAX_ALLOWED >= __MAC_14_2
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#define GLISTENING_TAPS 1
#endif

namespace tv::sys {

static std::string osErr(OSStatus s) {
    char c[5] = {char(s >> 24), char(s >> 16), char(s >> 8), char(s), 0};
    for (char &x : c)
        if (x && (x < 32 || x > 126)) return std::to_string(int(s));  // not a four-char code
    return std::string("'") + c + "' (" + std::to_string(int(s)) + ")";
}

static long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef GLISTENING_TAPS
class API_AVAILABLE(macos(14.2)) TapCapture : public Capture {
public:
    ~TapCapture() override {
        if (proc_) AudioDeviceStop(agg_, proc_), AudioDeviceDestroyIOProcID(agg_, proc_);
        if (agg_ != kAudioObjectUnknown) AudioHardwareDestroyAggregateDevice(agg_);
        if (tap_ != kAudioObjectUnknown) AudioHardwareDestroyProcessTap(tap_);
    }

    std::string open() {
        @autoreleasepool {
            CATapDescription *d = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
            d.name = @"glistening";
            d.privateTap = YES;
            d.muteBehavior = CATapUnmuted;
            NSString *tapUID = [[d.UUID UUIDString] retain];
            OSStatus st = AudioHardwareCreateProcessTap(d, &tap_);
            [d release];
            [tapUID autorelease];
            if (st != noErr) return "cannot create a process tap: " + osErr(st);

            AudioObjectPropertyAddress fa{kAudioTapPropertyFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
            UInt32 sz = sizeof fmt_;
            if ((st = AudioObjectGetPropertyData(tap_, &fa, 0, nullptr, &sz, &fmt_)) != noErr) return "cannot read the tap format: " + osErr(st);
            if (fmt_.mFormatID != kAudioFormatLinearPCM || !(fmt_.mFormatFlags & kAudioFormatFlagIsFloat) || fmt_.mBitsPerChannel != 32 ||
                fmt_.mChannelsPerFrame < 1 || fmt_.mSampleRate < 8000)
                return "unexpected tap format (" + std::to_string(int(fmt_.mSampleRate)) + " Hz, " + std::to_string(fmt_.mBitsPerChannel) + " bit)";
            rs_ = Resampler(fmt_.mSampleRate);

            NSDictionary *desc = @{
                @kAudioAggregateDeviceNameKey : @"glistening system audio",
                @kAudioAggregateDeviceUIDKey : [[NSUUID UUID] UUIDString],
                @kAudioAggregateDeviceIsPrivateKey : @YES,
                @kAudioAggregateDeviceIsStackedKey : @NO,
                @kAudioAggregateDeviceTapAutoStartKey : @YES,
                @kAudioAggregateDeviceTapListKey : @[ @{@kAudioSubTapUIDKey : tapUID, @kAudioSubTapDriftCompensationKey : @YES} ],
            };
            if ((st = AudioHardwareCreateAggregateDevice((CFDictionaryRef)desc, &agg_)) != noErr)
                return "cannot create the capture device: " + osErr(st);
        }
        OSStatus st = AudioDeviceCreateIOProcID(agg_, ioProc, this, &proc_);
        if (st != noErr) return proc_ = nullptr, "cannot attach to the capture device: " + osErr(st);
        startNs_ = nowNs(), lastNs_ = 0;
        if ((st = AudioDeviceStart(agg_, proc_)) != noErr) return "cannot start capture: " + osErr(st);
        return "";
    }

    int read(float *stereo, int maxFrames) override { return int(ring_.pop(stereo, size_t(maxFrames))); }

    // The aggregate runs on the tap's clock and calls back continuously, even
    // in silence: a long gap means the stream is gone (device lost, daemon restart).
    std::string error() override {
        const long long last = lastNs_.load(), now = nowNs();
        if (now - (last ? last : startNs_.load()) > 3000000000LL) return "Core Audio stopped delivering system audio";
        return "";
    }

    std::string describe() override {
        return "Core Audio process tap (all processes, unmuted) · " + std::to_string(int(fmt_.mSampleRate)) + " Hz, " +
               std::to_string(fmt_.mChannelsPerFrame) + " ch float32" + (ring_.dropped() ? ", " + std::to_string(ring_.dropped()) + " frames dropped" : "");
    }

private:
    AudioObjectID tap_ = kAudioObjectUnknown, agg_ = kAudioObjectUnknown;
    AudioDeviceIOProcID proc_ = nullptr;
    AudioStreamBasicDescription fmt_{};
    Ring ring_{SR};
    Resampler rs_;
    std::atomic<long long> startNs_{0}, lastNs_{0};
    static constexpr int CHUNK = 2048;
    float stereo_[2 * CHUNK], out_[2 * (CHUNK * 4 + 8)];  // out_: room for upsampling from 11 kHz

    // Real-time thread: no locks, no allocation. Takes the first two channels
    // of whatever layout arrives (one interleaved buffer, or one per channel).
    static OSStatus ioProc(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *in, const AudioTimeStamp *, AudioBufferList *,
                           const AudioTimeStamp *, void *ctx) {
        auto &self = *static_cast<TapCapture *>(ctx);
        self.lastNs_.store(nowNs());
        if (!in || !in->mNumberBuffers) return noErr;
        const AudioBuffer &b0 = in->mBuffers[0];
        const bool planar = b0.mNumberChannels <= 1;  // one buffer per channel (or mono)
        const int ch0 = int(std::max<UInt32>(1, b0.mNumberChannels));
        const int frames = int(b0.mDataByteSize / (sizeof(float) * ch0));
        const float *L = static_cast<const float *>(b0.mData), *R = L;
        if (in->mNumberBuffers > 1 && in->mBuffers[1].mData) R = static_cast<const float *>(in->mBuffers[1].mData);
        if (!L) return noErr;
        for (int off = 0; off < frames; off += CHUNK) {
            const int n = std::min(CHUNK, frames - off);
            if (planar)
                for (int i = 0; i < n; i++) self.stereo_[2 * i] = L[off + i], self.stereo_[2 * i + 1] = R[off + i];
            else downmix(L + size_t(off) * ch0, ch0, n, self.stereo_);
            if (self.rs_.maxOut(n) > CHUNK * 4 + 8) return noErr;  // a format this code doesn't expect (rate under 11 kHz)
            self.ring_.push(self.out_, size_t(self.rs_.run(self.stereo_, n, self.out_)));
        }
        return noErr;
    }
};
#endif

std::unique_ptr<Capture> openSystemCapture(std::string &err) {
#ifdef GLISTENING_TAPS
    if (@available(macOS 14.2, *)) {
        auto c = std::make_unique<TapCapture>();
        err = c->open();
        if (err.empty()) return c;
        return nullptr;
    }
    err = "system audio capture needs macOS 14.2 or later (Core Audio process taps)";
#else
    err = "built without Core Audio process taps (needs the macOS 14.2 SDK)";
#endif
    return nullptr;
}

// Stereo float 44.1 kHz on the default output; Core Audio converts to the
// device's rate. Three ~23 ms buffers keep latency low; a starved queue plays silence.
class QueuePlayer : public Player {
public:
    ~QueuePlayer() override {
        if (q_) AudioQueueStop(q_, true), AudioQueueDispose(q_, true);
    }
    std::string open() {
        AudioStreamBasicDescription f{};
        f.mSampleRate = SR, f.mFormatID = kAudioFormatLinearPCM, f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        f.mBytesPerPacket = f.mBytesPerFrame = 8, f.mFramesPerPacket = 1, f.mChannelsPerFrame = 2, f.mBitsPerChannel = 32;
        OSStatus st = AudioQueueNewOutput(&f, refill, this, nullptr, nullptr, 0, &q_);
        if (st != noErr) return q_ = nullptr, "cannot open the default output: " + osErr(st);
        for (int i = 0; i < 3; i++) {
            AudioQueueBufferRef b;
            if ((st = AudioQueueAllocateBuffer(q_, FRAMES * 8, &b)) != noErr) return "cannot allocate playback buffers: " + osErr(st);
            refill(this, q_, b);
        }
        if ((st = AudioQueueStart(q_, nullptr)) != noErr) return "cannot start playback: " + osErr(st);
        return "";
    }
    void write(const float *stereo, int frames) override { ring_.push(stereo, size_t(frames)); }
    std::string describe() override { return "AudioQueue on the default output, 44100 Hz stereo float32"; }

private:
    static constexpr int FRAMES = 1024;
    AudioQueueRef q_ = nullptr;
    Ring ring_{SR / 2};
    static void refill(void *ctx, AudioQueueRef q, AudioQueueBufferRef b) {
        auto &self = *static_cast<QueuePlayer *>(ctx);
        float *p = static_cast<float *>(b->mAudioData);
        const size_t n = self.ring_.pop(p, FRAMES);
        std::memset(p + 2 * n, 0, (FRAMES - n) * 8);
        b->mAudioDataByteSize = FRAMES * 8;
        AudioQueueEnqueueBuffer(q, b, 0, nullptr);
    }
};

std::unique_ptr<Player> openPlayer(std::string &err) {
    auto p = std::make_unique<QueuePlayer>();
    err = p->open();
    if (err.empty()) return p;
    return nullptr;
}

}  // namespace tv::sys
