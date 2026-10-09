#!/bin/bash
# port/web/scripts/release_web.sh [--data] - publish the browser client.
#
# The page, the client (ran.js + ran.wasm) and the game-data archive parts are assets of the
# T31K/RAN GitHub release "web"; main-server (routes/ran-web.js) streams them at
#   https://api.kaleidoscopical.com/ran/play/
# with the WebSocket relay to the game servers on the same host. --data also repacks and
# uploads the game data (port/web/scripts/package_data.sh; players re-download it once).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
REPO="T31K/RAN"
TAG="web"
OUT="$ROOT/port/build/web/out"
DATA="$ROOT/port/build/web/data"
cd "$ROOT"

echo "== build"
port/web/scripts/build_web.sh | tail -1
cp port/web/shell/* "$OUT/"

if [ "${1:-}" = "--data" ] || [ ! -f "$DATA/game.json" ]; then
  echo "== pack game data"
  port/web/scripts/package_data.sh "${RAN_CLIENT:-$HOME/Projects/RAN/client}" "$DATA"
fi

if ! gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
  gh release create "$TAG" -R "$REPO" --prerelease -t "RanOdyssey in the browser" \
    -n "Files of the browser client, served by api.kaleidoscopical.com/ran/play (port/web)."
fi
echo "== upload client"
gh release upload "$TAG" -R "$REPO" --clobber "$OUT/index.html" "$OUT/loader.js" "$OUT/unpack-worker.js" "$OUT/splash.jpg" "$OUT/ran.js" "$OUT/ran.wasm"
echo "== upload data (parts already there are skipped)"
HAVE=$(gh release view "$TAG" -R "$REPO" --json assets -q '.assets[].name')
for p in "$DATA"/game-*.part*; do
  n=$(basename "$p")
  echo "$HAVE" | grep -qx "$n" || gh release upload "$TAG" -R "$REPO" "$p"
done
gh release upload "$TAG" -R "$REPO" --clobber "$DATA/game.json"
echo "published: https://api.kaleidoscopical.com/ran/play/"
