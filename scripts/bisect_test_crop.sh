#!/usr/bin/env bash
# Bisect helper: rebuild and run crop conformance + sunset_logo tests.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${C_BUILD_DIR:-$ROOT/build-clean}"
cmake --build "$BUILD" -j"$(nproc)" 2>&1 | tail -5
ctest --test-dir "$BUILD" -R "c_conformance_sunset_logo|c_unit_crop_conformance" --output-on-failure
exit 0
