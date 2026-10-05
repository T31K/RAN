#!/bin/bash
# port/scripts/fetch_sparkle.sh - download Sparkle (the macOS auto-updater the native app embeds)
# into port/third_party/sparkle: Sparkle.framework plus its tools (bin/generate_keys,
# bin/generate_appcast, bin/sign_update). Idempotent.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="2.10.0"
DEST="$ROOT/port/third_party/sparkle"
if [ -f "$DEST/.version" ] && [ "$(cat "$DEST/.version")" = "$VERSION" ]; then
    echo "Sparkle $VERSION already in $DEST"; exit 0
fi
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/sparkle.tar.xz" "https://github.com/sparkle-project/Sparkle/releases/download/$VERSION/Sparkle-$VERSION.tar.xz"
rm -rf "$DEST"; mkdir -p "$DEST"
tar -xf "$TMP/sparkle.tar.xz" -C "$DEST"
echo "$VERSION" > "$DEST/.version"
echo "Sparkle $VERSION -> $DEST"; ls "$DEST"
