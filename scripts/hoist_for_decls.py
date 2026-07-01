#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Hoist C99 for-init declarations to the start of enclosing blocks (C89 style)."""

from __future__ import annotations

import re
import sys
from collections import OrderedDict
from pathlib import Path

TYPE_RE = (
    r"size_t|ssize_t|int|uint32_t|uint8_t|uint16_t|uint64_t|int32_t|int64_t|"
    r"float|double|char|long|unsigned\s+int|unsigned\s+short|unsigned\s+long|short"
)
FOR_RE = re.compile(
    rf"(\bfor\s*\(\s*)((?:const\s+)?)((?:{TYPE_RE}))\s+(\w+)\s*=",
)


def block_starts_at_each_index(text: str) -> list[int]:
    stack: list[int] = []
    block_at = [-1] * len(text)
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch in ("'", '"'):
            quote = ch
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and i + 1 < n:
            nxt = text[i + 1]
            if nxt == "/":
                i += 2
                while i < n and text[i] != "\n":
                    i += 1
                continue
            if nxt == "*":
                i += 2
                while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                    i += 1
                i = min(i + 2, n)
                continue
        if ch == "{":
            stack.append(i)
        elif ch == "}" and stack:
            stack.pop()
        block_at[i] = stack[-1] if stack else -1
        i += 1
    return block_at


def indent_after_brace(text: str, brace_pos: int) -> str:
    line_start = text.rfind("\n", 0, brace_pos) + 1
    base = re.match(r"[ \t]*", text[line_start:brace_pos]).group(0)
    return base + "    "


def transform(text: str) -> tuple[str, int]:
    block_at = block_starts_at_each_index(text)
    block_decls: dict[int, OrderedDict[str, str]] = {}
    edits: list[tuple[int, int, str, str]] = []
    count = 0

    for m in FOR_RE.finditer(text):
        block_start = block_at[m.start()]
        if block_start < 0:
            continue
        const_kw = m.group(2)
        typ = re.sub(r"\s+", " ", m.group(3).strip())
        var = m.group(4)
        block_decls.setdefault(block_start, OrderedDict())
        if var not in block_decls[block_start]:
            block_decls[block_start][var] = f"{const_kw}{typ} {var};"
        edits.append((m.start(1), m.end(), f"{m.group(1)}{var} =", "replace"))
        count += 1

    for block_start, decls in block_decls.items():
        if not decls:
            continue
        indent = indent_after_brace(text, block_start)
        decl_lines = "".join(f"{indent}{decl}\n" for decl in decls.values())
        insert_pos = block_start + 1
        if insert_pos < len(text) and text[insert_pos] == "\n":
            insert_pos += 1
        edits.append((insert_pos, insert_pos, decl_lines, "insert"))

    for pos, end, chunk, kind in sorted(edits, key=lambda e: e[0], reverse=True):
        if kind == "replace":
            text = text[:pos] + chunk + text[end:]
        else:
            text = text[:pos] + chunk + text[pos:]

    return text, count


def main(argv: list[str]) -> int:
    roots = [Path(p) for p in argv[1:]] if len(argv) > 1 else [Path("c/src"), Path("c/tests")]
    total_files = 0
    total_loops = 0
    for root in roots:
        for path in sorted(root.rglob("*.c")):
            if "build" in path.parts:
                continue
            if path.name.startswith(".#"):
                continue
            original = path.read_text()
            updated, n = transform(original)
            if updated != original:
                path.write_text(updated)
                total_files += 1
                total_loops += n
                print(f"updated {path} ({n} for-loops)")
    print(f"done: {total_files} files, {total_loops} for-loops")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
