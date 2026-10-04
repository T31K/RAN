#!/bin/bash
# port/scripts/check-file-struct-sizes.sh - on-disk companion of check-struct-sizes.sh (P1.3).
# Builds port/tests/golden/file_struct_sizes.cpp natively (same flags as check-struct-sizes.sh) and
# compares the size of every type the client reads from / writes to files as raw bytes with the
# golden sizes produced by MSVC x86 in CI (port/tests/golden/file_sizes_win32.txt, CI artifact
# 'file-sizes-win32'). Regenerate the type list with port/scripts/gen_file_struct_sizes.py.
# Exit 0 = identical. Writes the macOS sizes to port/build/file_sizes_macos.txt.
# RAN_LAYOUT_DUMP=1 also writes clang's record layouts to port/build/file_struct_layouts.txt
# (input of port/scripts/analyze_file_structs.py).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
GOLDEN="port/tests/golden/file_sizes_win32.txt"
OUT_DIR="port/build"; mkdir -p "$OUT_DIR"
BIN="$OUT_DIR/file_struct_sizes"

# Reuse compile_one.sh's include list (it refreshes port/build/ran_incs.txt).
port/scripts/compile_one.sh port/tests/golden/struct_sizes.cpp 1 >/dev/null 2>&1 || true
INCS=(-I"$ROOT/port/compat/include" -I"$ROOT/port/third_party/dxvk-install/include/dxvk")
while IFS= read -r d; do INCS+=("-I$d"); done < "$OUT_DIR/ran_incs.txt"
ODBC_INC="$(brew --prefix unixodbc 2>/dev/null)/include"
[ -d "$ODBC_INC" ] && INCS+=("-idirafter" "$ODBC_INC")

FLAGS=(-std=c++14 -fms-extensions -fdeclspec -Wno-everything -DNDEBUG -D_LIB)
if [ "${RAN_LAYOUT_DUMP:-0}" = 1 ]; then
  clang++ "${FLAGS[@]}" "${INCS[@]}" -fsyntax-only -Xclang -fdump-record-layouts \
    port/tests/golden/file_struct_sizes.cpp > "$OUT_DIR/file_struct_layouts.txt" 2>/dev/null \
    || { echo "layout dump failed"; exit 2; }
fi
clang++ "${FLAGS[@]}" "${INCS[@]}" port/tests/golden/file_struct_sizes.cpp -o "$BIN" -liconv \
  || { echo "build failed"; exit 2; }
"$BIN" > "$OUT_DIR/file_sizes_macos.txt" || { echo "run failed"; exit 2; }
echo "macOS: $(wc -l < "$OUT_DIR/file_sizes_macos.txt" | tr -d ' ') on-disk types measured"

if [ ! -f "$GOLDEN" ]; then
  echo "no golden file yet ($GOLDEN): download it from the CI artifact 'file-sizes-win32'"
  echo "(static analysis meanwhile: python3 port/scripts/analyze_file_structs.py -> docs/port/file-structs.md)"
  exit 3
fi
tr -d '\r' < "$GOLDEN" | sort > "$OUT_DIR/file_golden.sorted"
sort "$OUT_DIR/file_sizes_macos.txt" > "$OUT_DIR/file_macos.sorted"
join -a 2 -e MISSING -o 0,1.2,2.2 "$OUT_DIR/file_golden.sorted" "$OUT_DIR/file_macos.sorted" \
  | awk '$2 != $3 { print "  " $1 ": windows " $2 ", macos " $3 }' > "$OUT_DIR/file_sizes.diff"
N=$(wc -l < "$OUT_DIR/file_macos.sorted" | tr -d ' ')
if [ ! -s "$OUT_DIR/file_sizes.diff" ]; then
  echo "PASS: all $N on-disk type sizes match Windows x86"
  exit 0
fi
echo "FAIL: $(wc -l < "$OUT_DIR/file_sizes.diff" | tr -d ' ') of $N on-disk types differ:"
cat "$OUT_DIR/file_sizes.diff"
exit 1
