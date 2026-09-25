# Build on macOS (sender/receiver + engine app)

This package includes source code for:

- `sommacampagna_engine` (standalone GUI app)
- `sommacampagna_sender` (AU and VST3)
- `sommacampagna_receiver` (AU and VST3)

## Prerequisites

- macOS 12+ recommended
- Xcode + Command Line Tools
- CMake 3.22+
- Ninja (`brew install ninja`)
- JUCE 8.0.12 source checkout (local path)

## Configure

From the repository root:

```bash
cmake -B build-external-osx -S external-summing-windows -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DJUCE_DIR="/path/to/JUCE"
```

Alternatively, set `JUCE_DIR` and run `./buildpack/build-external-macos.sh` from the repository root.

## Build

```bash
cmake --build build-external-osx --target sommacampagna_engine
cmake --build build-external-osx --target sommacampagna_sender_VST3
cmake --build build-external-osx --target sommacampagna_sender_au_AU
cmake --build build-external-osx --target sommacampagna_receiver_VST3
cmake --build build-external-osx --target sommacampagna_receiver_au_AU
```

Verify that each executable contains both slices:

```bash
lipo -verify_arch arm64 x86_64 build-external-osx/sommacampagna_engine_artefacts/Release/sommacampagna_engine.app/Contents/MacOS/sommacampagna_engine
lipo -verify_arch arm64 x86_64 build-external-osx/sommacampagna_sender_artefacts/Release/VST3/sommacampagna_sender.vst3/Contents/MacOS/sommacampagna_sender
lipo -verify_arch arm64 x86_64 build-external-osx/sommacampagna_sender_au_artefacts/Release/AU/sommacampagna_sender.component/Contents/MacOS/sommacampagna_sender
lipo -verify_arch arm64 x86_64 build-external-osx/sommacampagna_receiver_artefacts/Release/VST3/sommacampagna_receiver.vst3/Contents/MacOS/sommacampagna_receiver
lipo -verify_arch arm64 x86_64 build-external-osx/sommacampagna_receiver_au_artefacts/Release/AU/sommacampagna_receiver.component/Contents/MacOS/sommacampagna_receiver
```

## Artifacts

- Engine app:
  - `build-external-osx/sommacampagna_engine_artefacts/Release/sommacampagna_engine.app`
- Sender VST3:
  - `build-external-osx/sommacampagna_sender_artefacts/Release/VST3/sommacampagna_sender.vst3`
- Receiver VST3:
  - `build-external-osx/sommacampagna_receiver_artefacts/Release/VST3/sommacampagna_receiver.vst3`
- Sender AU:
  - `build-external-osx/sommacampagna_sender_au_artefacts/Release/AU/sommacampagna_sender.component`
- Receiver AU:
  - `build-external-osx/sommacampagna_receiver_au_artefacts/Release/AU/sommacampagna_receiver.component`

## Install plugins on macOS

System-wide:

- `/Library/Audio/Plug-Ins/VST3/`
- `/Library/Audio/Plug-Ins/Components/`

User-only:

- `~/Library/Audio/Plug-Ins/VST3/`
- `~/Library/Audio/Plug-Ins/Components/`

## Runtime notes

- Start `sommacampagna_engine.app` first.
- Then load sender/receiver in DAW.
- UDP port discovery file is written to `~/Library/sommacampagna/udp-ports.txt`.
