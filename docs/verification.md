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

- Other three operating systems were not tested locally. Prior remote CI at `fef00e2`: Linux, Windows and Intel macOS PASS; macOS Arm FAIL (bundle skipped).
- Live audio: a CoreAudio tap with a default AudioQueue diagnostic received 0 frames and reported that CoreAudio stopped delivering system audio. The stalled run was stopped. The cause is unproven. Human consent and output-routing verification are needed; no privilege was granted. No audio capture is guaranteed.
- npm registry returned 404/ENEEDAUTH. Nothing was published or released.
- Discussions are disabled and have no categories. `gh repo edit --enable-discussions` was denied by auto-review as insufficient setting authorization. The command did not execute and was not retried.
- Automatic CodeRabbit is disabled. Pullfrog (trusted app 1768019) is required by active ruleset 25354593. Zero repository Actions secrets does not establish provider billing.

## Publication hold

Publication stays held until no-new-spend is verified, current-head review and CI pass, and the owner chooses the exact source, version and artifacts. The license remains GPL-3.0-only. No bot commands, merges, workflow or review-setting changes, paid Extra or credentials were used. The host independently verifies the final integration after apply.

## Owner choices retained

Bug-only Issues; Discussions for features (pending); pull requests at the maintainer's discretion; new styles encouraged; Ghostty and Kitty on macOS/Linux.
