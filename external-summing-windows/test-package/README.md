# sommacampagna external summing (Windows MVP)

This is a new, isolated project folder that leaves the original multichannel VST3 project untouched.

## What is included

- `sommacampagna_engine.exe`: external localhost UDP summing engine
- `sommacampagna_sender.vst3`: sender plugin
  - stereo send
  - target pair selector (`Pair 1..8`)
  - pre-send gain (`-60 dB .. +12 dB`)
  - local queue and UDP diagnostics
- `sommacampagna_receiver.vst3`: receiver plugin
  - returns main stereo sum from engine by default
  - output trim
  - connection and transport diagnostics in UI

## Important MVP notes

- Transport is localhost UDP only (`127.0.0.1`).
- Audio is uncompressed float32 PCM tagged with the DAW sample position; no codec or rate conversion is used.
- The engine accumulates concurrent sender streams into eight stereo pairs and processes their sum.
- All sender pairs are mixed against one common sample-frame cursor. A DAW host that does not provide sample positions is not supported for synchronized mixing.
- Sender and receiver networking runs on dedicated worker threads.
- The total buffer target is split between the engine and receiver; changes apply after sender streams stop.
- Buffers add latency only; a clock correction never accelerates or slows the audio.
- Real-time playback is supported; faster-than-real-time/offline bounce is not guaranteed.

## Ports

- Sender -> Engine default: `45570`
- Engine -> Receiver default: `45571`
- Runtime ports and the buffer allocation are published through the shared discovery file.

## Build (Windows)

```powershell
cmake -B build-external -S external-summing-windows -G "Visual Studio 17 2022" -DJUCE_DIR="C:/path/to/JUCE"
cmake --build build-external --config Release --target sommacampagna_engine
cmake --build build-external --config Release --target sommacampagna_sender_VST3
cmake --build build-external --config Release --target sommacampagna_receiver_VST3
cmake --build build-external --config Release --target sommacampagna_transport_tests
ctest --test-dir build-external --build-config Release --output-on-failure
```

## Quick run

1. Start `sommacampagna_engine.exe`.
2. Load `sommacampagna_sender.vst3` on source tracks.
3. Choose target pair and pre-send gain in each sender.
4. Load `sommacampagna_receiver.vst3` on a return track.
5. Receiver outputs the engine main stereo sum.
