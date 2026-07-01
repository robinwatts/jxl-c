#!/usr/bin/env bash
# Resolve jxl-oxide Rust workspace root (submodule or monorepo parent).
set -euo pipefail

JXL_C_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ -n "${JXL_OXIDE_RUST_ROOT:-}" ]]; then
    cd "$JXL_OXIDE_RUST_ROOT" && pwd
    exit 0
fi

if [[ -d "$JXL_C_ROOT/third_party/jxl-oxide/crates/jxl-oxide-tests" ]]; then
    echo "$JXL_C_ROOT/third_party/jxl-oxide"
    exit 0
fi

if [[ -d "$JXL_C_ROOT/../crates/jxl-oxide-tests" ]]; then
    echo "$(cd "$JXL_C_ROOT/.." && pwd)"
    exit 0
fi

echo "$JXL_C_ROOT/third_party/jxl-oxide"
