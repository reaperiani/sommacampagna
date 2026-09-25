# Build on macOS (sender/receiver + engine app)

This package includes source code for:

- `sommacampagna_engine` (standalone GUI app)
- `sommacampagna_sender` (VST3)
- `sommacampagna_receiver` (VST3)

## Prerequisites

- macOS 12+ recommended
- Xcode + Command Line Tools
- CMake 3.22+
- Ninja (`brew install ninja`)
- JUCE source checkout (local path)

## Configure

From the repository root:

```bash
cmake -B build-external-osx -S external-summing-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="/path/to/JUCE"
```

## Build

```bash
cmake --build build-external-osx --target sommacampagna_engine
cmake --build build-external-osx --target sommacampagna_sender_VST3
cmake --build build-external-osx --target sommacampagna_receiver_VST3
```

## Artifacts

- Engine app:
  - `build-external-osx/sommacampagna_engine_artefacts/Release/sommacampagna_engine.app`
- Sender VST3:
  - `build-external-osx/sommacampagna_sender_artefacts/Release/VST3/sommacampagna_sender.vst3`
- Receiver VST3:
  - `build-external-osx/sommacampagna_receiver_artefacts/Release/VST3/sommacampagna_receiver.vst3`

## Install plugins on macOS

System-wide:

- `/Library/Audio/Plug-Ins/VST3/`

User-only:

- `~/Library/Audio/Plug-Ins/VST3/`

## Runtime notes

- Start `sommacampagna_engine.app` first.
- Then load sender/receiver in DAW.
- UDP port discovery file is written to:
  - `~/Library/Application Support/sommacampagna/udp-ports.txt`
