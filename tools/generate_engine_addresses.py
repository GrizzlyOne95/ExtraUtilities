#!/usr/bin/env python3
# Copyright (C) 2026 GrizzlyOne95
#
# This file is part of Extra Utilities.
#
# Extra Utilities is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
# details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
"""Generate src/Util/EngineAddresses.generated.h from exu.json.

exu.json is the single source of truth for BZR engine addresses. Every
catalog entry becomes `ExtraUtilities::EngineAddresses::<Group>::<Name>`, an
enumerator with underlying type uintptr_t (see the header comment for why not a
constexpr variable), and feature code uses those names instead of raw
literals (tools/validate_hardening.py rejects engine-range literals anywhere
else in src/). The constants are plain virtual addresses of the qualified
2.2.301 image; code that dereferences or patches them stays behind the
existing runtime build gate.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Optional

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = ROOT / "exu.json"
DEFAULT_OUTPUT = ROOT / "src" / "Util" / "EngineAddresses.generated.h"

IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
ADDRESS_RE = re.compile(r"^0x[0-9A-Fa-f]{8}$")

# Names a catalog key may not take, because the generated identifier would not
# compile or would shadow something every translation unit sees.
RESERVED = {
    "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const",
    "constexpr", "continue", "default", "delete", "do", "double", "else", "enum", "explicit", "export",
    "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
    "namespace", "new", "noexcept", "nullptr", "operator", "private", "protected", "public", "register",
    "return", "short", "signed", "sizeof", "static", "struct", "switch", "template", "this", "throw",
    "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
    "while", "uintptr_t", "std",
}


def catalog_groups(catalog: dict) -> Iterable[tuple[str, list[tuple[str, dict]]]]:
    addresses = catalog.get("addresses")
    if not isinstance(addresses, dict) or not addresses:
        raise ValueError("exu.json has no 'addresses' object")
    for group, entries in addresses.items():
        if group.startswith("_") or group.startswith("$"):
            continue
        if not isinstance(entries, dict):
            raise ValueError(f"catalog group {group!r} is not an object")
        rows: list[tuple[str, dict]] = []
        for name, entry in entries.items():
            if name.startswith("_") or name.startswith("$"):
                continue
            # "value" entries are structure offsets and constants, not
            # engine addresses; they stay documentation-only.
            if isinstance(entry, dict) and "value" in entry and "address" not in entry:
                continue
            if not isinstance(entry, dict) or "address" not in entry:
                raise ValueError(f"catalog entry {group}.{name} must be an object with an 'address' or a 'value'")
            rows.append((name, entry))
        yield group, rows


def check_identifier(kind: str, value: str) -> None:
    if not IDENTIFIER_RE.match(value) or value in RESERVED:
        raise ValueError(f"catalog {kind} {value!r} is not usable as a C++ identifier")


def comment_text(text: object) -> str:
    # Single line, and nothing that could close or nest a comment.
    return " ".join(str(text).split()).replace("*/", "* /").replace("/*", "/ *")


def render(catalog_path: Path) -> str:
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    target = catalog.get("target", {})
    version = target.get("version")
    if not isinstance(version, str) or not version:
        raise ValueError("exu.json target.version is missing")

    lines = [
        "/*",
        " * AUTO-GENERATED FILE. DO NOT EDIT BY HAND.",
        " *",
        " * Source: exu.json",
        " * Regenerate with: python tools/generate_engine_addresses.py",
        " */",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        f"// Engine virtual addresses for BZR {version} x86, one per exu.json entry.",
        "// Only valid once the runtime build gate (BuildValidation / RuntimeGate)",
        "// has accepted the running executable; code that reads, calls or patches",
        "// them must stay behind that gate. tools/validate_hardening.py rejects",
        "// engine-range literals anywhere else in src/.",
        "//",
        "// The addresses are enumerators, not constexpr variables, so the generated",
        "// code is identical to the literals they replace: MSVC folds `(T*)enumerator`",
        "// into a constant initializer exactly like `(T*)0x...`, but for",
        "// `(T*)constexpr_variable` in an inline variable it also emits a /include",
        "// directive that keeps every such variable alive in the linked image.",
        "namespace ExtraUtilities::EngineAddresses",
        "{",
        f'\tinline constexpr const char* kCatalogVersion = "{version}";',
    ]

    seen_groups: set[str] = set()
    total = 0
    for group, rows in catalog_groups(catalog):
        check_identifier("group", group)
        if group in seen_groups:
            raise ValueError(f"duplicate catalog group {group!r}")
        seen_groups.add(group)
        lines.extend(["", f"\tnamespace {group}", "\t{", "\t\tenum : uintptr_t", "\t\t{"])
        seen_names: set[str] = set()
        for name, entry in rows:
            check_identifier("entry", name)
            if name in seen_names:
                raise ValueError(f"duplicate catalog entry {group}.{name}")
            seen_names.add(name)
            address = entry["address"]
            if not isinstance(address, str) or not ADDRESS_RE.match(address):
                raise ValueError(f"catalog entry {group}.{name} needs an 0xXXXXXXXX address, got {address!r}")
            value = int(address, 16)
            kind = entry.get("type")
            if kind:
                lines.append(f"\t\t\t// {comment_text(kind)}")
            lines.append(f"\t\t\t{name} = 0x{value:08X}u,")
            total += 1
        lines.extend(["\t\t};", "\t}"])

    if total == 0:
        raise ValueError("exu.json has no address entries")

    lines.extend([
        "",
        f"\tinline constexpr unsigned kEntryCount = {total}u;",
        "}",
        "",
    ])
    return "\n".join(lines)


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Generate the EXU engine-address header from exu.json.")
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="Fail if the committed generated header is stale")
    args = parser.parse_args(argv)

    try:
        content = render(args.catalog.resolve())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"engine address generation failed: {exc}", file=sys.stderr)
        return 2

    if args.check:
        if not args.output.exists():
            print(f"generated engine address header is missing: {args.output}", file=sys.stderr)
            return 1
        existing = args.output.read_text(encoding="utf-8")
        if existing != content:
            print(
                "generated engine address header is stale; run "
                "`python tools/generate_engine_addresses.py` and commit the result",
                file=sys.stderr,
            )
            return 1
        print("Engine address header generation check passed")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    # newline="\n" keeps the committed bytes identical on Windows and Linux.
    with args.output.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
    print(f"Generated {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
