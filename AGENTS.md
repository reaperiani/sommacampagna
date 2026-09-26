# AGENTS.md

## Project Boundaries
- This is C++20/JUCE with two independent CMake projects: the root `sommacampagna` plugin and `external-summing-windows/` (historical name) for the cross-platform engine, sender, and receiver.
- Root entrypoints are `Source/PluginProcessor.*` and `PluginEditor.*`; `Source/SummerDSP.h` is also compiled into the external engine, so DSP changes affect both products.
- External entrypoints are `engine/main.cpp`, `sender/PluginProcessor.*`, and `receiver/PluginProcessor.*`; transport/config ABI lives in `shared/Protocol.h` and `PortDiscovery.h`.
- `external/JUCE/` and `external/JUCE-master/` are ignored local checkouts, not repository-owned source. Do not edit them; `buildpack/bootstrap-juce.sh` fetches JUCE 8.0.12.

## Build Commands
- There are no CMake presets. Every configure requires `-DJUCE_DIR=<JUCE checkout>`; CI and release scripts use JUCE 8.0.12.
- Root Windows VST3: `cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DJUCE_DIR="C:/path/to/JUCE"`, then `cmake --build build --config Release --target sommacampagna_VST3`.
- Root macOS/Linux: run `./buildpack/bootstrap-juce.sh`, then `./buildpack/build-macos.sh` or `./buildpack/build-linux.sh`; these build VST3+AU or VST3+LV2 respectively.
- External Windows: `cmake -B build-external -S external-summing-windows -G "Visual Studio 17 2022" -A x64 -DJUCE_DIR="C:/path/to/JUCE"`, then `cmake --build build-external --config Release --target sommacampagna_engine sommacampagna_sender_VST3 sommacampagna_receiver_VST3`.
- External macOS Universal: `JUCE_DIR="/path/to/JUCE-8.0.12" ./buildpack/build-external-macos.sh`; this builds engine, VST3, and AU for `arm64;x86_64` and verifies every binary with `lipo`.

## Verification
- No unit/integration/CTest/lint/formatter suite is committed. Build the smallest affected target; changes to `Protocol.h`, `PortDiscovery.h`, or shared sender/receiver behavior require all three external Windows targets and the macOS VST3/AU CI build.
- Manual root-plugin checks require a `16 in / 2 out` layout. External checks require engine + sender(s) + receiver, matching sample rates, real-time playback, and common DAW buffer sizes.
- External offline/faster-than-real-time bounce is not guaranteed, and its buffering latency is not reported to the DAW for PDC; test parallel/dry paths explicitly.

## External Transport
- UDP is loopback-only: sender to engine defaults to `45570`, engine to receiver to `45571`; runtime ports and global `transmissionBufferMs` are published under `juce::File::userApplicationDataDirectory/sommacampagna/udp-ports.txt`.
- UDP/file work belongs on the existing worker threads, never in `processBlock`. Audio callbacks must not allocate, lock, log, block, or perform file/network I/O.
- The sender always clears its plugin output; `bypassSend` suppresses sending and is not audio passthrough.
- `PacketHeader` is sent as a raw fixed-layout struct. Coordinate protocol/layout changes across sender, engine, and receiver, preserve its static assertions, and bump `protocolVersion` for incompatible packets.
- Keep old port files readable: missing or invalid `transmissionBufferMs` must fall back safely and remain clamped to protocol limits.

## Compatibility Constraints
- Do not rename APVTS parameter IDs, plugin/manufacturer codes, or bundle IDs after release; hosts use them for automation, session recall, and plugin identity.
- Keep nonlinear/accumulation DSP internals in `double` and audio-buffer I/O in `float`.
- Preserve the existing 4-space C++ formatting and brace style; avoid unrelated reformatting.

## CI And Releases
- `.github/workflows/external-summing-build.yml` builds Windows x64 and macOS Universal on relevant pushes/PRs; tag builds also package both platforms and generate SHA-256 checksums.
- External versioning comes from `external-summing-windows/CMakeLists.txt`. Update it with `CHANGELOG.md`, push the candidate, and require both platform jobs to pass before tagging the exact matching `v<version>`.
- A matching tag creates a draft GitHub Release. The workflow may replace assets only while it remains a draft and intentionally refuses to overwrite a published release.
- CI artifacts are not Authenticode-signed, Developer-ID-signed, or notarized. Follow `external-summing-windows/RELEASE.md` before publishing, and never commit signing credentials to this public repository.
