# Architecture

A concise map of gListening's dataflow, sources and test seams. See the [README](../README.md) for usage and [demo media](demos/README.md) for rendered clips.

## Dataflow

```text
system capture / ffmpeg-decoded file
        -> analysis.hpp (bands, onsets, tempo, drops) -> music.hpp (eased to frame rate)
        -> Director (when to cut) + ScenePicker (which scene)
        -> Engine (GLSL scenes, uniforms such as uFlow)
        -> post pass (bloom, vignette, dithering) -> flash limiter
        -> term.hpp encoders: kitty graphics or half blocks  (+ tui.hpp overlays)
```

1. **Input.** Live system output is captured per platform; `--file` decodes through FFmpeg, which must be on PATH. `VIZ_PCM_CMD` can override capture with a command printing f32le stereo 44.1 kHz.
2. **Analysis.** `analysis.hpp` computes per-band onset flux, tempo, a beat clock, drops, section changes, a 64-band log spectrum and the waveform. `music.hpp` eases these to frame rate.
3. **Director.** Decides when to cut (drop, section change, phrase length, timer fallback) and which scene fits the music's energy against each scene's `@busy` rating.
4. **Engine.** Renders the selected scene's GLSL (`scenes/*.frag`, optional `.vert` particle pass) with shared audio uniforms documented in `scenes/common.glsl`.
5. **Post and limiter.** Post pass adds bloom, drop-only chromatic aberration, vignette and dithering. The flash limiter caps per-frame luminance rise, re-running post at lower exposure if needed.
6. **Output.** `term.hpp` encodes frames as kitty graphics or truecolor half blocks; `tui.hpp` draws the HUD and panels.

`--dump` bypasses the terminal: it renders headless through the same engine and writes PPM frames.

## Source map

| Path | Role |
|---|---|
| `viz.cpp` | main loop, CLI |
| `engine.hpp`, `gl.hpp` | renderer, GL loader |
| `gpu.hpp`, `platform/gpu_*.cpp` | GL context per OS |
| `platform/` | OS audio and platform code |
| `analysis.hpp`, `music.hpp` | audio analysis |
| `director.hpp` | Director, ScenePicker |
| `tui.hpp`, `term.hpp`, `config.hpp` | UI, output encoders, settings |
| `milkdrop.hpp` | optional MilkDrop mode |
| `scenes/`, `shaders/` | GLSL, embedded at build time |
| `tests/` | unit, render, pty smoke tests |
| `cmake/` | shader embedding, dump smoke script |

## Platform distinctions

| | GL context | Audio capture |
|---|---|---|
| macOS | CGL (`gpu_cgl.cpp`) | CoreAudio taps (`audio_macos.mm`); needs recording permission |
| Windows | WGL (`gpu_wgl.cpp`) | WASAPI loopback (`audio_wasapi.cpp`) |
| Linux | EGL (`gpu_egl.cpp`) | default output monitor via `parec` |

Audio and graphics are independent: capture location is the machine running gListening, while the display protocol depends on the terminal. MilkDrop (projectM 3.x) is Linux-only, optional and enabled by CMake only when projectM is found; it is not part of the built-in fourteen scenes.

## Build and test seams

- CMake 3.21+, C++17. Shaders under `scenes/` and `shaders/` are embedded into a generated `shaders.inc`, so rebuilds track shader edits.
- Static libraries `glistening_gpu` (context) and `glistening_platform` (OS code) separate platform code from the executable, so tests link only what they need.
- `ctest` runs `unit` (`tests/test.cpp`), `render`, `render_regressions` and `render_wide` (`tests/render.cpp`, needs a real GL 3.3 context), `version`, `self_test`, `dump_smoke`, and pty smoke tests `terminal_blocks` and `terminal_kitty`. Config-writing tests use a build-tree config directory.
- `VIZ_SHADERS` loads shaders from disk and `VIZ_SEED` fixes the look, for iteration and reproduction.
- Platforms other than the author's have not been verified by this document.
