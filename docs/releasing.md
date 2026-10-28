# Releasing gListening

Runbook and record for `glistening` 0.1.0 (Node >=20 for the npm tarball). For end-user installation, see the [README](../README.md) and [install guide](install.md).

## Released: v0.1.0

[v0.1.0](https://github.com/namelesstherebel/glistening/releases/tag/v0.1.0) is published on GitHub and is the latest release.

- Tag `v0.1.0` targets merge commit `34deaa8992c05eb775392a6a88714caa28b5d56e` (PR #7). The merge commit was not rebuilt.
- The artifacts were built by tested CI run 40614502474 from source head `d68325d76a92cc83e6ddd02006e8d29b4d86d126` (build commit `0944250b7b54650a9bdd3f27a4681527838e500b`). The source tree `05e087344db1e9904b6ad87003bb124f8538f1b7` is identical to the merged tree.
- All four native jobs and the npm bundle job passed, with Windows 10 tests and the other 9 checks passing. Both Semgrep checks and the merge Semgrep run 40621241439 passed. Pullfrog approval was exact and successful.
- Public check: all nine release assets were downloaded anonymously (HTTP 200) and digest-checked. The public npm tarball was installed offline and its self-test passed.
- Not claimed: Intel macOS is unsigned, Apple Silicon macOS is ad hoc signed, and notarization is not claimed.
- Live audio: the maintainer personally tested live playback and accepted it. The hosted diagnostic did not pass, and no further consent request or retest is planned.
- License: GPL-3.0-only, preserved in the release as `LICENSE`.

## Release assets

Base URL: `https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/`

| Asset | SHA-256 |
|---|---|
| `glistening-0.1.0-darwin-arm64-d68325d.tar.gz` | `048c8aced9209f3ac5c3800b34d30fced815e94c625cf7c7d638193c6d2f6ebf` |
| `glistening-0.1.0-darwin-x64-d68325d.tar.gz` | `8d942b63aaa278fe95ea1db170515790b8662078a89253f7e0c7deb7e1955967` |
| `glistening-0.1.0-linux-x64-d68325d.tar.gz` | `728cebb509874391f7197bbfd726316e5c4dba743bf736b275062b207cfc1a72` |
| `glistening-0.1.0-win32-x64-d68325d.zip` | `6b480e5563cbc7fca4d29736a038d5f1c9c15b9031ba5d3a6bc8c3d5ad55606c` |
| `glistening-0.1.0-d68325d.tgz` | `980837fab8247b7583749a3089a1103bde45e85f05e3874e3537a6be9f62e98d` |
| `LICENSE` | `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986` |
| `manifest.json` | `878f9925be7adfe38cddbc5c73f46f7145da0a7a966f0b2994bc05608b1b47b0` |
| `SHA256SUMS` | `8684192601badfe48ca8b04b50dac3240581c7e0162bf4110c39cff638770508` |
| `SOURCE.json` | `e0114cbd7b03d5ef9a431be35c6d42cbd18fc0305b836f4d05186118c250be5a` |

## npm registry

The package is **not** published to the npm registry. `npm whoami` returned ENEEDAUTH, which blocks registry publication only; it did not block the GitHub release or tarball. Registry publication would need an owner-selected existing account, version and payload. Do not create credentials or guess a namespace.

## Targets

`linux-x64`, `darwin-arm64`, `darwin-x64`, `win32-x64`. Executables are `prebuilt/<target>/glistening`, or `glistening.exe` on Windows.

## Never publish

- A raw source `npm pack`. `package.json` `files` lists `bin/`, `prebuilt/` and `LICENSE`, so a source pack can miss prebuilt binaries.
- Test-only Windows Mesa DLLs (`opengl32.dll`, `libgallium_wgl.dll`). `check-installed.mjs` copies them into the test install only.

## Procedure for a future release

1. Run the native configure, build and ctest steps for each target. Use the repository's CMake setup. These commands are not defined in the packaging scripts.
2. Run `npm test`.
3. Collect the four tested executables as `artifacts/<target>/<name>`.
4. Assemble the bundle:

   ```
   node scripts/bundle.mjs artifacts dist
   ```

   `bundle.mjs` checks each executable's size and native format and architecture. It writes `prebuilt/SHA256SUMS.json` and runs `npm pack`. It refuses to reuse an existing `dist/glistening-0.1.0` directory.
5. Validate the final archive from outside the checkout:

   ```
   npm run test:install -- dist/glistening-0.1.0.tgz
   ```

   This installs with `--ignore-scripts`, checks the manifest, checksums and file allow-list (`BUNDLE CONTENTS PASS`), then runs the installed launcher's `--version` and `--self-test` (`INSTALL SMOKE PASS`).

Release requirements: an exact approved source commit and version, applicable CI passing, a current-head review by the trusted Pullfrog app, and no merge or deploy bypass. Use binaries built from the exact release source; never reuse binaries from an earlier head.

## Binary archive requirements

Any native binary archive must include `LICENSE`, the source version, checksums and provenance. GPL-3.0-only applies, so link to the corresponding source. Do not claim signing or notarization beyond what is true. Make no support or rights promises.

## User install

See [install.md](install.md#prebuilt-downloads) for download, checksum and run commands. For the npm tarball, run `setup --dry-run` first; `setup` installs system dependencies.
