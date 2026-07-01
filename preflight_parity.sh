#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUST_ROOT="$("$ROOT_DIR/scripts/rust_root.sh")"
C_BUILD_DIR="${C_BUILD_DIR:-$ROOT_DIR/build-dbg}"

if [[ ! -f "$C_BUILD_DIR/CMakeCache.txt" ]]; then
  echo "[preflight] Configure C build directory: $C_BUILD_DIR"
  cmake -S "$ROOT_DIR" -B "$C_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi

echo "[preflight] Rust frame parity tests"
cargo test --manifest-path "$RUST_ROOT/Cargo.toml" -p jxl-frame --test grayalpha_toc

echo "[preflight] Build C tests"
cmake --build "$C_BUILD_DIR" -j4

echo "[preflight] Full C test suite"
ctest --test-dir "$C_BUILD_DIR" --output-on-failure

echo "[preflight] OK"
