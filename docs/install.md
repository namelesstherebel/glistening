# Install Glistening

Glistening [v0.1.0](https://github.com/namelesstherebel/glistening/releases/tag/v0.1.0) is published on GitHub. Download a prebuilt archive below, or [build from source](#build-from-source).

Official links were checked on 27 Oct 2026. Commands for Ubuntu, Debian and Windows source builds were not executed locally.

## Prebuilt downloads

Native archives need no Node.js and no compiler. Live audio needs the runtime requirements below. FFmpeg is optional on every OS and only for `--file`.

### Runtime requirements (prebuilt)

- **macOS:** 14.2 or later.
- **Windows:** 10 1809+ or 11, x64, with an OpenGL 3.3 graphics driver.
- **Linux:** x64 with EGL and OpenGL 3.3, plus PulseAudio or PipeWire-pulse and `parec` for live audio.
- **FFmpeg (optional):** only for `--file`, on all OSes.

| Platform | Asset | SHA-256 |
|---|---|---|
| macOS Apple Silicon | [glistening-0.1.0-darwin-arm64-d68325d.tar.gz](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-darwin-arm64-d68325d.tar.gz) | `048c8aced9209f3ac5c3800b34d30fced815e94c625cf7c7d638193c6d2f6ebf` |
| macOS Intel | [glistening-0.1.0-darwin-x64-d68325d.tar.gz](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-darwin-x64-d68325d.tar.gz) | `8d942b63aaa278fe95ea1db170515790b8662078a89253f7e0c7deb7e1955967` |
| Linux x64 | [glistening-0.1.0-linux-x64-d68325d.tar.gz](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-linux-x64-d68325d.tar.gz) | `728cebb509874391f7197bbfd726316e5c4dba743bf736b275062b207cfc1a72` |
| Windows x64 | [glistening-0.1.0-win32-x64-d68325d.zip](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-win32-x64-d68325d.zip) | `6b480e5563cbc7fca4d29736a038d5f1c9c15b9031ba5d3a6bc8c3d5ad55606c` |
| Complete npm tarball | [glistening-0.1.0-d68325d.tgz](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-d68325d.tgz) | `980837fab8247b7583749a3089a1103bde45e85f05e3874e3537a6be9f62e98d` |

Also in the release: [SHA256SUMS](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/SHA256SUMS), [manifest.json](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/manifest.json), [SOURCE.json](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/SOURCE.json) and the GPL-3.0 [LICENSE](https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/LICENSE).

Each command below uses a new, empty folder. Archives extract to a folder named like the archive without its extension.

### macOS Apple Silicon

```sh
mkdir glistening-0.1.0 && cd glistening-0.1.0
curl -fLO https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-darwin-arm64-d68325d.tar.gz
echo "048c8aced9209f3ac5c3800b34d30fced815e94c625cf7c7d638193c6d2f6ebf  glistening-0.1.0-darwin-arm64-d68325d.tar.gz" | shasum -a 256 -c
tar -xzf glistening-0.1.0-darwin-arm64-d68325d.tar.gz
./glistening-0.1.0-darwin-arm64-d68325d/glistening --self-test
```

### macOS Intel

```sh
mkdir glistening-0.1.0 && cd glistening-0.1.0
curl -fLO https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-darwin-x64-d68325d.tar.gz
echo "8d942b63aaa278fe95ea1db170515790b8662078a89253f7e0c7deb7e1955967  glistening-0.1.0-darwin-x64-d68325d.tar.gz" | shasum -a 256 -c
tar -xzf glistening-0.1.0-darwin-x64-d68325d.tar.gz
./glistening-0.1.0-darwin-x64-d68325d/glistening --self-test
```

The Intel build is unsigned and the Apple Silicon build is ad hoc signed. Neither is notarized, and no notarization is claimed.

### Linux x64

```sh
mkdir glistening-0.1.0 && cd glistening-0.1.0
curl -fLO https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-linux-x64-d68325d.tar.gz
echo "728cebb509874391f7197bbfd726316e5c4dba743bf736b275062b207cfc1a72  glistening-0.1.0-linux-x64-d68325d.tar.gz" | sha256sum -c
tar -xzf glistening-0.1.0-linux-x64-d68325d.tar.gz
./glistening-0.1.0-linux-x64-d68325d/glistening --self-test
```

### Windows x64 (PowerShell)

```powershell
mkdir glistening-0.1.0; cd glistening-0.1.0
Invoke-WebRequest -Uri https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-win32-x64-d68325d.zip -OutFile glistening-0.1.0-win32-x64-d68325d.zip
(Get-FileHash glistening-0.1.0-win32-x64-d68325d.zip -Algorithm SHA256).Hash.ToLower()
# expected: 6b480e5563cbc7fca4d29736a038d5f1c9c15b9031ba5d3a6bc8c3d5ad55606c
Expand-Archive glistening-0.1.0-win32-x64-d68325d.zip -DestinationPath .
.\glistening-0.1.0-win32-x64-d68325d\glistening.exe --self-test
```

### npm tarball (Node 20+)

The tarball contains all four native executables. It is a GitHub download. The package is not published to the npm registry, so install from the GitHub tarball URL below. Run these two lines in PowerShell or a POSIX shell with Node 20+:

```
npm install --prefix ./glistening-local --ignore-scripts --no-audit --no-fund https://github.com/namelesstherebel/glistening/releases/download/v0.1.0/glistening-0.1.0-d68325d.tgz
node ./glistening-local/node_modules/glistening/bin/glistening.cjs --self-test
```

The tarball's SHA-256 is in the table above.

## Build from source

Source builds need the compilers and tools below. Prebuilt archives do not.

### Source requirements

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

- **Node.js 20 or later**, preferably a supported LTS from [nodejs.org](https://nodejs.org/en/download). It is needed only for the npm tarball and launcher tests, not for native archives.
- **Python 3 with ffmpeg and ffprobe**. It is needed only for the demo.

## npm launcher

The package is not published to the npm registry. Install the [GitHub tarball](#npm-tarball-node-20) instead. Registry publication is blocked separately by missing npm authentication (ENEEDAUTH), not by GitHub.

The launcher's setup command has two modes:

- `setup --dry-run` is read-only. It reports what it would do and changes nothing.
- `setup` installs system dependencies. Run it only when you want that.

This guide does not cover paid add-ons or driver installation.
