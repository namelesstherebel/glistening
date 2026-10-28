# Releasing gListening

Release-readiness runbook for `glistening` 0.1.0 (Node >=20). **Nothing has been published, tagged or released.** For end-user installation, see the [README](../README.md).

## Status (27 Oct)

- Local checks passed on Apple Silicon: 9 native tests, npm 10 tests, install smoke. All 14 demos verified (6.4 MiB).
- GitHub workflow runs on pull requests and manually only. Artifacts are kept 2 days.
- Historical AEC head, run 40592503463: all four native builds and the full npm bundle passed; the four archive integrity checks and the final bundle clean-install passed on Linux CI and Apple Silicon.
- The keyboard change supersedes those AEC binaries. CI and artifacts for the new head are pending, and no release exists yet.
- No tag or release pipeline exists.
- The maintainer personally tested live playback on 27 Oct 2026. The hosted zero-frame diagnostic never passed, OS consent was not inferred, and no further consent or retest is required.
- The release is authorized, pending coordinated review and the required current-head Pullfrog review.

## Blockers

- A bundle needs four genuine, freshly built native executables from the exact release source. Binaries from the superseded AEC head must not be reused.
- npm registry lookup for `glistening` returned 404, and `npm whoami` returned ENEEDAUTH. This blocks registry publication only, not a GitHub tarball or release. Registry publication needs an owner-selected existing account, version and payload. Do not create credentials or guess a namespace.
- Review billing is no longer an open hold for draft PR publication. The signed-in Pullfrog console (https://pullfrog.com/console/namelesstherebel/glistening, verified 27 Oct 2026) shows: provider ChatGPT (Codex), an existing connected subscription inherited from the account; model `openai/gpt-6.1-sol` (High); account plan Free; personal BYOK API keys unconfigured; no Router used. Draft PRs checkbox OFF, auto-merge OFF, Fix CI on non-Pullfrog PRs OFF, and no active runs. A draft PR therefore does not trigger Pullfrog review, automatic CodeRabbit is disabled, and no settings were changed. Normal native/semgrep CI may run. The draft review check is expected to be missing. An actual review verdict is still required before merge or release. No new credit spending or API fallback is authorized.
- Historical Pullfrog review run 39801015273 (job 119901382450) reviewed PR 6 head `fef00e2` using Sol. It does not cover any newer head; a current-head Pullfrog review is required.

## Targets

`linux-x64`, `darwin-arm64`, `darwin-x64`, `win32-x64`. Executables are `prebuilt/<target>/glistening`, or `glistening.exe` on Windows.

## Never publish

- A raw source `npm pack`. `package.json` `files` lists `bin/`, `prebuilt/` and `LICENSE`, so a source pack can miss prebuilt binaries.
- Test-only Windows Mesa DLLs (`opengl32.dll`, `libgallium_wgl.dll`). `check-installed.mjs` copies them into the test install only.

## Procedure

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

## Final release requirements

- Exact approved source commit and version.
- Applicable CI passing, plus a review of the current head by the trusted Pullfrog app (1768019).
- Owner live-playback testing is done (27 Oct 2026); no further consent or retest is required.
- Release of version 0.1.0 from the exact source is authorized, after CI, review and artifact checks on the new head.
- No merge or deploy bypass.
- npm account authentication is absent (ENEEDAUTH). This blocks registry publication only and needs an owner-selected account; it does not block a GitHub tarball or release.

## Binary archive requirements

Any native binary archive must include `LICENSE`, the source version, checksums and provenance. GPL-3.0-only applies, so link to the corresponding source. Do not claim signing or notarization. Make no support or rights promises.

## User install (once a verified archive exists)

```
npm install --prefix ./glistening-local ./glistening-0.1.0.tgz
```

Run `setup --dry-run` first, and explain that `setup` installs dependencies.
