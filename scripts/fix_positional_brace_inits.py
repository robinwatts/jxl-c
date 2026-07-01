#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR Apache-2.0
"""Convert non-constant positional brace initializers on locals to C89 field/element stores."""

from __future__ import annotations

import re
import sys
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from fix_designated_inits import find_matching_brace, line_indent, skip_strings_and_comments, skip_ws

IDENT = r"[A-Za-z_]\w*"
STRUCT_TYPEDEF_RE = re.compile(
    rf"typedef\s+struct\s*\{{([\s\S]*?)\}}\s*({IDENT})\s*;",
)
FIELD_LINE_RE = re.compile(
    rf"^\s*(?:const\s+)?(?:struct\s+{IDENT}\s+)?"
    rf"(?:(unsigned|signed)\s+)?"
    rf"((?:const\s+)?{IDENT}(?:\s*\*+)?)\s*({IDENT})\s*;\s*$",
    re.MULTILINE,
)
LOWER_IDENT_RE = re.compile(r"\b[a-z_][\w]*\b")
SKIP_IDENTS = {
    "true",
    "false",
    "NULL",
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


def split_top_level_commas(body: str) -> list[str]:
    parts: list[str] = []
    depth = 0
    paren = 0
    start = 0
    i = 0
    n = len(body)
    while i < n:
        ch = body[i]
        if ch in ("'", '"') or (ch == "/" and i + 1 < n and body[i + 1] in ("/", "*")):
            i = skip_strings_and_comments(body, i)
            continue
        if ch == "(":
            paren += 1
        elif ch == ")":
            paren = max(0, paren - 1)
        elif ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        elif ch == "," and depth == 0 and paren == 0:
            parts.append(body[start:i].strip())
            start = i + 1
        i += 1
    tail = body[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def parse_struct_fields(body: str) -> list[str]:
    fields: list[str] = []
    for line in body.splitlines():
        m = FIELD_LINE_RE.match(line)
        if m:
            fields.append(m.group(3))
    return fields


def build_struct_map(paths: list[Path]) -> dict[str, list[str]]:
    structs: dict[str, list[str]] = {}
    for path in paths:
        if path.suffix not in (".c", ".h"):
            continue
        if "build" in path.parts or path.name.startswith(".#"):
            continue
        text = path.read_text()
        for m in STRUCT_TYPEDEF_RE.finditer(text):
            name = m.group(2)
            fields = parse_struct_fields(m.group(1))
            if fields:
                structs[name] = fields
    return structs


def strip_comments_strings(text: str) -> str:
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch in ("'", '"') or (ch == "/" and i + 1 < n and text[i + 1] in ("/", "*")):
            end = skip_strings_and_comments(text, i)
            out.append(" " * (end - i))
            i = end
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def has_non_constant_init(body: str) -> bool:
    cleaned = strip_comments_strings(body)
    if "->" in cleaned:
        return True
    for ident in LOWER_IDENT_RE.findall(cleaned):
        if ident in SKIP_IDENTS:
            continue
        return True
    return False


def is_file_scope(text: str, decl_start: int) -> bool:
    prefix = text[:decl_start]
    line_start = prefix.rfind("\n") + 1
    line = prefix[line_start:decl_start]
    depth = 0
    for ch in prefix:
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
    if depth != 0:
        return False
    stripped = line.lstrip()
    return stripped.startswith("static ") or (
        line_start == 0 and not stripped.startswith("#")
    )


def strip_const(quals: str) -> str:
    parts = [q for q in quals.split() if q != "const"]
    return (" ".join(parts) + " ") if parts else ""


def convert_declaration(
    text: str,
    decl_start: int,
    open_idx: int,
    structs: dict[str, list[str]],
) -> tuple[str, int] | None:
    prefix = text[:open_idx]
    line_start = prefix.rfind("\n", 0, decl_start) + 1
    decl_line = text[line_start:open_idx]
    m = re.match(
        rf"^(\s*)((?:const|static)\s+)*(.+?)\s+({IDENT})(\s*(\[[^\]]*\]))?\s*=\s*$",
        decl_line,
    )
    if m is None:
        return None
    indent = m.group(1)
    quals = m.group(2) or ""
    if "static" in quals.split():
        return None
    if is_file_scope(text, decl_start):
        return None

    typ = re.sub(r"\s+", " ", m.group(3).strip())
    var = m.group(4)
    array_dims = m.group(5) or ""

    close = find_matching_brace(text, open_idx)
    if close < 0:
        return None
    end = close + 1
    if end < len(text) and text[end] == ";":
        end += 1
    body = text[open_idx + 1 : close]
    if not body.strip():
        return None
    if has_designated(body):
        return None
    if not has_non_constant_init(body):
        return None

    values = split_top_level_commas(body)
    if not values:
        return None

    quals_out = strip_const(quals)
    base_type = typ
    is_array = bool(array_dims)

    out = f"{indent}{quals_out}{base_type} {var}{array_dims};\n"
    fields = structs.get(base_type)

    if is_array:
        if any("{" in v for v in values):
            return None
        for idx, value in enumerate(values):
            out += f"{indent}{var}[{idx}] = {value};\n"
    elif fields and len(fields) == len(values):
        for field, value in zip(fields, values):
            out += f"{indent}{var}.{field} = {value};\n"
    elif fields and len(values) == 1 and "{" not in values[0]:
        out = f"{indent}{quals_out}{base_type} {var};\n"
        out += f"{indent}{var} = {values[0]};\n"
    else:
        if any("{" in v for v in values):
            return None
        if not fields or len(fields) != len(values):
            return None
        for field, value in zip(fields, values):
            out += f"{indent}{var}.{field} = {value};\n"

    return text[:line_start] + out + text[end:], 1


def has_designated(body: str) -> bool:
    return bool(re.search(r"\.(?:\w+\.)*\w+\s*=", body))


def convert_text(text: str, structs: dict[str, list[str]]) -> tuple[str, int]:
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
            decl_start = text.rfind("=", 0, i)
            if decl_start < 0:
                i += 1
                continue
            conv = convert_declaration(text, decl_start, i, structs)
            if conv is not None:
                text, count = conv
                total += count
                changed = True
                i = decl_start
                n = len(text)
                continue
            i += 1
        if not changed:
            break
    return text, total


def collect_paths(inputs: list[Path]) -> list[Path]:
    paths: list[Path] = []
    for item in inputs:
        if item.is_file():
            paths.append(item)
        else:
            paths.extend(sorted(item.rglob("*.c")))
            paths.extend(sorted(item.rglob("*.h")))
    return [p for p in paths if "build" not in p.parts and not p.name.startswith(".#")]


def main(argv: list[str]) -> int:
    if len(argv) > 1:
        inputs = [Path(p) for p in argv[1:]]
    else:
        inputs = [Path("c/src"), Path("c/tests")]
    paths = collect_paths(inputs)
    c_paths = [p for p in paths if p.suffix == ".c"]
    h_paths = [p for p in paths if p.suffix == ".h"]
    structs = build_struct_map(c_paths + h_paths)

    total_files = 0
    total_inits = 0
    for path in c_paths:
        original = path.read_text()
        updated, n = convert_text(original, structs)
        if updated != original:
            path.write_text(updated)
            total_files += 1
            total_inits += n
            print(f"updated {path} ({n} inits)")
    print(f"done: {total_files} files, {total_inits} inits")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
