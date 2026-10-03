#!/bin/bash
# port/scripts/check-paths.sh [client_dir] - Phase 1 gate P1.5.
# Opens every client data file through Windows-style spellings (backslashes, any case) via the
# compat path resolver (port/tests/golden/path_resolve.cpp):
#   1. on the real client directory (default macOS volume: case-insensitive APFS)
#   2. on a case-sensitive APFS disk image holding the same tree (empty files, same names),
#      which is where the component-by-component case matching actually has to work.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLIENT="${1:-$HOME/Projects/RAN/client}"
BIN="$ROOT/port/build/path_resolve"; mkdir -p "$ROOT/port/build"
clang++ -std=c++17 -Wno-everything -I"$ROOT/port/compat/include" \
  -I"$ROOT/port/third_party/dxvk-install/include/dxvk" \
  "$ROOT/port/tests/golden/path_resolve.cpp" -o "$BIN" -liconv || { echo "build failed"; exit 2; }

"$BIN" "$CLIENT/data"; r1=$?

IMG="$(mktemp -d)/rancs"
hdiutil create -quiet -size 64m -type SPARSE -fs "Case-sensitive APFS" -volname RANCS "$IMG" || exit 2
MNT="$(hdiutil attach -nobrowse "$IMG.sparseimage" | awk -F'\t' '/RANCS/ {print $NF}')"
trap 'hdiutil detach -quiet "$MNT" 2>/dev/null; rm -rf "$(dirname "$IMG")"' EXIT
(cd "$CLIENT/data" && find . -type d) | while IFS= read -r d; do mkdir -p "$MNT/data/$d"; done
(cd "$CLIENT/data" && find . -type f) | while IFS= read -r f; do : > "$MNT/data/$f"; done
"$BIN" "$MNT/data"; r2=$?
[ $r1 -eq 0 ] && [ $r2 -eq 0 ]
