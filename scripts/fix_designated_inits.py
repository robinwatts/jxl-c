#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Convert C99 designated initializers and compound literals to C89-style code."""

from __future__ import annotations

import re
import sys
from pathlib import Path

IDENT = r"[A-Za-z_]\w*"
FIELD_PATH_RE = re.compile(rf"\.({IDENT}(?:\.{IDENT})*)\s*=\s*")


def skip_strings_and_comments(text: str, i: int) -> int:
    n = len(text)
    if i >= n:
        return n
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


def skip_ws(text: str, i: int) -> int:
    n = len(text)
    while i < n:
        if text[i] in " \t\r\n":
            i += 1
            continue
        nxt = skip_strings_and_comments(text, i)
        if nxt != i + 1:
            i = nxt
            continue
        break
    return i


def find_matching_brace(text: str, open_idx: int) -> int:
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        ch = text[i]
        if ch in ("'", '"') or (ch == "/" and i + 1 < n and text[i + 1] in ("/", "*")):
            i = skip_strings_and_comments(text, i)
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def parse_value(text: str, i: int) -> tuple[str, int]:
    i = skip_ws(text, i)
    n = len(text)
    if i >= n:
        return "", i
    if text[i] == "{":
        close = find_matching_brace(text, i)
        if close < 0:
            return text[i:], n
        return text[i : close + 1], close + 1
    start = i
    depth_paren = 0
    while i < n:
        ch = text[i]
        if ch in ("'", '"') or (ch == "/" and i + 1 < n and text[i + 1] in ("/", "*")):
            i = skip_strings_and_comments(text, i)
            continue
        if ch == "(":
            depth_paren += 1
            i += 1
            continue
        if ch == ")":
            if depth_paren:
                depth_paren -= 1
            i += 1
            continue
        if depth_paren == 0 and ch in ",}":
            break
        i += 1
    return text[start:i].strip(), i


def flatten_designated(text: str, prefix: str) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    i = 0
    n = len(text)
    while i < n:
        i = skip_ws(text, i)
        if i >= n:
            break
        if text[i] != ".":
            i += 1
            continue
        m = FIELD_PATH_RE.match(text[i:])
        if not m:
            i += 1
            continue
        path = m.group(1)
        i += m.end()
        value, i = parse_value(text, i)
        value = value.strip()
        full_path = f"{prefix}.{path}" if prefix else path
        if value.startswith("{") and value.endswith("}"):
            inner = value[1:-1]
            if re.search(r"\.\w+\s*=", inner):
                out.extend(flatten_designated(inner, full_path))
            else:
                out.append((full_path, value))
        else:
            out.append((full_path, value))
        i = skip_ws(text, i)
        if i < n and text[i] == ",":
            i += 1
    return out


def has_designated(text: str) -> bool:
    return bool(re.search(r"\.\w+\s*=", text))


def line_indent(text: str, pos: int) -> str:
    line_start = text.rfind("\n", 0, pos) + 1
    m = re.match(r"[ \t]*", text[line_start:pos])
    return m.group(0) if m else ""


COMPOUND_COUNTER = 0


def next_compound_var() -> str:
    global COMPOUND_COUNTER
    COMPOUND_COUNTER += 1
    return f"compound_tmp_{COMPOUND_COUNTER}"


def strip_const_from_decl(decl: str) -> str:
    return re.sub(r"^const\s+", "", decl.strip())


def make_assignments(var: str, pairs: list[tuple[str, str]], indent: str) -> str:
    return "".join(f"{indent}{var}.{path} = {value};\n" for path, value in pairs)


def convert_init_block(prefix: str, var: str, typ: str | None, body: str, indent: str) -> str:
    if has_designated(body):
        pairs = flatten_designated(body, "")
        if typ:
            return f"{indent}{typ} {var} = {{0}};\n" + make_assignments(var, pairs, indent)
        return make_assignments(var, pairs, indent)
    init = body.strip()
    if typ:
        return f"{indent}{typ} {var} = {{{init}}};\n"
    return f"{indent}{var} = {{{init}}};\n"


def try_convert_at(text: str, open_idx: int) -> tuple[str, int, int] | None:
    prefix = text[:open_idx]
    stripped = prefix.rstrip()
    if not stripped:
        return None
    tail = stripped[-160:]
    if not re.search(
        rf"(?:return\s+\(\s*{IDENT}\s*\)|=\s*\(\s*{IDENT}\s*\)|"
        rf"(?:const\s+)?(?:static\s+)?(?:(?:{IDENT})\s+)+{IDENT}\s*=)\s*$",
        tail,
    ):
        return None
    if re.search(r"\[\s*$", tail) or re.search(
        rf"{IDENT}\s*\[\s*{IDENT}\s*\]\s*=\s*$", tail
    ):
        return None
    if re.search(r"\bcase\s+.*:\s*$", tail):
        return None
    tail_off = len(stripped) - len(tail)

    close = find_matching_brace(text, open_idx)
    if close < 0:
        return None
    body = text[open_idx + 1 : close]
    indent = line_indent(text, open_idx)
    end = close + 1
    if end < len(text) and text[end] == ";":
        end += 1

    m_ret = re.search(rf"return\s+\(\s*({IDENT})\s*\)\s*$", tail)
    if m_ret and has_designated(body):
        typ = m_ret.group(1)
        var = "result"
        repl = convert_init_block("", var, typ, body, indent)
        repl += f"{indent}return {var};\n"
        return stripped[: tail_off + m_ret.start()] + repl, end, 1

    m_assign = re.search(
        rf"(?:const\s+)?(?:static\s+)?(?:(?:{IDENT})\s+)+({IDENT})\s*=\s*$",
        tail,
    )
    if m_assign and has_designated(body):
        var = m_assign.group(1)
        decl = tail[m_assign.start() : m_assign.end()].rstrip()
        decl = re.sub(r"\s*=\s*$", "", decl)
        decl = strip_const_from_decl(decl)
        pairs = flatten_designated(body, "")
        repl = f"{decl};\n" + make_assignments(var, pairs, indent)
        return stripped[: tail_off + m_assign.start()] + repl, end, 1

    m_compound_ret = re.search(rf"return\s+\(\s*({IDENT})\s*\)\s*$", tail)
    if m_compound_ret and not has_designated(body):
        typ = m_compound_ret.group(1)
        var = "result"
        repl = convert_init_block("", var, typ, body, indent)
        repl += f"{indent}return {var};\n"
        return stripped[: tail_off + m_compound_ret.start()] + repl, end, 1

    m_compound = re.search(rf"=\s*\(\s*({IDENT})\s*\)\s*$", tail)
    if m_compound and has_designated(body):
        typ = m_compound.group(1)
        var = next_compound_var()
        eq_pos = tail_off + m_compound.start()
        line_start = stripped.rfind("\n", 0, eq_pos) + 1
        lhs_expr = stripped[line_start:eq_pos].strip()
        pairs = flatten_designated(body, "")
        repl = f"{indent}{typ} {var};\n" + make_assignments(var, pairs, indent)
        repl += f"{indent}{lhs_expr} = {var};\n"
        return stripped[:line_start] + repl, end, 1

    if m_compound and not has_designated(body):
        typ = m_compound.group(1)
        var = next_compound_var()
        eq_pos = tail_off + m_compound.start()
        line_start = stripped.rfind("\n", 0, eq_pos) + 1
        lhs_expr = stripped[line_start:eq_pos].strip()
        init = body.strip()
        repl = f"{indent}{typ} {var} = {{{init}}};\n{indent}{lhs_expr} = {var};\n"
        return stripped[:line_start] + repl, end, 1

    return None


def convert_text(text: str) -> tuple[str, int]:
    total = 0
    while True:
        changed = False
        i = 0
        n = len(text)
        while i < n:
            ch = text[i]
            if ch in ("'", '"') or (ch == "/" and i + 1 < n and text[i + 1] in ("/", "*")):
                i = skip_strings_and_comments(text, i)
                continue
            if ch != "{":
                i += 1
                continue
            conv = try_convert_at(text, i)
            if conv is not None:
                head, end, c = conv
                text = head + text[end:]
                i = len(head)
                total += c
                changed = True
                n = len(text)
                continue
            i += 1
        if not changed:
            break
    return text, total


def convert_array_of_designated(text: str) -> tuple[str, int]:
    """Handle `type name[N] = { { .f = v, }, ... };` array-of-struct designated inits."""
    pattern = re.compile(
        rf"^(\s*)(?:const\s+)?((?:{IDENT})(?:\s*\*+)?(?:\s+{IDENT})*)\s+({IDENT})\s*"
        rf"(\[[^\]]+\])\s*=\s*\{{",
        re.MULTILINE,
    )
    total = 0
    while True:
        m = pattern.search(text)
        if not m:
            break
        indent, typ, name, dims = m.group(1), m.group(2), m.group(3), m.group(4)
        open_idx = m.end() - 1
        close = find_matching_brace(text, open_idx)
        if close < 0:
            break
        body = text[open_idx + 1 : close]
        if not has_designated(body):
            break
        # Split top-level array elements
        elems: list[str] = []
        i = 0
        while i < len(body):
            i = skip_ws(body, i)
            if i >= len(body):
                break
            if body[i] != "{":
                break
            elem_close = find_matching_brace(body, i)
            if elem_close < 0:
                break
            elems.append(body[i + 1 : elem_close])
            i = elem_close + 1
            i = skip_ws(body, i)
            if i < len(body) and body[i] == ",":
                i += 1
        if not elems:
            break
        out = f"{indent}{typ} {name}{dims};\n"
        for idx, elem in enumerate(elems):
            pairs = flatten_designated(elem, "")
            out += make_assignments(f"{name}[{idx}]", pairs, indent)
        end = close + 1
        if end < len(text) and text[end] == ";":
            end += 1
        text = text[: m.start()] + out + text[end:]
        total += 1
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
            paths.extend(sorted(item.rglob("*.h")))
    total_files = 0
    total_changes = 0
    for path in paths:
        if "build" in path.parts or "_deps" in path.parts:
            continue
        original = path.read_text()
        global COMPOUND_COUNTER
        COMPOUND_COUNTER = 0
        updated, n1 = convert_text(original)
        updated, n2 = convert_array_of_designated(updated)
        n = n1 + n2
        if updated != original:
            path.write_text(updated)
            total_files += 1
            total_changes += n
            print(f"updated {path} ({n} blocks)")
    print(f"done: {total_files} files, {total_changes} blocks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
