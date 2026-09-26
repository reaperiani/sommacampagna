# sommacampagna external summing

This isolated project contains an external summing engine plus sender and
receiver plugins. It leaves the original multichannel plugin untouched.

## Inspiration

The external summing architecture is inspired by the shared-console concepts
discussed in [The Analog Molecule - 3D Console Network](https://forum.cockos.com/showthread.php?t=305604),
created and shared on the Cockos forum by Punchipum / DocShadrach. This project
is a separate implementation and is not presented as an official version of, or
replacement for, The Analog Molecule.

## What is included

- `sommacampagna_engine.exe`: external localhost UDP summing engine
- `sommacampagna_sender`: sender plugin (VST3 on Windows/macOS, AU on macOS)
  - stereo send
  - target pair selector (`Pair 1..8`)
  - pre-send gain (`-60 dB .. +12 dB`)
- `sommacampagna_receiver`: receiver plugin (VST3 on Windows/macOS, AU on macOS)
  - returns main stereo sum from engine by default
  - output trim
  - connection status in UI

## Important MVP notes

- Transport is localhost UDP only (`127.0.0.1`).
- The engine accumulates concurrent sender streams and processes their stereo sum.
- Sender and receiver networking runs on dedicated worker threads rather than DAW audio callbacks.
- Real-time playback is supported; faster-than-real-time/offline bounce is not yet guaranteed.
- Packet loss and reordering trigger receiver re-synchronization.
- Engine and receiver apply bounded clock-drift correction while keeping the configured buffer target.
- Engine, sender, receiver, and DAW must use the same sample rate.
- The external path adds buffering latency that is not yet reported to the DAW for plug-in delay compensation.

## Ports

- Sender -> Engine default: `45570`
- Engine -> Receiver default: `45571`
- If a default port is occupied, the engine publishes the selected runtime ports through the shared discovery file.

## Build (Windows)

```powershell
cmake -B build-external -S external-summing-windows -G "Visual Studio 17 2022" -DJUCE_DIR="C:/path/to/JUCE"
cmake --build build-external --config Release --target sommacampagna_engine
cmake --build build-external --config Release --target sommacampagna_sender_VST3
cmake --build build-external --config Release --target sommacampagna_receiver_VST3
```

For Universal macOS VST3/AU builds, see [`BUILD-OSX.md`](BUILD-OSX.md).
Release packaging and publication gates are documented in [`RELEASE.md`](RELEASE.md).

## Quick run

1. Start `sommacampagna_engine.exe`.
2. Load `sommacampagna_sender.vst3` on source tracks.
3. Choose target pair and pre-send gain in each sender.
4. Load `sommacampagna_receiver.vst3` on a return track.
5. Receiver outputs the engine main stereo sum.
