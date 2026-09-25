#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
JUCE_DIR="$ROOT_DIR/external/JUCE"

if [ -d "$JUCE_DIR/.git" ]; then
    echo "JUCE already present at: $JUCE_DIR"
    exit 0
fi

mkdir -p "$ROOT_DIR/external"
git clone --depth 1 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
echo "JUCE cloned to: $JUCE_DIR"
