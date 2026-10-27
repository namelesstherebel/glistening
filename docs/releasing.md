# Releasing gListening

Release-readiness runbook for `glistening` 0.1.0 (Node >=20). **Nothing has been published, tagged or released.** For end-user installation, see the [README](../README.md).

## Status (27 Oct)

- Local checks passed on Apple Silicon: 9 native tests, npm 10 tests, install smoke. All 14 demos verified (6.4 MiB).
- GitHub workflow runs on pull requests and manually only. Artifacts are kept 2 days. At remote `fef00e2`, Mac arm64 failed, the other three passed, and the bundle step was skipped.
- No tag or release pipeline exists.
- The host will prepare a local Apple Silicon archive only and report its hashes. That is not universal release readiness.

## Blockers

- A bundle needs four genuine, freshly built native executables. Linux and Windows binaries from earlier local builds were not verified against fresh source. Do not use them.
- npm registry lookup for `glistening` returned 404, and `npm whoami` returned ENEEDAUTH. The owner must choose and authorize an existing account, version and payload. Do not create credentials or guess a namespace.
- Automatic review spend is unconfirmed. CodeRabbit automatic review is disabled. The Pullfrog workflow references optional provider secrets, but `gh secret list` returned no names, so cost and billing cannot be inferred. No automatic review has been triggered. Use a normal branch and draft PR only once this is established as safe.

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
- Owner visual acceptance.
- No merge or deploy bypass.
- Owner-selected npm account.

## Binary archive requirements

Any native binary archive must include `LICENSE`, the source version, checksums and provenance. GPL-3.0-only applies, so link to the corresponding source. Do not claim signing or notarization. Make no support or rights promises.

## User install (once a verified archive exists)

```
npm install --prefix ./glistening-local ./glistening-0.1.0.tgz
```

Run `setup --dry-run` first, and explain that `setup` installs dependencies.
