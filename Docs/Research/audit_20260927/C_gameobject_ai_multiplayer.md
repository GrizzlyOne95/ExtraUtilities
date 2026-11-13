# Worksheet C: GameObject, AI, command replacement, animation, ordnance, multiplayer (2026-09-27)

Reviewer scope (actual line counts at aec8c0a; the task brief's counts were stale):
`src/Game/GameObject.cpp` 5496, `src/Game/GameObject.h` 128, `src/Game/CommandReplacement.cpp` 1021,
`src/Game/CommandReplacement.h` 38, `Docs/Research/COMMAND_REPLACEMENT.md` 97, `src/Game/AnimationApi.h` 520,
`Docs/ANIMATION_API.md` 114, `Definitions/Animation.lua` 111, `src/Game/Ordnance.cpp/.h` 153/51,
`src/Game/Multiplayer.cpp/.h` 101/49, `src/Game/game_state.cpp/.h` 145/34, `src/Game/Steam.cpp/.h` 30/30,
`src/bzr.h` 564 (usage only). Base: origin/main aec8c0a. All files read end to end.

Method notes: callers grepped across `src/`, `include/`, `tests/`, `Definitions/` and the `exuExports[]`
table (`src/luaexport.cpp:547-942`). Engine behaviour claims marked "disasm" were checked with capstone
against the installed GOG `battlezone98redux.exe` (2.2.301). The checked sites were:

- the engine's handle lookup `0x004DA060`;
- `GameObject::GetHandle` `0x00462380`;
- `UpdateLives` `0x006260F0`;
- the Wingman Hunt function `0x006136A0-0x006136E8`;
- the MP patch sites `0x005C8320-0x005C835C` and `0x0056F00C-0x0056F027`;
- the engine `LuaCheckStatus` `0x004FF600`.

Nothing was built or run.

## 1. Section map

### `src/Game/GameObject.cpp` (5496 lines)

This is a god file. It holds GameObject memory access, the Ogre material/entity/light/animation wrappers, terrain TRN parsing and the AI RTTI probes.

| Lines | Feature | Status |
|---|---|---|
| 1-44 | includes | - |
| 49-101 | MSVC RTTI descriptor structs; material pass/texture-unit handle structs | production (support) |
| 103-143 | `PolymorphicObjectInfo`, `ScannedFieldInfo`, terrain slot binding structs | diagnostic (support) |
| 145-241 | packed engine layouts: `CarrierWeaponSelectionLayout`, `UnitTaskLayout`, `RecycleTaskLayout`, `ScavengerProcessLayout` (no `static_assert(offsetof)`) | experimental |
| 243-262 | GameObject field offsets (carrier 0x1A0, mode list 0x1A4/0x1D0/0x1D4, weapon mask 0x210 xor 0x33333333), terrain slot table | production |
| 264-309 | `g_cachedMaterials` (process-lifetime `unordered_map<string, SharedPtr<Material>>`), `LogMaterialDebug` (fopen/fclose per line) | production (see C-8) |
| 311-575 | RTTI walking: `TryCopyReadableCString`, `NormalizeMsvcTypeName`, `TryGetPolymorphicMetadata`, base-class/hierarchy readers (SEH) | diagnostic |
| 577-701 | string/path utilities; `TryCallLuaStringFunction` (pcall `GetMapTRNFilename`) | production |
| 703-906 | terrain TRN discovery (`TryFindFileRecursive`, `TryResolveTerrainDefinitionPath`) and `[Atlases] MaterialName` parser | production |
| 908-977 | Lua table field readers for terrain (raise `luaL_error`) | production |
| 979-1108 | raw memory probes: `TryReadPointerField`, `TryReadUInt32Field`, `IsReadablePointer` (VirtualQuery), `TryResolveHandleValue` (the only place a handle is validated, via `GetHandle(obj)==h`), `TryGetHandleFromObject` | diagnostic |
| 1110-1181 | weapon-selection / mode-list readers (SEH) | production |
| 1183-1232 | `TryGetClassLabelFromLua` (pcall stock `GetClassLabel`), `GetScanBytesArgument` | production |
| 1234-1422 | AI task candidate scoring, push helpers, optional table-field readers | experimental |
| 1424-1644 | `ScanPolymorphicChildren`, `ScanAlignedFields`, push-array helpers | diagnostic |
| 1646-2118 | Ogre material C++ `try/catch` wrappers (resolve/clone/pass/texture-unit/colour) | production |
| 2120-2637 | Ogre entity/render/animation-state SEH-only wrappers | production |
| 2638-2831 | `GetRenderableEntity`/`GetLightObject` (GameObject+0xF0 -> +0x94/+0xA8), light SEH wrappers | production |
| 2833-3121 | SEH shells over the C++ material wrappers, tint-pass collector, texture-unit setters | production |
| 3123-3299 | colour conversion, pass colour SEH shells, light colour/spot wrappers | production |
| 3301-3392 | `TrySetAsUser`, comm-tower power read (inline asm `[obj+0x238]`), sub-entity index helpers | production |
| 3395-3439 | exported animation bridge used by `AnimationApi.h` | production |
| 3441-3480 | bindings `SetAsUser`, `IsCommTowerPowered`, `GetHandle` | production |
| 3482-3648 | sub-entity/material name bindings, `MaterialExists`, `CloneMaterial` | production |
| 3650-4078 | material pass colours, texture/scroll/rotate bindings, `GetTerrainMaterialName`, `SetTerrainTextureSet`, `SetMaterialPassColors` | production |
| 4080-4313 | entity visible/shadows/distance/visibility/query/render-queue bindings | production |
| 4315-4529 | headlight and generic light bindings | production |
| 4531-4667 | low-level entity animation bindings | production |
| 4669-4714 | `GetMass`/`SetMass`, `GetObj` | production |
| 4716-4876 | construction-rig selection, weapon mask/selection info | production |
| 4878-5413 | AI process/task inspection (`GetAiProcess*`, `GetAiTask*`, `GetAiRecycleTaskState`) and `SetAiTaskState` | diagnostic (getters) / experimental (`SetAiTaskState`) |
| 5416-5495 | radar period/range, velocity jammer | production |

No turret-pitch or attack-reveal code lives in this file at aec8c0a. Grep puts those features in
`src/Patches/UnitVo.cpp` and `src/luaexport.cpp`.

Candidate split boundaries (no behaviour change):
1. `src/Game/GameObjectHandle.{h,cpp}`: a single `TryGetLiveObject(handle)` (see C-1) plus lines 979-1108.
2. `src/Game/AiInspection.cpp`: lines 49-128, 145-241, 311-575, 1183-1644 and 4878-5413. This is about 1,500 lines of RTTI/scan probes.
3. `src/Ogre/MaterialApi.cpp`, in the Ogre runtime layer per ARCHITECTURE.md: lines 264-309, 1646-2118, 2833-3257 and 3482-4078. This is about 2,000 lines.
4. `src/Game/TerrainTrn.cpp`: lines 577-977. This is filesystem work and belongs under the platform-compatibility rules.
5. `src/Game/EntityRenderApi.cpp`: lines 2120-2831, 3259-3299 and 4080-4667.
6. What remains in `GameObject.cpp`: mass, radar, jammer, SetAsUser, comm tower and weapon selection. That is under 600 lines.

### `src/Game/CommandReplacement.cpp` (1021 lines)

| Lines | Feature | Status |
|---|---|---|
| 46-135 | command ids, 44-byte Wingman Hunt signature (duplicated in `profiles/bzr_2.2.301.json` anchor), process-lifetime globals | production |
| 137-213 | executable section enumeration, pattern find, rel32 resolve | production |
| 215-267 | stock command name normalisation, registry key/lookup, `ReleaseEntry` (`luaL_unref`) | production |
| 269-439 | Hunt label pointer: `.rdata` string scan, `.data` 16-byte pointer-sequence scan, raw `VirtualProtect` + pointer write | experimental |
| 441-557 | pcall wrappers for `GetCurrentCommand`, `IsSelected`, `SetCommand`, `GetTime` | production |
| 559-640 | native hook callees and naked thunk `WingmanHuntActivationHook` | production |
| 642-696 | static-init signature resolution and `std::unique_ptr<Hook>` (10 bytes at `0x006136AD` on GOG) | production |
| 698-770 | label override update, `ResetState`, `ReleaseState` | production |
| 772-818 | `DispatchRegisteredReplacement` (pcall) | production |
| 820-1020 | Lua bindings incl. `UpdateCommandReplacements` polling fallback | production / fallback |

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| C-1 | [High/Med] | `src/bzr.h:245-248`, `src/LuaHelpers.h:196-203`; every `GetObj(h)` binding in `GameObject.cpp`, `Ordnance.cpp:52` | **No handle aliveness check anywhere.** `CheckHandle` accepts any userdata (light or full). `GameObject::GetObj` is pure arithmetic: `base + (h>>20)*0x400`. The engine's own lookup `0x004DA060` also rejects `h==0` and requires `[obj+0x15C] == (h & 0xFFFFF)`; EXU skips both checks. The arena is 0x1000 static slots, so every handle maps to mapped memory and SEH never fires. A stale handle therefore either aliases whichever object now owns the slot, or reads a dead slot whose embedded pointers are dangling (scanner 0x198, jammer 0x19C, carrier 0x1A0, `aiProcess` 0xFC, entity via 0xF0). | Setters write through dangling pointers into freed heap: `SetRadarRange/Period` 5433-5439/5461-5466, `SetVelocJam` 5489-5492, `SetAiTaskState` 5238-5279, and the entity/light/material/animation setters via `GetRenderableEntity`/`GetLightObject` 2666-2722. After slot reuse they mutate the wrong unit: `SetMass` 4695-4700, and `SetAsUser` 3441-3447, which calls engine `SetAsUser` on a dead or foreign object. Mission scripts routinely hold handles of units that die. Stock `IsValid` exists because "a non-nil handle does not imply the underlying object still exists" (`Docs/BZR_LUA_AGENT_REFERENCE.md:247`). | Add `TryGetLiveObject(h)` = `h != 0 && GetObj(h)->[0x15C] == (h & 0xFFFFF)`. Catalog `GameObject::GetObjByHandle 0x004DA060` and the serial offset in `exu.json`. Use the helper in every handle binding and return nil/false for dead handles. `IsCommTowerPowered` and `GetConstructionRigSelectionInfo` are already implicitly validated, because they call stock `GetClassLabel` first. | Read all 60 handle bindings. Grepped `GetObj(` (87 hits in this file, 6 elsewhere). Disasm of `0x004DA060` shows the compare at `+0x15C`; `0x00462380` returns 0 when `+0x15C` is 0. Not verified: whether the engine nulls member pointers on destroy. That is why the freed-heap case is Conf Med; the wrong-object case after slot reuse is certain. |
| C-2 | [High/Med] | `GameObject.cpp:5230-5279`, `158-194`, `196-219`, `1306-1354` | **`SetAiTaskState` type confusion.** `TryFindBestAiTask` (no preferred type) picks the highest-scoring RTTI child whose name contains "Task" or "Attack". The result is always cast to `UnitTaskLayout` and written at +0x44..+0xAC, with no SEH and no type check. EXU's own `RecycleTaskLayout` puts `curState/nextState` at +0x50/+0x54 and ends near +0x74. So on a RecycleTask, `gotoDir` lands on the recycle state machine, and `omegaScale`/`pitch` (+0x6C/+0xAC) write past the object. Values are not range-checked: `luaL_checknumber` accepts NaN, inf and negatives. Fields are written one at a time while later fields can still raise (`TryGetOptionalNumberField` -> `luaL_checknumber`, `TryGetOptionalVectorField` -> `CheckVectorOrSingles`), so one bad field leaves a half-applied task. | Heap corruption or an invalid engine AI state for scavengers and any unit whose primary task is not a `UnitTask`. NaN steering factors propagate into physics. In MP only the local copy of the task is written, so on a remote-owned unit this is a silent local divergence. | Require `TypeMatches(taskInfo, "UnitTask")` (hierarchy) before writing. Add `static_assert(offsetof(UnitTaskLayout, pitch) == ...)`. Parse and validate every field (finite, sane ranges) into locals first, then write under SEH. The layouts also need re-deriving: pad member names disagree with computed offsets by +8 in `UnitTaskLayout` (`pad_21` sits at 0x19, `pad_94` at 0x8C) and by -4/+4 in `RecycleTaskLayout`. | Computed both layouts by hand. Grep finds no layout docs or tests: `braccel`/`UnitTaskLayout` appear only here and in `Definitions/ExtraUtils.lua:1847-1852`. The claim that RecycleTask does not derive from UnitTask comes from EXU's own two layouts, not from the binary. |
| C-3 | [High/High] | `Multiplayer.cpp:27-55`, `Multiplayer.h:31-32` | **An error leaves a temporary patch installed.** `BuildAsyncObject`/`BuildSyncObject` call `Reload()` on an `InlinePatch`, run stock `BuildObject` with an unprotected `lua_call`, then call `Unload()`. Any error in `BuildObject` longjmps past `Unload()`: a bad argument type, an unknown ODF, or `BuildObject` shadowed or nil. The NOP patch at `0x005C833D` (11 bytes) or `0x005C833B` (2 bytes) then stays active until `HandleLuaStateClosing` -> `UnloadAllPatches`. | Every later `BuildObject` in the mission is forced async (local-only) or forced sync. In MP that creates objects other peers never see, so one script error causes a desync. This is exactly the temporary-patch-lifetime hazard. | Use `lua_pcall(L, argC, 1, 0)`, then `Unload()`, then re-raise with `lua_error(L)` on error. | Read. Disasm confirms the patched bytes are `je` and `mov ecx,[ebp-4]; add ecx,0x18; call 0x4B8460`, and that both patch sites fall on instruction boundaries. |
| C-4 | [Med/High] | `Ordnance.cpp:28-60`, `62-105` | **Raw `Ordnance*` round-trips through Lua.** `BuildOrdnance` returns the engine pointer as lightuserdata. `GetOrdnanceAttribute` accepts any userdata, including a Handle. It then dereferences `ord->ordnanceClass->odf`, `ord->obj->transform` and `ord->owner->owner`, and calls engine `GetHandle` on the last one, with no SEH and no liveness check. `BuildOrdnance` also passes `GetObj(owner)->obj` (unvalidated, see C-1) to engine `OrdnanceClass::Build 0x00586FF0`. | Ordnance lives for seconds. Reading after it expires is a use-after-free read, and passing the wrong userdata is an AV that terminates the game. The Definitions comment warns authors (`ExtraUtils.lua:2011-2020`), but the API gives them no way to check. | At minimum, wrap the reads in SEH. Better: return an opaque token (pointer + `initTime` + `ownerHandle`) and validate the triple before dereferencing. | Read. Grepped `exuExports`: only `BuildOrdnance` and `GetOrdnanceAttribute` are exported. |
| C-5 | [Med/High] | `Ordnance.cpp:32-42` | **The ordnance class map is a function-local static built once per process.** It snapshots the engine class vector on the first call and is never rebuilt. Ordnance classes first loaded in a later mission are therefore "not found". If the engine frees classes between missions, the cached `OrdnanceClass*` values dangle and `Build` is called on freed memory. Separately, `strlen(ord->odf) - 4` underflows for a short `odf`; the resulting `std::length_error` escapes a `lua_CFunction` and terminates the game. | Failures that depend on mission order; a potential call through a stale pointer. | Build the map per Lua state (clear it in `HandleLuaStateClosing`) or look up on a miss. Guard the length. | Read. `VectorSpider` reads the begin/end pair at `0x009C915C`. Not verified: whether classes survive a mission change. |
| C-6 | [Med/High] | `GameObject.cpp:3441-3480`, `Multiplayer.cpp:63-76`, `Ordnance.cpp:55,87`, `CommandReplacement.cpp:454` (via `LuaHelpers.h:51`) | **Bindings call engine functions with no build gate.** Only patches go through `EnableDeferredPatchActivation` (`luaexport.cpp:485`); bindings are registered unconditionally (`luaexport.cpp:955`). These are called directly: `GetHandle 0x00462380`, `SetAsUser 0x004DB930`, `UpdateLives 0x006260F0`, `OrdnanceClass::Build 0x00586FF0`, and `LuaCheckStatus 0x004FF600` (not in `exu.json`). `exu.GetHandle(obj)` also hands an arbitrary Lua pointer to the engine with no SEH, so passing a Handle instead of a `GameObject*` is an immediate AV. | Fails open on any unqualified executable (ARCHITECTURE.md: "Failing closed is preferable"). A documented but easy misuse crashes the game. | Cache `BuildValidation::IsSupportedBzr2301()` once per Lua state. Every binding that calls or writes a fixed address should return nil/false when it is false. SEH-guard `GetHandle`. | Read. Grep for `IsSupportedBzr2301` finds only `BasicPatch.h:194` and `BuildValidation.h`. Disasm confirms the five addresses are the intended functions on GOG. |
| C-7 | [Med/Med] | `CommandReplacement.cpp:973-1017`, `698-724` | **The registry is iterated while Lua callbacks run.** `UpdateCommandReplacements` loops `for (auto& [key, entry] : g_replacements)`, calls `DispatchRegisteredReplacement` (the user callback) inside the loop, then writes `entry.lastObservedCommand`. A callback that calls `RemoveStockCmdReplacement` (erase) or `ReplaceStockCmd` (insert, possibly rehashing) invalidates both the iterator and `entry`. `UpdateHuntLabelOverride` has the same shape around the global `IsSelected`, which Lua can override. | Use-after-free write and iterator UB. The callback case is reached only on the polling fallback (native hook not resolved). The label case is reached on every tick if a mission overrides `IsSelected`. The native path re-finds the entry after dispatch (586) and is safe. | Snapshot keys into a `std::vector<uint64_t>` and re-`find` after every Lua call. | Read. Traced the erase in `RemoveStockCmdReplacement` 884-885. |
| C-8 | [Med/Med] | `GameObject.cpp:268, 288-309, 1654-1662, 2150-2164, 2166-2242` | **`g_cachedMaterials` pins Ogre materials for the whole process.** Every entity API call validates the entity via `IsRenderableEntityCandidate` -> `TryGetMaterialName`, which inserts that sub-entity's `SharedPtr<Material>` into the cache. `TryResolveMaterialCpp` and the set-material paths return the cached object before asking `MaterialManager`. Nothing ever clears or prunes the cache (grep: 4 hits, all in this file). | A material that Ogre removes or reloads across missions keeps being served from the cache: `MaterialExists` can return true for a removed material, and `SetMaterialName` can apply an orphaned one. The cache grows without bound. The `SharedPtr` releases run during exu.dll static destruction, which may be after Redux has shut Ogre down. | Clear the cache in `HandleLuaStateClosing`. Do not cache on read paths, or check `resourceExists` before serving a hit. | Read; grep. The exit-time crash was not reproduced. |
| C-9 | [Med/Med] | `Ordnance.h:32`, `Multiplayer.h:30,34`, `Scanner.h:141-151` | **Scanner writes outlive the mission.** `SetCoeffBallistic` (`0x008A2858`), `SetLives` and `SetShowScoreboard` write through process-lifetime `Scanner`s. Those restore only in their destructors at DLL unload; none is reset at `HandleLuaStateClosing`. | A mission's ballistic coefficient carries into the next mission, including MP sessions whose peers never set it, so trajectories desync. The Definitions note (`ExtraUtils.lua:2037-2043`) puts the burden on authors. | Restore Scanner-backed writes at Lua-state close, by registering them like patches. Ownership question: whether EXU should allow a global physics write in MP at all is MP policy, which belongs to OpenShim. | Read. Grepped for restore calls: there are none. |
| C-10 | [Low/Med] | `CommandReplacement.cpp:559-593, 772-803` | **The native hook uses the Lua API outside a protected call.** The hook runs from engine code with no Lua frame on the C stack. Before its `lua_pcall` it calls `lua_getglobal`, `lua_pushlightuserdata`, `lua_rawgeti` and three `lua_pushstring`s. Any of these can raise into `lua_atpanic` and exit the process: a memory error, a Lua 5.1 `__gc` finalizer error triggered by `luaC_checkGC` inside `lua_pushstring`, or a strict-mode `_G` metatable. The thunk's callees are neither `noexcept` nor wrapped in `try`. `FindStockCommand` allocates a `std::string`, and a C++ exception cannot unwind through the naked thunk. | Rare, but the failure mode is process exit with no log. | Run all the Lua work in one `lua_cpcall` trampoline. Make `TryHandleWingmanHuntActivation` `noexcept` with an internal `try/catch`. | Read. Disasm shows the hook fires from `Wingman::SetActiveMode`, not from Lua. |
| C-11 | [Low/High] | `GameObject.cpp` 3567-3620, 3622-3648, 3650-4078, 3449-3472, 5209-5295; `AnimationApi.h:299-479`; `Ordnance.cpp:44-51` | **C++ objects stay live across Lua calls that can raise (leaks only).** Affected objects: `std::string` material/texture/animation names, `std::vector` pass lists and terrain updates, and `PolymorphicObjectInfo`. The raising calls: `luaL_check*`, `luaL_argerror`, `CheckBool`, `CheckColorOrSingles`, `CheckVectorOrSingles`, `TryReadOptional*Field` (`luaL_error`) and `GetSubEntity` (`luaL_error`). `BuildOrdnance` also builds a `std::format` temporary and passes its `c_str()` to `luaL_argerror`. No RAII patch guard, lock or Ogre `SharedPtr` is involved. | A heap leak per failed call for strings longer than 15 characters, which material names usually are. | Parse all arguments into `const char*`/PODs first; construct `std::string`s after the last call that can raise. | Read every binding in scope. |
| C-12 | [Low/Med] | `GameObject.cpp:2120-2831, 3259-3333, 2166-2242` | SEH `__except(EXCEPTION_EXECUTE_HANDLER)` is the only guard around the Ogre entity/animation/light calls, so SEH swallows any Ogre C++ exception (0xE06D7363) instead of catching it properly. This is the same class of issue as OpenShim P0-7. The material path already does it right: C++ `try/catch` inside the SEH shell (1646-2118, wrapped by 2833-3239). | Practical exposure is low (`getAnimationState` is preceded by `hasAnimationState`), but the pattern is inconsistent. | Filter the `__except` to `EXCEPTION_ACCESS_VIOLATION` and add an inner `try/catch` like the material wrappers. | Read. |
| C-13 | [Low/High] | `CommandReplacement.cpp:689-696, 642-687` | The Hunt hook is resolved during static initialisation, i.e. inside `DllMain`. It scans every executable section and calls `Logging::LogMessage` (fopen, `std::mutex`, path resolution) under the loader lock. That contradicts `PublicAPI.cpp:105-106`. Activation itself is correctly deferred and build-gated: the `BasicPatch` constructor sets it INACTIVE and `EnableDeferredPatchActivation` reloads it. | File I/O under the loader lock; this is the pattern OpenShim fixed as F4. | Construct the `Hook` lazily from `Init`/`HandleLuaStateAttached`. | Read `BasicPatch.h:192-264` and `Hook.h`. |
| C-14 | [Low/High] | `CommandReplacement.cpp:612-640`, `Hook.h:62-72` | `Hook` installs `call [p]` (FF 15), not a jump. The naked thunk never pops that return address and leaves via `jmp resume`. It is correct today only because the resume path (`0x006136B7 mov al,1; jmp 0x6136E5`) ends in `mov esp,ebp; pop ebp; ret 4`. | The stack silently skews if the target or resume point ever moves ahead of a `pop` that uses `esp`. | Add `add esp, 4` before `jmp eax`, or comment the dependency. | Disasm of `0x006136A0-0x006136E8`. |
| C-15 | [Low/Med] | `CommandReplacement.cpp:311-439` | The Hunt label override writes an engine `.data` pointer with a raw `VirtualProtect` (to RWX), outside `BasicPatch`, and points it into exu.dll's `g_huntLabelBuffer`. Restore depends on `ReleaseState`, which works because Lua 5.1 finalizes the newer lifecycle sentinel before the older `_LOADLIB` handle. Resolution scans the full 37.8 MB `.data` VirtualSize byte by byte, because the label table is built at runtime: the 16-byte sequence is absent from the file's initialized `.data`. `g_huntLabelResolutionAttempted` makes an early miss permanent for the process. | A one-off hitch of tens of ms on the first override. If the first call comes too early, labels stay disabled until restart. | Resolve the table from the code site that fills it (and catalog that site), or retry the scan once per mission. | Python check of the raw `.data` in the GOG exe. |
| C-16 | [Low/Med] | `GameObject.cpp:703-801` | When no TRN is given explicitly, each `GetTerrainMaterialName`/`SetTerrainTextureSet` call recursively walks `Missions`, `addon` and `Workshop` under the exe directory. `std::filesystem::exists(root)` (709) is the throwing overload, called inside a `lua_CFunction`. `gameRoot/"Workshop"` does not match Steam's `steamapps/workshop/content/301650` layout (see `Docs/BZR_PLATFORM_COMPATIBILITY.md`). | An unbounded directory walk per call; terminate on a filesystem error; Steam workshop maps are not found. | Use the `error_code` overload. Prefer the engine's resolved TRN path. Follow the platform doc for workshop roots. | Read. |
| C-17 | [Low/Med] | `CommandReplacement.cpp:698-734, 945-1020` | Registry entries for dead units are never pruned. `UpdateCommandReplacements` does one `IsSelected` pcall per Hunt entry per tick, plus `GetTime`, even when the native hook is active. | O(n) Lua calls per tick if a mission registers a replacement for every produced unit. | Prune entries whose `IsValid(h)` is false during the update. | Read. |

Offending lines for High/Med items:

C-1 (`src/bzr.h:245-248`, `src/LuaHelpers.h:196-203`, `GameObject.cpp:5429-5439`):
```cpp
static GameObject* GetObj(handle h)
{
    return (GameObject*)(((h >> 0x14) * 0x400) + 0x260DB20);
}
...
return reinterpret_cast<BZR::handle>(lua_touserdata(L, idx));   // any userdata
...
BZR::Scanner* scanner = BZR::GameObject::GetObj(h)->GetScanner();
scanner->period = period;           // stale slot -> write through dangling Scanner*
```
Engine lookup (disasm `0x004DA060`): `and edx, 0xfffff; cmp dword ptr [eax + 0x15c], edx; je ok; xor eax, eax`.

C-2 (`GameObject.cpp:5231-5274`):
```cpp
if (!TryFindBestAiTask(aiProcess, 0x100, taskInfo))      // any "*Task*"/"*Attack*"
...
auto* task = reinterpret_cast<UnitTaskLayout*>(taskInfo.object);
if (TryGetOptionalNumberField(L, 2, "pitch", numericValue)) task->pitch = numericValue;       // +0xAC
if (TryGetOptionalVectorField(L, 2, "gotoDir", vectorValue)) task->gotoDir = vectorValue;      // +0x50
```

C-3 (`Multiplayer.cpp:29-37`):
```cpp
buildObjectAlwaysAsync.Reload();
...
lua_call(L, argC, 1);            // error -> longjmp; Unload() never runs
buildObjectAlwaysAsync.Unload();
```

C-4 (`Ordnance.cpp:69-89`):
```cpp
BZR::Ordnance* ord = reinterpret_cast<BZR::Ordnance*>(lua_touserdata(L, 1));
...
lua_pushstring(L, ord->ordnanceClass->odf);
...
BZR::handle handle = BZR::GameObject::GetHandle(ownerObj->owner);
```

C-5 (`Ordnance.cpp:32-41`):
```cpp
static const std::unordered_map<std::string, BZR::OrdnanceClass*> ordnanceMap = []() { ...
    std::string odfNoExtension(ord->odf, strlen(ord->odf) - 4);
```

C-6 (`GameObject.cpp:3474-3479`, `Multiplayer.cpp:70-73`):
```cpp
auto gameObject = reinterpret_cast<BZR::GameObject*>(CheckHandle(L, 1));
BZR::handle h = BZR::GameObject::GetHandle(gameObject);     // arbitrary pointer, no SEH, no build gate
...
if (isNetGame.Read() == true) { BZR::Multiplayer::UpdateLives(); }   // 0x006260F0, ungated
```

C-7 (`CommandReplacement.cpp:973-1016`):
```cpp
for (auto& [key, entry] : g_replacements)
{   ...
    const bool handled = DispatchRegisteredReplacement(handle, "Hunt", "stock_command_poll"); // user Lua
    ...
    entry.lastObservedCommand = commandAfterCallback;   // entry may have been erased
```

C-8 (`GameObject.cpp:2154-2155`, `1654-1662`):
```cpp
outName = Ogre::GetMaterialNameSubEntity(subEntity);
CacheMaterialHandle(outName, ::Ogre::GetSubEntityMaterial(subEntity));   // every entity API call
...
if (TryGetCachedMaterial(materialName, cachedMaterial)) { outMaterial = cachedMaterial.getPointer(); ... return ...; }
```

C-9 (`Ordnance.cpp:148-152`):
```cpp
float newCoeff = static_cast<float>(luaL_checknumber(L, 1));
coeffBallistic.Write(newCoeff);      // restored only at DLL unload
```

### Answers to the task questions

1. **Section map.** See section 1.

2. **Handle safety.**
   - `CheckHandle` is `lua_isuserdata` plus a `reinterpret_cast`, and it accepts full userdata too.
   - `GetObj` never returns null (handle 0 maps to slot 0), so the `obj == nullptr` checks at 4725 and 5215 are dead.
   - No binding in scope has an arena, generation or vtable check. The only generation check (`GetHandle(obj) == candidate`, 1073) is used by the diagnostic field scanner, not by bindings.
   - Bindings that dereference a Lua-derived pointer without an aliveness check:
     - **Writes:**
       - `SetAsUser` (engine call), `SetMass`, `SetRadarPeriod`, `SetRadarRange`, `SetVelocJam`, `SetAiTaskState`.
       - `SetEntityVisible/CastShadows/RenderingDistance/VisibilityFlags/QueryFlags/RenderQueueGroup`, `SetSubEntityVisible`.
       - `SetMaterialName`, `SetEntityMaterial`, `SetSubEntityMaterial`.
       - `SetHeadlight*`, `SetLight*`, `SetEntityAnimation*`.
       - `exu.animation.Play/Stop/Restart/SetEnabled/SetLoop/SetWeight/Seek` (gameObject targets).
       - `BuildOrdnance` (owner).
     - **Reads:**
       - `GetMass`, and `GetObj`, which exposes the raw pointer.
       - `GetHandle`, which passes an arbitrary pointer into the engine with no SEH.
       - `GetRadar*` and `GetVelocJam`, with no SEH.
       - `GetAiProcess*`, `GetAiTask*`, `GetAiRecycleTaskState`. The typed field reads have no SEH, and `recycleTask->me->pos` at 5407 dereferences a second pointer.
       - `GetSelectedWeaponMask`, `GetWeaponSelectionInfo`, and the entity/light/material/animation getters.
       - `GetOrdnanceAttribute` (raw `Ordnance*`).
   - Outcome on a dead handle: the arena itself never faults. After slot reuse the call acts on another unit. Before reuse, it follows whatever pointers the destroyed object left behind, into freed heap. SEH only helps when one of those pointers happens to hit unmapped memory.

3. **AI task-state writes.**
   - `SetAiTaskState` writes `braccelFactor`, `strafeFactor`, `steerFactor`, `omegaFactor`, `omegaScale`, `pitch`, `gotoForce` and `gotoDir` on the heuristically chosen task, plus `Patch::setTurboUnits[h]`.
   - There are no range or finite checks.
   - `curState/nextState` are not written directly. On a non-UnitTask target such as a RecycleTask, though, the `gotoDir` write overlaps them (C-2).
   - `Patch::setTurboUnits` is keyed by handle and never pruned or reset at Lua-state close (`GlobalTurbo.h:46`; grep finds no `erase`/`clear`).

4. **CommandReplacement path.**
   - The native path runs:
     1. `Wingman::SetActiveMode` (`0x006136A0`) handles mode 0x0D.
     2. The `Hook`'s `call [p]` at `0x006136AD` enters the naked thunk (pushad/pushfd).
     3. The thunk calls `TryHandleWingmanHuntActivation(this)`.
     4. That calls `GetHandle`, then does the registry `find`.
     5. It calls `IsSelected` via pcall.
     6. `DispatchRegisteredReplacement` runs the callback with `lua_pcall(L, 4, 1, 0)`.
   - Errors are reported through the engine's `LuaCheckStatus`, which logs and does not raise (disasm). An error counts as "not handled", so stock Hunt runs.
   - Registry refs are released in `ReleaseState`, which `HandleLuaStateClosing` calls (`PublicAPI.cpp:134`). `ResetState` also releases them when the owner matches.
   - With `Lua::state` null and no owner, the callee returns false and the original `SetCommand(CMD_HUNT)` runs.
   - A new `Init` rebinds `g_ownerState` and drops the old entries without `luaL_unref`. That is correct for a dead VM.
   - The `Hook` itself:
     - It is a process-lifetime `inline std::unique_ptr`. It never moves, which the `FF 15` pointer-in-object scheme requires.
     - It is resolved by a 44-byte masked signature that is also a required profile anchor.
     - Its preimage is captured at construction.
     - It is activated only by the build-gated deferred activation, and removed by `UnloadAllPatches`.
   - Residual issues: C-7, C-10, C-13, C-14, C-15.

5. **AnimationApi.**
   - No Ogre pointer is retained. `ResolveTargetEntity` re-resolves on every call. GameObject targets go through `GetRenderableEntity`. First-person targets call `GetProcAddress` for `OpenShimResolveLocalFirstPersonEntity` each time, with no cached function pointer. The returned generation is ignored.
   - Every `AnimationState` call goes through the SEH-only wrappers in `GameObject.cpp:2418-2636` (C-12). `getAnimationState` is always preceded by `hasAnimationState`, so its throwing path is not normally reachable.
   - Lua error paths: in `Play`, `Stop`, `SetEnabled`, `SetLoop`, `SetWeight` and `Seek`, `const std::string name` is live across `ReadPlayOptions`, `CheckBool`, `luaL_checknumber` and `luaL_argerror` (C-11, leak only).
   - `RawGetInfo` calls `lua_settop(L, 0)` before pushing. That is harmless because it is the last use of the arguments.
   - GameObject targets inherit C-1.

6. **Multiplayer.**
   - `SetLives` writes `0x008E8D04` directly and calls `UpdateLives 0x006260F0`. `UpdateLives` reads lives and sends net message 0x13 (disasm), so the value is broadcast by design. Neither step is build-gated (C-6).
   - `SetShowScoreboard` writes `0x02A17494`; it affects local UI only.
   - `GetMyNetID` reads one byte at `0x009180D4`, while the engine reads a 16-bit word there (`movzx ecx, word ptr [0x9180d4]` in `UpdateLives`).
   - `BuildAsyncObject`, `BuildSyncObject` and `DisableStartingRecycler` use `InlinePatch`es. These are build-gated through deferred activation but carry no `expectedBytes`. The async/sync pair leaks its patch on error (C-3).
   - `SetCoeffBallistic` is a client-side write to a global physics value that outlives the mission (C-9).
   - MP fairness and ownership: all of these are local writes, applied by whichever peer runs the script. Mission scripts run on every peer, so parity is the author's responsibility, except where EXU itself breaks it (C-3 stuck patch, C-9 carry-over). Whether a scripting DLL should be able to write MP-global physics at all is an OpenShim policy question and is not moved here.

7. **`game_state.cpp`.**
   - It holds no state. It does seven stateless reads of engine globals under SEH, plus `GetCursorInfo`.
   - It is called per frame from `Overlay.cpp:288`, and also from `IO.cpp`.
   - Nothing needs resetting.
   - It has no license header and a snake_case filename.

8. **Raw address census.** See section 4.

## 3. Dead / unreferenced code

| Symbol | Location | Verification |
|---|---|---|
| `Ordnance::SetOrdnanceAttribute` | `src/Game/Ordnance.cpp:107`, decl `Ordnance.h:47` | Grep over `src include tests Definitions` finds only the declaration, the definition and its own error string. Not in `exuExports[]`. |
| `Ordnance::SetOrdnanceVelocity` | `src/Game/Ordnance.cpp:129`, decl `Ordnance.h:48` | Same check; 2 hits. |
| `GameState::IsSingleplayerPauseMenuOpen` | `src/Game/game_state.cpp:125`, decl `game_state.h:29` | 2 hits (decl + def). |
| `GameState::IsMultiplayerPauseMenuOpen` | `src/Game/game_state.cpp:115`, decl `game_state.h:30` | 2 hits (decl + def). |
| unreachable `obj == nullptr` branches | `GameObject.cpp:4725` (partially), `5215-5220` | `GetObj` is arithmetic and never returns null. |
| `g_wingmanHuntActivationHookInitialized` | `CommandReplacement.cpp:689` | 1 hit. It exists only to force the initializer's side effects, which is intentional but opaque. |

All 80 functions declared in `GameObject.h` are exported; each was checked against `luaexport.cpp`.

## 4. Raw address census

Counted with the regex `0x00[4-9A-F][0-9A-F]{5}`, plus `0x0[12][0-9A-F]{6}` for high `.data`:

| File | Count | Notes |
|---|---|---|
| `GameObject.cpp`, `GameObject.h`, `CommandReplacement.*`, `AnimationApi.h`, `Ordnance.*`, `Multiplayer.cpp`, `Steam.*`, `game_state.h` | 0 | These use `bzr.h`, signature scans or offsets. |
| `Multiplayer.h` | 3 | `0x005C833D` NOP 11, `0x005C833B` NOP 2 and `0x0056F014` JMP. All three are **written**, none is in `exu.json`, and none has `expectedBytes`. They are build-gated by deferred activation. Bytes and instruction boundaries verified by disasm. |
| `game_state.cpp` | 7 | All **read** only, under SEH. `0x00945549`, `0x0094557C`, `0x009454EC`, `0x00918320`, `0x00918324` and `0x00918328` are not in `exu.json`; `0x00918310` is (`GameUI.EscapeWrapperActive`). |
| `bzr.h` | 68 + 9 | Provenance is worksheet A's job. The entries **used from this scope** are listed in the next table. |

`bzr.h` addresses used by this scope:

| Address | Use | exu.json | Build-gated |
|---|---|---|---|
| `0x00462380` GameObject::GetHandle | **called** | yes | no |
| `0x0260DB20` arena base | computed into, read, **written** (mass) | yes (`GetObj_base`) | no |
| `0x004DB930` SetAsUser | **called** | yes | no |
| `0x006260F0` UpdateLives | **called** | yes | no |
| `0x00586FF0` OrdnanceClass::Build | **called** | yes | no |
| `0x009C915C` OrdnanceClassList | read | yes | no |
| `0x008A2858` coeffBallistic | read/**written** | yes | no |
| `0x008E8D04` lives | read/**written** | yes | no |
| `0x02A17494` showScoreboard | read/**written** | yes | no |
| `0x00917F7B` isNetGame, `0x009180D4` myNetID, `0x0260B1D0` steam64 | read | yes | no |
| `0x004FF600` LuaCheckStatus (`LuaHelpers.h:51`) | **called** from `CommandReplacement.cpp` | **no** | no |
| Wingman Hunt block (sig) -> `0x006136AD` | **hooked** | The pattern is a profile anchor, not an `exu.json` entry. | yes |
| Hunt label table (runtime `.data`) | **written** (raw VirtualProtect) | no (runtime scan) | no |

One engine function is worth cataloguing for C-1: `0x004DA060`, the handle-to-object lookup with the serial check. It is not in `exu.json`.

## 5. Lifetime / ownership notes

| State | Lifetime | Reset at the right boundary? |
|---|---|---|
| Wingman Hunt `Hook` (`CommandReplacement.cpp:690`) | process object, activation per Lua state | Yes. Deferred activation in `Init`; `UnloadAllPatches` at close. |
| `g_replacements`, `g_ownerState`, registry refs | Lua state | Yes. `ResetState` in `Init`; `ReleaseState` in `HandleLuaStateClosing`. |
| Hunt label pointer override | Lua state (intended) | Yes, via `ReleaseState`/`ResetState`, but it sits outside the patch registry (C-15). |
| `g_huntLabelPointer`, `g_huntLabelResolutionAttempted` | process | A failed resolution is never retried (C-15). |
| `g_cachedMaterials` | process | **No** (C-8). |
| `ordnanceMap` | process (function static) | **No** (C-5). |
| `Patch::setTurboUnits` (written from `SetAiTaskState`) | process | **No**; never pruned or cleared. |
| `coeffBallistic`, `lives`, `showScoreboard` Scanners | process; restored at DLL unload | **No** reset at mission or Lua-state close (C-9). |
| `skipStartingRecycler` | temporary until Lua-state close | Yes. `Reload()` leaves `m_requestedStatus` INACTIVE, so the next `Init` does not re-apply it. |
| `buildObjectAlwaysAsync/Sync` | temporary, around one call | **No**, not on error (C-3). |
| AnimationApi targets | per call | Yes; no Ogre pointer is retained. |
| Ogre entity/light pointers in GameObject.cpp | per call | Yes; re-derived from the GameObject on each call. The GameObject itself is subject to C-1. |

## 6. Performance notes

- **Entity/light/animation bindings.** Each call re-derives the entity and validates it with three Ogre calls, a `std::string` copy and an `unordered_map<std::string, SharedPtr>` assignment (`IsRenderableEntityCandidate` -> `TryGetMaterialName` -> `CacheMaterialHandle`).
  - `exu.animation.Play` resolves the named state five times: `RawHas`, `RawSetTime`, `RawSetLoop`, `RawSetWeight`, `RawSetEnabled`.
  - Each resolution does `hasSkeleton`, `getAllAnimationStates`, `hasAnimationState` and `getAnimationState`.
  - First-person targets add two `GetModuleHandleA`/`GetProcAddress` pairs.
  - The total is a few microseconds per call; it matters only if a mission drives many units per frame.
- **Logging on failure paths.** `GetRenderableEntity` logs through `LogMaterialDebug` on every failed validation (2689), and that opens and closes `exu_material_debug.log` each time. A script that polls an entity-less or dead handle every frame therefore opens a file every frame. `SetAiTaskState` logs via `Logging::LogMessage` on every call.
- **`UpdateCommandReplacements`.** Per tick: one `GetTime` pcall, plus one `IsSelected` pcall per registered Hunt entry. The polling fallback adds one or two `GetCurrentCommand` pcalls per entry.
- **`game_state` probes.** One `GetCursorInfo` syscall plus seven reads per call, called every frame from the overlay suppression check. Negligible.
- **`Scanner::Read/Write`.** `GetLives`, `GetMyNetID`, `GetShowScoreboard`, `Get/SetCoeffBallistic` and `GetSteam64` each run `SignatureResolver::IsReadableRange` (one `VirtualQuery`). Negligible unless polled every frame.
- **AI scan APIs.** `GetAiProcessInfo`, `GetAiTaskInfo` and `GetAiTaskFieldScan` do up to 128 `VirtualQuery` calls plus RTTI walks per call. They are diagnostic only.
- **One-off costs.**
  - The Hunt signature scan of `.text` at DLL load.
  - The Hunt label `.data` scan over 37.8 MB on the first label override (C-15).
  - A recursive directory walk per terrain call when no TRN is given (C-16).

## 7. Patterns worth keeping

- **Material wrappers.** C++ `try/catch` sits inside an SEH shell (`TryResolveMaterialCpp` 1646, wrapped by `TryResolveMaterial` 2833). New Ogre-facing code should copy this, not the SEH-only entity wrappers.
- **`TryResolveHandleValue` (1054-1088).** It is the only handle validation in the file. Promote it, with the `+0x15C` serial check, to the shared helper for C-1.
- **CommandReplacement.**
  - Callbacks run through `lua_pcall` with a `StackGuard`.
  - The native path re-finds the iterator after dispatch (586).
  - Registry refs are released at `HandleLuaStateClosing`.
  - The hook is signature-resolved, anchored in the build profile, deferred and build-gated.
- **AnimationApi / `OpenShimBridge`.** It resolves on every call and never caches Ogre pointers or `GetProcAddress` results across the DLL lifetime. The bridge fails closed when the `winmm.dll` export is absent.
- **Input validation on the newer setters.** `SetEntityRenderingDistance`, `SetEntityRenderQueueGroup`, `SetLight*`, `SetEntityAnimation*` and `exu.animation.SetWeight/Seek` apply `isfinite` and range checks.
- **Stock Lua calls from C++.** `TryCallLuaStringFunction` and `TryGetClassLabelFromLua` use `lua_pcall` and restore the stack.

## 8. Low-severity items

- `SetMass` rejects only 0, so NaN and negatives are accepted and `mass_inv` then becomes NaN. `SetRadarRange/Period`, `SetVelocJam` and `SetLightPowerScale` apply no range checks (4687-4706, 5429-5495).
- `IsCommTowerPowered` (3449-3472) calls `GetClassLabel` with an unprotected `lua_call`, then `luaL_checkstring(L, -1)`. A dead handle therefore surfaces as "bad argument #-1".
- `GetMyNetID` reads a `uint8_t` where the engine reads a 16-bit word (`bzr.h:347` vs disasm at `0x00626105`).
- `ReplaceStockCmd` and `UpdateCommandReplacements` compare `lua_State*` pointers. Calls from a coroutine thread therefore fail with "bound to a different lua state" (842-845, 951-954).
- `CommandReplacement.cpp:413` applies `VirtualProtect(... PAGE_EXECUTE_READWRITE)` to a `.data` pointer that is already writable; `PAGE_READWRITE` is enough.
- `CheckHandle` accepts full userdata (a vector userdata, for example) as a handle.
- `LogMaterialDebug` is also used for non-material subsystems (SetAsUser, GetMass, AI), so those failures land in `exu_material_debug.log`.
- `AnimationApi.h` is 520 lines of header-only bindings included only by `PublicAPI.cpp`; it could be a `.cpp`.
- `GetExecutableSections` (137) and `FindMainModuleSection` (269) duplicate the PE-section walking that `SignatureResolver`/`BuildValidation` already do.
- The Wingman signature exists twice, in `CommandReplacement.cpp:98-104` and in the profile anchor. Nothing tests that the two stay equal; `tools/qualify_bzr_build.py` reads only the profile.
- The Multiplayer `InlinePatch`es pass no `expectedBytes`. On GOG 2.2.301 they are safe (disasm), but only because of the anchor gate.
- The `0x0056F014` JMP overwrites 5 of the 7 bytes of `mov eax,[ebp-0x64]; push eax; mov ecx,[ebp-0x68]`. The stranded bytes are skipped, which is fine but deserves a comment.
- Docs drift:
  - `Definitions/Animation.lua` lacks `animation.TargetLocalFirstPerson()`. Its `localFirstPersonTarget` comment ("False until ... validated live") contradicts `Docs/ANIMATION_API.md:96-108`.
  - `ExtraUtils.lua:1340-1344` says `GetHandle` returns an integer; it returns lightuserdata.
  - `SetMass` (`ExtraUtils.lua:1762`) omits the `mass` parameter.
  - `exu.ORDNANCE` (`ExtraUtils.lua:189-195`, `luaexport.cpp:426-444`) omits `VELOCITY = 5` and `LIFE_TIME = 6`, although `GetOrdnanceAttribute` supports them.
  - The `SetLightPosition`/`SetLightDirection` docs do not say the values are in the light's Ogre node-local space. No `OgreRenderSpace` conversion is applied, which is correct for an attached headlight but undocumented.
  - `Docs/Research/COMMAND_REPLACEMENT.md` matches the code.
- `GetConstructionRigSelectionInfo` pushes the raw `GameObjectClass*` as `selectedClass` lightuserdata. The pointer is only meaningful for the current mission.
