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

- The other three platform targets were not tested locally. Historical CI on AEC head, run 40592503463: all four native builds and the full npm bundle passed; the four archive integrity checks and the final bundle clean-install passed on Linux CI and Apple Silicon. The later keyboard change supersedes those AEC binaries. CI and artifacts for the new head are pending, and no release exists yet.
- Live audio: the maintainer personally tested live playback on 27 Oct 2026. The hosted zero-frame diagnostic never passed: it received 0 of 132300 frames over 3 s, with a stalled teardown in `TapCapture::~TapCapture` (CoreAudio `AudioDeviceDestroyIOProcID`). The default output was valid and the tap matched it. OS consent was not inferred and no TCC changes were made. The maintainer states no further consent or retest is required. The diagnostic's root cause is not asserted, and the hosted diagnostic is not claimed as a pass.
- npm registry lookup returned 404 and `npm whoami` ENEEDAUTH. This blocks registry publication only, not a GitHub tarball or release. Nothing has been published or released.
- Discussions were enabled on 27 Oct 2026 after explicit owner approval, with the categories Announcements, General, Ideas, Polls, Q&A and Show and Tell. The Ideas URL was verified: https://github.com/namelesstherebel/glistening/discussions/categories/ideas. An earlier denial of the enable command is historic and superseded by the authorized activation.
- Automatic CodeRabbit is disabled. Pullfrog (trusted app 1768019) is required by active ruleset 25354593. Historical review run 39801015273 (job 119901382450) reviewed PR 6 head `fef00e2` using Sol and does not cover any newer head; a current-head Pullfrog review is still required.
- Pullfrog console, verified signed in on 27 Oct 2026 (https://pullfrog.com/console/namelesstherebel/glistening): provider ChatGPT (Codex), existing connected subscription inherited from the account; model `openai/gpt-6.1-sol` High; account Free; personal BYOK API keys unconfigured; no Router. Draft PRs OFF, auto-merge OFF, Fix CI on non-Pullfrog PRs OFF, no active runs. No settings were changed.

## Publication hold

The earlier unknown billing hold is resolved for draft PR publication: a draft PR does not trigger Pullfrog review and automatic CodeRabbit is disabled. Normal native/semgrep CI may run, and the draft review check is expected to be missing. No new credit spending or API fallback is authorized. An actual review verdict on the current head and passing CI are still required before merge or release. Proposed version 0.1.0 from the exact source, after CI, review and artifact checks, is the authorized download goal; no further generic owner release approval applies. Missing npm account authentication remains a separate npm publication blocker. The license remains GPL-3.0-only. No bot commands, merges, workflow or review-setting changes, paid Extra or credentials were used. The host independently verifies the final integration after apply.

## Owner choices retained

Bug-only Issues; Discussions (Ideas) for features; pull requests at the maintainer's discretion; new styles encouraged; Ghostty and Kitty on macOS/Linux.
