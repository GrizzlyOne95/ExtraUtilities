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
"""Static validation for EXU API/documentation and hardening invariants."""

from __future__ import annotations

import hashlib
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
    ("exu.animation", "src/Game/AnimationApi.cpp", "Definitions/Animation.lua", "animation", "luaL_Reg functions[] = {"),
    ("exu.fps", "src/Game/AnimationApi.cpp", "Definitions/Fps.lua", "fps", "luaL_Reg fpsFunctions[] = {"),
    ("exu.storage", "src/Util/StorageApi.cpp", "Definitions/Storage.lua", "storage", "luaL_Reg functions[] = {"),
    ("exu.continuity", "src/Game/ContinuityApi.cpp", "Definitions/Continuity.lua", "continuity", "luaL_Reg functions[] = {"),
)


def check_api_parity() -> None:
    runtime = registered_names("src/luaexport.cpp", "const luaL_Reg exuExports[] = {")
    require_parity("exu", "Definitions/ExtraUtils.lua", runtime, documented_names("Definitions/ExtraUtils.lua", "exu"))
    total = len(runtime)

    for label, source, definitions_path, table, start_marker in SUB_API_TABLES:
        sub_runtime = registered_names(source, start_marker)
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


# Engine image range for the supported x86 build (image base 0x00400000).
ENGINE_ADDRESS_MIN = 0x00400000
ENGINE_ADDRESS_MAX = 0x02FFFFFF

# Literals in src/ that fall inside the engine range but are not engine
# addresses. Every entry needs a reason; keep this list short.
ENGINE_LITERAL_ALLOWLIST: dict[int, str] = {}

HEX_LITERAL_RE = re.compile(r"\b0[xX]([0-9A-Fa-f]+)[uUlL]*\b")
# Comments and string literals are blanked (newlines kept) before scanning.
COMMENT_OR_STRING_RE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"', re.S)
IDA_PATTERN_RE = re.compile(r"^(?:[0-9A-F]{2}|\?\??)(?: (?:[0-9A-F]{2}|\?\??))*$")


def catalog_address_entries(node: object, prefix: str = ""):
    if not isinstance(node, dict):
        return
    for key, value in node.items():
        if key.startswith("_") or not isinstance(value, dict):
            continue
        path = f"{prefix}.{key}" if prefix else key
        if "address" in value:
            yield path, value
        else:
            yield from catalog_address_entries(value, path)


# The only files in src/ that may spell engine addresses as literals: both are
# generated from exu.json (and the build profile) and checked in CI with
# --check, so the catalog stays the single source of truth.
GENERATED_ADDRESS_HEADERS = {
    "src/Util/EngineAddresses.generated.h",
    "src/Util/BzrBuildProfile.generated.h",
}

# Lua5.1-BZR is C and cannot include the generated C++ header; its one engine
# address must match the catalog instead.
LUA_DUMMYNODE_DEFINE_RE = re.compile(r"#define\s+dummynode\s+\(\(Node\s*\*\)\s*0[xX]([0-9A-Fa-f]+)\)")


def check_engine_address_census() -> None:
    """src/ may not spell an engine-range literal anywhere except the generated
    headers: feature code names addresses through EngineAddresses, which is
    generated from exu.json. Every catalogued signature must also be a
    well-formed IDA-style pattern."""
    catalog = json.loads(read("exu.json"))
    catalogued: set[int] = set()
    patterns = 0
    for path, entry in catalog_address_entries(catalog.get("addresses", {})):
        try:
            catalogued.add(int(entry["address"], 16))
        except (TypeError, ValueError):
            fail(f"exu.json entry {path} has an unparsable address: {entry.get('address')!r}")
        pattern = entry.get("pattern")
        if pattern is None:
            continue
        if not isinstance(pattern, str) or not IDA_PATTERN_RE.match(pattern):
            fail(f"exu.json entry {path} has a malformed IDA-style pattern: {pattern!r}")
        if all(token.startswith("?") for token in pattern.split()):
            fail(f"exu.json entry {path} has a pattern with no literal bytes")
        if entry.get("pattern_kind") not in ("code", "reference"):
            fail(f"exu.json entry {path} needs pattern_kind 'code' or 'reference'")
        patterns += 1

    for generated in GENERATED_ADDRESS_HEADERS:
        if not (ROOT / generated).is_file():
            fail(f"generated address header is missing: {generated}")

    raw: list[str] = []
    scanned = 0
    for path in sorted((ROOT / "src").rglob("*")):
        relative = path.relative_to(ROOT).as_posix()
        if path.suffix.lower() not in (".c", ".cpp", ".h", ".hpp") or relative in GENERATED_ADDRESS_HEADERS:
            continue
        scanned += 1
        source = path.read_text(encoding="utf-8", errors="replace")
        code = COMMENT_OR_STRING_RE.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), source)
        for match in HEX_LITERAL_RE.finditer(code):
            value = int(match.group(1), 16)
            if not ENGINE_ADDRESS_MIN <= value <= ENGINE_ADDRESS_MAX or value in ENGINE_LITERAL_ALLOWLIST:
                continue
            line = code.count("\n", 0, match.start()) + 1
            if value in catalogued:
                hint = "use its EngineAddresses name"
            else:
                hint = "catalogue it in exu.json, regenerate the header, and use its EngineAddresses name"
            raw.append(f"{relative}:{line}: 0x{value:08X} ({hint})")

    if raw:
        print(
            "Engine-range literals in src/ outside the generated headers (addresses come from exu.json through "
            "src/Util/EngineAddresses.generated.h; allowlist a non-address constant with a reason):"
        )
        for entry in raw:
            print(f"  - {entry}")
        raise SystemExit(1)

    ltable = read("Lua5.1-BZR/src/ltable.c")
    define = LUA_DUMMYNODE_DEFINE_RE.search(ltable)
    dummynode = resolve_catalog_path(catalog, "Lua.dummynode")
    if not define or int(define.group(1), 16) != int(dummynode["address"], 16):
        fail("Lua5.1-BZR/src/ltable.c dummynode must match exu.json Lua.dummynode")

    print(
        f"Engine address census OK: no engine-range literals in {scanned} src/ files outside the generated headers; "
        f"ltable.c dummynode matches the catalog; {patterns} catalog patterns well-formed"
    )


def check_hardening_markers() -> None:
    hook = read("src/Hook.h")
    entry_detour = read("src/EntryDetour32.h")
    scanner = read("src/Scanner.h")
    basic = read("src/BasicPatch.h")
    dllmain = read("src/dllmain.cpp")
    lua_state = read("src/LuaState.h")
    add_scrap = read("src/Patches/AddScrapCallback.cpp")
    pilot_policy_h = read("src/Game/PilotAnimationPolicy.h")
    pilot_policy_cpp = read("src/Game/PilotAnimationPolicy.cpp")
    pilot_profile_h = read("src/Game/PilotAnimationProfile.h")
    pilot_timing_h = read("src/Game/PilotTransitionTiming.h")
    # The pilot animation policy owns "what should happen" and must stay pure
    # data: any engine, patch, Ogre, or Lua access belongs to the seam, not here.
    # The table-override math is held to the same rule.
    pilot_policy_includes = re.findall(
        r'^\s*#include\s+[<"]([^>"]+)[>"]', pilot_policy_h + pilot_policy_cpp + pilot_profile_h + pilot_timing_h, re.M
    )
    pilot_policy_impure = [
        inc for inc in pilot_policy_includes
        if inc.startswith(("Ogre/", "Patches/", "Util/")) or inc in ("bzr.h", "BasicPatch.h", "Hook.h", "Windows.h", "lua.hpp")
    ]
    # The timing trace is written from inside Person::Simulate, so it must
    # stay engine-free and allocation-free, and it must not survive into the
    # next Lua state.
    pilot_trace_h = read("src/Game/PilotTrace.h")
    pilot_trace_includes = re.findall(r'^\s*#include\s+[<"]([^>"]+)[>"]', pilot_trace_h, re.M)
    pilot_trace_impure = [
        inc for inc in pilot_trace_includes
        if not inc.startswith(("atomic", "cmath", "cstddef", "cstdint"))
    ]
    pilot_intercept = read("src/Game/PilotFsmIntercept.cpp")

    required = [
        ("Hook move deletion", "Hook(Hook&&) = delete;" in hook),
        ("entry detour move deletion", "EntryDetour32(EntryDetour32&&) = delete;" in entry_detour),
        ("entry detour mandatory preimage", "entry detour requires a complete expected-byte preimage" in entry_detour),
        ("entry detour RX trampoline", "PAGE_EXECUTE_READ" in entry_detour),
        ("entry detour absolute transfer", "target[0] = 0x68;" in entry_detour and "target[5] = 0xC3;" in entry_detour),
        ("Scanner move deletion", "Scanner(Scanner&&) = delete;" in scanner),
        ("instruction cache flush", "FlushInstructionCache" in basic),
        ("expected-byte validation", "expected bytes do not match" in basic),
        ("runtime BZR build gate", "BuildValidation::IsSupportedBzr2301()" in basic),
        ("runtime gate recorded in Init", "RuntimeGate::SetSupported(supportedBuild)" in read("src/luaexport.cpp")),
        ("Scanner writes gated", "RuntimeGate::IsSupported()" in scanner),
        ("deferred requested status", "m_requestedStatus = s;" in basic),
        ("Lua-state generation", "m_generation" in lua_state),
        ("protected AddScrap call", "lua_pcall(L, 2, 1, 0)" in add_scrap),
        ("pilot animation policy defaults to stock", "static_assert(IsStockOnly(Policy{})" in pilot_policy_h),
        ("pilot animation policy stays engine-free", not pilot_policy_impure),
        (
            "pilot animation policy reset at Lua-state init and mission reset",
            "PilotAnimationPolicy::ResetMissionState();" in read("src/luaexport.cpp")
            and "PilotAnimationPolicy::ResetMissionState();" in read("src/PublicAPI.cpp"),
        ),
        ("pilot timing trace stays engine-free", not pilot_trace_impure),
        # Overrides are applicable only through kBuildSupport, and the Lua
        # capability must follow that constant (not a literal) AND the seam's
        # install-time table qualification.
        ("pilot override support declared in one place", "constexpr Support kBuildSupport{" in pilot_policy_h),
        (
            "pilot override capability follows kBuildSupport",
            "HasOverrideSupport(PilotAnimationPolicy::kBuildSupport)" in read("src/Game/AnimationApi.cpp"),
        ),
        (
            "pilot override capability requires qualified clip tables",
            "PilotFsmIntercept::AreOverridesAvailable()" in read("src/Game/AnimationApi.cpp")
            and "g_tablesQualified.store(QualifyTables()" in pilot_intercept,
        ),
        # The clip tables are global to every Person: the local call's writes
        # are undone straight after the stock call, with nothing in between.
        (
            "pilot clip tables restored after the stock call",
            re.search(
                r"original\(person, dt\);\s*if \(plan\.write\)\s*\{\s*if \(!WriteTablesSeh\(plan\.stock\)\)",
                pilot_intercept,
            ) is not None,
        ),
        ("pilot overrides stand down in multiplayer", "IsNetGameSeh()" in pilot_intercept),
        ("pilot policy refuses unsupported policies", "if (!IsSupported(policy, kBuildSupport))" in pilot_policy_cpp),
        (
            "pilot timing trace reset with the seam stats",
            re.search(r"void ResetStats\(\) noexcept\s*\{[^}]*g_trace\.Reset\(\);", pilot_intercept) is not None
            and "PilotFsmIntercept::ResetStats();" in read("src/luaexport.cpp"),
        ),
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
    # RegisterFunctions (LuaCppBarrier.h) is luaL_register behind the C++
    # exception barrier; it creates the exu table the same way.
    register = luaexport.find('RegisterFunctions(L, "exu"')
    if lua_check < 0 or register < 0 or lua_check > register:
        fail("luaopen_exu must check the Lua dummynode anchor before RegisterFunctions creates any table")

    # Static initializers run inside DllMain on every mission load. Signature
    # scans and resolution belong in Init.
    static_init = re.compile(r"^[ \t]*inline[ \t]+[\w:<>\*& \t]+?[ \t]+\w+[ \t]*=[ \t]*(?:Initialize|Resolve)\w*\(", re.M)
    for path in sorted((ROOT / "src").rglob("*.[ch]*")):
        for match in static_init.finditer(path.read_text(encoding="utf-8", errors="replace")):
            fail(f"namespace-scope initializer runs resolution under the loader lock: {path.relative_to(ROOT).as_posix()}: {match.group(0).strip()}")

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


# The two BZR policy documents are shared byte-for-byte by EXU, OpenShim,
# Campaign Reimagined and bzfile. Each repository pins the same hashes, so an
# edit in one repository fails here until the document and these hashes are
# updated in all four. Line endings are normalised so Windows and Linux
# checkouts agree.
SHARED_BZR_DOCS = {
    "Docs/BZR_LUA_AGENT_REFERENCE.md": "95a146ae81c94a84c2b4c0767f7e2a6ab4aa1ee40148e2414a72733b13ac2fd1",
    "Docs/BZR_PLATFORM_COMPATIBILITY.md": "b9af9f6452996080a046949f3164e102ec8aa4eefaa9d0c4b8d194e52b516fb3",
}


def check_shared_bzr_docs() -> None:
    for path, expected in SHARED_BZR_DOCS.items():
        body = (ROOT / path).read_bytes().replace(b"\r\n", b"\n")
        actual = hashlib.sha256(body).hexdigest()
        if actual != expected:
            fail(
                f"{path} no longer matches the shared copy (sha256 {actual}). Make the same change in "
                "ExtraUtilities, BZR-OpenShim, Campaign Reimagined and bzfile, then update SHARED_BZR_DOCS "
                "in each repository's check."
            )
    print(f"Shared BZR docs OK: {len(SHARED_BZR_DOCS)} documents match the pinned shared copies")


BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)
EXCEPT_RE = re.compile(r"__except\s*\(")
LUA_PUSH_RE = re.compile(r"\b(lua_pushcfunction|lua_pushcclosure|lua_cpcall)\s*\(\s*L\s*,\s*([^,)]+)")


def check_exception_barriers() -> None:
    """P1-15: every __except uses the shared filter (C++ exceptions and stack
    overflow pass through), and every function handed to Lua runs behind the
    C++ exception barrier."""
    excepts = 0
    pushes = 0
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        rel = path.relative_to(ROOT).as_posix()
        source = LINE_COMMENT_RE.sub("", BLOCK_COMMENT_RE.sub("", path.read_text(encoding="utf-8")))
        for match in EXCEPT_RE.finditer(source):
            excepts += 1
            filter_text = source[match.end():match.end() + 80]
            if not re.match(r"\s*(?:ExtraUtilities::)?(?:Seh::)?Filter\(", filter_text):
                line = source.count("\n", 0, match.start()) + 1
                fail(f"{rel}:{line}: __except must use Seh::Filter (Util/SehGuard.h)")
        if rel != "src/LuaCppBarrier.h" and re.search(r"\bluaL_(register|openlib)\s*\(", source):
            fail(f"{rel}: register Lua functions with RegisterFunctions (LuaCppBarrier.h), not luaL_register")
        for match in LUA_PUSH_RE.finditer(source):
            if rel == "src/LuaCppBarrier.h":
                continue
            pushes += 1
            target = match.group(2).strip()
            if not re.match(r"&(?:Lua::)?CppBarrier<", target):
                line = source.count("\n", 0, match.start()) + 1
                fail(f"{rel}:{line}: {match.group(1)} must pass a CppBarrier<> function, not {target}")
    print(f"Exception barriers OK: {excepts} __except filters, {pushes} individually pushed Lua functions")


def main() -> None:
    check_api_parity()
    check_shared_bzr_docs()
    check_versions()
    check_address_catalog()
    check_engine_address_census()
    check_hardening_markers()
    check_patch_preimages()
    check_exception_barriers()
    print("All EXU hardening validation checks passed.")


if __name__ == "__main__":
    main()
