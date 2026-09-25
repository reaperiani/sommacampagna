# sommacampagna external summing (Windows MVP)

This is a new, isolated project folder that leaves the original multichannel VST3 project untouched.

## What is included

- `sommacampagna_engine.exe`: external localhost UDP summing engine
- `sommacampagna_sender.vst3`: sender plugin
  - stereo send
  - target pair selector (`Pair 1..8`)
  - pre-send gain (`-60 dB .. +12 dB`)
- `sommacampagna_receiver.vst3`: receiver plugin
  - returns main stereo sum from engine by default
  - output trim
  - connection status in UI

## Important MVP notes

- Transport is localhost UDP only (`127.0.0.1`).
- This is a functional MVP skeleton.
- Engine currently forwards the latest sender block to main sum output.
  - Next iteration should accumulate multiple concurrent sender streams by `sessionId` + `pairIndex`.

## Ports

- Sender -> Engine: `45570`
- Engine -> Receiver: `45571`

## Build (Windows)

```powershell
cmake -B build-external -S external-summing-windows -G "Visual Studio 17 2022" -DJUCE_DIR="C:/path/to/JUCE"
cmake --build build-external --config Release --target sommacampagna_engine
cmake --build build-external --config Release --target sommacampagna_sender_VST3
cmake --build build-external --config Release --target sommacampagna_receiver_VST3
```

## Quick run

1. Start `sommacampagna_engine.exe`.
2. Load `sommacampagna_sender.vst3` on source tracks.
3. Choose target pair and pre-send gain in each sender.
4. Load `sommacampagna_receiver.vst3` on a return track.
5. Receiver outputs the engine main stereo sum.
