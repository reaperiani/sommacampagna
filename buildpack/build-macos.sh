#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build-macos"
JUCE_DIR="${JUCE_DIR:-$ROOT_DIR/external/JUCE}"

if [ ! -d "$JUCE_DIR" ]; then
    echo "JUCE not found at: $JUCE_DIR"
    echo "Run: ./buildpack/bootstrap-juce.sh"
    exit 1
fi

cmake -B "$BUILD_DIR" -S "$ROOT_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR="$JUCE_DIR"
cmake --build "$BUILD_DIR" --config Release --target sommacampagna_VST3
cmake --build "$BUILD_DIR" --config Release --target sommacampagna_AU

echo "Done. Artifact expected in:"
echo "$BUILD_DIR/sommacampagna_artefacts/Release/VST3/"
echo "$BUILD_DIR/sommacampagna_artefacts/Release/AU/"
