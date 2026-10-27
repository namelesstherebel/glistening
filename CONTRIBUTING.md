# Contributing to gListening

Thanks for your interest. gListening renders music-reactive visuals in the terminal. It is licensed GPL-3.0-only, and contributions are accepted under that same license. This guide does not change the license or add any support commitments.

## Ways to help

- **Bugs:** open an Issue using the bug report template. Issues are for bugs only.
- **Feature requests and ideas:** these belong in GitHub Discussions. Discussions is **not enabled yet** (no categories exist), so this is pending activation. Until then, please don't file feature requests as Issues. Check back once Discussions is turned on.
- **Pull requests:** welcome. They are reviewed on merit, at the maintainer's discretion and when time allows. There is no review deadline or guaranteed response time.
- **New visualizer styles:** we explicitly encourage them. A new scene is one of the best contributions you can make (see [Adding a scene](#adding-a-scene)).

If a PR is declined, please know it isn't personal. A change can be a poor fit for scope, maintenance cost, or direction, and there are no hard feelings. You're welcome to keep your fork or try again with a different idea.

## Requirements

- CMake 3.21 or newer and a C++17 compiler
- Node.js 20 or newer (only for the npm launcher tests)
- A working OpenGL 3.3 context. The render tests fail on hosts without one.
- Linux: `pkg-config` and EGL development files. Optional MilkDrop mode needs projectM 3.x (Linux only).
- macOS 14.2 or newer is the deployment target (Core Audio process taps). Windows builds use CMake.

## Checkout, configure, build, test

```sh
git clone https://github.com/namelesstherebel/glistening.git
cd glistening
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
npm test
```

The executable is `build/glistening` on Linux/macOS and `build\Release\glistening.exe` on Windows. `npm test` runs `node --test tests/package.test.cjs` and covers the npm launcher only. The C++ behavior is covered by `ctest`. Tests write config into the build tree, not your real settings.

On Linux and macOS a plain `make` and `make test` also work (see the `Makefile`). CMake is the path for Windows.

Please don't commit build output. A passing build on one platform is not evidence for the others, so say which platform you actually tested.

## Code style

There is no formatter or linter configured, and please don't add one in a feature PR. Match the surrounding code:

- Compact C++17: follow the existing naming, layout and brace style of the file you edit. The build uses `-Wall` (and `/W3` on MSVC), so keep it warning-free.
- GLSL: scene shaders live in `scenes/`, shared shaders in `shaders/`. Reuse the common header (`scenes/common.glsl`) and its uniforms rather than redeclaring them.
- Platform-specific code stays under `platform/`. Keep OS-specific behavior out of shared files.
- Keep diffs focused. Avoid unrelated reformatting or renames.

## Project layout

- `viz.cpp` and the root `*.hpp` files: application and renderer
- `scenes/`, `shaders/`: GLSL, embedded into the binary at build time by `cmake/embed-shaders.cmake`
- `platform/`: OS-specific GPU context and audio capture
- `tests/`: unit, render, terminal smoke and npm launcher tests
- `bin/`: npm launcher

## Tests

Add meaningful tests for each behavior change. Prefer a regression check that would fail before your change and pass after it. Say in the PR what fails without the fix. Cosmetic-only or docs-only changes may not need new tests, but explain why.

Config and capture must stay permission-safe: tests must not read or write a contributor's real config (use `XDG_CONFIG_HOME` / `APPDATA` pointing at a temp directory, as the CMake tests do) and must not require system audio capture. `--check-audio` needs explicit capture permission and is not a mandatory test.

## Adding a scene

A new scene should include:

1. **Scene shader** in `scenes/`. Shaders are embedded by the CMake glob (re-run configure/build after adding files; the Makefile uses its own wildcard list).
2. **Scene metadata and registration** wherever existing scenes are registered in the source. Follow an existing scene as the pattern, so it appears in `--list-scenes`.
3. **Render checks:** the scene renders non-blank, finite output across sizes (including wide) via `glistening_render`. Run `ctest` and add a regression in `tests/render.cpp` if your scene has special behavior.
4. **Audio response:** show it reacts to music and stays sensible on silence.
5. **Flash safety:** avoid rapid full-screen flashing or strobing. Describe how brightness changes are bounded, and mention any flashing risk in the PR.
6. **Media rights:** see below.

Run the new scene with `--test-signal` and, where you can, `--dump` to review frames.

## Rights and media

- Use only audio you have the right to share. Do not attach commercial recordings, private songs or others' samples.
- Generate or record original test audio, or use clearly licensed material, and state its source and license in the PR.
- Screenshots, clips and previews must be your own work or have a stated license and provenance.
- Don't include secrets, tokens, private paths or personal data in logs, configs or captures.

## Bug reports

Please include enough to reproduce: OS and version, CPU architecture, terminal and its graphics mode (blocks or kitty), GPU and driver or software renderer, backend (EGL, CGL, WGL), audio source, and the build commit SHA or version from `glistening --version`. Use the bug report template.

## Pull requests

- Keep each PR scoped to one change with a readable diff.
- Describe what changed, how you validated it, and on which platforms.
- Link evidence: test output, and screenshots or clips for visual changes.
- Be honest about dependencies, limitations and anything untested.
- Expect normal CI checks to be reviewed against the **current head** of your branch. Re-check after pushing new commits.
- For visual changes, the repository owner makes the final visual acceptance.
- Don't change merge, deploy, repository settings or license files as part of a contribution, and don't edit reviewer or automation configuration.
