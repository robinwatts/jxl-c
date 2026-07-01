#!/usr/bin/env bash
# Create a standalone jxl-c.git repo from the current c/ tree (run from jxl-oxide monorepo).
#
# Usage:
#   c/scripts/export_jxl_c_repo.sh [destination]
#
# Default destination: ../jxl-c.git (sibling of jxl-oxide.git)
set -euo pipefail

MONO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$MONO_ROOT/c"
DEST="${1:-$(dirname "$MONO_ROOT")/jxl-c.git}"

if [[ ! -f "$SRC/CMakeLists.txt" ]]; then
    echo "export: expected C port at $SRC" >&2
    exit 1
fi

echo "==> Export $SRC -> $DEST"
rm -rf "$DEST"
mkdir -p "$DEST"

rsync -a \
    --exclude 'build/' \
    --exclude 'build-*/' \
    --exclude 'build2/' \
    --exclude 'Testing/' \
    --exclude '.vscode/' \
    --exclude '__pycache__/' \
    "$SRC/" "$DEST/"

cd "$DEST"
if [[ ! -d .git ]]; then
    git init -b main
fi

if [[ ! -d third_party/jxl-oxide ]]; then
    mkdir -p third_party
    git submodule add https://github.com/tirr-c/jxl-oxide.git third_party/jxl-oxide || {
        echo "export: submodule add failed; link local monorepo for dev:" >&2
        ln -sf "$MONO_ROOT" third_party/jxl-oxide
    }
fi

echo "==> Done. Next steps:"
echo "  cd $DEST"
echo "  ./scripts/bootstrap.sh   # if submodule not populated"
echo "  cmake -S . -B build && cmake --build build && ctest --test-dir build"
