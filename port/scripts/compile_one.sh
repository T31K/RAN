#!/bin/bash
# port/scripts/compile_one.sh <file.cpp|file.h> [error-limit]
# Syntax-checks one client source file with the native macOS flags: our compat headers first,
# then DXVK's native Windows/D3D9 headers, then every include dir the client .vcxproj files use.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FILE="$1"; LIMIT="${2:-20}"
cd "$ROOT"
INCS=(-I"$ROOT/port/compat/include" -I"$ROOT/port/third_party/dxvk-install/include/dxvk")
# The include list comes from the .vcxproj files; it is cached per repo state so parallel
# callers (compile_probe.sh) never race on a half-written file.
CACHE="$ROOT/port/build/ran_incs.txt"
if [ ! -s "$CACHE" ] || [ -n "$(find "$ROOT" -maxdepth 2 -name '*.vcxproj' -newer "$CACHE" 2>/dev/null | head -1)" ]; then
  mkdir -p "$ROOT/port/build"
  TMPLIST="$(mktemp "$ROOT/port/build/incs.XXXXXX")"
  for proj in "[Client]__Game" "[Lib]__Engine" "[Lib]__EngineSound" "[Lib]__EngineUI" "[Lib]__MfcEx" \
              "[Lib]__NetClient" "[Lib]__RanClient" "[Lib]__RanClientUI"; do
    vcx=$(ls "$proj"/*.vcxproj | head -1)
    grep -a -o "<AdditionalIncludeDirectories>[^<]*" "$vcx" | sed 's/<AdditionalIncludeDirectories>//' \
      | tr ';' '\n' | grep -v '%(' | while read -r d; do
        [ -n "$d" ] && (cd "$proj" && cd "$d" 2>/dev/null && pwd)
      done
  done | awk '!seen[$0]++' > "$TMPLIST"
  mv -f "$TMPLIST" "$CACHE"   # atomic replace
fi
while IFS= read -r d; do INCS+=("-I$d"); done < "$CACHE"
# ODBC headers for the server-side DbAction code that lives in the shared RanClient library
# (Homebrew unixODBC; headers only - the client never opens a database). Searched last.
ODBC_INC="$(brew --prefix unixodbc 2>/dev/null)/include"
[ -d "$ODBC_INC" ] && INCS+=("-idirafter" "$ODBC_INC")
# SDL3 for the platform layer (port/platform); searched last so it never shadows game headers.
SDL_INC="$(brew --prefix sdl3 2>/dev/null)/include"
[ -d "$SDL_INC" ] && INCS+=("-idirafter" "$SDL_INC")
LANG_FLAG=()
case "$FILE" in *.h) LANG_FLAG=(-x c++-header) ;; esac
# No -fms-compatibility: it hides __GNUC__ and breaks Apple's SDK headers. MSVC-only C++ in the
# game is fixed in source instead; -fms-extensions keeps __declspec/__int64 style extensions.
# No -fshort-wchar either: it silently breaks libc++'s char16_t/wchar_t algorithms on macOS.
# WCHAR is char16_t instead (2 bytes, same layout as Windows).
# C++14 like the Windows build (VS2022 v143 default; no LanguageStandard in the .vcxproj files).
# That also keeps std::auto_ptr/random_shuffle and avoids std::byte clashing with the game's byte.
# RAN_OBJ_OUT=<file.o>: build object code instead (port/scripts/build_native.sh), with the
# Release defines of the shipped Win32 build.
MODE=(-fsyntax-only)
[ -n "${RAN_OBJ_OUT:-}" ] && MODE=(-c -o "$RAN_OBJ_OUT" -O1 -g0 -DNDEBUG -D_LIB)
clang++ ${LANG_FLAG[@]+"${LANG_FLAG[@]}"} -std=c++14 "${MODE[@]}" -fms-extensions -fdeclspec \
  -Wno-everything -ferror-limit="$LIMIT" \
  "${INCS[@]}" "$FILE"
