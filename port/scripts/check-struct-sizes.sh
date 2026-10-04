#!/bin/bash
# port/scripts/check-struct-sizes.sh - Phase 1 gate P1.3.
# Builds port/tests/golden/struct_sizes.cpp natively (same flags as compile_one.sh, Release
# defines like the shipped Win32 build) and compares every network-message struct size with
# the golden sizes produced by MSVC x86 in CI (port/tests/golden/msg_sizes_win32.txt).
# Exit 0 = identical. Writes the macOS sizes to port/build/msg_sizes_macos.txt.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
GOLDEN="port/tests/golden/msg_sizes_win32.txt"
OUT_DIR="port/build"; mkdir -p "$OUT_DIR"
BIN="$OUT_DIR/struct_sizes"

# Reuse compile_one.sh's include list (it refreshes port/build/ran_incs.txt).
port/scripts/compile_one.sh port/tests/golden/struct_sizes.cpp 1 >/dev/null 2>&1 || true
INCS=(-I"$ROOT/port/compat/include" -I"$ROOT/port/third_party/dxvk-install/include/dxvk")
while IFS= read -r d; do INCS+=("-I$d"); done < "$OUT_DIR/ran_incs.txt"
ODBC_INC="$(brew --prefix unixodbc 2>/dev/null)/include"
[ -d "$ODBC_INC" ] && INCS+=("-idirafter" "$ODBC_INC")

clang++ -std=c++14 -fms-extensions -fdeclspec -Wno-everything -DNDEBUG -D_LIB \
  "${INCS[@]}" port/tests/golden/struct_sizes.cpp -o "$BIN" -liconv || { echo "build failed"; exit 2; }
"$BIN" > "$OUT_DIR/msg_sizes_macos.txt" || { echo "run failed"; exit 2; }
echo "macOS: $(wc -l < "$OUT_DIR/msg_sizes_macos.txt" | tr -d ' ') structs measured"

if [ ! -f "$GOLDEN" ]; then
  echo "no golden file yet ($GOLDEN): download it from the CI artifact 'msg-sizes-win32'"
  exit 3
fi
# Compare every struct measured on macOS with its Windows size (the golden file may list more:
# structs excluded later as non-wire stay in it until the next CI refresh).
tr -d '\r' < "$GOLDEN" | sort > "$OUT_DIR/golden.sorted"
sort "$OUT_DIR/msg_sizes_macos.txt" > "$OUT_DIR/macos.sorted"
join -a 2 -e MISSING -o 0,1.2,2.2 "$OUT_DIR/golden.sorted" "$OUT_DIR/macos.sorted" \
  | awk '$2 != $3 { print "  " $1 ": windows " $2 ", macos " $3 }' > "$OUT_DIR/msg_sizes.diff"
N=$(wc -l < "$OUT_DIR/macos.sorted" | tr -d ' ')
if [ ! -s "$OUT_DIR/msg_sizes.diff" ]; then
  echo "PASS: all $N struct sizes match Windows x86"
  exit 0
fi
echo "FAIL: $(wc -l < "$OUT_DIR/msg_sizes.diff" | tr -d ' ') of $N structs differ:"
cat "$OUT_DIR/msg_sizes.diff"
exit 1
