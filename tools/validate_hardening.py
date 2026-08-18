#!/usr/bin/env python3
"""Static validation for EXU API/documentation and hardening invariants."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def check_api_parity() -> None:
    source = read("src/luaexport.cpp")
    marker = "const luaL_Reg exuExports[] = {"
    start = source.find(marker)
    if start < 0:
        fail("could not locate exuExports registration table")
    end = source.find("{ 0, 0 }", start)
    if end < 0:
        fail("could not locate exuExports sentinel")

    table = source[start:end]
    runtime = set(re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,', table))
    definitions = set(
        re.findall(r"\bfunction\s+exu\.([A-Za-z_][A-Za-z0-9_]*)\s*\(", read("Definitions/ExtraUtils.lua"))
    )

    missing_docs = sorted(runtime - definitions)
    stale_docs = sorted(definitions - runtime)
    if missing_docs or stale_docs:
        if missing_docs:
            print("Runtime exports missing from Definitions/ExtraUtils.lua:")
            for name in missing_docs:
                print(f"  - {name}")
        if stale_docs:
            print("Definition functions not present in runtime export table:")
            for name in stale_docs:
                print(f"  - {name}")
        raise SystemExit(1)

    print(f"API parity OK: {len(runtime)} Lua functions")


def check_versions() -> None:
    about = read("src/About.h")
    public = read("include/ExtraUtils.h")
    defs = read("Definitions/ExtraUtils.lua")

    runtime = re.search(r'version\s*=\s*"([^"]+)"', about)
    header = re.search(r'EXU_VERSION_EXPECTED\s+"([^"]+)"', public)
    definition = re.search(r"definitions for Extra Utilities version ([0-9.]+)", defs)
    if not runtime or not header or not definition:
        fail("could not resolve all EXU version declarations")

    values = {runtime.group(1), header.group(1), definition.group(1)}
    if len(values) != 1:
        fail(
            "version declarations disagree: "
            f"runtime={runtime.group(1)} header={header.group(1)} definitions={definition.group(1)}"
        )

    print(f"Version parity OK: {runtime.group(1)}")


def resolve_catalog_path(catalog: dict, path: str) -> dict:
    node = catalog.get("addresses", {})
    for part in path.split("."):
        if not isinstance(node, dict) or part not in node:
            fail(f"build profile references missing exu.json catalog entry: {path}")
        node = node[part]
    if not isinstance(node, dict):
        fail(f"build profile catalog entry is not an object: {path}")
    return node


def check_address_catalog() -> None:
    catalog = json.loads(read("exu.json"))
    target = catalog.get("target", {})
    if target.get("version") != "2.2.301" or target.get("architecture") != "x86":
        fail("exu.json target must remain BZR 2.2.301 x86")

    profile = json.loads(read("profiles/bzr_2.2.301.json"))
    if profile.get("schema_version") != 1:
        fail("unsupported BZR build-profile schema")
    if profile.get("version") != target.get("version") or profile.get("architecture") != target.get("architecture"):
        fail("BZR build profile must match exu.json target version/architecture")

    anchors = profile.get("anchors", [])
    runtime_anchors = [anchor for anchor in anchors if anchor.get("runtime_gate") and anchor.get("required")]
    if len(runtime_anchors) < 3:
        fail("runtime BZR build profile must contain at least three required anchors")

    if not any(anchor.get("name") == "Camera.Set_View" for anchor in runtime_anchors):
        fail("runtime BZR build profile is missing its Set_View anchor")

    for anchor in anchors:
        if anchor.get("source") == "catalog":
            entry = resolve_catalog_path(catalog, anchor.get("catalog_path", ""))
            if not isinstance(entry.get("pattern"), str) or not entry["pattern"].strip():
                fail(f"catalog-backed build anchor lacks a signature: {anchor.get('name')}")
        elif anchor.get("source") == "inline":
            if not isinstance(anchor.get("pattern"), str) or not anchor["pattern"].strip():
                fail(f"inline build anchor lacks a signature: {anchor.get('name')}")
        else:
            fail(f"unsupported build-anchor source: {anchor.get('source')}")

    generated = read("src/Util/BzrBuildProfile.generated.h")
    build_validation = read("src/Util/BuildValidation.h")
    required_generated_markers = [
        'kProfileId = "bzr-2.2.301"',
        'kGameVersion = "2.2.301"',
        '"Camera.Set_View"',
        "kRuntimeAnchors",
    ]
    for marker in required_generated_markers:
        if marker not in generated:
            fail(f"generated BZR runtime profile is missing marker: {marker}")

    if "BzrBuildProfile.generated.h" not in build_validation or "BzrBuildProfile::kRuntimeAnchors" not in build_validation:
        fail("runtime BZR build gate is not consuming the generated multi-anchor profile")

    print(f"Address catalog/runtime build profile OK: BZR 2.2.301 x86 ({len(runtime_anchors)} required anchors)")


def check_hardening_markers() -> None:
    hook = read("src/Hook.h")
    scanner = read("src/Scanner.h")
    basic = read("src/BasicPatch.h")
    dllmain = read("src/dllmain.cpp")
    lua_state = read("src/LuaState.h")
    add_scrap = read("src/Patches/AddScrapCallback.cpp")

    required = [
        ("Hook move deletion", "Hook(Hook&&) = delete;" in hook),
        ("Scanner move deletion", "Scanner(Scanner&&) = delete;" in scanner),
        ("instruction cache flush", "FlushInstructionCache" in basic),
        ("expected-byte validation", "expected bytes do not match" in basic),
        ("runtime BZR build gate", "BuildValidation::IsSupportedBzr2301()" in basic),
        ("deferred requested status", "m_requestedStatus = s;" in basic),
        ("Lua-state generation", "m_generation" in lua_state),
        ("protected AddScrap call", "lua_pcall(L, 2, 1, 0)" in add_scrap),
    ]
    for label, ok in required:
        if not ok:
            fail(f"hardening invariant missing: {label}")

    forbidden_loader_calls = [
        "ResetLogFileForCurrentProcess",
        "Logging::LogMessage",
        "ShutdownOverlaySupport",
        "AllocConsole",
        "FreeConsole",
    ]
    for token in forbidden_loader_calls:
        if token in dllmain:
            fail(f"DllMain still performs nontrivial loader-lock work: {token}")

    print("Hardening markers OK")


def main() -> None:
    check_api_parity()
    check_versions()
    check_address_catalog()
    check_hardening_markers()
    print("All EXU hardening validation checks passed.")


if __name__ == "__main__":
    main()
