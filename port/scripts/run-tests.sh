#!/bin/bash
# Build and run every native-port test (compat layer C++ tests + Python tool tests).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cmake -S "$ROOT/port/tests" -B "$ROOT/port/tests/build" -G Ninja >/dev/null
cmake --build "$ROOT/port/tests/build"
ctest --test-dir "$ROOT/port/tests/build" --output-on-failure
(cd "$ROOT/port/scripts" && python3 -m unittest -q test_inventory test_ppm_diff)
