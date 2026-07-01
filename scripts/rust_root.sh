#!/usr/bin/env bash
# Resolve jxl-oxide Rust workspace root (third_party/jxl-oxide submodule).
set -euo pipefail

JXL_C_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ -n "${JXL_OXIDE_RUST_ROOT:-}" ]]; then
    cd "$JXL_OXIDE_RUST_ROOT" && pwd
    exit 0
fi

echo "$JXL_C_ROOT/third_party/jxl-oxide"
