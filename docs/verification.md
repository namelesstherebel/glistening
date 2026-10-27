# Verification

Evidence from 27 Oct 2026 on macOS, Apple Silicon (Apple M1 Ultra), OpenGL 4.1, AppleClang 21, FFmpeg 8.1, Python 3.14.7. Base renderer source: `484b531d3cb4f4cb56115b5fd643784ddfedcb6d`.

## Passed locally

- Configure and build: PASS.
- CTest: 9/9 PASS in 21.86 s on the normal GPU. An initial sandboxed software-render run was interrupted and replaced.
- npm tests: 10/10 PASS.
- Self-test: 14 scenes PASS.
- Clean local Apple Silicon `npm install` outside the checkout: PASS.
- Setup dry run (`brew ffmpeg`): PASS; no installer executed.
- Source guard refuses publishing, as intended.
- Demos: 14 real renderer MP4s (H.264 yuv420p, 640x360, 24 fps, 144 frames, 6 s, AAC 96k, faststart), 14 JPGs, the original WAV and a hash/tool-version manifest, 6.37 MiB in total. All were decoded and hash-checked; 3 representative previews were inspected.

## Not verified

- The other three platform targets were not tested locally. Prior remote CI at `fef00e2`: Linux, Windows and Intel macOS PASS; macOS Arm FAIL (bundle skipped).
- Live audio: the latest retest again received the same 0 of 132300 frames over 3 s after the consent wait. A sample of PID 19253 shows a stalled teardown on the main thread: `TapCapture::~TapCapture` -> `AudioDeviceDestroyIOProcID_mac_imp` -> `HALC_ProxyIOContext::_TellServerAboutStreamUsage` -> `mach_msg`. The process eventually exited with code 1. The default output (Mac Studio Speakers, 48 kHz) is valid and the tap matches it. No consent was accepted because CUA access to UserNotificationCenter is forbidden, and no TCC changes were made. The OS attributes the gListening prompt to the hosting app, ChatGPT (com.openai.codex). The smallest owner action is to review and, if appropriate, allow that system-audio OS prompt; no driver install is needed. The root cause is not asserted (permission denial is not proven); the authorization result is awaited. Capture reactivity is not claimed, and live capture stays unverified.
- npm registry returned 404/ENEEDAUTH. Nothing was published or released.
- Discussions were enabled on 27 Oct 2026 after explicit owner approval, with the categories Announcements, General, Ideas, Polls, Q&A and Show and Tell. The Ideas URL was verified: https://github.com/namelesstherebel/glistening/discussions/categories/ideas. An earlier denial of the enable command is historic and superseded by the authorized activation.
- Automatic CodeRabbit is disabled. Pullfrog (trusted app 1768019) is required by active ruleset 25354593. Historical review run 39801015273 (job 119901382450) reviewed PR 6 head `fef00e2` using Sol and does not cover a new head.
- Pullfrog console, verified signed in on 27 Oct 2026 (https://pullfrog.com/console/namelesstherebel/glistening): provider ChatGPT (Codex), existing connected subscription inherited from the account; model `openai/gpt-6.1-sol` High; account Free; personal BYOK API keys unconfigured; no Router. Draft PRs OFF, auto-merge OFF, Fix CI on non-Pullfrog PRs OFF, no active runs. No settings were changed.

## Publication hold

The earlier unknown billing hold is resolved for draft PR publication: a draft PR does not trigger Pullfrog review and automatic CodeRabbit is disabled. Normal native/semgrep CI may run, and the draft review check is expected to be missing. No new credit spending or API fallback is authorized. An actual review verdict on the current head and passing CI are still required before merge or release. Proposed version 0.1.0 from the exact source, after CI, review and artifact checks, is the authorized download goal; no further generic owner release approval applies. Missing npm account authentication remains a separate npm publication blocker. The license remains GPL-3.0-only. No bot commands, merges, workflow or review-setting changes, paid Extra or credentials were used. The host independently verifies the final integration after apply.

## Owner choices retained

Bug-only Issues; Discussions (Ideas) for features; pull requests at the maintainer's discretion; new styles encouraged; Ghostty and Kitty on macOS/Linux.
