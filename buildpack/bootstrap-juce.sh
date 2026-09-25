#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
JUCE_DIR="${JUCE_DIR:-$ROOT_DIR/external/JUCE}"
JUCE_TAG="8.0.12"

if [ -d "$JUCE_DIR/.git" ]; then
    CURRENT_TAG="$(git -C "$JUCE_DIR" describe --tags --exact-match 2>/dev/null || true)"
    if [ "$CURRENT_TAG" = "$JUCE_TAG" ]; then
        echo "JUCE $JUCE_TAG already present at: $JUCE_DIR"
        exit 0
    fi

    echo "JUCE at $JUCE_DIR is '$CURRENT_TAG', expected '$JUCE_TAG'."
    echo "Remove it or set JUCE_DIR to a JUCE $JUCE_TAG checkout."
    exit 1
fi

mkdir -p "$ROOT_DIR/external"
git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
echo "JUCE $JUCE_TAG cloned to: $JUCE_DIR"
