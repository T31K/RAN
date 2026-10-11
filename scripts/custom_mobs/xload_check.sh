#!/bin/bash
# Build xload_check (the native port's own .x mesh-hierarchy loader on a fake device) and run it.
#   scripts/custom_mobs/xload_check.sh [file.x ...]     default: every skin in the install tree
# Also runs the port's xfile_sweep (template/blob checks) when it has been built
# (port/build/tests-main/xfile_sweep).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
P="$REPO/port"
BIN="$REPO/port/build/custom_mobs/xload_check"
mkdir -p "$(dirname "$BIN")"
c++ -std=gnu++17 -arch arm64 -w -I"$P/compat/include" -I"$P/third_party/dxvk-install/include/dxvk" \
    -I"$P/../Dependency/directx" -I"$P/tests" -o "$BIN" "$HERE/xload_check.cpp" \
    "$P/d3dx/d3dx9_xload.cpp" "$P/d3dx/xfile.cpp" "$P/d3dx/d3dx9_mesh.cpp" "$P/d3dx/d3dx9_meshutil.cpp" \
    "$P/d3dx/d3dx9_skin.cpp" "$P/d3dx/d3dx9_core.cpp" "$P/d3dx/d3dx9_math.cpp"
if [ $# -eq 0 ]; then
    set -- "$REPO"/port/build/custom_mobs/out/data/skin/*.x
fi
"$BIN" "$@"
SWEEP="$P/build/tests-main/xfile_sweep"
if [ -x "$SWEEP" ] && [ -d "$REPO/port/build/custom_mobs/out/data" ]; then
    "$SWEEP" "$REPO/port/build/custom_mobs/out/data"
fi
