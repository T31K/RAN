#!/bin/bash
# port/scripts/compile_one.sh <file.cpp|file.h> [error-limit]
# Syntax-checks one client source file with the native macOS flags: our compat headers first,
# then DXVK's native Windows/D3D9 headers, then every include dir the client .vcxproj files use.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FILE="$1"; LIMIT="${2:-20}"
cd "$ROOT"
INCS=(-I"$ROOT/port/compat/include" -I"$ROOT/port/third_party/dxvk-install/include/dxvk")
for proj in "[Client]__Game" "[Lib]__Engine" "[Lib]__EngineSound" "[Lib]__EngineUI" "[Lib]__MfcEx" \
            "[Lib]__NetClient" "[Lib]__RanClient" "[Lib]__RanClientUI"; do
  vcx=$(ls "$proj"/*.vcxproj | head -1)
  grep -a -o "<AdditionalIncludeDirectories>[^<]*" "$vcx" | sed 's/<AdditionalIncludeDirectories>//' \
    | tr ';' '\n' | grep -v '%(' | while read -r d; do
      [ -n "$d" ] && (cd "$proj" && cd "$d" 2>/dev/null && pwd)
    done
done | awk '!seen[$0]++' > /tmp/ran_incs.txt
while IFS= read -r d; do INCS+=("-I$d"); done < /tmp/ran_incs.txt
LANG_FLAG=()
case "$FILE" in *.h) LANG_FLAG=(-x c++-header) ;; esac
# No -fms-compatibility: it hides __GNUC__ and breaks Apple's SDK headers. MSVC-only C++ in the
# game is fixed in source instead; -fms-extensions keeps __declspec/__int64 style extensions.
clang++ "${LANG_FLAG[@]}" -std=c++20 -fsyntax-only -fshort-wchar -fms-extensions -fdeclspec \
  -Wno-everything -ferror-limit="$LIMIT" \
  "${INCS[@]}" "$FILE"
