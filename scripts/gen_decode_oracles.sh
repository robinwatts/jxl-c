#!/usr/bin/env bash
# Regenerate local decode parity oracle files (C-only; no Rust toolchain required).
#
# Reads fixture input.jxl from the jxl-oxide submodule; writes:
#   tests/oracle/decode/modular_pass_group_offsets.txt
#   tests/oracle/decode/modular_transformed_layouts.txt
#
# Usage: scripts/gen_decode_oracles.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUST_ROOT="$("$ROOT/scripts/rust_root.sh")"
FIXTURES_DIR="$RUST_ROOT/crates/jxl-oxide-tests/decode"
ORACLE_DIR="$ROOT/tests/oracle/decode"
BUILD="${JXL_GEN_BUILD:-$ROOT/build-gen-oracles}"

if [[ ! -d "$FIXTURES_DIR" ]]; then
    echo "gen_decode_oracles: fixtures missing: $FIXTURES_DIR" >&2
    echo "run: ./scripts/bootstrap.sh" >&2
    exit 1
fi

mkdir -p "$ORACLE_DIR"

echo "==> build gen_decode_oracles -> $BUILD"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" --target gen_decode_oracles -j"$(nproc)" >/dev/null

echo "==> write oracle files -> $ORACLE_DIR"
"$BUILD/gen_decode_oracles" "$FIXTURES_DIR" "$ORACLE_DIR"

echo "gen_decode_oracles: ok"
