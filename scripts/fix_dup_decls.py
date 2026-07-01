#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Convert duplicate block-local `TYPE var = expr` to `var = expr` after hoisting."""

from __future__ import annotations

import re
import sys
from pathlib import Path

TYPE_RE = (
    r"size_t|ssize_t|int|uint32_t|uint8_t|uint16_t|uint64_t|int32_t|int64_t|"
    r"float|double|char|long|unsigned\s+int|unsigned\s+short|unsigned\s+long|short|"
    r"struct\s+\w+|union\s+\w+|jxl_[A-Za-z0-9_]+"
)
TYPE_PATTERN = rf"(?:const\s+)?(?:{TYPE_RE})(?:\s*\*+)?"
DECL_RE = re.compile(rf"^\s*({TYPE_PATTERN})\s+(\w+)(\s*\[[^\]]*\])?\s*;\s*$")
INIT_RE = re.compile(rf"^(\s*)({TYPE_PATTERN})\s+(\w+)(\s*\[[^\]]*\])?\s*=\s*(.+);\s*$")


def brace_delta(stripped: str) -> tuple[int, int]:
    """Count block braces, ignoring braces in `= { ... }` initializers."""
    code = re.sub(r"=\s*\{[^}]*\}", "= 0", stripped)
    return code.count("{"), code.count("}")


def transform(text: str) -> tuple[str, int]:
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    block_stack: list[set[str]] = []
    fixes = 0

    for line in lines:
        stripped = line.rstrip("\n")
        open_braces, close_braces = brace_delta(stripped)

        if open_braces and not stripped.strip().startswith("}"):
            for _ in range(open_braces):
                block_stack.append(set())

        m_init = INIT_RE.match(stripped)
        if m_init and block_stack:
            var = m_init.group(3)
            declared = set()
            for scope in block_stack:
                declared |= scope
            if var in declared:
                indent = m_init.group(1)
                expr = m_init.group(5)
                line = f"{indent}{var} = {expr};\n"
                fixes += 1
            else:
                block_stack[-1].add(var)
        else:
            m_decl = DECL_RE.match(stripped)
            if m_decl and block_stack:
                block_stack[-1].add(m_decl.group(2))

        out.append(line)

        if close_braces:
            for _ in range(close_braces):
                if block_stack:
                    block_stack.pop()

    return "".join(out), fixes


def main(argv: list[str]) -> int:
    roots = [Path(p) for p in argv[1:]] if len(argv) > 1 else [Path("c/src"), Path("c/tests")]
    total_files = 0
    total_fixes = 0
    for root in roots:
        for path in sorted(root.rglob("*.c")):
            if "build" in path.parts:
                continue
            original = path.read_text()
            updated, n = transform(original)
            if updated != original:
                path.write_text(updated)
                total_files += 1
                total_fixes += n
                print(f"fixed {path} ({n} duplicates)")
    print(f"done: {total_files} files, {total_fixes} fixes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
