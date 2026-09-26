# Release process

The project version is defined by the `project(... VERSION ...)` declaration in `CMakeLists.txt`.

## Candidate build

1. Update the version and `CHANGELOG.md`.
2. Push the candidate commit and confirm both build jobs pass.
3. Create and push the matching tag, for example `v0.1.0`.
4. The workflow builds both platforms, creates SHA-256 checksums, and creates a draft GitHub Release.
5. Download the draft assets and complete the checks below before publishing it.

Re-running a tagged workflow may replace assets only while the release remains a draft. Published release assets are never overwritten by the workflow.

## Publication gates

- Confirm the project license, third-party notices, and JUCE distribution terms are present and correct.
- Build from a clean tagged checkout and confirm `BUILD-INFO.txt` contains the expected commit.
- Test the Windows package on a clean system with the documented Visual C++ Redistributable prerequisite.
- Sign the Windows engine and VST3 modules, timestamp them, and verify the Authenticode signatures.
- Sign the macOS app and plugins with Developer ID, notarize the distributed archive, and validate it with Gatekeeper.
- Verify the Windows binaries are x64 and every macOS executable contains both `arm64` and `x86_64` slices.
- Run VST3/plugin validation and `auval` for both products.
- Test sender-to-engine-to-receiver audio at 44.1, 48, 96, and 192 kHz with common DAW buffer sizes.
- Test installation and removal on clean Windows 10/11, Intel macOS 12+, and Apple Silicon macOS systems.
- Verify the downloaded ZIP files against `SHA256SUMS.txt`.

Unsigned CI artifacts are technical previews only and must not be presented as signed production releases.
