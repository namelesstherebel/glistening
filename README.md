# gListening

![Aurora, Laser Cathedral and Clockwork rendered by gListening](docs/demos/hero.png)

Music, rendered in your terminal.

- Fourteen original GLSL scenes driven by system audio, an audio file, or a test signal
- Beat detection and an automatic scene director
- Kitty-graphics output with a half-block fallback
- Keyboard controls and a persistent config

**Best terminals:** [Ghostty](https://ghostty.org/docs/install/binary) or [Kitty](https://sw.kovidgoyal.net/kitty/binary/) give full-resolution graphics. Other UTF-8, 24-bit terminals fall back to half blocks.

## Installation

gListening [v0.1.0](https://github.com/namelesstherebel/glistening/releases/tag/v0.1.0) is published on GitHub. Download a prebuilt archive for your platform; native archives need no Node.js.

| Platform | Download |
|---|---|
| macOS Apple Silicon | [glistening-0.1.0-darwin-arm64-d68325d.tar.gz](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-darwin-arm64-d68325d.tar.gz) |
| macOS Intel | [glistening-0.1.0-darwin-x64-d68325d.tar.gz](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-darwin-x64-d68325d.tar.gz) |
| Linux x64 | [glistening-0.1.0-linux-x64-d68325d.tar.gz](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-linux-x64-d68325d.tar.gz) |
| Windows x64 | [glistening-0.1.0-win32-x64-d68325d.zip](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-win32-x64-d68325d.zip) |
| Complete npm tarball (Node 20+) | [glistening-0.1.0-d68325d.tgz](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-d68325d.tgz) |

The release also includes `SHA256SUMS`, `manifest.json`, `SOURCE.json` and `LICENSE`. The package is **not** published to the npm registry; use the GitHub tarball above, not `npm install -g glistening`.

Example for Apple Silicon (see [docs/install.md](docs/install.md) for every platform, checksums and the npm tarball):

```sh
mkdir glistening-0.1.0 && cd glistening-0.1.0
curl -fLO https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-darwin-arm64-d68325d.tar.gz
echo "048c8aced9209f3ac5c3800b34d30fced815e94c625cf7c7d638193c6d2f6ebf  glistening-0.1.0-darwin-arm64-d68325d.tar.gz" | shasum -a 256 -c
tar -xzf glistening-0.1.0-darwin-arm64-d68325d.tar.gz
./glistening-0.1.0-darwin-arm64-d68325d/glistening --self-test
```

macOS builds are not notarized: the Intel build is unsigned and the Apple Silicon build is ad hoc signed.

### Build from source

#### Prerequisites

- **macOS 14.2+:** Xcode Command Line Tools, CMake
- **Linux:** EGL, OpenGL 3.3, a Pulse-compatible server with `parec`
- **Windows 10 1809+ / 11:** Visual Studio 2022 with C++, CMake
- **ffmpeg (optional):** needed only for file input (`--file`) on macOS and Windows

Source builds need CMake 3.21+ and a C++17 compiler. Node 20+ is needed only for the npm tarball and launcher tests. See [docs/install.md](docs/install.md) for required vs optional OS dependencies and official installers.

Copy/paste dependency commands:

```sh
# macOS
xcode-select --install
brew install cmake ffmpeg
brew install --cask ghostty   # OPTIONAL; or: brew install --cask kitty

# Ubuntu
sudo apt-get update
sudo apt-get install -y git g++ cmake pkg-config libegl1-mesa-dev libgl1-mesa-dri pulseaudio-utils ffmpeg
```

```powershell
# Windows: install the Visual Studio 2022 C++ workload first
# https://visualstudio.microsoft.com/downloads/
winget install --id Kitware.CMake --exact
winget install --id Gyan.FFmpeg --exact --source winget
```

Ghostty and Kitty are native macOS/Linux terminals and give the best results. Windows Terminal falls back to half blocks.

#### Build

```sh
git clone https://github.com/namelesstherebel/glistening.git
cd glistening
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Run `build/glistening` (Unix) or `build/Release/glistening.exe` (Windows).

Optional MilkDrop mode is Linux-only, needs projectM 3, and is not bundled. No MilkDrop clip is included here.

### Testing status

- **Release CI:** the tested CI run 40614502474 built the release source and passed all 4 native jobs and the npm bundle job, with 10 native tests passing on Windows and 9 on each other platform. Both Semgrep checks passed. PR #7 was merged with the required Pullfrog approval.
- **Public artifacts:** all 9 release assets were downloaded anonymously and digest-checked; the public npm tarball was installed offline and self-tested.
- **Live audio:** the maintainer personally tested live playback. The hosted diagnostic did not pass.

Full evidence and limits: [docs/verification.md](docs/verification.md).

## Usage

The examples use a source build from the repository root. With a prebuilt archive, run the `glistening` executable inside the extracted folder instead:

```sh
./build/glistening                          # visualize system audio
./build/glistening --file song.flac         # play and visualize a file
./build/glistening --file song.flac --mute  # visualize without playback
./build/glistening --check-audio --seconds 3  # quiet-tone loopback check
./build/glistening --self-test              # render checks, no audio hardware
./build/glistening --test-signal            # built-in test signal, no audio hardware needed
./build/glistening --list-scenes            # list scene IDs
./build/glistening --help                   # all options
```

On Windows the executable is `build\Release\glistening.exe`.

`--check-audio` plays a quiet tone and checks that it comes back through output loopback. The maintainer personally tested live playback on 27 Oct 2026. The hosted zero-frame diagnostic never passed (0 frames, cause unproven), so `--check-audio` is not a verified pass; see [docs/verification.md](docs/verification.md). If capture is silent, check macOS System Settings > Privacy & Security > Screen & System Audio Recording for the terminal or app hosting gListening, and confirm the correct default output device is selected. No audio driver is required.

System audio sources:

- macOS: CoreAudio tap (grant the audio-recording permission)
- Windows: WASAPI output loopback
- Linux: Pulse output monitor

A microphone is never substituted. Protected audio may be unavailable.

## Gallery

Each preview is a real 6-second offscreen render with original synthesized audio, not a terminal screen recording. Details: [docs/demos/README.md](docs/demos/README.md).

| Scene | Scene |
|---|---|
| **Aurora** (`aurora`)<br>![Aurora: green light curtains over a mountain lake](docs/demos/aurora.gif)<br>Folding light curtains over a lake<br>[MP4 with audio](docs/demos/aurora.mp4) | **Mercury** (`mercury`)<br>![Mercury: liquid chrome blobs in a dark studio](docs/demos/mercury.gif)<br>Liquid chrome blobs<br>[MP4 with audio](docs/demos/mercury.mp4) |
| **Galaxy** (`galaxy`)<br>![Galaxy: spiral of stars with a bright core](docs/demos/galaxy.gif)<br>Spiral of instanced stars<br>[MP4 with audio](docs/demos/galaxy.mp4) | **Tunnel** (`tunnel`)<br>![Tunnel: hexagonal corridor lit by spectrum rings](docs/demos/tunnel.gif)<br>Hexagonal corridor of spectrum rings<br>[MP4 with audio](docs/demos/tunnel.mp4) |
| **Ridgeline** (`ridgeline`)<br>![Ridgeline: contoured hills at dusk](docs/demos/ridgeline.gif)<br>Spectrum history as dusk landscape<br>[MP4 with audio](docs/demos/ridgeline.mp4) | **Ink** (`ink`)<br>![Ink: glowing dye swirling in water](docs/demos/ink.gif)<br>Luminous dye in a current<br>[MP4 with audio](docs/demos/ink.mp4) |
| **Quasicrystal** (`quasicrystal`)<br>![Quasicrystal: interfering waves forming rosettes](docs/demos/quasicrystal.gif)<br>Never-repeating wave rosettes<br>[MP4 with audio](docs/demos/quasicrystal.mp4) | **Phosphor** (`phosphor`)<br>![Phosphor: glowing CRT waveform ring](docs/demos/phosphor.gif)<br>CRT-beam waveform ring<br>[MP4 with audio](docs/demos/phosphor.mp4) |
| **Rain Glass** (`rainglass`)<br>![Rain Glass: raindrops on glass before a blurred neon street](docs/demos/rainglass.gif)<br>Rain on a window over neon<br>[MP4 with audio](docs/demos/rainglass.mp4) | **Laser Cathedral** (`cathedral`)<br>![Laser Cathedral: laser beams under tall arches](docs/demos/cathedral.gif)<br>Light beams through haze and arches<br>[MP4 with audio](docs/demos/cathedral.mp4) |
| **Magnetic Field** (`magnetic`)<br>![Magnetic Field: glowing iron filings around magnets](docs/demos/magnetic.gif)<br>Filings tracing three magnets<br>[MP4 with audio](docs/demos/magnetic.mp4) | **Loom** (`loom`)<br>![Loom: luminous woven threads](docs/demos/loom.gif)<br>Threads weaving an evolving fabric<br>[MP4 with audio](docs/demos/loom.mp4) |
| **Paper City** (`papercity`)<br>![Paper City: folded paper buildings lit along streets](docs/demos/papercity.gif)<br>Folded paper skyline<br>[MP4 with audio](docs/demos/papercity.mp4) | **Clockwork** (`clockwork`)<br>![Clockwork: meshing gears, pendulum and orrery](docs/demos/clockwork.gif)<br>Gears, pendulum and orrery<br>[MP4 with audio](docs/demos/clockwork.mp4) |

GIFs loop silently. Open an MP4 for higher-quality playback with audio, or download the [original synthesized WAV](docs/demos/demo-audio.wav). The [static previews](docs/demos/README.md#static-previews) remain available if you prefer still images.

## Controls

| Key | Action |
|---|---|
| `tab` | Control panel |
| `?` | Key help |
| `1`–`9`, `z` `x` `v` `g` `t` | Choose scene (mapping below) |
| `→` / `←` | Next / previous scene in order, wrapping across all 14 built-ins (outside the control panel) |
| `n`, `space` | Random next scene |
| `p` | Previous scene from history |
| `a` | Auto director on/off |
| `c` | Cycle palette |
| `r` | New look |
| `+` `-` / `[` `]` | Sensitivity / intensity |
| `h` | HUD |
| `q`/`esc` | Quit |

In the control panel, `←`/`→` adjust the selected setting and `↑`/`↓` move within the panel or help, unchanged. In MilkDrop mode, `←`/`→` keep their preset behavior.

Scene shortcuts, in gallery order:

| Key | Scene |
|---|---|
| `1` | `aurora` |
| `2` | `mercury` |
| `3` | `galaxy` |
| `4` | `tunnel` |
| `5` | `ridgeline` |
| `6` | `ink` |
| `7` | `quasicrystal` |
| `8` | `phosphor` |
| `9` | `rainglass` |
| `z` | `cathedral` |
| `x` | `magnetic` |
| `v` | `loom` |
| `g` | `papercity` |
| `t` | `clockwork` |

## Configuration

Settings are saved to `~/.config/glistening/config` (macOS/Linux) or `%APPDATA%\glistening\config` (Windows).

| Variable | Purpose |
|---|---|
| `VIZ_OUTPUT=blocks` | Force half-block output |
| `VIZ_MEDIUM=direct` | In-band image transfer, for SSH |
| `VIZ_FPS` | Frame rate (default 30) |

## Troubleshooting

- **Blurry or blocky image:** use Ghostty or Kitty, or check multiplexer graphics passthrough.
- **Silence on macOS:** approve the audio-recording permission.
- **Linux capture fails:** confirm a Pulse-compatible server and `parec` are running.
- **File playback fails:** ensure `ffmpeg` is on PATH. Reopen Windows terminals after installing it.
- **Over SSH:** set `VIZ_MEDIUM=direct`.

## Contributing

Please read [CONTRIBUTING](CONTRIBUTING.md) and the [architecture notes](docs/architecture.md). Issues are for bugs only. Feature ideas belong in [Discussions → Ideas](https://github.com/namelesstherebel/glistening/discussions/categories/ideas).

Pull requests are welcome but merged at the maintainer's discretion, and no hard feelings if one isn't. New visualizer styles are especially encouraged.

## License

GPL-3.0-only. See [LICENSE](LICENSE).
