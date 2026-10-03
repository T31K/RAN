#!/bin/bash
# port/scripts/check-cp949.sh [client_dir] - Phase 1 gate P1.4.
# Decrypts the game's string tables (glogic.rcc is a ZIP; each table is a 4-byte header +
# AES-256-ECB) plus the plain UI text tables, and runs them through the compat layer's
# CP949 <-> WCHAR conversion (port/tests/golden/cp949_roundtrip.cpp). Exit 0 = byte-exact.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CLIENT="${1:-$HOME/Projects/RAN/client}"
KEY=736b726b7268746c76646a214023777066726d666a677270676f21402324716b
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
BIN="$ROOT/port/build/cp949_roundtrip"; mkdir -p "$ROOT/port/build"

clang++ -std=c++14 -Wno-everything -I"$ROOT/port/compat/include" \
  -I"$ROOT/port/third_party/dxvk-install/include/dxvk" \
  "$ROOT/port/tests/golden/cp949_roundtrip.cpp" -o "$BIN" -liconv || { echo "build failed"; exit 2; }

FILES=()
for t in ItemStrTable.txt CrowStrTable.txt SkillStrTable.txt; do
  unzip -q -o -d "$WORK" "$CLIENT/data/glogic/glogic.rcc" "$t" || { echo "missing $t"; exit 2; }
  tail -c +5 "$WORK/$t" | openssl enc -aes-256-ecb -d -nopad -K "$KEY" > "$WORK/$t.dec" || exit 2
  FILES+=("$WORK/$t.dec")
done
for t in gameintext_kr.txt gameextext_kr.txt; do
  [ -f "$CLIENT/data/gui/$t" ] && FILES+=("$CLIENT/data/gui/$t")
done
"$BIN" "${FILES[@]}"
