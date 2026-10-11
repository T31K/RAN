#!/bin/bash
# port/web/scripts/build_web.sh [--debug] - browser build of the client (Emscripten, wasm32).
#
# Same sources as the native build (client_sources.py), compiled 10 at a time, incremental on
# source mtime (wipe port/build/web/obj after a compat header change, as for the native build).
# The game's projects become static libraries; the platform layer (port/platform, SDL3 port),
# D3DX (port/d3dx), the WebGL D3D9 (port/web/d3d9) and the web runtime (port/web/runtime,
# port/web/platform) are linked as objects. Output: port/build/web/out/ran.{js,wasm} + the page
# (port/web/shell/*).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$ROOT"
source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
DEBUG=0; [ "${1:-}" = "--debug" ] && DEBUG=1
# RAN_WEB_ASAN=1: AddressSanitizer build (memory errors reported with stacks) in its own folder.
B="$ROOT/port/build/web"; [ -n "${RAN_WEB_ASAN:-}" ] && B="$ROOT/port/build/web-asan"
mkdir -p "$B/obj" "$B/lib" "$B/out" "$B/deps"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
export ROOT B
[ $DEBUG = 1 ] && export RAN_WEB_DEBUG=1

# ---- third-party: LZO (the network layer's packet compression)
LZO_SRC="$HOME/.cache/ran-web-deps/lzo-2.10"
if [ ! -f "$B/deps/liblzo2.a" ]; then
  [ -d "$LZO_SRC" ] || { mkdir -p "$HOME/.cache/ran-web-deps" && (cd "$HOME/.cache/ran-web-deps" &&
      curl -sSLO https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz &&
      echo "c0f892943208266f9b6543b3ae308fab6284c5c90e627931446fb49b4221a072  lzo-2.10.tar.gz" | shasum -a 256 -c &&
      tar xzf lzo-2.10.tar.gz); }
  mkdir -p "$B/deps/lzo"
  for c in "$LZO_SRC"/src/*.c; do emcc -O2 -I"$LZO_SRC/include" -I"$LZO_SRC/src" -c "$c" -o "$B/deps/lzo/$(basename "$c" .c).o" || exit 1; done
  emar rcs "$B/deps/liblzo2.a" "$B/deps/lzo/"*.o
fi
mkdir -p "$ROOT/port/web/include/lzo"
for h in "$LZO_SRC"/include/lzo/*.h; do [ -f "$ROOT/port/web/include/lzo/$(basename "$h")" ] || cp "$h" "$ROOT/port/web/include/lzo/"; done

# ---- game sources
python3 port/scripts/client_sources.py --with-project > "$TMP/sources"
compile() {   # project<TAB>file
  p="${1%%	*}"; f="${1#*	}"
  o="$B/obj/$(echo "$p" | tr -d '[]')/$(echo "$f" | tr '/[]' '___').o"
  mkdir -p "$(dirname "$o")"
  [ "$o" -nt "$f" ] && exit 0
  RAN_OBJ_OUT="$o" "$ROOT/port/web/scripts/compile_one.sh" "$f" 3 >/dev/null 2>"$o.err"
  if [ $? -eq 0 ]; then rm -f "$o.err"; else echo "FAIL	$p	$f"; rm -f "$o"; fi
}
export -f compile
tr '\n' '\0' < "$TMP/sources" | xargs -0 -P 10 -I{} bash -c 'compile "$1"' _ {} > "$TMP/fails"
# NetServer is server code that lives next to client code in the shared libraries; its socket/
# ODBC files do not build (or link) for the client - natively neither.
grep FAIL "$TMP/fails" | grep -v "Lib]__NetServer" && { echo "game compile failures (see port/build/web/obj/*/*.err)"; exit 1; }
echo "game sources: ok ($(grep -c FAIL "$TMP/fails") NetServer files skipped)"

LIBS=()
for d in "$B"/obj/*/; do
  name=$(basename "$d")
  if [ "$name" = "Client__Game" ]; then LIBS+=("$d"*.o); continue; fi
  lib="$B/lib/lib$name.a"
  if [ ! -f "$lib" ] || [ -n "$(find "$d" -name '*.o' -newer "$lib" | head -1)" ]; then
    rm -f "$lib"; emar rcs "$lib" "$d"*.o
  fi
  LIBS+=("$lib")
done

# ---- port + web sources
OPT=(-O2 -g0); [ $DEBUG = 1 ] && OPT=(-O1 -g)
[ -n "${RAN_WEB_ASAN:-}" ] && OPT+=(-fsanitize=address -fsanitize-recover=address)
EXC=(-fwasm-exceptions)
port_compile() {   # file  extra-flags...
  local f="$1"; shift
  local o="$B/port_$(echo "${f#port/}" | tr '/' '_' | sed 's/\.cpp$//').o"
  # Rebuilt when the source or any port header is newer (a stale object with an old class
  # layout - e.g. d3d9_web.h's Device - corrupts the heap at run time).
  if [ ! "$o" -nt "$f" ] || [ -n "$(find port/web/include port/compat/include port/web/d3d9 port/web/platform port/platform port/d3dx -newer "$o" -name '*.h' 2>/dev/null | head -1)" ]; then
    RAN_OBJ_OUT="$o" RAN_WARN="$*" port/web/scripts/compile_one.sh "$f" 10 || { echo "FAIL $f"; return 1; }
  fi
  echo "$o"
}
PORT_OBJS=()
for f in port/platform/main_sdl.cpp port/platform/dinput_sdl.cpp port/platform/dsound_sdl.cpp \
         port/platform/cursor_sdl.cpp port/platform/c_bridge.cpp port/platform/win_shims.cpp \
         port/platform/snapshot.cpp \
         port/d3dx/*.cpp port/web/platform/*.cpp port/web/d3d9/*.cpp; do
  [ -f "$f" ] || continue
  o=$(port_compile "$f" --use-port=sdl3) || exit 1
  PORT_OBJS+=("$o")
done
WASMFS_INC="$EMSDK/upstream/emscripten/system/lib/wasmfs"
for f in port/web/runtime/*.cpp; do
  o="$B/runtime_$(basename "$f" .cpp).o"
  if [ ! "$o" -nt "$f" ] || [ port/web/include/ran_green.h -nt "$o" ]; then
    em++ -std=c++20 "${OPT[@]}" "${EXC[@]}" -I"$ROOT/port/web/include" -I"$WASMFS_INC" -c "$f" -o "$o" || exit 1
  fi
  PORT_OBJS+=("$o")
done

# ---- link
JSLIBS=()
for j in port/web/runtime/*.js port/web/platform/*.js port/web/d3d9/*.js; do [ -f "$j" ] && JSLIBS+=(--js-library "$j"); done
LINK=(-sJSPI -sJSPI_EXPORTS=ran_green_entry -sWASMFS -sFORCE_FILESYSTEM
      -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=8MB
      -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 --use-port=sdl3 -sUSE_ZLIB=1 -sUSE_OGG=1 -sUSE_VORBIS=1
      -sENVIRONMENT=web -sEXIT_RUNTIME=0 -sINVOKE_RUN=0
      -sEXPORTED_FUNCTIONS=_main,_ran_green_entry,_malloc,_free
      -sEXPORTED_RUNTIME_METHODS=callMain,ENV,UTF8ToString
      -Wl,--error-limit=0 "${EXC[@]}")
# Function names stay in the wasm (name section): crash stacks in the console are readable.
[ $DEBUG = 1 ] && LINK+=(-sASSERTIONS=1 -g) || LINK+=(-O2 --profiling-funcs)
[ -n "${RAN_WEB_ASAN:-}" ] && LINK+=(-fsanitize=address -sINITIAL_MEMORY=1GB)
em++ -o "$B/out/ran.js" "${PORT_OBJS[@]}" "${LIBS[@]}" "$B/deps/liblzo2.a" "${JSLIBS[@]}" "${LINK[@]}" 2> "$TMP/link.err"
if [ $? -ne 0 ]; then
  grep -a "undefined symbol" "$TMP/link.err" | sed -E 's/.*undefined symbol: //; s/ \(referenced by.*//' | sort -u > "$B/link-gaps.txt"
  echo "link failed: $(wc -l < "$B/link-gaps.txt" | tr -d ' ') undefined symbols -> port/build/web/link-gaps.txt"
  grep -av "undefined symbol" "$TMP/link.err" | head -20
  exit 1
fi
cp port/web/shell/* "$B/out/" 2>/dev/null
echo "LINKED $B/out/ran.wasm ($(du -h "$B/out/ran.wasm" | cut -f1))"
