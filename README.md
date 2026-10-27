# gListening

Music, rendered in your terminal.

- Fourteen original GLSL scenes driven by system audio, an audio file, or a test signal
- Beat detection and an automatic scene director
- Kitty-graphics output with a half-block fallback
- Keyboard controls and a persistent config

**Best terminals:** [Ghostty](https://ghostty.org/docs/install/binary) or [Kitty](https://sw.kovidgoyal.net/kitty/binary/) give full-resolution graphics. Other UTF-8, 24-bit terminals fall back to half blocks.

## Installation

gListening is installed **from source**. There are no releases and no npm package at present.

### Prerequisites

- **macOS 14.2+:** Xcode Command Line Tools, CMake
- **Linux:** EGL, OpenGL 3.3, a Pulse-compatible server with `parec`
- **Windows 10 1809+ / 11:** Visual Studio 2022 with C++, CMake
- **ffmpeg:** needed only for file input (`--file`) on macOS and Windows

All platforms need CMake 3.21+ and a C++17 compiler. Node 20 is the minimum for the optional launcher tests; a supported LTS release is recommended. See [docs/install.md](docs/install.md) for required vs optional OS dependencies and official installers, and [docs/releasing.md](docs/releasing.md) for the publication gates.

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

### Build

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

On Apple Silicon (27 Oct), the 9 native tests and 10 npm tests passed. Other operating systems have configured targets but were not retested locally. `npm test` only exercises the launcher; Node 20 is optional. Full evidence and limits: [docs/verification.md](docs/verification.md).

## Usage

From a source build, run the uninstalled executable from the repository root:

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

`--check-audio` plays a quiet tone and checks that it comes back through output loopback. Local live capture is **not verified**. The latest macOS diagnostic received 0 frames and the cause is unproven; see [docs/verification.md](docs/verification.md). Please check macOS System Settings > Privacy & Security > Screen & System Audio Recording for the terminal or app hosting gListening, and confirm the correct default output device is selected. No audio driver is required.

System audio sources:

- macOS: CoreAudio tap (grant the audio-recording permission)
- Windows: WASAPI output loopback
- Linux: Pulse output monitor

A microphone is never substituted. Protected audio may be unavailable.

## Gallery

Each preview is a real 6-second offscreen render with original synthesized audio, not a terminal screen recording. Details: [docs/demos/README.md](docs/demos/README.md).

| ID | Scene | Idea | Preview |
|---|---|---|---|
| `aurora` | Aurora | Folding light curtains over a lake | ![Aurora: green light curtains over a mountain lake](docs/demos/aurora.jpg) [MP4](docs/demos/aurora.mp4) |
| `mercury` | Mercury | Liquid chrome blobs | ![Mercury: liquid chrome blobs in a dark studio](docs/demos/mercury.jpg) [MP4](docs/demos/mercury.mp4) |
| `galaxy` | Galaxy | Spiral of instanced stars | ![Galaxy: spiral of stars with a bright core](docs/demos/galaxy.jpg) [MP4](docs/demos/galaxy.mp4) |
| `tunnel` | Tunnel | Hexagonal corridor of spectrum rings | ![Tunnel: hexagonal corridor lit by spectrum rings](docs/demos/tunnel.jpg) [MP4](docs/demos/tunnel.mp4) |
| `ridgeline` | Ridgeline | Spectrum history as dusk landscape | ![Ridgeline: contoured hills at dusk](docs/demos/ridgeline.jpg) [MP4](docs/demos/ridgeline.mp4) |
| `ink` | Ink | Luminous dye in a current | ![Ink: glowing dye swirling in water](docs/demos/ink.jpg) [MP4](docs/demos/ink.mp4) |
| `quasicrystal` | Quasicrystal | Never-repeating wave rosettes | ![Quasicrystal: interfering waves forming rosettes](docs/demos/quasicrystal.jpg) [MP4](docs/demos/quasicrystal.mp4) |
| `phosphor` | Phosphor | CRT-beam waveform ring | ![Phosphor: glowing CRT waveform ring](docs/demos/phosphor.jpg) [MP4](docs/demos/phosphor.mp4) |
| `rainglass` | Rain Glass | Rain on a window over neon | ![Rain Glass: raindrops on glass before a blurred neon street](docs/demos/rainglass.jpg) [MP4](docs/demos/rainglass.mp4) |
| `cathedral` | Laser Cathedral | Light beams through haze and arches | ![Laser Cathedral: laser beams under tall arches](docs/demos/cathedral.jpg) [MP4](docs/demos/cathedral.mp4) |
| `magnetic` | Magnetic Field | Filings tracing three magnets | ![Magnetic Field: glowing iron filings around magnets](docs/demos/magnetic.jpg) [MP4](docs/demos/magnetic.mp4) |
| `loom` | Loom | Threads weaving an evolving fabric | ![Loom: luminous woven threads](docs/demos/loom.jpg) [MP4](docs/demos/loom.mp4) |
| `papercity` | Paper City | Folded paper skyline | ![Paper City: folded paper buildings lit along streets](docs/demos/papercity.jpg) [MP4](docs/demos/papercity.mp4) |
| `clockwork` | Clockwork | Gears, pendulum and orrery | ![Clockwork: meshing gears, pendulum and orrery](docs/demos/clockwork.jpg) [MP4](docs/demos/clockwork.mp4) |

## Controls

| Key | Action |
|---|---|
| `tab` | Control panel |
| `?` | Key help |
| `1`–`9` | Choose scene |
| `space`, `→`/`n` | Next scene |
| `←`/`p` | Previous scene |
| `a` | Auto director on/off |
| `c` | Cycle palette |
| `r` | New look |
| `+` `-` / `[` `]` | Sensitivity / intensity |
| `h` | HUD |
| `q`/`esc` | Quit |

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
