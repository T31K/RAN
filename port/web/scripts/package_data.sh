#!/bin/bash
# port/web/scripts/package_data.sh [client-folder] [out-dir]
# Packs the game data for the browser client: a gzip'd ustar tar of the client folder (without
# the Windows-only files, as port/scripts/package_native_app.sh), split into 95 MB parts, plus
# game.json {version, bytes, parts}. The page downloads the parts once and unpacks them into the
# browser's storage (port/web/shell/unpack-worker.js); a new version re-downloads.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CLIENT="${1:-$HOME/Projects/RAN/client}"
OUT="${2:-$ROOT/port/build/web/data}"
mkdir -p "$OUT"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

echo "packing $CLIENT ..."
# The game regenerates its font caches (cache/) and never reads the Windows binaries.
( cd "$CLIENT" && find . -type f \
    ! -name '*.exe' ! -name '*.dll' ! -name '*.pdb' ! -name '*.bak' ! -name '*.des' ! -name '.DS_Store' \
    ! -name '*.log' ! -path './GameGuard/*' ! -path './cache/*' ! -path './$PLUGINSDIR/*' ! -path './$SYSDIR/*' \
    | sed 's|^\./||' | LC_ALL=C sort > "$TMP/list" )
echo "$(wc -l < "$TMP/list" | tr -d ' ') files"
GZ=(gzip -6); command -v pigz >/dev/null && GZ=(pigz -6)
( cd "$CLIENT" && tar --format ustar -cf - -T "$TMP/list" ) | "${GZ[@]}" > "$TMP/game.tar.gz"
VERSION=$(shasum -a 256 "$TMP/game.tar.gz" | cut -c1-16)
BYTES=$(stat -f %z "$TMP/game.tar.gz")
rm -f "$OUT"/game-*.part*
split -b 95m -a 3 "$TMP/game.tar.gz" "$OUT/game-$VERSION.part"
PARTS=$(cd "$OUT" && ls game-"$VERSION".part* | sed 's/.*/"&"/' | paste -sd, -)
echo "{\"version\":\"$VERSION\",\"bytes\":$BYTES,\"parts\":[$PARTS]}" > "$OUT/game.json"
echo "version $VERSION: $((BYTES / 1048576)) MB in $(ls "$OUT"/game-"$VERSION".part* | wc -l | tr -d ' ') parts -> $OUT"
