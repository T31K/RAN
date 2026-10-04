#!/bin/bash
# Build and run every native-port test (compat layer C++ tests + Python tool tests).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# RAN_TEST_BUILD=<dir> builds elsewhere (parallel sessions must not share a build tree).
BUILD="${RAN_TEST_BUILD:-$ROOT/port/tests/build}"
cmake -S "$ROOT/port/tests" -B "$BUILD" -G Ninja >/dev/null
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure
(cd "$ROOT/port/scripts" && python3 -m unittest -q test_inventory test_ppm_diff)
