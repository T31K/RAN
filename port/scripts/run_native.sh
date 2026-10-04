#!/bin/bash
# port/scripts/run_native.sh - run the native client (port/build/native/ran_client) against a
# client data folder, with DXVK-native on KosmicKrisp (see docs/port/spike-report.md).
#   port/scripts/run_native.sh [client-dir]      default: ~/Projects/RAN/client
# Extra environment is passed through (e.g. DXVK_LOG_LEVEL=debug).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GAME_DIR="${1:-$HOME/Projects/RAN/client}"
BIN="$ROOT/port/build/native/ran_client"
[ -x "$BIN" ] || { echo "build first: port/scripts/build_native.sh"; exit 1; }
[ -d "$GAME_DIR" ] || { echo "no client folder: $GAME_DIR"; exit 1; }

ICD="$(brew --prefix mesa)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json"
[ -f "$ICD" ] || { echo "KosmicKrisp ICD missing ($ICD): brew install mesa"; exit 1; }

export RAN_GAME_DIR="$GAME_DIR"
export DXVK_WSI_DRIVER=SDL3
export VK_DRIVER_FILES="$ICD"
export DXVK_LOG_PATH="${DXVK_LOG_PATH:-$ROOT/port/build/native}"
export DYLD_LIBRARY_PATH="$(brew --prefix vulkan-loader)/lib:$(brew --prefix sdl3)/lib:$ROOT/port/third_party/dxvk-install/lib"
cd "$GAME_DIR"
exec "$BIN" "${@:2}"
