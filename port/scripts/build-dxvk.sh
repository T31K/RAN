#!/bin/bash
# port/scripts/build-dxvk.sh — build DXVK's D3D9 front end natively for macOS (SDL3 WSI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TP="$ROOT/port/third_party"
SRC="$TP/dxvk"
OUT="$TP/dxvk-install"
mkdir -p "$TP"
TAG="${DXVK_TAG:-$(git ls-remote --tags --refs https://github.com/doitsujin/dxvk 'v2.*' | sed 's#.*/##' | sort -V | tail -1)}"
if [ ! -d "$SRC" ]; then
  git clone --depth 1 --branch "$TAG" --recurse-submodules https://github.com/doitsujin/dxvk "$SRC"
fi
for p in "$ROOT"/port/patches/dxvk/*.patch; do
  [ -e "$p" ] || continue
  git -C "$SRC" apply --check "$p" 2>/dev/null && git -C "$SRC" apply "$p"
done
WSI_OPTS="-Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled"
grep -q "native_sdl3" "$SRC/meson_options.txt" || WSI_OPTS="-Dnative_sdl2=enabled -Dnative_glfw=disabled"
MESON_OPTS=(--buildtype=release --prefix="$OUT"
  -Denable_d3d8=false -Denable_d3d9=true -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false $WSI_OPTS)
if [ -d "$SRC/build-mac" ]; then
  meson setup "$SRC/build-mac" "$SRC" "${MESON_OPTS[@]}" --reconfigure
else
  meson setup "$SRC/build-mac" "$SRC" "${MESON_OPTS[@]}"
fi
ninja -C "$SRC/build-mac"
ninja -C "$SRC/build-mac" install
echo "DXVK_TAG=$TAG"
echo "DXVK_INCLUDE=$(dirname "$(find "$OUT/include" -name d3d9.h | head -1)")"
echo "DXVK_LIB=$(find "$OUT/lib" -name 'libdxvk_d3d9*.dylib' | head -1)"
