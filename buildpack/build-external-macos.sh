#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-external-osx}"
JUCE_DIR="${JUCE_DIR:-$ROOT_DIR/external/JUCE}"

if [ ! -d "$JUCE_DIR" ]; then
    echo "JUCE 8.0.12 not found at: $JUCE_DIR"
    echo "Set JUCE_DIR to a JUCE 8.0.12 checkout."
    exit 1
fi

if ! grep -q 'project(JUCE VERSION 8.0.12' "$JUCE_DIR/CMakeLists.txt"; then
    echo "JUCE checkout at $JUCE_DIR is not version 8.0.12."
    exit 1
fi

cmake -B "$BUILD_DIR" -S "$ROOT_DIR/external-summing-windows" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DJUCE_DIR="$JUCE_DIR"

cmake --build "$BUILD_DIR" --target \
    sommacampagna_engine \
    sommacampagna_sender_VST3 \
    sommacampagna_sender_au_AU \
    sommacampagna_receiver_VST3 \
    sommacampagna_receiver_au_AU

binaries=(
    "$BUILD_DIR/sommacampagna_engine_artefacts/Release/sommacampagna_engine.app/Contents/MacOS/sommacampagna_engine"
    "$BUILD_DIR/sommacampagna_sender_artefacts/Release/VST3/sommacampagna_sender.vst3/Contents/MacOS/sommacampagna_sender"
    "$BUILD_DIR/sommacampagna_sender_au_artefacts/Release/AU/sommacampagna_sender.component/Contents/MacOS/sommacampagna_sender"
    "$BUILD_DIR/sommacampagna_receiver_artefacts/Release/VST3/sommacampagna_receiver.vst3/Contents/MacOS/sommacampagna_receiver"
    "$BUILD_DIR/sommacampagna_receiver_au_artefacts/Release/AU/sommacampagna_receiver.component/Contents/MacOS/sommacampagna_receiver"
)

for binary in "${binaries[@]}"; do
    lipo -verify_arch arm64 x86_64 "$binary"
    lipo -info "$binary"
done

echo "Universal external-summing artifacts are in: $BUILD_DIR"
