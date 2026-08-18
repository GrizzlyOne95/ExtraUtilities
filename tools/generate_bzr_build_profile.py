#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Optional

from qualify_bzr_build import anchor_pattern, load_profile


def cpp_identifier(name: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9]+", "_", name).strip("_")
    if not cleaned:
        cleaned = "Anchor"
    if cleaned[0].isdigit():
        cleaned = "_" + cleaned
    return cleaned


def render(profile_path: Path) -> str:
    profile, catalog = load_profile(profile_path)
    runtime_anchors = [anchor for anchor in profile["anchors"] if anchor.get("runtime_gate")]
    if not runtime_anchors:
        raise ValueError("profile has no runtime_gate anchors")

    lines = [
        "/*",
        " * AUTO-GENERATED FILE. DO NOT EDIT BY HAND.",
        " *",
        " * Source: profiles/bzr_2.2.301.json + exu.json",
        " * Regenerate with: python tools/generate_bzr_build_profile.py",
        " */",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace ExtraUtilities::BzrBuildProfile",
        "{",
        "\tenum class AnchorMatchMode : uint8_t",
        "\t{",
        "\t\tExpectedVa,",
        "\t\tExecutableContains,",
        "\t\tUniqueExecutable,",
        "\t};",
        "",
        "\tstruct AnchorSpec",
        "\t{",
        "\t\tconst char* name;",
        "\t\tconst int* pattern;",
        "\t\tsize_t patternSize;",
        "\t\tAnchorMatchMode mode;",
        "\t\tuintptr_t expectedVa;",
        "\t\tbool required;",
        "\t};",
        "",
        f'\tinline constexpr const char* kProfileId = "{profile["profile_id"]}";',
        f'\tinline constexpr const char* kGameVersion = "{profile["version"]}";',
        f"\tinline constexpr uintptr_t kImageBase = {int(profile['image_base'], 0):#010x}u;",
        "",
    ]

    anchor_rows = []
    mode_map = {
        "expected_va": "AnchorMatchMode::ExpectedVa",
        "executable_contains": "AnchorMatchMode::ExecutableContains",
        "unique_executable": "AnchorMatchMode::UniqueExecutable",
    }

    for index, anchor in enumerate(runtime_anchors):
        pattern = anchor_pattern(anchor, catalog)
        ident = f"kAnchor{index}_{cpp_identifier(anchor['name'])}"
        rendered = ", ".join("-1" if value is None else f"0x{value:02X}" for value in pattern)
        lines.append(f"\tinline constexpr int {ident}[] = {{ {rendered} }};")
        expected = int(anchor.get("expected_va", "0"), 0)
        mode = mode_map[anchor["match"]]
        required = "true" if anchor.get("required", False) else "false"
        name = anchor["name"].replace("\\", "\\\\").replace('"', '\\"')
        anchor_rows.append(
            f'\t\t{{ "{name}", {ident}, sizeof({ident}) / sizeof({ident}[0]), '
            f"{mode}, {expected:#010x}u, {required} }}"
        )

    lines.extend([
        "",
        "\tinline constexpr AnchorSpec kRuntimeAnchors[] =",
        "\t{",
        ",\n".join(anchor_rows),
        "\t};",
        "",
        "\tinline constexpr size_t kRuntimeAnchorCount =",
        "\t\tsizeof(kRuntimeAnchors) / sizeof(kRuntimeAnchors[0]);",
        "}",
        "",
    ])
    return "\n".join(lines)


def main(argv: Optional[list[str]] = None) -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Generate the compiled EXU BZR runtime build profile.")
    parser.add_argument("--profile", type=Path, default=root / "profiles" / "bzr_2.2.301.json")
    parser.add_argument("--output", type=Path, default=root / "src" / "Util" / "BzrBuildProfile.generated.h")
    parser.add_argument("--check", action="store_true", help="Fail if the committed generated header is stale")
    args = parser.parse_args(argv)

    try:
        content = render(args.profile.resolve())
    except Exception as exc:
        print(f"profile generation failed: {exc}", file=sys.stderr)
        return 2

    if args.check:
        if not args.output.exists():
            print(f"generated profile is missing: {args.output}", file=sys.stderr)
            return 1
        existing = args.output.read_text(encoding="utf-8")
        if existing != content:
            print(
                "generated BZR build profile is stale; run "
                "`python tools/generate_bzr_build_profile.py` and commit the result",
                file=sys.stderr,
            )
            return 1
        print("BZR build profile generation check passed")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8")
    print(f"Generated {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
