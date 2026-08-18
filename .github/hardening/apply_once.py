#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one occurrence, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


def matching_brace(text: str, opening: int) -> int:
    depth = 0
    i = opening
    quote = None
    line_comment = False
    block_comment = False
    while i < len(text):
        c = text[i]
        n = text[i + 1] if i + 1 < len(text) else ""
        if line_comment:
            if c == "\n":
                line_comment = False
            i += 1
            continue
        if block_comment:
            if c == "*" and n == "/":
                block_comment = False
                i += 2
                continue
            i += 1
            continue
        if quote:
            if c == "\\":
                i += 2
                continue
            if c == quote:
                quote = None
            i += 1
            continue
        if c == "/" and n == "/":
            line_comment = True
            i += 2
            continue
        if c == "/" and n == "*":
            block_comment = True
            i += 2
            continue
        if c in ('"', "'"):
            quote = c
            i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise RuntimeError("unmatched brace")


def replace_function_body(path: str, marker: str, body: str) -> None:
    text = read(path)
    start = text.find(marker)
    if start < 0:
        raise RuntimeError(f"{path}: function marker not found: {marker}")
    opening = text.find("{", start)
    if opening < 0:
        raise RuntimeError(f"{path}: opening brace not found: {marker}")
    closing = matching_brace(text, opening)
    write(path, text[:opening] + body + text[closing + 1 :])


def insert_after_function(path: str, marker: str, addition: str) -> None:
    text = read(path)
    start = text.find(marker)
    if start < 0:
        raise RuntimeError(f"{path}: function marker not found: {marker}")
    opening = text.find("{", start)
    closing = matching_brace(text, opening)
    write(path, text[: closing + 1] + addition + text[closing + 1 :])


# 1. Finish the inherited Lua stack-balance fix for matrices.
replace_function_body(
    "src/LuaHelpers.h",
    "\tinline BZR::MAT_3D CheckMatrix(lua_State* L, int idx)",
    r'''{
		BZR::MAT_3D mat{};
		if (!lua_isuserdata(L, idx))
		{
			luaL_typerror(L, idx, "matrix");
			return mat;
		}

		const int valueIndex = AbsoluteStackIndex(L, idx);
		auto readField = [L, valueIndex](const char* name)
		{
			lua_getfield(L, valueIndex, name);
			const float value = static_cast<float>(luaL_checknumber(L, -1));
			lua_pop(L, 1);
			return value;
		};

		mat.right_x = readField("right_x");
		mat.right_y = readField("right_y");
		mat.right_z = readField("right_z");
		mat.up_x = readField("up_x");
		mat.up_y = readField("up_y");
		mat.up_z = readField("up_z");
		mat.front_x = readField("front_x");
		mat.front_y = readField("front_y");
		mat.front_z = readField("front_z");
		mat.posit_x = readField("posit_x");
		mat.posit_y = readField("posit_y");
		mat.posit_z = readField("posit_z");
		return mat;
	}''',
)

# 2. Route existing signature scanners through the shared resolver.
replace_once(
    "src/Game/CommandReplacement.cpp",
    '#include "Util/Logging.h"\n',
    '#include "Util/Logging.h"\n#include "Util/SignatureResolver.h"\n',
)
replace_function_body(
    "src/Game/CommandReplacement.cpp",
    "\t\tconst uint8_t* FindPattern(const uint8_t* start, size_t size, const auto& pattern)",
    r'''{
			return SignatureResolver::FindPattern(start, size, pattern);
		}''',
)
insert_after_function(
    "src/Game/CommandReplacement.cpp",
    "\tvoid ResetState(lua_State* L)",
    r'''

	void ReleaseState(lua_State* L)
	{
		if (L == nullptr || g_ownerState != L)
		{
			return;
		}

		for (auto& [_, entry] : g_replacements)
		{
			ReleaseEntry(L, entry);
		}
		g_replacements.clear();
		g_ownerState = nullptr;
		g_lastUpdateAt = -1.0;
		RestoreStockHuntLabel();
		Logging::LogMessage("exu: command replacement registry released");
	}''',
)

replace_once(
    "src/UI/Overlay.cpp",
    '#include "Util/Logging.h"\n',
    '#include "Util/Logging.h"\n#include "Util/SignatureResolver.h"\n',
)
replace_function_body(
    "src/UI/Overlay.cpp",
    "\t\tbool IsReadableRange(const void* address, size_t length) noexcept",
    r'''{
			return SignatureResolver::IsReadableRange(address, length);
		}''',
)
replace_function_body(
    "src/UI/Overlay.cpp",
    "\t\tbool TryGetMainModuleTextSection(const uint8_t*& outData, size_t& outSize, uintptr_t& outAddress)",
    r'''{
			return SignatureResolver::TryGetModuleTextSection(
				GetModuleHandleA(nullptr), outData, outSize, outAddress);
		}''',
)
replace_function_body(
    "src/UI/Overlay.cpp",
    "\t\tuintptr_t FindMaskedPattern(",
    r'''{
			return SignatureResolver::FindMaskedPattern(
				data, dataSize, baseAddress, pattern, mask, patternSize);
		}''',
)
replace_function_body(
    "src/UI/Overlay.cpp",
    "\t\tbool MatchBytes(uintptr_t address, const std::array<uint8_t, N>& bytes) noexcept",
    r'''{
			return SignatureResolver::MatchBytes(address, bytes);
		}''',
)

# 3. Release Lua-owned registry references while the original VM is alive.
replace_once(
    "src/luaexport.cpp",
    "\t}\n\n\tvoid MakeEnums(lua_State* L, int exuIdx)\n",
    r'''	}

	void ReleaseLuaStateBindings(lua_State* L) noexcept
	{
		if (L == nullptr)
		{
			return;
		}

		ResetSanitizedStockStringPatchState(L);
		ResetObjectiveObjectPatchState(L);
		Logging::LogMessage("exu: released Lua-owned stock function bindings");
	}

	void MakeEnums(lua_State* L, int exuIdx)
''',
)

# 4. Keep editor metadata/version and runtime exports in lockstep. These exports
# already existed at runtime but had never been declared in the LuaLS definition
# file. Add conservative vararg declarations rather than silently leaving them
# invisible; their richer per-function annotations can be refined independently.
replace_once(
    "Definitions/ExtraUtils.lua",
    "--- This file provides the lua definitions for Extra Utilities version 1.0.0",
    "--- This file provides the lua definitions for Extra Utilities version 1.1.0",
)

missing_definition_exports = [
    "ClearAiUnitTuning",
    "ClearAllAiUnitTuning",
    "ClearVisuals",
    "DrawBox",
    "DrawLine",
    "GetAiTargetScoringEnabled",
    "GetAiTargetSelectEnabled",
    "GetAiUnitTuning",
    "GetCullDistance",
    "GetCullingEnabled",
    "GetHudSpriteRect",
    "GetInfiniteAmmo",
    "GetInfiniteScrap",
    "GetMusicTrack",
    "GetOrdnanceVelocMode",
    "GetViewportOverlaysEnabled",
    "GetWeaponMask",
    "GetWireframe",
    "PauseMusic",
    "ResetMissionHookOverrides",
    "ResetOverlaySupport",
    "RestoreAllHudSprites",
    "RestoreHudSprite",
    "ResumeMusic",
    "SetAiOdfGameplayTuningEnabled",
    "SetAiTargetScoringEnabled",
    "SetAiTargetSelectEnabled",
    "SetAiUnitTuning",
    "SetAttackRevealEnabled",
    "SetBomberAiRangeEnabled",
    "SetCullDistance",
    "SetCullingEnabled",
    "SetHowitzerVolleyEnabled",
    "SetHudSpriteRect",
    "SetHudSpriteVisible",
    "SetInfiniteAmmo",
    "SetInfiniteScrap",
    "SetJumpSnipeCrouch",
    "SetMusicTrack",
    "SetOrdnanceVelocMode",
    "SetTurretAimPitchEnabled",
    "SetUnderAttackAlertMode",
    "SetViewportOverlaysEnabled",
    "SetWeaponMaskCarrierBiasEnabled",
    "SetWireframe",
    "StopMusic",
]

definition_block = [
    "--- Runtime exports that predated their LuaLS declarations.",
    "--- These conservative declarations keep editor/runtime API parity exact.",
]
for name in missing_definition_exports:
    definition_block.extend([
        "--- @param ... any",
        f"function exu.{name}(...) end",
        "",
    ])
replace_once(
    "Definitions/ExtraUtils.lua",
    "\nreturn exu",
    "\n" + "\n".join(definition_block).rstrip() + "\n\nreturn exu",
)

# 5. Make PRs build-gated and run the hardening/API smoke checks in normal CI.
replace_once(
    ".github/workflows/release.yml",
    "  workflow_dispatch:       # Manual trigger (artifact upload only, no release)\n",
    "  pull_request:\n    branches: [main]\n  workflow_dispatch:       # Manual trigger (artifact upload only, no release)\n",
)
replace_once(
    ".github/workflows/release.yml",
    "      - name: Checkout code\n        uses: actions/checkout@v4\n",
    "      - name: Checkout code\n        uses: actions/checkout@v4\n\n      - name: Validate EXU API and hardening invariants\n        run: python tools/validate_hardening.py\n",
)
replace_once(
    ".github/workflows/release.yml",
    "      - name: Build Solution\n        run: msbuild ExtraUtilities.sln /p:Configuration=Release /p:Platform=x86\n",
    "      - name: Build Solution\n        run: msbuild ExtraUtilities.sln /p:Configuration=Release /p:Platform=x86\n\n      - name: Build hardening smoke tests\n        run: msbuild tests\\HardeningSmoke.vcxproj /p:Configuration=Release /p:Platform=Win32\n\n      - name: Run hardening smoke tests\n        run: tests\\bin\\Release\\HardeningSmoke.exe\n",
)

print("Applied EXU hardening integration edits.")
