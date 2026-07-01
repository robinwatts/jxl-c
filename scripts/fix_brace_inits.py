#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Merge hoisted declarations with separate brace initializers back into one line."""

from __future__ import annotations

import re
import sys
from pathlib import Path

BRACE_ASSIGN_RE = re.compile(r"^(\s*)(\w+)(\s*(?:\[[^\]]*\])+)?\s*=\s*(\{.*\});\s*$")
DECL_ONLY_RE = re.compile(
    r"^(\s*)((?:const|static|volatile|register)\s+)*"
    r"([\w\s\*]+?)\s+(\w+)(\s*(?:\[[^\]]*\])+)?\s*;\s*$"
)


def depth_at(lines: list[str], idx: int) -> int:
    d = 0
    for i in range(idx):
        d += lines[i].count("{") - lines[i].count("}")
    return d


def find_block_lines(lines: list[str]) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    stack: list[int] = []
    for i, line in enumerate(lines):
        for ch in line:
            if ch == "{":
                stack.append(i)
            elif ch == "}" and stack:
                start = stack.pop()
                ranges.append((start, i))
    return ranges


def is_decl_only(line: str) -> re.Match[str] | None:
    s = line.rstrip("\n")
    if "=" in s:
        return None
    if not s.strip().endswith(";"):
        return None
    return DECL_ONLY_RE.match(s)


def fix_block(lines: list[str], open_line: int, close_line: int) -> int:
    if open_line + 1 >= close_line:
        return 0

    target_depth = depth_at(lines, open_line + 1)
    first = open_line + 1
    last = close_line - 1
    if first > last:
        return 0

    decl_map: dict[str, int] = {}
    decl_end = first - 1
    i = first
    while i <= last:
        if depth_at(lines, i) != target_depth:
            break
        s = lines[i].strip()
        if s == "" or s.startswith("//") or s.startswith("/*") or s.startswith("#"):
            i += 1
            continue
        m = is_decl_only(lines[i])
        if m is None:
            break
        var = m.group(4)
        decl_map[var] = i
        decl_end = i
        i += 1

    if not decl_map:
        return 0

    count = 0
    j = decl_end + 1
    while j <= last:
        if depth_at(lines, j) != target_depth:
            j += 1
            continue
        m = BRACE_ASSIGN_RE.match(lines[j].rstrip("\n"))
        if m is None:
            j += 1
            continue
        var = m.group(2)
        init = m.group(4)
        if var not in decl_map:
            j += 1
            continue
        decl_idx = decl_map[var]
        dm = is_decl_only(lines[decl_idx])
        if dm is None:
            j += 1
            continue
        indent = dm.group(1)
        quals = dm.group(2) or ""
        typ = dm.group(3).strip()
        arrays = dm.group(5) or ""
        lines[decl_idx] = f"{indent}{quals}{typ} {var}{arrays} = {init};\n"
        del lines[j]
        del decl_map[var]
        count += 1
        last -= 1

    return count


def transform(text: str) -> tuple[str, int]:
    lines = text.splitlines(keepends=True)
    total = 0
    for open_line, close_line in sorted(find_block_lines(lines), reverse=True):
        total += fix_block(lines, open_line, close_line)
    return "".join(lines), total


def main(argv: list[str]) -> int:
    if len(argv) > 1:
        inputs = [Path(p) for p in argv[1:]]
    else:
        inputs = [Path("c/src"), Path("c/tests")]
    paths: list[Path] = []
    for item in inputs:
        if item.is_file():
            paths.append(item)
        else:
            paths.extend(sorted(item.rglob("*.c")))
    total_files = 0
    total_fixes = 0
    for path in paths:
        if "build" in path.parts:
            continue
        original = path.read_text()
        updated, n = transform(original)
        if updated != original:
            path.write_text(updated)
            total_files += 1
            total_fixes += n
            print(f"updated {path} ({n} fixes)")
    print(f"done: {total_files} files, {total_fixes} fixes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
