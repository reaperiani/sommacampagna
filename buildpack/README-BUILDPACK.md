# sommacampagna Buildpack (macOS + Linux)

This buildpack adds simple scripts to build the plugin on macOS and Linux.

## Contents

- `buildpack/bootstrap-juce.sh`: clones JUCE locally (shallow clone)
- `buildpack/build-macos.sh`: configures and builds VST3 + AU on macOS
- `buildpack/build-external-macos.sh`: builds the external engine plus Universal AU and VST3 plugins
- `buildpack/build-linux.sh`: configures and builds VST3 + LV2 on Linux

## Prerequisites

- CMake 3.22+
- A C++20 compiler
- Git
- JUCE source (you can use `bootstrap-juce.sh`)

macOS:
- Xcode Command Line Tools (`xcode-select --install`)

Linux:
- Ninja recommended
- Example Debian/Ubuntu packages:
  - `build-essential`
  - `cmake`
  - `ninja-build`
  - `pkg-config`
  - `libasound2-dev`
  - `libx11-dev`
  - `libxext-dev`
  - `libxinerama-dev`
  - `libxrandr-dev`
  - `libxcursor-dev`
  - `libfreetype6-dev`
  - `libfontconfig1-dev`
  - `libgl1-mesa-dev`
  - `libcurl4-openssl-dev`

## Quick Start

From project root:

```bash
chmod +x buildpack/*.sh
./buildpack/bootstrap-juce.sh
./buildpack/build-macos.sh
# external-summing engine, AU, and VST3 products:
./buildpack/build-external-macos.sh
# or
./buildpack/build-linux.sh
```

By default JUCE is cloned into `external/JUCE` and build output is in `build-macos` or `build-linux`.

## Artifacts

- macOS VST3: `build-macos/sommacampagna_artefacts/Release/VST3/`
- macOS AU: `build-macos/sommacampagna_artefacts/Release/AU/`
- External macOS app/plugins: `build-external-osx/*_artefacts/Release/`
- Linux VST3: `build-linux/sommacampagna_artefacts/Release/VST3/`
- Linux LV2: `build-linux/sommacampagna_artefacts/Release/LV2/`

## Notes

- Format matrix:
  - macOS: `VST3`, `AU`
  - Linux: `VST3`, `LV2`
- On Linux/macOS, plugin scan path depends on DAW and user/system VST3 directories.
