#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Hoist single-line mid-block variable declarations to block starts (C89 style)."""

from __future__ import annotations

import re
import sys
from collections import OrderedDict
from pathlib import Path

TYPE_RE = (
    r"size_t|ssize_t|int|uint32_t|uint8_t|uint16_t|uint64_t|int32_t|int64_t|"
    r"float|double|char|long|unsigned\s+int|unsigned\s+short|unsigned\s+long|short"
)
TYPE_PATTERN = (
    rf"(?:{TYPE_RE}|struct\s+\w+|union\s+\w+|jxl_[A-Za-z0-9_]+|FILE|DIR|"
    rf"__m128i?|__m256i?|__m128d|__m256d)"
)
QUAL_RE = r"(?:const|static|volatile|register)\s+"
DECL_RE = re.compile(
    rf"^(\s*)((?:{QUAL_RE})*)"
    rf"(?:({TYPE_PATTERN})(\s*\*+)?\s+|({TYPE_PATTERN}\s+\*+\s+))"
    rf"(\w+)"
    rf"(\s*(?:\[[^\]]*\])+)?"
    rf"(\s*=\s*.+)?"
    rf"\s*;\s*$"
)
LABEL_RE = re.compile(r"^\s*\w+\s*:\s*$")
STMT_PREFIX_RE = re.compile(
    r"^\s*(?:"
    r"if\b|else\b|for\b|while\b|do\b|switch\b|case\b|default\b|return\b|goto\b|"
    r"break\b|continue\b|\#\s*(?:if|else|elif|endif|define|include|pragma)\b|"
    r"\}\s*"
    r")"
)
FUNC_DEF_RE = re.compile(
    r"^\s*(?:static\s+|inline\s+)*[\w\s\*]+\([^;]*\)\s*\{?\s*$"
)
STRUCT_BLOCK_RE = re.compile(
    r"(?:^|[\s;])(?:struct|union|enum)\s+[\w\s\*]*\{?\s*$"
)
IDENT_RE = re.compile(r"\b[A-Za-z_]\w*\b")
SKIP_IDENTS = {
    "NULL",
    "true",
    "false",
    "sizeof",
    "return",
    "if",
    "else",
    "for",
    "while",
    "do",
    "switch",
    "case",
    "default",
    "break",
    "continue",
    "goto",
}


def is_aggregate_definition_block(text: str, brace_open: int) -> bool:
    """Return true when `{` opens a struct/union/enum definition, not a statement block."""
    line_start = text.rfind("\n", 0, brace_open) + 1
    prefix = text[line_start:brace_open].strip()
    if not prefix:
        return False
    if prefix.endswith("{"):
        prefix = prefix[:-1].strip()
    if STRUCT_BLOCK_RE.search(prefix + " {"):
        return True
    if prefix.endswith("typedef struct") or prefix.endswith("typedef union") or prefix.endswith(
        "typedef enum"
    ):
        return True
    return False


def skip_strings_and_comments(text: str, i: int) -> int:
    n = len(text)
    ch = text[i]
    if ch in ("'", '"'):
        quote = ch
        i += 1
        while i < n:
            if text[i] == "\\":
                i += 2
                continue
            if text[i] == quote:
                return i + 1
            i += 1
        return n
    if ch == "/" and i + 1 < n:
        nxt = text[i + 1]
        if nxt == "/":
            i += 2
            while i < n and text[i] != "\n":
                i += 1
            return i
        if nxt == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            return min(i + 2, n)
    return i + 1


def find_block_ranges(text: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    stack: list[int] = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch in ("'", '"') or (ch == "/" and i + 1 < n and text[i + 1] in ("/", "*")):
            i = skip_strings_and_comments(text, i)
            continue
        if ch == "{":
            stack.append(i)
        elif ch == "}" and stack:
            start = stack.pop()
            ranges.append((start, i))
        i += 1
    ranges.sort(key=lambda r: (r[1] - r[0], r[0]))
    return ranges


def line_starts(text: str) -> list[int]:
    starts = [0]
    for i, ch in enumerate(text):
        if ch == "\n":
            starts.append(i + 1)
    return starts


def pos_to_line(pos: int, starts: list[int]) -> int:
    lo, hi = 0, len(starts) - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        if starts[mid] <= pos:
            lo = mid + 1
        else:
            hi = mid - 1
    return hi


def line_range_for_block(text: str, brace_open: int, brace_close: int, starts: list[int]) -> tuple[int, int]:
    first = pos_to_line(brace_open, starts) + 1
    last = pos_to_line(brace_close, starts) - 1
    return first, last


def depth_at_line_start(lines: list[str], line_idx: int) -> int:
    depth = 0
    for i in range(line_idx):
        depth += lines[i].count("{") - lines[i].count("}")
    return depth


def is_comment_or_empty(line: str) -> bool:
    s = line.strip()
    return (
        s == ""
        or s.startswith("//")
        or s.startswith("/*")
        or s.startswith("*")
        or s.startswith("#")
    )


def is_declaration_line(line: str) -> re.Match[str] | None:
    stripped = line.rstrip("\n")
    if ";" not in stripped:
        return None
    if LABEL_RE.match(stripped):
        return None
    if STMT_PREFIX_RE.match(stripped):
        return None
    if FUNC_DEF_RE.match(stripped):
        return None
    if "union" in stripped and "{" in stripped:
        return None
    return DECL_RE.match(stripped)


def decl_names_in_line(line: str) -> list[str]:
    m = is_declaration_line(line)
    if m is None:
        return []
    return [m.group(6)]


def init_identifiers(init: str | None) -> set[str]:
    if not init:
        return set()
    body = init.lstrip()
    if body.startswith("="):
        body = body[1:].lstrip()
    return {t for t in IDENT_RE.findall(body) if t not in SKIP_IDENTS}


def function_params_before(lines: list[str], first: int) -> set[str]:
    """Parameter names from the function definition enclosing this block."""
    start = max(0, first - 50)
    chunk = "".join(lines[start:first])
    open_idx = chunk.rfind("(")
    close_idx = chunk.rfind(")")
    if open_idx < 0 or close_idx < open_idx:
        return set()
    params = chunk[open_idx + 1 : close_idx]
    names: set[str] = set()
    for part in params.split(","):
        part = part.strip()
        if not part:
            continue
        tokens = IDENT_RE.findall(part)
        if tokens:
            name = tokens[-1]
            if name not in SKIP_IDENTS:
                names.add(name)
    return names


def hoist_unsafe_at_line(lines: list[str], decl_line_idx: int, first: int, target_depth: int) -> bool:
    """True when hoisting would move an initializer before its dependencies."""
    m = is_declaration_line(lines[decl_line_idx])
    if m is None:
        return False
    init = m.group(8)
    if not init:
        return False
    init_body = init.lstrip()
    if init_body.startswith("="):
        init_body = init_body[1:].lstrip()
    if init_body.startswith("{"):
        needed = init_identifiers(init_body)
        if needed:
            return True
        return False
    needed = init_identifiers(init)
    if not needed:
        return False
    arrays = m.group(7) or ""
    if arrays:
        for token in IDENT_RE.findall(arrays):
            if token not in SKIP_IDENTS and token not in {"sizeof"}:
                needed.add(token)
    var = m.group(6)
    available: set[str] = function_params_before(lines, first)
    for i in range(first, decl_line_idx):
        if depth_at_line_start(lines, i) != target_depth:
            continue
        s = lines[i].strip()
        if not s or s.startswith("//") or s.startswith("/*"):
            continue
        dm = is_declaration_line(lines[i])
        if dm is not None:
            available.add(dm.group(6))
            continue
        if "=" in s and not s.startswith("#"):
            lhs = s.split("=", 1)[0].strip()
            if lhs and lhs.replace("_", "").isalnum():
                available.add(lhs.split("[", 1)[0].strip())
    return bool(needed - available - {var})


def split_decl_line(m: re.Match[str]) -> tuple[str, str | None]:
    indent = m.group(1)
    quals = m.group(2) or ""
    typ = m.group(3) or ""
    stars = m.group(4) or ""
    if m.group(5):
        typ = m.group(5).rstrip()
        stars = ""
    name = m.group(6)
    arrays = m.group(7) or ""
    init = m.group(8)
    decl = f"{indent}{quals}{typ}{stars} {name}{arrays};"
    if init:
        init_body = init.lstrip()
        if init_body.startswith("="):
            init_body = init_body[1:].lstrip()
        if init_body.startswith("{"):
            return f"{indent}{quals}{typ}{stars} {name}{arrays}{init};", None
        return decl, f"{indent}{name}{init};"
    return decl, None


def in_preprocessor_branch(line: str) -> bool:
    s = line.lstrip()
    return s.startswith("#if") or s.startswith("#ifdef") or s.startswith("#ifndef") or s.startswith(
        "#else"
    ) or s.startswith("#elif") or s.startswith("#endif")


def find_hoist_insert_at(output: list[str]) -> int:
    insert_at = 0
    while insert_at < len(output):
        line = output[insert_at]
        if is_comment_or_empty(line):
            insert_at += 1
            continue
        if re.match(r"^\s*enum\s*\{", line):
            while insert_at < len(output):
                if ";" in output[insert_at]:
                    insert_at += 1
                    break
                insert_at += 1
            continue
        if is_declaration_line(line) is not None:
            insert_at += 1
            continue
        break

    probe = insert_at
    if probe < len(output) and in_preprocessor_branch(output[probe]):
        s = output[probe].lstrip()
        if s.startswith(("#if", "#ifdef", "#ifndef")):
            depth = 1
            i = probe + 1
            while i < len(output) and depth > 0:
                line = output[i]
                ls = line.lstrip()
                if ls.startswith("#if"):
                    depth += 1
                elif ls.startswith("#endif"):
                    depth -= 1
                elif depth == 1 and ls.startswith("#else"):
                    return i + 1
                i += 1
    return insert_at


def hoist_block_lines(lines: list[str], first: int, last: int) -> int:
    if first > last or last >= len(lines):
        return 0

    target_depth = depth_at_line_start(lines, first)
    hoisted: OrderedDict[str, str] = OrderedDict()
    output: list[str] = []
    past_stmt = False
    count = 0

    for i in range(first, last + 1):
        line = lines[i]
        if depth_at_line_start(lines, i) != target_depth:
            output.append(line)
            continue

        if is_comment_or_empty(line):
            output.append(line)
            continue

        if in_preprocessor_branch(line):
            output.append(line)
            if line.lstrip().startswith(("#if", "#ifdef", "#ifndef")):
                past_stmt = True
            continue

        m = is_declaration_line(line)
        if m is not None:
            quals = m.group(2) or ""
            has_const = "const" in quals.split()
            if past_stmt:
                var = m.group(6)
                if m.group(8) and (has_const or "static" in quals.split()):
                    output.append(line)
                    continue
                if hoist_unsafe_at_line(lines, i, first, target_depth):
                    output.append(line)
                    continue
                if has_const or "static" in quals.split():
                    decl_line = line if line.endswith("\n") else line + "\n"
                    if var not in hoisted:
                        hoisted[var] = decl_line
                else:
                    decl, assign = split_decl_line(m)
                    decl_line = decl if decl.endswith("\n") else decl + "\n"
                    if var not in hoisted:
                        hoisted[var] = decl_line
                    if assign:
                        output.append(assign if assign.endswith("\n") else assign + "\n")
                count += 1
                continue
            output.append(line)
            continue

        stripped = line.strip()
        if stripped:
            past_stmt = True
        output.append(line)

    if not hoisted:
        return 0

    merged: list[str] = []
    insert_at = find_hoist_insert_at(output)
    merged.extend(output[:insert_at])
    merged.extend(hoisted.values())
    merged.extend(output[insert_at:])

    lines[first : last + 1] = merged
    return count


def transform(text: str) -> tuple[str, int]:
    total = 0
    while True:
        lines = text.splitlines(keepends=True)
        starts = line_starts(text)
        ranges = find_block_ranges(text)
        if not ranges:
            break
        changed = False
        for brace_open, brace_close in ranges:
            if is_aggregate_definition_block(text, brace_open):
                continue
            first, last = line_range_for_block(text, brace_open, brace_close, starts)
            if first > last or last >= len(lines):
                continue
            n = hoist_block_lines(lines, first, last)
            if n > 0:
                text = "".join(lines)
                total += n
                changed = True
                break
        if not changed:
            break
    return text, total


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
    total_decls = 0
    for path in paths:
        if "build" in path.parts:
            continue
        if path.name.startswith(".#"):
            continue
        original = path.read_text()
        updated, n = transform(original)
        if updated != original:
            path.write_text(updated)
            total_files += 1
            total_decls += n
            print(f"updated {path} ({n} declarations)")
    print(f"done: {total_files} files, {total_decls} declarations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
