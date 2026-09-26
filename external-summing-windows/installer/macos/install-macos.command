#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This installer runs only on macOS." >&2
    exit 1
fi

if [[ -z "${HOME:-}" || "$HOME" == "/" ]]; then
    echo "Refusing to install with an unsafe HOME directory." >&2
    exit 1
fi

APP_SOURCE="$SCRIPT_DIR/Applications/sommacampagna_engine.app"
SENDER_VST3_SOURCE="$SCRIPT_DIR/Plugins/VST3/sommacampagna_sender.vst3"
RECEIVER_VST3_SOURCE="$SCRIPT_DIR/Plugins/VST3/sommacampagna_receiver.vst3"
SENDER_AU_SOURCE="$SCRIPT_DIR/Plugins/Components/sommacampagna_sender.component"
RECEIVER_AU_SOURCE="$SCRIPT_DIR/Plugins/Components/sommacampagna_receiver.component"

APP_DEST="$HOME/Applications/sommacampagna_engine.app"
SENDER_VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3/sommacampagna_sender.vst3"
RECEIVER_VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3/sommacampagna_receiver.vst3"
SENDER_AU_DEST="$HOME/Library/Audio/Plug-Ins/Components/sommacampagna_sender.component"
RECEIVER_AU_DEST="$HOME/Library/Audio/Plug-Ins/Components/sommacampagna_receiver.component"

for source in \
    "$APP_SOURCE" \
    "$SENDER_VST3_SOURCE" \
    "$RECEIVER_VST3_SOURCE" \
    "$SENDER_AU_SOURCE" \
    "$RECEIVER_AU_SOURCE"; do
    if [[ ! -d "$source" ]]; then
        echo "Missing package component: $source" >&2
        echo "Run this installer from the extracted release package." >&2
        exit 1
    fi
done

cat <<EOF
Close all DAWs before continuing.

sommacampagna will be installed for the current user only:

  $APP_DEST
  $SENDER_VST3_DEST
  $RECEIVER_VST3_DEST
  $SENDER_AU_DEST
  $RECEIVER_AU_DEST

The installer will remove the quarantine attribute only from these installed bundles.
EOF

printf "Continue? [y/N] "
read -r reply
case "$reply" in
    y|Y|yes|YES) ;;
    *) echo "Installation cancelled."; exit 0 ;;
esac

mkdir -p \
    "$HOME/Applications" \
    "$HOME/Library/Audio/Plug-Ins/VST3" \
    "$HOME/Library/Audio/Plug-Ins/Components"

current_temp=""
cleanup()
{
    if [[ -n "$current_temp" && -e "$current_temp" ]]; then
        rm -rf "$current_temp"
    fi
}
trap cleanup EXIT

install_bundle()
{
    local source="$1"
    local destination="$2"

    current_temp="${destination}.installing.$$"
    rm -rf "$current_temp"
    ditto "$source" "$current_temp"
    rm -rf "$destination"
    mv "$current_temp" "$destination"
    current_temp=""
    xattr -dr com.apple.quarantine "$destination" 2>/dev/null || true
    if xattr -lr "$destination" 2>/dev/null | grep -q 'com.apple.quarantine'; then
        echo "Warning: macOS quarantine remains on $destination." >&2
    fi
    echo "Installed: $destination"
}

install_bundle "$APP_SOURCE" "$APP_DEST"
install_bundle "$SENDER_VST3_SOURCE" "$SENDER_VST3_DEST"
install_bundle "$RECEIVER_VST3_SOURCE" "$RECEIVER_VST3_DEST"
install_bundle "$SENDER_AU_SOURCE" "$SENDER_AU_DEST"
install_bundle "$RECEIVER_AU_SOURCE" "$RECEIVER_AU_DEST"

killall AudioComponentRegistrar 2>/dev/null || true

validate_au()
{
    local subtype="$1"
    local name="$2"

    if auval -v aufx "$subtype" Reap >/dev/null 2>&1; then
        echo "Validated AU: $name"
    else
        echo "Warning: AU validation did not pass for $name. Re-scan plugins in the DAW." >&2
    fi
}

if command -v auval >/dev/null 2>&1; then
    validate_au Ssnd sommacampagna_sender
    validate_au Rrcv sommacampagna_receiver
fi

cat <<EOF

Installation complete.
Start the engine with:
  open "$APP_DEST"

Then re-scan plugins in the DAW.
EOF
