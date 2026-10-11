#!/bin/bash
# port/web/scripts/compile_one.sh <file.cpp|file.c|file.h> [error-limit]
# Compiles one client source for the browser build (Emscripten, wasm32). Same include order as
# the native build (port/scripts/compile_one.sh): our compat headers first, then the D3D9/Win32
# base headers (vendored from DXVK's native headers), then the .vcxproj include dirs.
# RAN_OBJ_OUT=<file.o> builds object code; otherwise syntax-check only.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
FILE="$1"; LIMIT="${2:-20}"
cd "$ROOT"
[ -n "${EMSDK:-}" ] || source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
INCS=(-I"$ROOT/port/web/include" -I"$ROOT/port/compat/include" -I"$ROOT/port/web/third_party/d3d9-headers")
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
  mv -f "$TMPLIST" "$CACHE"
fi
while IFS= read -r d; do INCS+=("-I$d"); done < "$CACHE"
# ODBC headers (types only, for server-side DbAction code living in RanClient). Searched last.
INCS+=("-idirafter" "$ROOT/port/web/third_party/odbc-include")
OPT=(-O2 -g0)
[ -n "${RAN_WEB_ASAN:-}" ] && OPT+=(-fsanitize=address -fsanitize-recover=address)
[ -n "${RAN_WEB_DEBUG:-}" ] && OPT=(-O1 -g)
MODE=(-fsyntax-only)
[ -n "${RAN_OBJ_OUT:-}" ] && MODE=(-c -o "$RAN_OBJ_OUT" "${OPT[@]}" -DNDEBUG -D_LIB)
# Native wasm exception handling: the game throws and catches in a few places.
COMMON=(-DRAN_WEB=1 -fms-extensions -fdeclspec -Wno-everything -ferror-limit="$LIMIT"
        -Werror=return-type -fwasm-exceptions)
case "$FILE" in
  *.c) exec emcc "${MODE[@]}" "${COMMON[@]}" -I"$ROOT/port/compat/include" -include ran_c_compat.h \
         -I"$(dirname "$FILE")" "$FILE" ;;
  *.h) LANG_FLAG=(-x c++-header) ;;
  *)   LANG_FLAG=() ;;
esac
TRAPS=(-Werror=non-pod-varargs -Werror=delete-abstract-non-virtual-dtor)
exec em++ ${LANG_FLAG[@]+"${LANG_FLAG[@]}"} -std=c++14 "${MODE[@]}" "${COMMON[@]}" "${TRAPS[@]}" \
  ${RAN_WARN:-} "${INCS[@]}" "$FILE"
