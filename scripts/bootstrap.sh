#!/usr/bin/env bash
# Initialize the jxl-oxide submodule and verify fixtures + local oracles.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ORACLE_DIR="$ROOT/tests/oracle/decode"

if [[ -f .gitmodules ]]; then
    git submodule update --init --recursive third_party/jxl-oxide
fi

RUST_ROOT="$("$ROOT/scripts/rust_root.sh")"
DECODE_DIR="$RUST_ROOT/crates/jxl-oxide-tests/decode"

if [[ ! -d "$DECODE_DIR" ]]; then
    echo "bootstrap: decode fixtures missing under $RUST_ROOT" >&2
    echo "bootstrap: run: git submodule update --init --recursive third_party/jxl-oxide" >&2
    exit 1
fi

missing=0
for f in modular_pass_group_offsets.txt modular_transformed_layouts.txt; do
    if [[ ! -f "$ORACLE_DIR/$f" ]]; then
        echo "bootstrap: missing local oracle $f" >&2
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    echo "bootstrap: run: $ROOT/scripts/gen_decode_oracles.sh" >&2
    exit 1
fi

echo "bootstrap: jxl-oxide submodule at $RUST_ROOT"
echo "bootstrap: decode oracles at $ORACLE_DIR"
