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


LINE_COMMENT_RE = re.compile(r"//[^\n]*")
REGISTRATION_ROW_RE = re.compile(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,')
SENTINEL_ROW_RE = re.compile(r"\{\s*(?:nullptr|NULL|0)\s*,\s*(?:nullptr|NULL|0)\s*\}")


def registered_names(path: str, start_marker: str) -> set[str]:
    """Names in a luaL_Reg table. Comments are stripped first so a
    commented-out row is not counted as a live export."""
    source = read(path)
    start = source.find(start_marker)
    if start < 0:
        fail(f"could not locate registration table in {path}")
    sentinel = SENTINEL_ROW_RE.search(source, start)
    if not sentinel:
        fail(f"could not locate registration table sentinel in {path}")
    table = LINE_COMMENT_RE.sub("", source[start:sentinel.start()])
    return set(REGISTRATION_ROW_RE.findall(table))


def documented_names(path: str, table: str) -> set[str]:
    return set(re.findall(rf"\bfunction\s+{table}\.([A-Za-z_][A-Za-z0-9_]*)\s*\(", read(path)))


def require_parity(label: str, definitions_path: str, runtime: set[str], definitions: set[str]) -> None:
    missing_docs = sorted(runtime - definitions)
    stale_docs = sorted(definitions - runtime)
    if missing_docs or stale_docs:
        if missing_docs:
            print(f"Runtime {label} exports missing from {definitions_path}:")
            for name in missing_docs:
                print(f"  - {name}")
        if stale_docs:
            print(f"{definitions_path} functions not present in the {label} runtime table:")
            for name in stale_docs:
                print(f"  - {name}")
        raise SystemExit(1)


SUB_API_TABLES = (
    ("exu.animation", "src/Game/AnimationApi.h", "Definitions/Animation.lua", "animation"),
    ("exu.storage", "src/Util/StorageApi.h", "Definitions/Storage.lua", "storage"),
    ("exu.continuity", "src/Game/ContinuityApi.h", "Definitions/Continuity.lua", "continuity"),
)


def check_api_parity() -> None:
    runtime = registered_names("src/luaexport.cpp", "const luaL_Reg exuExports[] = {")
    require_parity("exu", "Definitions/ExtraUtils.lua", runtime, documented_names("Definitions/ExtraUtils.lua", "exu"))
    total = len(runtime)

    for label, header, definitions_path, table in SUB_API_TABLES:
        sub_runtime = registered_names(header, "luaL_Reg functions[] = {")
        require_parity(label, definitions_path, sub_runtime, documented_names(definitions_path, table))
        total += len(sub_runtime)

    print(f"API parity OK: {len(runtime)} exu functions, {total} including sub-APIs")


def check_versions() -> None:
    about = read("src/About.h")
    public = read("include/ExtraUtils.h")
    defs = read("Definitions/ExtraUtils.lua")
    # The resource script is UTF-16LE; the DLL's version resource is read from it.
    resource = (ROOT / "Resource/Resource.rc").read_text(encoding="utf-16")

    runtime = re.search(r'version\s*=\s*"([^"]+)"', about)
    header = re.search(r'EXU_VERSION_EXPECTED\s+"([^"]+)"', public)
    definition = re.search(r"definitions for Extra Utilities version ([0-9.]+)", defs)
    if not runtime or not header or not definition:
        fail("could not resolve all EXU version declarations")

    numeric_versions = re.findall(r"\b(?:FILE|PRODUCT)VERSION\s+(\d+),(\d+),(\d+),\d+", resource)
    string_versions = re.findall(r'VALUE\s+"(?:File|Product)Version",\s*"(\d+\.\d+\.\d+)(?:\.\d+)?"', resource)
    if len(numeric_versions) != 2 or len(string_versions) != 2:
        fail("could not resolve FILEVERSION/PRODUCTVERSION declarations in Resource/Resource.rc")
    resource_values = {".".join(parts) for parts in numeric_versions} | set(string_versions)

    values = {runtime.group(1), header.group(1), definition.group(1)} | resource_values
    if len(values) != 1:
        fail(
            "version declarations disagree: "
            f"runtime={runtime.group(1)} header={header.group(1)} definitions={definition.group(1)} "
            f"resource={sorted(resource_values)}"
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

    required_anchor_names = {anchor.get("name") for anchor in runtime_anchors}
    for required_name in (
        "Overlay pause wrapper",
        "Overlay game shell wrapper",
        "Lua dummynode",
        "Wingman Hunt activation",
    ):
        if required_name not in required_anchor_names:
            fail(f"runtime BZR build profile is missing required anchor: {required_name}")

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
        '"Overlay pause wrapper"',
        '"Overlay game shell wrapper"',
        '"Wingman Hunt activation"',
        '"Lua dummynode"',
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
        ("runtime gate recorded in Init", "RuntimeGate::SetSupported(supportedBuild)" in read("src/luaexport.cpp")),
        ("Scanner writes gated", "RuntimeGate::IsSupported()" in scanner),
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
    luaexport = read("src/luaexport.cpp")
    lua_check = luaexport.find("BuildValidation::IsLuaCoreCompatible()")
    register = luaexport.find('luaL_register(L, "exu"')
    if lua_check < 0 or register < 0 or lua_check > register:
        fail("luaopen_exu must check the Lua dummynode anchor before luaL_register creates any table")

    for token in forbidden_loader_calls:
        if token in dllmain:
            fail(f"DllMain still performs nontrivial loader-lock work: {token}")

    print("Hardening markers OK")


# Patches whose address comes from a signature scan that already verifies the
# bytes before the object is constructed.
SIGNATURE_RESOLVED_PATCHES: set[str] = set()

PATCH_DECLARATION_RE = re.compile(r"^[ \t]*(?:inline[ \t]+)?(?:Hook|InlinePatch)[ \t]+(\w+)\((.*?)\);", re.M | re.S)


def check_patch_preimages() -> None:
    """Every namespace-scope patch at a fixed address must carry the stock
    bytes it expects, so it fails closed on a different build or when another
    module already owns the site."""
    missing = []
    count = 0
    for path in sorted((ROOT / "src").rglob("*.[ch]*")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in PATCH_DECLARATION_RE.finditer(text):
            name, arguments = match.group(1), match.group(2)
            if name in SIGNATURE_RESOLVED_PATCHES:
                continue
            count += 1
            # The expected-byte vector is the last brace-enclosed argument.
            if not re.search(r"\{\s*0x[0-9A-Fa-f]{2}(?:\s*,\s*0x[0-9A-Fa-f]{2})*\s*\}\s*$", arguments.strip()):
                missing.append(f"{path.relative_to(ROOT).as_posix()}: {name}")
    if missing:
        print("Fixed-address patches without expected bytes:")
        for entry in missing:
            print(f"  - {entry}")
        raise SystemExit(1)
    print(f"Patch preimages OK: {count} fixed-address patches carry expected bytes")


def main() -> None:
    check_api_parity()
    check_versions()
    check_address_catalog()
    check_hardening_markers()
    check_patch_preimages()
    print("All EXU hardening validation checks passed.")


if __name__ == "__main__":
    main()
