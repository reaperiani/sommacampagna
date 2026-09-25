# AGENTS.md

## Repo Shape

- C++20 JUCE/CMake repo with two separate products: root `sommacampagna` multichannel plugin and `external-summing-windows` sender/engine/receiver MVP.
- Root plugin entrypoints: `Source/PluginProcessor.*`, `Source/PluginEditor.*`, DSP in `Source/SummerDSP.h`.
- External summing entrypoints: `external-summing-windows/engine/main.cpp`, `sender/PluginProcessor.*`, `receiver/PluginProcessor.*`, shared UDP/config in `external-summing-windows/shared/Protocol.h` and `PortDiscovery.h`.
- `external/JUCE-master/` is a vendored JUCE checkout; do not edit it unless the task is explicitly about JUCE itself.

## Build Commands

- All CMake config requires `-DJUCE_DIR=<path-to-JUCE>`; there are no CMake presets.
- Root Windows configure/build:
  `cmake -B build -S . -G "Visual Studio 17 2022" -DJUCE_DIR="C:/path/to/JUCE"`
  `cmake --build build --config Release --target sommacampagna_VST3`
- Root macOS/Linux scripts expect JUCE at `external/JUCE` unless `JUCE_DIR` is set:
  `./buildpack/bootstrap-juce.sh`
  `./buildpack/build-macos.sh`
  `./buildpack/build-linux.sh`
- External summing Windows configure/build:
  `cmake -B build-external -S external-summing-windows -G "Visual Studio 17 2022" -DJUCE_DIR="C:/path/to/JUCE"`
  `cmake --build build-external --config Release --target sommacampagna_engine`
  `cmake --build build-external --config Release --target sommacampagna_sender_VST3`
  `cmake --build build-external --config Release --target sommacampagna_receiver_VST3`
- External summing Universal macOS build:
  `JUCE_DIR="/path/to/JUCE-8.0.12" ./buildpack/build-external-macos.sh`
  This builds the engine plus sender/receiver VST3 and AU products for `arm64;x86_64`.

## Verification

- No committed unit, integration, CTest, lint, or formatter pipeline exists.
- After code changes, build the smallest affected CMake target; for shared external protocol changes, build `sommacampagna_engine`, `sommacampagna_sender_VST3`, and `sommacampagna_receiver_VST3`.
- Manual audio checks matter: root plugin expects `16 in / 2 out`; external summing expects engine running first, senders on source tracks, receiver on return/aux.

## External Summing Gotchas

- Transport is localhost UDP (`127.0.0.1`): sender to engine defaults `45570`, engine to receiver defaults `45571`.
- Engine, sender, and receiver discover runtime ports via `juce::File::userApplicationDataDirectory/sommacampagna/udp-ports.txt`; this path is cross-platform through JUCE, not Windows-only.
- `PortDiscovery.h` also carries global `transmissionBufferMs`; keep old config files compatible by defaulting/clamping missing or invalid values.
- Sender mutes its plugin output after sending to avoid doubled audio.
- Engine GUI owns the global transmission buffer control; receiver reads it from the shared config file.

## Code Constraints

- Keep APVTS parameter IDs stable; changing IDs breaks automation/session recall.
- Audio callbacks must not allocate, lock, log, do file I/O, or block; preallocate buffers and use atomics for processor/editor or thread communication.
- Use `double` for nonlinear/accumulation DSP internals and `float` for audio buffer I/O.
- Preserve existing 4-space formatting and brace style; avoid unrelated reformatting.

## Existing Instruction Files

- No `.cursorrules`, `.cursor/rules/`, `.github/copilot-instructions.md`, `CLAUDE.md`, or repo-local `opencode.json` are present at the time this file was updated.
