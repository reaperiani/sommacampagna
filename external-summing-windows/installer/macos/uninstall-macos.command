#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This uninstaller runs only on macOS." >&2
    exit 1
fi

if [[ -z "${HOME:-}" || "$HOME" == "/" ]]; then
    echo "Refusing to uninstall with an unsafe HOME directory." >&2
    exit 1
fi

purge=false
case "${1:-}" in
    "") ;;
    --purge) purge=true ;;
    *) echo "Usage: bash uninstall-macos.command [--purge]" >&2; exit 2 ;;
esac

targets=(
    "$HOME/Applications/sommacampagna_engine.app"
    "$HOME/Library/Audio/Plug-Ins/VST3/sommacampagna_sender.vst3"
    "$HOME/Library/Audio/Plug-Ins/VST3/sommacampagna_receiver.vst3"
    "$HOME/Library/Audio/Plug-Ins/Components/sommacampagna_sender.component"
    "$HOME/Library/Audio/Plug-Ins/Components/sommacampagna_receiver.component"
)

echo "Close all DAWs before continuing."
echo "The following user-local bundles will be removed:"
for target in "${targets[@]}"; do
    echo "  $target"
done

if [[ "$purge" == true ]]; then
    echo "  $HOME/Library/sommacampagna (configuration)"
fi

printf "Continue? [y/N] "
read -r reply
case "$reply" in
    y|Y|yes|YES) ;;
    *) echo "Uninstall cancelled."; exit 0 ;;
esac

for target in "${targets[@]}"; do
    if [[ -e "$target" ]]; then
        rm -rf "$target"
        echo "Removed: $target"
    fi
done

if [[ "$purge" == true && -d "$HOME/Library/sommacampagna" ]]; then
    rm -rf "$HOME/Library/sommacampagna"
    echo "Removed: $HOME/Library/sommacampagna"
fi

killall AudioComponentRegistrar 2>/dev/null || true
echo "Uninstall complete. Restart the DAW if it is running."
