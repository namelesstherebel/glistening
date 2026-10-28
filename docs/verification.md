# Verification

## Public release v0.1.0

Release: https://github.com/namelesstherebel/glistening/releases/tag/v0.1.0 (latest). Tag `v0.1.0` targets merge `34deaa8992c05eb775392a6a88714caa28b5d56e`.

- **Source and CI:** tested CI run 40614502474 built commit `0944250b7b54650a9bdd3f27a4681527838e500b` from source head `d68325d76a92cc83e6ddd02006e8d29b4d86d126`. Its tree `05e087344db1e9904b6ad87003bb124f8538f1b7` is identical to the merged tree. The merge commit itself was not rebuilt.
- **Passed:** all 4 native jobs and the npm bundle job; Windows 10 tests and the other 9 checks; both Semgrep checks and merge Semgrep run 40621241439. The exact Pullfrog approval succeeded and PR #7 was merged.
- **Public artifacts:** all 9 release assets were fetched anonymously (HTTP 200) and their SHA-256 digests matched. The public npm tarball `glistening-0.1.0-d68325d.tgz` was installed offline and its self-test passed. Digests are listed in [releasing.md](releasing.md#release-assets).
- **Signing:** Intel macOS is unsigned, Apple Silicon is ad hoc signed, and notarization is not claimed.
- **npm registry:** not published. `npm whoami` returned ENEEDAUTH, which blocks registry publication only, not the GitHub release.
- **Live audio:** the maintainer personally tested live playback and accepted it; no further consent request is planned. The hosted diagnostic did not pass (0 of 132300 frames over 3 s, with a stalled teardown in `TapCapture::~TapCapture`); its root cause is not asserted, and it is not claimed as a pass.
- **License:** GPL-3.0-only.

## Earlier local checks

Evidence from 27 Oct 2026 on macOS, Apple Silicon (Apple M1 Ultra), OpenGL 4.1, AppleClang 21, FFmpeg 8.1, Python 3.14.7. These predate the release and were not repeated on the other operating systems locally.

- Configure and build: PASS.
- CTest: 9/9 PASS. npm tests: 10/10 PASS.
- Self-test: 14 scenes PASS.
- Clean local Apple Silicon `npm install` outside the checkout: PASS.
- Setup dry run (`brew ffmpeg`): PASS; no installer executed.
- Source guard refuses publishing, as intended.
- Demos: 14 real renderer MP4s (H.264 yuv420p, 640x360, 24 fps, 144 frames, 6 s, AAC 96k, faststart), 14 JPGs, the original WAV and a hash/tool-version manifest, 6.37 MiB in total. All were decoded and hash-checked; 3 representative previews were inspected.

## Contribution routes

Bug-only Issues; [Discussions → Ideas](https://github.com/namelesstherebel/glistening/discussions/categories/ideas) for features; pull requests at the maintainer's discretion; new styles encouraged. Ghostty and Kitty are the best terminals on macOS and Linux.
