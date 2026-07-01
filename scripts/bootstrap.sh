#!/usr/bin/env bash
# Initialize the jxl-oxide submodule and verify fixtures are present.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -f .gitmodules ]]; then
    echo "bootstrap: missing .gitmodules (are you in the jxl-c repo root?)" >&2
    exit 1
fi

git submodule update --init --recursive third_party/jxl-oxide

RUST_ROOT="$("$ROOT/scripts/rust_root.sh")"
if [[ ! -d "$RUST_ROOT/crates/jxl-oxide-tests/decode" ]]; then
    echo "bootstrap: fixtures missing under $RUST_ROOT" >&2
    exit 1
fi

echo "bootstrap: jxl-oxide at $RUST_ROOT"
