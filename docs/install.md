# Install Glistening

Glistening has no published releases. Build it from source using the steps below.

Official links were checked on 27 Oct 2026. Commands for Ubuntu, Debian and Windows were not executed locally.

## Requirements

- A C++17 compiler and OpenGL 3.3.
- [CMake](https://cmake.org/download/) 3.21 or later on all platforms.
- [git](https://git-scm.com/downloads) to clone the repository.
- [FFmpeg](https://ffmpeg.org/download.html) is required for file input only. Live capture on macOS and Windows does not need FFmpeg.
- macOS: [Xcode Command Line Tools](https://developer.apple.com/xcode/resources/).
- Linux: [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/) (or PipeWire-pulse) for live audio, and [Mesa](https://www.mesa3d.org/) for OpenGL.

## macOS 14.2+

1. Install the command line tools:

   ```sh
   xcode-select --install
   ```

2. Install [Homebrew](https://brew.sh). Follow the instructions on that page. Do not pipe a download into a shell.
3. Install the build dependencies:

   ```sh
   brew install cmake ffmpeg
   ```

4. Optional: install one terminal, Ghostty or kitty.

   ```sh
   brew install --cask ghostty
   # or
   brew install --cask kitty
   ```

   Official binaries: [Ghostty](https://ghostty.org/docs/install/binary), [kitty](https://sw.kovidgoyal.net/kitty/binary/). Native macOS and Linux are the best experience.

## Ubuntu 22.04+ / Debian 12+

```sh
sudo apt-get update
sudo apt-get install -y git g++ cmake pkg-config libegl1-mesa-dev libgl1-mesa-dri pulseaudio-utils ffmpeg
```

- Live audio needs an existing PulseAudio or PipeWire-pulse server. There is no microphone fallback.
- Optional: `sudo apt-get install kitty` installs kitty from your distribution.
- Ghostty's documentation lists an Ubuntu package only for 26.04+. Do not assume `apt-get install ghostty` works on other releases. See the [Ghostty install page](https://ghostty.org/docs/install/binary).

## Windows 10 1809+ / Windows 11 (x64)

Install:

- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) with the Desktop development with C++ workload.
- [CMake](https://cmake.org/download/) 3.21 or later.
- [FFmpeg](https://ffmpeg.org/download.html).

With [winget](https://learn.microsoft.com/en-us/windows/package-manager/winget/) already available, you can install CMake and FFmpeg:

```powershell
winget install --id Kitware.CMake --exact
winget install --id Gyan.FFmpeg --exact --source winget
```

Then reopen **Developer PowerShell for VS 2022** so the tools are on `PATH`.

Use Windows Terminal, which supports truecolor blocks. Ghostty and kitty have no native Windows build.

## Build and run

```sh
git clone https://github.com/namelesstherebel/glistening.git glistening
cd glistening
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Run on macOS or Linux:

```sh
./build/glistening
```

Run on Windows:

```powershell
.\build\Release\glistening.exe
```

## Optional tools

- **Node.js 20 or later**, preferably a supported LTS from [nodejs.org](https://nodejs.org/en/download). It is needed only for the npm launcher and its tests.
- **Python 3 with ffmpeg and ffprobe**. It is needed only for the demo.

## npm launcher

The package is not in the npm registry (it returns 404), and no releases exist. Do not run `npm install -g glistening`.

Only if you have a verified release archive, install it locally:

```sh
npm install --prefix ./glistening-local ./glistening-0.1.0.tgz
./glistening-local/node_modules/.bin/glistening
```

The launcher's setup command has two modes:

- `setup --dry-run` is read-only. It reports what it would do and changes nothing.
- `setup` installs system dependencies. Run it only when you want that.

This guide does not cover paid add-ons or driver installation.
