# Worksheet H: Cross-cutting mechanical sweeps (2026-09-27)

Reviewer scope: repository-wide sweeps over `src/` (116 files, 38,982 lines incl.
`include/`), `include/ExtraUtils.h` (123), `tests/` (host tests + smoke, 1,008
lines of C++), `Definitions/*.lua`, `Workshop/*.lua` (2 files), `examples/*.lua`.
Vendored `include/lua*.h` and `third_party/` are excluded except where cited.
Base: origin/main aec8c0a. All counts and line numbers were re-derived from a clean
`git archive aec8c0a` export in the scratchpad, because the shared working tree was
modified by other workstreams during the audit (55 files changed at the time of
writing, including the deletion of `WeaponConvergenceMath.h`). Subsystem reviewers own the per-file reading; this
worksheet owns the ten sweeps below and the findings that only show up across
files. Where a finding overlaps a subsystem worksheet it is cross-referenced
(worksheet F is the only one present at the time of writing).

Method. Throw-away Python in the session scratchpad (not in the repo):
a comment/string-stripping C++ tokenizer, a brace-matching function extractor
(1,473 function bodies, 543 with a `lua_State*` parameter), a namespace-scope
declaration scanner, and a call graph by simple name. Every candidate reported
as a finding or as dead code was then opened and re-read; callers were grepped.
Known limitations, stated once: name-based call resolution (no overload or
namespace resolution; collisions were removed by hand, e.g. Lua
`GetSubEntityMaterial` vs the C++ helper of the same name), macros are not
expanded, lambdas are not analysed as `lua_CFunction`s, and `lua_getfield` on a
non-table is not counted as a raising call in sweep 3 (it is in sweep 4).

Toolchain facts used throughout (verified in project files):

- `ExtraUtilities.vcxproj`: Debug has `<ExceptionHandling>Sync</ExceptionHandling>`
  (`/EHsc`); Release sets nothing, and the MSBuild default is also `/EHsc`.
  No `/EHa` anywhere.
- Lua is not the game's copy: `Lua5.1-BZR.vcxproj` builds `Lua5.1-BZR/src/*.c`
  as a static library linked into `exu.dll`, compiled as C, so
  `LUAI_THROW` is `longjmp` (`include/luaconf.h:624`). The `jmp_buf` it jumps to
  is the one the game's own `luaD_rawrunprotected` set up. It is an MSVC x86
  `_setjmp3` buffer, so `longjmp` does a SEH "safe" unwind that calls
  `__CxxFrameHandler` for every skipped frame.
- Under `/EHsc` the compiler assumes an `extern "C"` function never throws, and
  every `lua_*`/`luaL_*` is `extern "C"`. It may therefore skip the EH-state
  bookkeeping around those calls. Whether a destructor runs when one of them
  `longjmp`s depends on the optimiser: it may run, be skipped, or run from a
  stale state. I classify std::string/std::vector locals as **leak (likely)**
  and treat anything with side effects (locks, patch guards) as **UB-class**.
  Microsoft documents only the portable guarantee, which is none.
- exu.dll lifetime equals Lua-state lifetime. Lua 5.1 `ll_unloadlib`
  (`Lua5.1-BZR/src/loadlib.c:122`) `FreeLibrary`s C modules at `lua_close`, and
  EXU does not pin itself (see also F worksheet, F-3). Every static and every
  `inline` global is therefore rebuilt per mission, in `DllMain` under the loader
  lock.

## 1. Section map

Not applicable: this worksheet owns no files. Subsystem worksheets map the
large files (`Environment.cpp` 5,822, `GameObject.cpp` 5,496, `Overlay.cpp`
3,215, `UnitVo.cpp` 1,634, `OgreNativeFontBridge.cpp` 1,330, `ControlPanel.cpp`
1,290, `ContinuityApi.h` 1,097, `CommandReplacement.cpp` 1,021, `StorageApi.h`
1,000, `luaexport.cpp` 982, `OS.cpp` 947).

## 2. Findings

| ID | [Sev/Conf] | file:line | finding | why it matters | suggested fix | how verified |
|----|-----------|-----------|---------|----------------|---------------|--------------|
| H-1 | [High/Med] | `src/Patches/BulletHitCallback.cpp:36-94`, `src/Patches/BulletInitCallback.cpp:33-78`, `src/Patches/AddScrapCallback.cpp:31-52`, `src/LuaHelpers.h:74-93` | Three native hook callbacks can raise Lua errors from engine frames. They call `lua_getfield(L,-1,"BulletHit"/"BulletInit"/"AddScrap")` on the value of global `exu` without `lua_istable`, and BulletHit/BulletInit build the matrix argument with `PushMatrix`. `PushMatrix` does `lua_getglobal("SetMatrix")` + **unprotected `lua_call`** *before* the `lua_pcall`. BulletHit/BulletInit also skip the `L == nullptr` check that AddScrap has. | These run inside ordnance simulation, outside any Lua call, so `L->errorJmp` is NULL. `luaD_throw` then calls the panic function and `exit(EXIT_FAILURE)`. The game exits on a script mistake: global `exu` reassigned or niled, `SetMatrix` shadowed, or a strict-globals `__index` metatable. If an outer pcall does exist, it `longjmp`s across engine frames instead. Per-bullet path. | Build the matrix as a plain table (or push the 12 numbers and call `SetMatrix` inside the protected call). Check `lua_istable` before indexing `exu`. Better: one shared `CallExuCallback(name, pushArgs)` that `lua_pcall`s a C function doing all of the pushing, as `AiTargetSelect.cpp:150-167` and `CommandReplacement.cpp:441-470` already do correctly. | Read all three callbacks and `PushMatrix`. Grepped every `Lua::state` use (7 sites): only these three index `exu` unchecked. Every other native-context path checks `lua_istable` and uses `lua_pcall`. `luaD_throw` behaviour read in `Lua5.1-BZR/src/ldo.c`. |
| H-2 | [High/Med] | `src/UI/Overlay.cpp:2938`, `:2953`, `:3064` | Three Lua bindings call throwing Ogre methods with no `try` and no `__try`: `OverlayContainer::addChild` (duplicate child name, `ERR_DUPLICATE_ITEM`), `OverlayContainer::removeChild(name)` (name not a child, `ERR_ITEM_NOT_FOUND`), and `OverlayElement::setMaterialName` (unknown material, `ERR_ITEM_NOT_FOUND` in 1.10). | An `Ogre::Exception` leaving a `lua_CFunction` crosses Lua's C frames, which have no EH tables, and reaches the game's frames, so the game terminates. Trigger: `exu.AddOverlayElementChild` twice, `RemoveOverlayElementChild` on a non-child, or `SetOverlayMaterial` with a typo. | Wrap each call in `try { } catch (const ::Ogre::Exception&)` (and `catch (...)`), return false, and log once. Or check first (`getChild`, `MaterialManager::resourceExists`). | Sweep 5: 87 direct `::Ogre::` call expressions, of which 16 are unguarded and listed. Of those 16, only these 3 can throw (`show`/`hide`/`setColour`/`setMetricsMode` do not; the 8 `Ogre.h` getters are called under callers' `__try`). `Overlay.cpp` has 31 `__try` blocks and no C++ `try` at all (grepped). Throw behaviour is from Ogre 1.10 source knowledge. The headers in `third_party/` do not document it, hence Med. |
| H-3 | [Med/High] | `src/BasicPatch.h:226-264`; 26 sites (sweep 2 table) | **None of the 26 static code/vtable patch sites is byte-verified.** The `expectedBytes` parameter of `BasicPatch`/`Hook`/`InlinePatch` has **0 callers** in the repo. The preimage is whatever was at the address when exu.dll loaded, and `ValidatePreimage` only proves it has not changed since. None of the 26 addresses is in `exu.json`. Only the dynamically resolved sites check identity: Overlay (4, `MatchBytes`), viewport scheme (3, `FF 15`+IAT), Radar (2, `E8`+target), AiTargetSelect (11, RTTI+slot / call target), CommandReplacement (unique signature), UnitVo (first-match signature). | The global gate is the 3-anchor exe check (`BzrBuildProfile.generated.h:36-44`). If OpenShim (or any other patcher) wrote one of these sites first, EXU silently adopts the foreign bytes as "original", overwrites them, and restores them on unload. The ShotConvergence entries overwrite two vtable slots (`0x0088A4FC`, `0x00889418`) without checking the old pointer. The architecture rule "every write to game code must be byte-verified" is met by 0 of 26 static sites. | Pass the stock bytes (from GOG 2.2.301) as `expectedBytes` at every static site, and add each site to `exu.json` with its pattern. For the two vtable writes, pass the expected original function pointer. Add a host test that fails if a `Hook`/`InlinePatch` is constructed from a literal without an expected-bytes argument. | Grepped `expectedBytes\|MatchBytes` over `src/` (only the `Overlay.cpp` wrapper). Enumerated all `Hook`/`InlinePatch` constructions. Cross-checked every address against `exu.json` with the script (both `0x00…` and bare spellings). |
| H-4 | [Med/Med] | `src/Ogre/Ogre.h:100-356`, `src/bzr.h:367-381`; `src/Game/Culling.cpp:26-31`; `src/UI/Renderer.cpp:52` | **71 distinct raw OgreMain.dll RVAs, and 65 function pointers built from them, are called with no module identity check.** The RVAs are 57 literals in `Ogre.h` plus 14 offsets in `bzr.h`; 8 of the 14 are used by `Ogre.h`'s 65 `CalculateAddress(…, OGRE)` globals and 6 are unused. `BuildValidation` checks only the exe. All 57 `Ogre.h` literals are absent from `exu.json` (the 14 `bzr.h` ones are catalogued). Several are not function-aligned (`0x18219`, `0x2E4C9`, `0x01983`, `0x0FB69`), i.e. incremental-link thunks, which move on any OgreMain rebuild. Every one of these methods is exported by mangled name, and newer code (`ResolveOgreProc`, `OgreMaterialShim.h` `Detail::ResolveProc`) already resolves them that way. | A different OgreMain.dll (Redux update, a mod, a GOG/Steam divergence) turns these 65 function pointers into jumps to arbitrary code. Most are inside `__try(EXCEPTION_EXECUTE_HANDLER)`, so the result is either a swallowed AV or silent corruption. `Culling::UpdateUnit` → `Ogre::SetVisible` (RVA `0x220EF0`) is unguarded and runs **per unit per tick** when culling is on. | Replace the RVA table with `GetProcAddress` by mangled name, using the existing `ResolveOgreProc` pattern. Otherwise add an OgreMain identity check (size of image, timestamp, or a byte anchor) to `BuildValidation` and fail closed. Drop the 7 unused RVAs (dead-code list). | Counted `CalculateAddress(` (65) and the offset uses. Grepped `exu.json` for each RVA (the 14 in the `rva` block are present, the 51 in `Ogre.h` are not). Read `Culling.cpp`, where no `__try` surrounds the call. |
| H-5 | [Med/High] | `src/bzr.h:245-248`; 30 unvalidated call sites | **Handle to object conversion does not validate the handle.** `GetObj(h)` is pure arithmetic, `((h>>20)*0x400)+0x260DB20`: it never returns null and does not compare the generation. 30 of its 32 callers use the result directly. Examples: `SetRadarPeriod`/`SetRadarRange`/`SetVelocJam` (`GameObject.cpp:5428-5494`) write floats through a `Scanner*`/`Jammer*` read out of the slot; `SelectAdd`/`SelectOne` (`ControlPanel.cpp:1269-1288`) pass the pointer to native code; `BuildOrdnance` (`Ordnance.cpp:52`). The `obj == nullptr` tests after `GetObj` (e.g. `GameObject.cpp:4722,5235`, `UnitVo.cpp:1413,1540`) can never fire. Two validating resolvers exist and duplicate each other: `GameObject.cpp:1054` `TryResolveHandleValue` and `AiTargetSelect.cpp:426` `TryResolveHandle`. Both check `GetHandle(obj) == h`. | A dead or stale handle (common in mission scripts), or any userdata (`CheckHandle` accepts full userdata too), resolves to whichever object now occupies the slot, or to a freed slot. `SetVelocJam(deadHandle, x)` can write through a garbage `Jammer*`. | Move one validated `TryResolveHandle(h, GameObject*&)` into `bzr.h`/`GameObject.h`, route all 32 sites through it, and return nil/false on mismatch. Make `CheckHandle` reject full userdata. | Listed all `GetObj(` sites (32). Read each. Only the two resolvers compare handles. Read the `GetObj` body. |
| H-6 | [Med/High] | `src/Game/Environment.cpp:306-331`, `:3212-3580`; `src/Util/Logging.h:16-120`; `Workshop/exu_weather.lua:461-467` | **Unbudgeted file logging on per-frame Lua paths.** `SetSunAmbient/Diffuse/Specular/Direction`, `SetTimeOfDay`, `SetSunPowerScale` and `SetSunShadowFarDistance` each emit 3–6 `LogEnvironmentDebug` lines per call, plus `DescribeLuaCaller` (`lua_getinfo` + string). Each line costs a mutex, `ResetLogFileForCurrentProcess` (`GetModuleFileNameA`, `CreateDirectoryA`, 3–4 `std::string`s, a set lookup), a `std::ofstream` open/append/close, and 2 `OutputDebugStringA`. The shipped Workshop weather controller calls `SetSunDiffuse` (and `SetFog`, `SetAmbientLight`) from `Weather.Update` every frame whenever `ambientScale ~= 1`. `LogMessage` (189 call sites) costs 2× `GetModuleFileNameA`, 2× `CreateDirectoryA` and `fopen`/`fclose` per line. | Several file-system syscalls per frame on the render thread, and `exu_environment_debug.log` grows without bound for the session: truncated once per process, never rotated or capped. | Resolve the log path once, keep one `FILE*` per log (or a buffered writer flushed on state close), and put the `[EXU::Set*] enter/parsed/calling` diagnostics behind a debug flag or a per-message budget. | Read `LogEnvironmentDebug`, `WriteEnvironmentDebug`, `LogMessage`, `OpenSessionLogFile` and `GetLogFilePath`. Read `Weather.Update` → `ApplyEnvironment` in `Workshop/exu_weather.lua` (lines 600-644, 434-468). Counted wrappers per call in the setters. |
| H-7 | [Med/High] | `src/Patches/GlobalTurbo.cpp:56-80`, `src/Patches/GlobalTurbo.h:46`, `src/BasicPatch.h:157-182,305-318`, `src/InlinePatch.h:34-59` | **Per-unit, per-tick code patching.** `DoSelectiveTurboPatch` (called from both the Begin and End hooks of every unit task tick) flips `turboPatch1`/`turboPatch2` to the unit's value and back for any handle ever passed to `SetUnitTurbo`. `false` is stored rather than erased, and the map is never pruned. Every flip is 2× `VirtualProtect` + `memcpy` + `FlushInstructionCache`; every re-activation also runs `ValidatePreimage` → `MatchBytes` (a `VirtualQuery` loop plus a SEH frame). A unit whose setting differs from the global one costs 8 `VirtualProtect`, 4 `FlushInstructionCache` and 2 `VirtualQuery` per tick. | For N such units at simulation rate that is thousands of page-protection syscalls per second, plus i-cache flushes. Handles are reused, so a new unit inherits a dead unit's turbo setting. The path is live only when OpenShim does not export `OpenShimHasUnitTurboHooks`. | Rewrite the two sites once, to a stub that reads a per-unit flag (the hook already receives the `GameObject*`). Erase the map entry on `SetUnitTurbo(h, false)`, and prune dead handles. | Read `DoSelectiveTurboPatch`, `SetStatus`/`Reload`/`Unload`/`RestorePatch`/`DoPatch`. Grepped `setTurboUnits` for `erase`/`clear` (none). Call graph from `TurboPatchBegin` (sweep 7). |
| H-8 | [Med/Med] | 90 functions (sweep 3) | **C++ objects live across raising Lua calls.** 79 registered bindings and 11 helpers hold `std::string`/`std::vector` locals across `luaL_check*`/`luaL_opt*`/`luaL_argerror`/`luaL_error`/`CheckBool`/`CheckHandle`/`CheckVectorOrSingles`/`CheckColorOrSingles`. No lock and no `ScopedPatchDisable` is ever live across a raising call: the UnitVo locks are taken after all checks, and `ScopedPatchDisable` has 0 users. | Per toolchain facts: a leak at best, UB-class at worst. Worst case: `StaticGeometry::Create` (`StaticGeometry.cpp:504-511`) reserves up to 100,000 × 40 B = 4 MB, then `luaL_argerror`s inside the fill loop. Most others leak one heap string per bad call; strings of 15 characters or fewer sit in the SSO buffer and leak nothing. | Validate every argument into PODs/`const char*` before constructing owning objects. For the loops (`StaticGeometry::Create`, `SetUnitVoAlternates`, `SetTerrainTextureSet`), collect errors into a flag and raise after the containers go out of scope, or run the body under `lua_cpcall`. | Script (sweep 3) plus manual reading of `StaticGeometry::Create`, `SetUnitVoAlternates`, `SetTerrainTextureSet`, `SaveGame`, the UnitVo lock sites, and the `ScopedPatchDisable` grep. Overlaps F-16 (which covers Storage/OS only). |
| H-9 | [Med/Med] | `src/Patches/UnitVo.cpp:603-625`, `src/Game/CommandReplacement.cpp:640-697`, `src/Ogre/Ogre.h:124-356`, 36 `Scanner` globals, 26 patch globals | **Heavy work in DLL static initialisation, under the loader lock, on every mission load.** About 130 dynamic initialisers: 36 `Scanner` constructors (`VirtualQuery` + `VirtualProtect` to `PAGE_EXECUTE_READWRITE` on `.data` and heap pages, never narrowed until unload); 26 patch objects (`VirtualQuery` + preimage copy); 65 `GetModuleHandleA("OgreMain.dll")`; 2 full executable-section signature scans (`InitializeUnitVoQueueHooks`, `InitializeWingmanHuntActivationHook`) that call `Logging::LogMessage`, i.e. `CreateDirectoryA` + `fopen` inside `DllMain`; 5 OpenShim export probes (`GlobalTurbo.cpp:42`, `ControlPanel.cpp:847-866`). | Contradicts `dllmain.cpp:36-38` and `PublicAPI.cpp:105-106` ("File-system, CRT, mutex, and console work is deliberately performed here rather than from DllMain"). File I/O and `std::mutex` construction under the loader lock deadlock if another thread holds a CRT or file-system lock that needs the loader. It also makes data pages RWX. | Turn the two scanners and all patch/Scanner globals into function-local statics or an explicit `InstallNativeState()` called from `HandleLuaStateAttached`. Keep `DllMain` initialisers constant-initialised only. | Scope scanner (sweep 6) over namespace-scope initialisers. Read both `Initialize*` functions and `Logging::LogMessage`. Counted `inline (const )?Scanner` (36) and `CalculateAddress` (65). |
| H-10 | [Med/Low] | `src/Game/Environment.h:30`, `src/Ogre/Ogre.h:81-92`, `src/Game/Environment.cpp:3156-3186, 5789-5821` | `Environment::fog` is a `Scanner` whose address, `sceneManager + 0x128`, is computed once at DLL load. Every other scene-manager user re-reads the pointer per call, and `TryInitializeOgre` explicitly logs and supports a rebind ("was %p … rebind=%d"). | If the scene manager is null when `require "exu"` runs, `GetFog`/`SetFog` are silent no-ops for the whole mission, because `Scanner` refuses `0x128`. If the scene manager is replaced mid-mission, `SetFog` writes 24 bytes into the old object. | Delete the `fog` Scanner and compute `GetFog()` inside `GetFog`/`SetFog`, after the existing null check. | Read all three sites and grepped `fog.` users (2). Whether BZR ever swaps scene managers mid-mission is not proven, hence Low confidence. |
| H-11 | [Med/Med] | `src/Scanner.h:141-151`; 25 Scanners listed in §5 | 25 of 36 `Scanner` globals keep the default `Restore::ENABLED`, including **8 that EXU only reads** (`Satellite::state/cursorPos/camPos/clickPos`, `Steam::steam64`, `Reticle::position/object/matrix`). At every `lua_close`, `FreeLibrary` → static destructors write back into live game globals the values captured when the DLL loaded. | Stale reticle target/matrix, satellite state and cursor values get written into the engine during mission teardown. For the 17 writable ones, "restore on unload" happens after the mission's Lua has gone and silently reverts player-visible settings (F-3 covers `playOption`). | Use `Restore::DISABLED` for read-only scanners. For writable ones, record only what a script changed and restore it explicitly in `HandleLuaStateClosing`. | Grepped `inline (const )?Scanner` without `Restore::DISABLED` (25). Read the `Scanner` destructor. |
| H-12 | [Med/Med] | 222 of 235 `__try` blocks; 19 thunk-called C++ callbacks | **SEH as the Ogre/C++ exception barrier.** 222 `__except(EXCEPTION_EXECUTE_HANDLER)` (plus 2 equivalent) wrap Ogre or engine calls. They swallow C++ exceptions (`0xE06D7363`, leaking the exception object) and access violations alike, then carry on with possibly half-built Ogre state. Only `OgreNativeFontBridge.cpp` (11 blocks) filters C++ exceptions out. Separately, none of the 19 C++ functions called from naked hook thunks (`call X` in `__asm`) is `noexcept` or has a `try` barrier, and several allocate (`QueueUnitVo`, the Overlay wrappers, `DoSelectiveTurboPatch`'s `unordered_map`). | Same class as OpenShim P0-7. A real bug (bad pointer, bad handle) is hidden behind "crashed code=0x…" debug-log lines, and state is not rolled back. A `bad_alloc` in a thunk callback unwinds into engine frames. | Filter `__except` to `EXCEPTION_ACCESS_VIOLATION` (copy `HandleNativeOverlayException`, `OgreNativeFontBridge.cpp:125-129`) and catch `Ogre::Exception` with C++ `try`. Give thunk callbacks a `try { } catch (...) {}` body. | Sweep 5 census (per-file list in §11), sweep 4 thunk list, and a `noexcept` grep of the 19 targets. |
| H-13 | [Low/High] | `examples/PhysicsImpact.lua:177-178`; `src/Game/Ordnance.cpp:107-140`, `Ordnance.h:34-43`; `src/luaexport.cpp:426-444`; `Definitions/Animation.lua` | Docs and examples drift. `PhysicsImpact.lua` calls `exu.SetOrdnanceVelocity` (defined but never registered) and reads `exu.ORDNANCE.VELOCITY`, which `MakeEnums` never creates (codes 5/6 `VELOCITY`/`LIFE_TIME` are accepted by `GetOrdnanceAttribute` but not exported). `SetOrdnanceAttribute`/`SetOrdnanceVelocity` write through an unvalidated `Ordnance*` light userdata. `exu.animation.TargetLocalFirstPerson` is registered but absent from `Definitions/Animation.lua`. The three OpenShim-facing exports (`EXU_UpdateCullingForUnit`, `EXU_GetTeamEngineFlameColor`, `ExuNotifyMissionSimulationState`) are not declared in `include/ExtraUtils.h` or documented as ABI. | The shipped example errors on first use. The private ABI can drift unnoticed. | Either register and document the ordnance setters with pointer validation, or delete them and fix the example. Document the three bridge exports next to the public ones. | Diffed registered names (373 + 23) against `Definitions/*.lua` and every `exu.X` use in `examples/`, `Workshop/`, `tests/host/*.lua` (script). |
| H-14 | [Low/High] | §3 | Dead code: 487-line `WeaponConvergenceMath.h` included by nothing; `ScopedPatchDisable` unused; 2 unregistered Lua functions; 2 declared-never-defined; 10 unused functions/ABI helpers; 19 unused engine addresses, offsets and RVAs; commented-out volume API. | Maintenance noise. The unused addresses inflate the raw-address census and `exu.json`. | Delete (list in §3). | Tokenizer sweep plus per-candidate grep over `src/`, `include/`, `tests/`, `Definitions/`, `examples/`, `Workshop/`. |
| H-15 | [Low/High] | §12 | 14 groups of duplicated helpers: 4 Ogre proc resolvers + ~25 inline `GetProcAddress` lambdas; 3 private `GetExecutableSections`/`ExecutableSection` copies beside `SignatureResolver`'s; 6 log wrappers, of which `LogNativeOverlayMessage` is a verbatim copy of `LogMessage`; 2 validated handle resolvers; a 25× copy-pasted OpenShim `fn/attempted/loggedMissing` triple; world render origin read raw twice, bypassing `OgreRenderSpace.h`. | Fixes land in one copy and miss the others (e.g. `UnitVo`'s scanner is first-match while `SignatureResolver` has a unique variant nobody calls). | Consolidate per group (§12). | Grep sweep (§12). |

**H-1 quotes**

```cpp
// src/Patches/BulletHitCallback.cpp:36-43, 76-82, 93
lua_State* L = Lua::state;            // no null check (AddScrap has one)
StackGuard guard(L);
lua_getglobal(L, "exu");
lua_getfield(L, -1, "BulletHit");     // raises if global exu is not indexable
...
Lua::PushMatrix(L, *transform);       // lua_getglobal("SetMatrix") + lua_call(L, 12, 1): unprotected
...
int status = lua_pcall(L, 5, 0, 0);   // only this last call is protected
```

**H-2 quotes**

```cpp
// src/UI/Overlay.cpp:2938, 2953, 3064
parent->::Ogre::OverlayContainer::addChild(child);
parent->::Ogre::OverlayContainer::removeChild(childName);
element->::Ogre::OverlayElement::setMaterialName(materialName);
```

**H-3 quotes**

```cpp
// src/BasicPatch.h:226-230 — expectedBytes defaults to empty; 0 callers pass it
BasicPatch(uintptr_t address, size_t length, Status status, std::vector<uint8_t> expectedBytes = {})
// src/Patches/AddScrapCallback.cpp:87 (typical)
Hook addScrapHook(0x005E1016, &AddScrapCallback, 6, BasicPatch::Status::ACTIVE);
// src/Patches/ShotConvergence.cpp:31 — replaces a vtable entry, old value unchecked
InlinePatch shotConvergence(wingmanWeaponAimVftableEntry, &walkerUpdateWeaponAim, 4, InlinePatch::Status::INACTIVE);
```

**H-4 quotes**

```cpp
// src/Ogre/Ogre.h:124-126 (one of 65)
inline uintptr_t getDirectionAddr = BasicScanner::CalculateAddress(0x14042, OGRE);
// src/Game/Culling.cpp:26-31 — per unit per tick, no __try
void* entity = obj->GetOgreEntity();          // [obj+0xF0]+0x94, no null check on [obj+0xF0]
Ogre::SetVisible(entity, visible);            // raw RVA 0x220EF0
```

**H-5 quotes**

```cpp
// src/bzr.h:245-248
static GameObject* GetObj(handle h)
{ return (GameObject*)(((h >> 0x14) * 0x400) + 0x260DB20); }
// src/Game/GameObject.cpp:5488-5492
BZR::Jammer* jammer = BZR::GameObject::GetObj(h)->GetJammer();
if (jammer != nullptr) { jammer->maxSpeed = maxSpeed; }
```

**H-6 quotes**

```cpp
// src/Game/Environment.cpp:3280-3287 (SetSunDiffuse; siblings identical)
auto caller = DescribeLuaCaller(L);
LogEnvironmentDebug("[EXU::SetSunDiffuse] enter caller=%s argType=%s terrainMasterLight=%p", ...);
// src/Game/Environment.cpp:313-320 — per line
ExtraUtilities::Logging::ResetLogFileForCurrentProcess("exu_environment_debug.log");
std::ofstream file(ExtraUtilities::Logging::GetLogFilePath("exu_environment_debug.log"), std::ios::app);
```

**H-7 quote**

```cpp
// src/Patches/GlobalTurbo.cpp:62-78 — both hooks, every unit task tick
case TurboCode::BEGIN:
    if (setTurboUnits.contains(h)) { turboPatch1.SetStatus(setTurboUnits.at(h)); turboPatch2.SetStatus(setTurboUnits.at(h)); }
case TurboCode::END:
    if (setTurboUnits.contains(h)) { turboPatch1.SetStatus(globalTurboEnabled); turboPatch2.SetStatus(globalTurboEnabled); }
```

**H-8 quote**

```cpp
// src/Game/StaticGeometry.cpp:504-511
std::vector<Instance> instances;
instances.reserve(instanceCount);                 // up to 100000 * 40 bytes
for (...) { lua_rawgeti(L, 4, index);
    if (!lua_istable(L, -1)) { return luaL_argerror(L, 4, "each instance must be a table"); } // longjmp over the vector
```

**H-9 quote**

```cpp
// src/Patches/UnitVo.cpp:621-625 — runs in DllMain; scans .text and writes log files
inline uintptr_t g_unitVoQueueHooksInitialized = InitializeUnitVoQueueHooks();
Hook unitVoSayQueueHook(g_unitVoSayQueueCallSite, &UnitVoSayQueueHook, 8, BasicPatch::Status::ACTIVE);
```

**H-10/H-11 quotes**

```cpp
// src/Game/Environment.h:30
inline Scanner fog(Ogre::GetFog(), BasicScanner::Restore::DISABLED);   // sceneManager+0x128 frozen at DLL load
// src/UI/Reticle.h:30-33 — read-only, but Restore::ENABLED by default: written back at FreeLibrary
inline const Scanner position(BZR::Reticle::position);
inline const Scanner object(BZR::Reticle::object);
```

## 3. Dead / unreferenced code (Sweep 1)

Method: every identifier in a declaration context in `src/` and
`include/ExtraUtils.h`, counted across `src/`, `include/` and `tests/`, which
includes the `exuExports[]` table and the three `luaL_Reg` tables. Candidates
were names whose every occurrence is a declaration. Each candidate was then
grepped by hand across the repo, including `Definitions/`, `examples/`,
`Workshop/` and `Docs/`. Excluded as not dead: the `extern "C"` exports
(`PublicAPI.cpp` 4 + `luaopen_exu` + the 3 OpenShim bridge exports), public
header API (`EXU_VERSION_EXPECTED`, `EXU_MultiplayerNicknameResult`), RAII
globals (`g_slotRestoreGuard`), side-effect initialiser globals
(`g_unitVoQueueHooksInitialized`, `g_wingmanHuntActivationHookInitialized`), the
26 patch globals, and ABI layout fields/padding (91 fields in `bzr.h`,
`GameObject.cpp` RTTI/AI layouts, `Environment.cpp:72-91`, `ShotConvergence.cpp`).

**Functions / types (verified unreferenced), 25**

| symbol | file:line | note |
|---|---|---|
| `WeaponConvergenceMath.h` (whole file, 487 lines: namespace `WeaponConvergence`, `Solve`, `AimPointAccumulator::Add/Count`, `Normalize`, `IsFinite(Matrix)`, …) | `src/Patches/WeaponConvergenceMath.h:1-487` | Included by no TU, not in `vcxproj`, not in `tests/linux/run.sh`. One commit: 2cd3bf2 "wip(convergence)". |
| `ScopedPatchDisable` | `src/BasicPatch.h:352-378` | 0 users. |
| `SetOrdnanceAttribute` | `src/Game/Ordnance.cpp:107`, `.h:47` | Lua function never registered. |
| `SetOrdnanceVelocity` | `src/Game/Ordnance.cpp:129`, `.h:48` | Lua function never registered; used by an example (H-13). |
| `GetOrdnanceVelocRatio`, `SetOrdnanceVelocRatio` | `src/Patches/OrdnanceVelocity.h:33-34` | Declared, never defined. |
| `IsSingleplayerPauseMenuOpen`, `IsMultiplayerPauseMenuOpen` | `src/Game/game_state.cpp:115,125`, `.h:29-30` | Defined, never called. |
| `ResolveEntityDetachObjectFromBone` | `src/Game/Environment.cpp:1976` | Resolver with no caller. |
| `HasBzrNetNicknameBridge` | `src/OpenShimBridge.h:97` | No caller. |
| `FindUniqueMaskedPattern` | `src/Util/SignatureResolver.h:352` | No caller (UnitVo uses first-match `FindPattern` instead). |
| `RenderProfileBridge::ApiVersion`, `RenderProfileBridge::GetActiveBackend` | `src/RenderProfileBridge.h:66,115` | No caller. |
| `RenderEffectBridge::ApiVersion` | `src/RenderEffectBridge.h:114` | No caller. |
| `VECTOR_3D_LONG::ToVec` | `src/bzr.h:91` | No caller. |
| `DoEventHooks` | `src/luaexport.cpp:469` | Empty body, called once. |
| `SetRenderQueueGroupSubEntity` + `setRenderQueueGroupSubEntityAddr` | `src/Ogre/Ogre.h:200-202` | Raw RVA `0x2454B`, never called. |
| forward decls `SubEntity`, `Entity` | `src/Ogre/OgreMaterialShim.h:71-72` | Unused. |
| `OgreOverlayShim.h` ABI mirror members: `setTiling`, `setUV`, `setTransparent`, `setBorderSize`×3, `setBorderMaterialName`, `setSpaceWidth`, `setColourTop`, `setColourBottom`, `setAlignment`; `GMM_PIXELS`; `Resource::backgroundThread`; `isTemplate` fields | `src/Ogre/OgreOverlayShim.h:54-169` | Declared `dllimport`, never called: `Overlay.cpp` resolves the same methods by mangled name (`TryCallPanelSetTiling` etc.). |
| `SectionView::virtualAddress` | `src/Util/SignatureResolver.h:28` | Written, never read. |
| `CapEnhancedResources` | `src/RenderProfileBridge.h:49` | Not reported by `GetRenderCapabilities` (`Environment.cpp:5585-5603`). |
| `kProfileId`, `kGameVersion`, `kRuntimeAnchorCount` | `src/Util/BzrBuildProfile.generated.h:31,32,46` | Generated, unused (keep if the generator wants them). |

**Unused engine addresses / offsets / RVAs, 19** (also in §4): `bzr.h:143`
`viewFrustum` 0x008EABE0, `:180` `editMode` 0x009454B8, `:205` `sunDirection`
0x02CEB830, `:513` `cockpitWireframeProjectionRadius` 0x009173C0, `:514`
`radarLeftBase` 0x009782A0, `:517` `cockpitWireframeCenterBase` 0x008E7918,
`:523-526` `edgeMinX/MaxX/MinZ/MaxZ` 0x00917388..94, `:497` `soundStruct2`
0x00915594 (only commented-out users), `:499-500` `sfxOffset`/`voiceOffset`, and
`:376-381` the six Ogre sky RVAs `get{SkyBox,SkyDome,SkyPlane}{GenParameters,Node}Offset`,
superseded by `Ogre.h:370-419`. They are also catalogued in `exu.json`.

**Test-only (referenced only from `tests/`), 5**: `kResultRejectedParam`,
`kResultRejectedValue` (`RenderEffectNames.h:67-68`), `IsSteamBuild`,
`IsGogBuild` (`BuildValidation.h:249,254`), `MISSION_SAVE_PROBE_SIZE`
(`NativeSaveFlag.h:36`). Fine to keep.

**Commented-out code, 5 blocks** (sweep 9): `SoundOptions.cpp:166-198` (4
functions, 30 lines), `SoundOptions.h:31-32,40-43`, `luaexport.cpp:936-939`.

**Unregistered `int f(lua_State*)`**: 406 definitions and 396 registrations
(373 `exuExports` + 12 animation + 6 storage + 5 continuity). The unregistered
ones are the internal installers/lifecycle functions plus exactly the 2 Ordnance
setters above.

Total counted as dead: **25 function/type items (the `OgreOverlayShim.h` mirror counted once) + 19 addresses/offsets/RVAs + 5
comment blocks** (one 487-line file).

## 4. Raw address census (Sweep 2)

Regex `0x00[4-9A-F]xxxxx`, `0x02xxxxxx` and 7-digit `0x2xxxxxx`, over
comment-stripped `src/` + `include/ExtraUtils.h`. **149 literals, 144 distinct**,
in 17 files. There are also 17 comment-only mentions, and 71 OgreMain RVAs
(57 literals in `Ogre.h` + 14 offsets in `bzr.h`) that the regex cannot see (H-4).

Per file: `bzr.h` 77, `UI/ControlPanel.cpp` 15, `Patches/AiTargetSelect.cpp` 14,
`Game/game_state.cpp` 7, `Patches/ShotConvergence.h` 6, `UI/Overlay.cpp` 6,
`Game/Environment.cpp` 4, `Patches/GlobalTurbo.h` 4,
`Patches/OrdnanceVelocity.cpp` 4, `Game/Multiplayer.h` 3, `UI/Radar.cpp` 3, and
1 each in `Game/Environment.h`, `LuaHelpers.h`, `Patches/AddScrapCallback.cpp`,
`Patches/BulletHitCallback.cpp`, `Patches/BulletInitCallback.cpp`,
`Patches/KillMessages.cpp`.

Use classes (literals): WRITTEN 48 (26 static code/vtable patch sites with no
preimage; 16 dynamically guarded call/vtable/IAT sites; 4 data stores in hook
asm; 1 `SetTimeOfDay` store; 1 function address written into a vtable).
CALLED 26 (15 `bzr.h` function pointers with no identity check; 9 others).
READ via `Scanner` 21, READ+WRITTEN via `Scanner` 16, READ/WRITTEN direct 22,
ANCHOR 2, UNUSED 11.

**Duplicates (same address defined twice or more), 4**: 0x00492EC0
(`Radar.cpp:239` constant and `bzr.h:547` fn pointer); 0x008E77A8
(`bzr.h:337` `uiScaling` and `bzr.h:515` `radarLeft`, documented alias);
0x00918324 (`game_state.cpp:18` and `Overlay.cpp:1418,1435` inside asm);
0x0094672C (`bzr.h:476` `userProfilePtr` and `bzr.h:496` `soundStruct1`,
documented alias).

**Used in code, absent from `exu.json` (hex, bare and RVA spellings checked),
71 distinct:** all 26 static patch sites, plus 0x004FF600 `LuaCheckStatus`
(called at 10 sites, no identity check), 0x00462070, 0x00583500, 0x00614020,
0x005F0930, 0x00417F60, 0x00681A00, 0x0060F320, 0x0047C070, 0x0094F4B0,
0x00869810 (IAT slot read and called), 0x00681585, 0x00682AA0, 0x00682EA7,
0x0049325F, 0x0049405B, 0x004634A5, 0x00463593, 0x00463670, 0x00463A46,
0x00463B34, 0x00463C11, 0x0088A6EC, 0x0088A5C0, 0x0088AB9C, 0x0088B178,
0x0088AF98, 0x00887A64, 0x0091812B, 0x00918324, 0x00918320, 0x00918328,
0x009454EC, 0x00945549, 0x0094557C, the 8 HUD coordinate globals
0x0091826C..0x009182A0, 0x025F8E4C, and 0x005D4690 (present in
`profiles/bzr_2.2.301.json` as an anchor, not in `exu.json`). Of the 26 CALLED
literals, **9 are absent** (0x00417F60, 0x00462070, 0x0047C070, 0x004FF600,
0x00583500, 0x005F0930, 0x00614020, 0x00681A00, 0x00869810). Of the 48 WRITTEN,
**47 are absent**; only 0x02CD94E4 `timeOfDay` is catalogued.

**`exu.json` entries whose address appears nowhere in `src/`: 0 of 75**. The 14
OgreMain `rva` entries all match `bzr.h:368-381`, but 6 of them are unused
constants (§3).

Full per-literal table (`file:line | address | use | in exu.json`):

| file:line | address | use | in exu.json |
|---|---|---|---|
| `src/Game/Environment.cpp:5081` | 0x00869810 | READ IAT slot + BYTE-COMPARED (disp) then CALLED through | no |
| `src/Game/Environment.cpp:5083` | 0x00681585 | WRITTEN disp32 of FF15 (leaked InlinePatch; guarded: FF 15 + disp==IAT) | no |
| `src/Game/Environment.cpp:5084` | 0x00682AA0 | WRITTEN disp32 of FF15 (leaked InlinePatch; guarded) | no |
| `src/Game/Environment.cpp:5085` | 0x00682EA7 | WRITTEN disp32 of FF15 (leaked InlinePatch; guarded) | no |
| `src/Game/Environment.h:158` | 0x00683370 | WRITTEN code (InlinePatch RET, no preimage) | no |
| `src/Game/Multiplayer.h:31` | 0x005C833D | WRITTEN code (InlinePatch, no preimage) | no |
| `src/Game/Multiplayer.h:32` | 0x005C833B | WRITTEN code (InlinePatch, no preimage) | no |
| `src/Game/Multiplayer.h:36` | 0x0056F014 | WRITTEN code (InlinePatch, no preimage) | no |
| `src/Game/game_state.cpp:12` | 0x00945549 | READ direct (pause/UI state, SEH) | no |
| `src/Game/game_state.cpp:13` | 0x0094557C | READ direct (pause/UI state, SEH) | no |
| `src/Game/game_state.cpp:14` | 0x009454EC | READ direct (pause/UI state, SEH) | no |
| `src/Game/game_state.cpp:16` | 0x00918320 | READ direct (pause/UI state, SEH) | no |
| `src/Game/game_state.cpp:17` | 0x00918310 | READ direct (pause/UI state, SEH) | yes |
| `src/Game/game_state.cpp:18` | 0x00918324 | READ direct (pause/UI state, SEH) | no |
| `src/Game/game_state.cpp:19` | 0x00918328 | READ direct (pause/UI state, SEH) | no |
| `src/LuaHelpers.h:51` | 0x004FF600 | CALLED (LuaCheckStatus, 10 sites, no identity check) | no |
| `src/Patches/AddScrapCallback.cpp:87` | 0x005E1016 | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/AiTargetSelect.cpp:56` | 0x00583500 | CALLED (original impl) + COMPARED (vtable slot value) | no |
| `src/Patches/AiTargetSelect.cpp:57` | 0x00614020 | CALLED (original impl) + COMPARED (vtable slot value) | no |
| `src/Patches/AiTargetSelect.cpp:62` | 0x00462070 | CALLED (VectorMagnitude) + BYTE-COMPARED (call target) | no |
| `src/Patches/AiTargetSelect.cpp:77` | 0x004634A5, 0x00463593, 0x00463670 | WRITTEN rel32 call (raw VirtualProtect; guarded: E8 + target) | no |
| `src/Patches/AiTargetSelect.cpp:78` | 0x00463A46, 0x00463B34, 0x00463C11 | WRITTEN rel32 call (raw VirtualProtect; guarded: E8 + target) | no |
| `src/Patches/AiTargetSelect.cpp:95-99` | 0x0088A6EC, 0x0088A5C0, 0x0088AB9C, 0x0088B178, 0x0088AF98 | WRITTEN vtable slot +0xE4 (raw VirtualProtect; guarded: RTTI + slot value) | no |
| `src/Patches/BulletHitCallback.cpp:168` | 0x00480771 | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/BulletInitCallback.cpp:128` | 0x00480363 | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/GlobalTurbo.h:34` | 0x00601CA3 | WRITTEN code (InlinePatch, no preimage) | no |
| `src/Patches/GlobalTurbo.h:39` | 0x00601CB5 | WRITTEN code (InlinePatch, no preimage) | no |
| `src/Patches/GlobalTurbo.h:41` | 0x00601C92 | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/GlobalTurbo.h:42` | 0x00601CCD | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/KillMessages.cpp:88` | 0x0062627F | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/OrdnanceVelocity.cpp:163` | 0x004803D4 | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/OrdnanceVelocity.cpp:281` | 0x0048F658 | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/OrdnanceVelocity.cpp:299` | 0x0056B254 | WRITTEN code (Hook FF15, no preimage) | no |
| `src/Patches/OrdnanceVelocity.cpp:302` | 0x0048F639 | WRITTEN code (InlinePatch, no preimage) | no |
| `src/Patches/ShotConvergence.h:30` | 0x0088A4FC | WRITTEN vtable slot (InlinePatch 4 bytes, no preimage) | no |
| `src/Patches/ShotConvergence.h:31` | 0x00889418 | WRITTEN vtable slot (InlinePatch 4 bytes, no preimage) | no |
| `src/Patches/ShotConvergence.h:32` | 0x005F0930 | CALLED (no identity check) | no |
| `src/Patches/ShotConvergence.h:33` | 0x0060F320 | WRITTEN as value into vtable slot 0x0088A4FC | no |
| `src/Patches/ShotConvergence.h:35` | 0x00417F60 | CALLED (no identity check) | no |
| `src/Patches/ShotConvergence.h:36` | 0x00681A00 | CALLED (no identity check) | no |
| `src/UI/ControlPanel.cpp:36-41` | 0x005C6FF0, 0x005C712B, 0x005C719B, 0x005C72F1, 0x005C7361 | WRITTEN code (Hook FF15, no preimage) | no |
| `src/UI/ControlPanel.cpp:89` | 0x0047C070 | CALLED from hook asm (HUD palette selector) | no |
| `src/UI/ControlPanel.cpp:90` | 0x0094F4B0 | READ (`this` for palette selector call) | no |
| `src/UI/ControlPanel.cpp:92-95` | 0x0091829C, 0x009182A0, 0x0091826C, 0x00918270, 0x00918280, 0x00918284, 0x00918278, 0x0091827C | READ+WRITTEN direct (HUD text coordinates) | no |
| `src/UI/Overlay.cpp:99` | 0x005D4690 | ANCHOR (MatchBytes at expected VA; profile anchor) | no (profiles only) |
| `src/UI/Overlay.cpp:118` | 0x005D42E0 | ANCHOR (MatchBytes at expected VA; profile anchor) | yes |
| `src/UI/Overlay.cpp:1392` | 0x00887A64 | PUSHED constant inside hook asm | no |
| `src/UI/Overlay.cpp:1401` | 0x0091812B | WRITTEN data from hook asm (replays stock store) | no |
| `src/UI/Overlay.cpp:1418,1435` | 0x00918324 | WRITTEN data from hook asm (replays stock store) | no |
| `src/UI/Radar.cpp:238` | 0x0049325F, 0x0049405B | WRITTEN rel32 call (leaked InlinePatch; guarded: E8 + target) | no |
| `src/UI/Radar.cpp:239` | 0x00492EC0 | BYTE-COMPARED (call target) | yes |
| `src/bzr.h:137` | 0x008EAAE0 | READ via Scanner (`mainCam`) + direct (`Radar.cpp:400`) | yes |
| `src/bzr.h:139-142` | 0x008EAD10, 0x008EAB10, 0x008A2688, 0x008A25FC | READ+WRITTEN via Scanner (Restore::ENABLED) | yes |
| `src/bzr.h:143` | 0x008EABE0 | UNUSED | yes |
| `src/bzr.h:167` | 0x02CECEA0 | READ via Scanner | yes |
| `src/bzr.h:170,173,176` | 0x0061D120, 0x008203F0, 0x00440300 | CALLED (fn pointer, no identity check) | yes |
| `src/bzr.h:180` | 0x009454B8 | UNUSED | yes |
| `src/bzr.h:181,182,183` | 0x004A7709, 0x005E10D7, 0x0060A8C6 | WRITTEN code (Hook FF15 via `bzr.h` constant, no preimage) | no |
| `src/bzr.h:189` | 0x00978E20 | READ (`this` for SelectX calls) | yes |
| `src/bzr.h:192,195,198` | 0x004A6CD0, 0x004A6D50, 0x004A6C70 | CALLED (no identity check) | yes |
| `src/bzr.h:203` | 0x00871A80 | READ+WRITTEN via Scanner (Restore::ENABLED) | yes |
| `src/bzr.h:204` | 0x02CD94E4 | WRITTEN direct (SetTimeOfDay, SEH) | yes |
| `src/bzr.h:205` | 0x02CEB830 | UNUSED | yes |
| `src/bzr.h:208,211` | 0x0068A230, 0x0067E0E0 | CALLED (no identity check) | yes |
| `src/bzr.h:220` | 0x025F8E4C | READ direct (world render origin; 2 sites) | no |
| `src/bzr.h:242,251` | 0x00462380, 0x004DB930 | CALLED (no identity check) | yes |
| `src/bzr.h:247` | 0x0260DB20 | READ (GetObj pool base arithmetic; 32 call sites) | yes |
| `src/bzr.h:255` | 0x00917AFC | READ direct (user object) | yes |
| `src/bzr.h:258` | 0x00920C78 | READ via Scanner + direct (`Radar.cpp:338`) | yes |
| `src/bzr.h:336,337,342,347` | 0x009183B8, 0x008E77A8, 0x00917F7B, 0x009180D4 | READ via Scanner | yes |
| `src/bzr.h:345,349` | 0x008E8D04, 0x02A17494 | READ+WRITTEN via Scanner (Restore::ENABLED) | yes |
| `src/bzr.h:353` | 0x006260F0 | CALLED (no identity check) | yes |
| `src/bzr.h:384,386` | 0x00920CA0, 0x00920EA0 | READ via Scanner | yes |
| `src/bzr.h:428` | 0x009C915C | READ (VectorSpider over ordnance class list) | yes |
| `src/bzr.h:429` | 0x00586FF0 | CALLED (no identity check) | yes |
| `src/bzr.h:470` | 0x008A2858 | READ+WRITTEN via Scanner (Restore::ENABLED) | yes |
| `src/bzr.h:476,496` | 0x0094672C | READ via Scanner (pointer chain) | yes |
| `src/bzr.h:479` | 0x025CFA1C | READ+WRITTEN via Scanner (Restore::ENABLED) | yes |
| `src/bzr.h:484-487` | 0x008E8F9C, 0x009C9194, 0x009C91B4, 0x009C9188 | READ via Scanner (`const`, but Restore::ENABLED; H-11) | yes |
| `src/bzr.h:488-491` | 0x009C91D0, 0x00872400, 0x008723F4, 0x009C91B0 | READ+WRITTEN via Scanner (Restore::ENABLED) | yes |
| `src/bzr.h:497` | 0x00915594 | UNUSED (only commented-out users) | yes |
| `src/bzr.h:505` | 0x0260B1D0 | READ via Scanner (`const`, Restore::ENABLED) | yes |
| `src/bzr.h:510,511` | 0x008EAAAC, 0x008E77B0 | READ+WRITTEN via Scanner (Restore::ENABLED) | yes |
| `src/bzr.h:512,521,522,516` | 0x008E7754, 0x008E7924, 0x008E7928, 0x008E77AC | READ+WRITTEN direct (Radar) | yes |
| `src/bzr.h:513,514,517,523-526` | 0x009173C0, 0x009782A0, 0x008E7918, 0x00917388, 0x0091738C, 0x00917390, 0x00917394 | UNUSED | yes |
| `src/bzr.h:543,547,550,553` | 0x00404CF0, 0x00492EC0, 0x00460FC0, 0x0046AF20 | CALLED (no identity check) | yes |
| `src/bzr.h:558,559,561,562` | 0x025CE714, 0x025CE79C, 0x00979F40, 0x025CE6F8 | READ via Scanner (`const`, Restore::ENABLED; H-11) | yes |
| `src/bzr.h:560` | 0x00886B20 | READ+WRITTEN via Scanner (Restore::ENABLED) | yes |

## 5. Lifetime / ownership notes (Sweep 6)

exu.dll is unloaded at every `lua_close` (see method). So "process lifetime" in
`ARCHITECTURE.md` §Lifetimes is, for EXU, Lua-state lifetime plus a short tail.
The sentinel `__gc` runs `HandleLuaStateClosing`
(`PublicAPI.cpp:122-146`); other finalisers and then `FreeLibrary` follow. State
not cleared in `HandleLuaStateClosing` is still cleared by DLL unload. The
questions are what native code touches it in that tail, and what the static
destructors do.

Statics holding Lua state, registry refs, Ogre/game pointers, or keyed
containers (namespace scope and function-local), and where each is reset:

| state | file:line | kind | reset at `HandleLuaStateClosing`? |
|---|---|---|---|
| `Lua::state` | `LuaState.h:88` | `lua_State*` + generation | yes (`state.Clear`) |
| `g_sanitizedStockStringFunctions[].originalRef`, `g_originalSetObjectiveOn/OffRef`, `g_activeObjectiveHandles` | `luaexport.cpp:55-65` | registry refs, handle vector | yes (`ReleaseLuaStateBindings`) |
| `g_ownerState`, `g_replacements` (key handle+cmd, holds `callbackRef`), `g_huntLabelPointer`/stock label | `CommandReplacement.cpp:125-135` | `lua_State*`, registry refs, pointer into exe `.data` | yes (`ReleaseState` + `RestoreStockHuntLabel`) |
| 26 static patch objects + dynamic `new InlinePatch` (Radar 2, viewport 3) + `unique_ptr<Hook>` (Overlay 4, Wingman 1) | various | code patches | yes (`UnloadAllPatches`; Overlay hooks destroyed in `ShutdownOverlaySupport`) |
| `overlaySystemInstance`, `attachedOverlaySceneManagers`, `knownElements`, `overlayVisibilityStates` | `Overlay.cpp:58-98` | Ogre pointers, name-keyed maps | yes (`ShutdownOverlaySupport`) |
| `g_records` | `StaticGeometry.cpp:90` | name → Ogre `StaticGeometry*` + owner | yes (`Shutdown`). Stale-owner test is a pointer compare (ABA if a new manager reuses the address). |
| **`AiTargetSelect` 5 vtable slots, 6 call sites, `dispatchEnabled`, `scoreDispatchEnabled`** | `AiTargetSelect.cpp:77-108,598-633`, `.h:36,41` | raw code patches outside `BasicPatch` | **no**. Only the static destructor `g_slotRestoreGuard` at `FreeLibrary` restores them. In the tail the hooks read `Lua::state == nullptr` and pass through, so this is safe today. |
| **`g_cachedMaterials`** | `GameObject.cpp:268` | name → `Ogre::SharedPtr<Material>` | **no**; never pruned. Released by static destruction; pins materials for the mission. |
| **`g_initializedSceneManager/TerrainMasterLight`, `g_ogreInitialized`, `g_particleCameraFollowers`** | `Environment.cpp:5769-5771,1890` | Ogre pointers, names | **no**; reset in `Init` (`ResetOgreInitialization`) and by unload |
| **`Environment::fog`** | `Environment.h:30` | Ogre pointer captured at load | **no** (H-10) |
| **`setTurboUnits`** | `GlobalTurbo.h:46` | handle → bool | **no**; never pruned (H-7) |
| **`teamEngineFlameColors`**, **`messageMap`** | `EngineFlameColor.cpp:38`, `KillMessages.cpp:29` | team-keyed | **no**; unload only (bounded by team count) |
| **`unitVoAlternates`**, **`aiUnitTuning`** | `UnitVo.h:36,56` | name- / handle-keyed | **no**; `aiUnitTuning` cleared by `ClearAllAiUnitTuning`/`ResetMissionHookOverrides`, `unitVoAlternates` only by unload |
| **25 `Scanner` globals with `Restore::ENABLED`** (`Camera.h:30-33`, `Environment.h:29`, `Multiplayer.h:30,34`, `Ordnance.h:32`, `Satellite.h:28-35`, `Steam.h:28`, `Radar.h:28-29`, `Reticle.h:30-33`, `PlayOption.h:29,31`) | | game data pointers | **no**; they *write* at static destruction (H-11) |
| `g_commandMenuRects` | `ControlPanel.cpp:107` | pointer into exe data found by scan | no (stable address) |
| Radar baselines (`stockProjectionBase`, `baselines`, `s_refreshOrdinal`) | `Radar.cpp:85,115,216` | captured engine values | no (per DLL instance) |
| ~40 function-local cached OpenShim/Ogre `GetProcAddress` results (`UnitVo.cpp:669-1001`, `ControlPanel.cpp:388-519`, `Overlay.cpp:1728-2086`, `OgreMaterialShim.h:84-354`, `Environment.cpp:892-2100`, `StaticGeometry.cpp:114-198`, `OgreNativeFontBridge.cpp:82-276`) | | fn pointers into `winmm.dll`/OgreMain | n/a; target modules outlive exu.dll |

Items with **no reset at Lua-state close**: AiTargetSelect patches and flags,
`g_cachedMaterials`, the Environment Ogre binding and `fog`, `setTurboUnits`,
`teamEngineFlameColors`, `messageMap`, `unitVoAlternates` (and `aiUnitTuning`
unless reset by script), and the 25 restoring Scanners. None is reachable from
native code after close today; all depend on `FreeLibrary` actually happening.
If anything ever pins exu.dll (an OpenShim `LoadLibrary`, a second `require` from
another state), they all become cross-mission leaks. Resetting them explicitly in
`HandleLuaStateClosing` would make that assumption unnecessary.

`inline` initialisation-order check: every cross-variable dependency (`fog` →
`Ogre::sceneManager`, `Ogre.h` fn pointer → its `*Addr`, `controlPanel` →
`p_controlPanel`, UnitVo/Wingman hooks → their `*Initialized` resolver,
`BasicPatch::deferredPatches`, which is constant-initialised) is defined earlier
in every TU that defines the dependent, so no static-init-order bug was found.

## 6. Performance notes (Sweep 7)

Census of 775 syscall/cost sites in `src/`. The class comes from the call graph
from 33 hot roots (native hook callbacks, the 3 OpenShim-called exports, and the
two Lua functions designed to run per Update), then from registered Lua
functions:

| category | HOT | per-Lua-call | one-shot/other | static-init | cached(static) |
|---|---|---|---|---|---|
| `LogMessage` | 16 | 143 | 29 | 1 | – |
| other log wrappers (`LogEnvironmentDebug` 159 sites, `LogMaterialDebug`, `LogNativeOverlayMessage`, `LogNativeSave`) | 13 | 353 | 5 | 5 | – |
| `Scanner::Read`/`Write` (each a `VirtualQuery`) | 4 | 56 | – | – | – |
| `VirtualQuery` direct or via `IsReadableRange`/`MatchBytes` | 8 | 16 | 9 | 6 | – |
| `VirtualProtect` | 8 | 6 | 2 | – | – |
| `GetModuleHandleA/W` | 1 | 12 | 7 | – | 5 |
| `GetProcAddress` | – | 12 | 20 | – | – |
| `OpenShimBridge::Resolve`/`HasExport` (both) | – | 37 uncached call sites | 4 | 1 | 24 |
| `fopen`/`CreateFile`/`CreateDirectory`/`GetModuleFileName` | 5 | 9 | – | – | – |
| `GetEnvironmentVariableA` | – | 2 (`StorageApi.h:203,211`) | – | – | – |
| Ogre `getByName` | 1 | 4 | – | – | – |
| `std::string` from `const char*` in bindings | – | ≈120 (every `luaL_checkstring` → `std::string`) | – | – | – |

Top-20 hot-path sites, most expensive first (cost reasoned from code, not measured):

1. `GlobalTurbo.cpp:56-80` `DoSelectiveTurboPatch`, **per unit per tick**: 8 `VirtualProtect` + 4 `FlushInstructionCache` + 2 `VirtualQuery`/SEH per differing unit (H-7).
2. `Environment.cpp:3212-3580` `SetSun*`/`SetTimeOfDay` setters, **per frame via `Workshop/exu_weather.lua`**: 3–6 file open/append/close + 2× `OutputDebugStringA` + `lua_getinfo` per call (H-6).
3. `Logging.h:91-120` `LogMessage`, on 16 hot-root paths (Overlay transitions, Radar layout, viewport scheme hook): 2× `GetModuleFileNameA`, 2× `CreateDirectoryA`, `fopen`/`fclose`, about 8 heap allocations per line.
4. `BulletInitCallback.cpp:28-70`, **per ordnance spawn**: 2 Lua table lookups always. With a handler: `PushMatrix` (global lookup + 12 pushes + `lua_call` allocating a matrix userdata) + `lua_pcall` + `strlen`/`strncpy`.
5. `BulletHitCallback.cpp:30-94`, **per ordnance hit**: same as 4.
6. `AiTargetSelect.cpp:113-190` `DispatchScore`, **per candidate per AI target search** when scoring is on: 2 SEH-guarded `GetHandle` + `lua_getglobal`/`getfield` + `lua_pcall` per candidate.
7. `AiTargetSelect.cpp:496-580` `Dispatch`, **per AI target search** when enabled: SEH owner/handle reads + `lua_pcall`.
8. `CommandReplacement.cpp:698-730, 405-439` `UpdateCommandReplacements`, **per frame**: a `TryIsSelected` `lua_pcall` per replacement; with a Hunt replacement selected, `WriteHuntLabelPointer` does 2× `VirtualProtect` every frame.
9. `Environment.cpp:4260+` `UpdateParticleFollowers`, **per frame** (`exu_weather.lua:644`): `TryInitializeOgre` (2 `Scanner::Read` → 2 `VirtualQuery`) + viewport lookup through Root singletons under SEH + by-name particle lookups (`std::string`) per follower.
10. `Environment.cpp:4356-4530, 4572-4600` emitter/affector setters, **per emitter per frame** in `exu_weather.lua:392-426`: 1–3 `std::string`s + by-name `getParticleSystem` + `StringInterface::setParameter` (string parse inside Ogre).
11. `Culling.cpp:13-32` via `EXU_UpdateCullingForUnit`/`DoSelectiveTurboPatch`, **per unit per tick** when culling is on: raw-RVA `setVisible` every tick with no change test (H-4).
12. `ShotConvergence.cpp:151` `HoverCraftUpdateWeaponAimForReticle`, **per hovercraft per tick** when enabled: 3 unverified native calls (`0x005F0930`, `0x00417F60`, `0x00681A00`) plus matrix math.
13. `UnitVo.cpp:551-570` `QueueUnitVo`, **per VO event**: 2 `NormalizeFilename` `std::string`s + `std::mutex` + map lookup + queue walk.
14. `Overlay.cpp:1332-1370` pause/shell wrapper hooks, **per UI transition**: `SyncOverlayVisibilityState` does `getByName` per tracked overlay plus 2–4 `LogMessage` lines.
15. `Scanner.h:153-173` `Read`/`Write`, 56 per-Lua-call sites: 1 `VirtualQuery` each.
16. `OpenShimBridge.h:16-27` `Resolve`/`HasExport` at 37 uncached call sites (24 more are cached in function statics) (e.g. `GlobalTurbo.cpp:151,174,201,227`, `RenderProfileBridge.h:63` per call, `AnimationApi.h:287` per op): `GetModuleHandleA` (loader lock) + `GetProcAddress` per Lua call.
17. `ControlPanel.cpp:766-790` `ScrapPilotHudDrawHook`, **per frame**: native palette call + `RefreshScrapPilotHudFallback` (magic-static guards; cheap).
18. `Radar.cpp:160-225` `RefreshLayoutConcentric`, per HUD rebuild: 1–2 diagnostic `LogMessage` lines ("misn04 -> misn03" investigation left on).
19. `Environment.cpp:5093-5140` `GameViewportSetMaterialSchemeHook`, about 1 Hz per viewport: string compare + deduplicated log (fine).
20. `BuildValidation.h:208-222` `IsSupportedBzr2301`, per mission load (from `EnableDeferredPatchActivation`), plus the two static-init `.text` scans (H-9): whole-section byte scans, one-shot per mission.

## 7. Patterns worth keeping

- `CommandReplacement.cpp:441-560`: native-context Lua calls done right. Registry refs, `lua_isfunction` checks, `lua_pcall`, `StackGuard`, owner-state tracking, and release in both `ResetState` and `ReleaseState`.
- `OgreNativeFontBridge.cpp:125-129` `HandleNativeOverlayException`: an SEH filter that lets C++ exceptions continue to a real `catch`. Copy it into every `__except`.
- `StaticGeometry.cpp:409-435` `DestroyRecord`: C++ `try/catch` around an Ogre destroy, with a stale-owner check.
- `AiTargetSelect.cpp:647-700`: dynamic patch with RTTI-name + expected-slot-value identity, a count of installed slots, and a static-destructor restore guard.
- `Environment.cpp:5607-5646` and `Radar.cpp:260-280`: call-site retargeting verifies opcode + current target before writing.
- `OpenShimBridge.h` + `GlobalTurbo.cpp:34-42`: "export presence is the ownership contract". All 65 OpenShim export names EXU resolves are present in `BZR-OpenShim/src/winmm.def` (checked against OpenShim `45ca0b65`, local branch `agent/tuggable-thiscall-hook`), and every call site null-checks.
- `UnitVo.cpp:1050-1215`: all Lua argument checks happen before `std::lock_guard` is taken. The model for H-8.

## 8. Low-severity items

- `BulletHitCallback.cpp:47-51`, `BulletInitCallback.cpp:44-48`: `strncpy(formattedODF /*char[16]*/, odf, len - 4)`, with no check that `4 <= len <= 19`; per bullet. [Low/Med]
- `bzr.h:309-318` `GetOgreEntity` dereferences `[this+0xF0]` without a null check (per unit per tick in culling). [Low/Med]
- `BasicPatch.h:226-231` + `Hook.h:95-112`/`InlinePatch.h`: a patch constructed after activation is enabled starts with `m_status = ACTIVE` *before* `DoPatch`. A `VirtualProtect` failure leaves `IsActive()` true and `Reload()` never retries. Affects the dynamic Overlay, Wingman, Radar and viewport patches. [Low/Med]
- `AiTargetSelect.cpp:258-420` and `CommandReplacement.cpp:405-423` write code/data with raw `VirtualProtect` outside `BasicPatch`, so they are not gated by `IsSupportedBzr2301`/`patchActivationEnabled` and not unloaded by `UnloadAllPatches` (AiTargetSelect relies on a static destructor). [Low/High]
- `UnitVo.cpp:233-262` `ResolveCallSite` takes the **first** match of its signature, not the unique one; `SignatureResolver::FindUniqueMaskedPattern` exists and is unused. [Low/High]
- `Camera.cpp:146,200,246,292` and `GameObject.cpp:4804,4853`: `lua_push*` calls inside `__try(EXCEPTION_EXECUTE_HANDLER)`. An AV inside the Lua VM would be swallowed, leaving the VM inconsistent. [Low/Med]
- `ExtraUtilities.vcxproj:170,183`: `src\Util\OS.h` and `src\Util\Vec3.h` are listed as `ClCompile`. [Low/High]
- `src/Ogre/OgreBuildSettings.h` is included by nothing explicitly. Ogre's `OgreConfig.h:35` finds it only through MSVC's "directory of the including file" rule, because the only TU that includes real Ogre headers (`OgreNativeFontBridge.cpp`) lives in `src/Ogre/`. A TU elsewhere that includes an Ogre header would stop compiling. [Low/High]
- `Culling.cpp:1` copyright header says 2023-2025; everything else 2023-2026. [Low/High]
- `luaexport.cpp:121-126`: `luaL_error(L, errorMessage)` uses a non-literal format string (safe today; no `%` in callers). [Low/High]
- `ShutdownOverlaySupport`, `StaticGeometry::Shutdown` and `ReleaseLuaStateBindings` are `noexcept` and allocate (`LogMessage`). `HandleLuaStateClosing`'s `try/catch` cannot catch a `bad_alloc` escaping a `noexcept` callee (it terminates). [Low/Low]
- `Environment.cpp:2183` and `StaticGeometry.cpp:216-225` read the world render origin raw instead of going through `OgreRenderSpace.h`, which `ARCHITECTURE.md` makes mandatory for Ogre-facing positions. [Low/Med]
- Only one `todo` marker in `src/` (`bzr.h:109`); no `FIXME`/`HACK`/`XXX`/`#if 0` (§13).

## 9. Sweep 3: longjmp safety (full list, 90 functions: 79 registered bindings + 11 helpers)

Format: function; *object@declared-line → first raising call@line (+ further
raising calls while it is live)*. Raising set = direct `luaL_error`,
`luaL_check*`, `luaL_opt*`, `luaL_typerror`, `luaL_argerror`, `luaL_argcheck`,
`lua_error`, `lua_call`, `luaL_checkstack`, closed transitively over 332
functions (keyed by name + "takes `L`" to avoid C++/Lua name collisions). **Zero
sites hold a lock, a `ScopedPatchDisable` or a `FILE*`**. Every object is a
`std::string` or `std::vector`, so the impact is leak-class (H-8).

- `src/Game/AnimationApi.h:337` Play: std::string name@340 -> ReadPlayOptions@341
- `src/Game/AnimationApi.h:365` Stop: std::string name@368 -> CheckBool@369
- `src/Game/AnimationApi.h:405` SetEnabled: std::string name@408 -> CheckBool@409
- `src/Game/AnimationApi.h:422` SetLoop: std::string name@425 -> CheckBool@426
- `src/Game/AnimationApi.h:439` SetWeight: std::string name@442 -> luaL_checknumber@443 (+1)
- `src/Game/AnimationApi.h:460` Seek: std::string name@463 -> luaL_checknumber@464 (+1)
- `src/Game/Environment.cpp:207` TryParseLightingModeArg (helper): std::string rawMode@233 -> luaL_error@249
- `src/Game/Environment.cpp:3693` SetSkyBox: std::string materialName@3704 -> luaL_optnumber@3705 (+1)
- `src/Game/Environment.cpp:3743` SetSkyDome: std::string materialName@3754 -> luaL_optnumber@3755 (+6)
- `src/Game/Environment.cpp:3808` SetSkyPlane: std::string materialName@3819 -> TryReadSkyPlane@3821 (+6)
- `src/Game/Environment.cpp:3860` CreateParticleSystem: std::string name@3871 -> luaL_checkstring@3872 (+2); std::string templateName@3872 -> CheckVectorOrSingles@3876 (+1)
- `src/Game/Environment.cpp:3911` SetParticleSystemPosition: std::string name@3922 -> CheckVectorOrSingles@3923 (+1)
- `src/Game/Environment.cpp:3940` SetParticleSystemDirection: std::string name@3951 -> CheckVectorOrSingles@3952 (+1)
- `src/Game/Environment.cpp:3963` SetParticleSystemEmitting: std::string name@3974 -> CheckBool@3975
- `src/Game/Environment.cpp:3980` SetParticleSystemVisible: std::string name@3991 -> CheckBool@3992
- `src/Game/Environment.cpp:3997` SetParticleSystemSpeedFactor: std::string name@4008 -> luaL_checknumber@4009 (+1)
- `src/Game/Environment.cpp:4019` SetParticleSystemKeepLocalSpace: std::string name@4030 -> CheckBool@4031
- `src/Game/Environment.cpp:4036` SetParticleSystemMaterial: std::string name@4047 -> luaL_checkstring@4048 (+1); std::string materialName@4048 -> CheckOptionalParticleResourceGroup@4049
- `src/Game/Environment.cpp:4054` SetParticleSystemRenderQueueGroup: std::string name@4065 -> luaL_checkinteger@4066 (+1)
- `src/Game/Environment.cpp:4076` SetParticleSystemParticleQuota: std::string name@4087 -> luaL_checkinteger@4088 (+1)
- `src/Game/Environment.cpp:4098` SetParticleSystemDefaultDimensions: std::string name@4109 -> luaL_checknumber@4110 (+3)
- `src/Game/Environment.cpp:4126` AttachParticleSystemToCamera: std::string name@4137 -> CheckVectorOrSingles@4141 (+1)
- `src/Game/Environment.cpp:4172` AttachParticleSystemToObject: std::string name@4183 -> CheckHandle@4184 (+2)
- `src/Game/Environment.cpp:4207` AttachParticleSystemToBone: std::string name@4218 -> CheckHandle@4219 (+3); std::string boneName@4220 -> CheckVectorOrSingles@4224 (+1)
- `src/Game/Environment.cpp:4314` GetParticleEmitterEmissionRate: std::string name@4325 -> luaL_checkinteger@4326
- `src/Game/Environment.cpp:4338` SetParticleEmitterEnabled: std::string name@4349 -> luaL_checkinteger@4350 (+1)
- `src/Game/Environment.cpp:4356` SetParticleEmitterEmissionRate: std::string name@4367 -> luaL_checkinteger@4368 (+2)
- `src/Game/Environment.cpp:4379` SetParticleEmitterDirection: std::string name@4390 -> luaL_checkinteger@4391 (+2)
- `src/Game/Environment.cpp:4402` SetParticleEmitterPosition: std::string name@4413 -> luaL_checkinteger@4414 (+2)
- `src/Game/Environment.cpp:4425` SetParticleEmitterVelocity: std::string name@4436 -> luaL_checkinteger@4437 (+4)
- `src/Game/Environment.cpp:4453` SetParticleEmitterAngle: std::string name@4464 -> luaL_checkinteger@4465 (+2)
- `src/Game/Environment.cpp:4478` SetParticleEmitterTimeToLive: std::string name@4489 -> luaL_checkinteger@4490 (+4)
- `src/Game/Environment.cpp:4506` SetParticleEmitterColor: std::string name@4517 -> luaL_checkinteger@4518 (+4)
- `src/Game/Environment.cpp:4541` CheckParameterValue (helper): std::string text@4550 -> luaL_argerror@4553
- `src/Game/Environment.cpp:4572` GenericSetParameter (helper): std::string name@4583 -> luaL_checkinteger@4584 (+4); std::string parameter@4585 -> luaL_argerror@4588 (+2); std::string value@4591 -> luaL_argerror@4594
- `src/Game/Environment.cpp:4602` GenericGetParameter (helper): std::string name@4613 -> luaL_checkinteger@4614 (+2); std::string parameter@4615 -> luaL_argerror@4618
- `src/Game/Environment.cpp:4633` GenericGetType (helper): std::string name@4644 -> luaL_checkinteger@4645
- `src/Game/Environment.cpp:4658` GenericGetParameterNames (helper): std::string name@4669 -> luaL_checkinteger@4670
- `src/Game/Environment.cpp:4761` SetParticleSystemNonVisibleUpdateTimeout: std::string name@4772 -> luaL_checknumber@4773 (+1)
- `src/Game/Environment.cpp:5426` TryParseRenderProfileArg (helper): std::string raw@5433 -> luaL_error@5454
- `src/Game/GameObject.cpp:3449` IsCommTowerPowered: std::string classLabel@3457 -> luaL_error@3461
- `src/Game/GameObject.cpp:3567` SetMaterialName: std::string materialName@3570 -> GetSubEntity@3579 (+2)
- `src/Game/GameObject.cpp:3590` SetEntityMaterial: std::string materialName@3593 -> CheckOptionalResourceGroup@3600
- `src/Game/GameObject.cpp:3605` SetSubEntityMaterial: std::string materialName@3609 -> GetSubEntity@3616 (+1)
- `src/Game/GameObject.cpp:3622` MaterialExists: std::string materialName@3624 -> CheckOptionalResourceGroup@3625
- `src/Game/GameObject.cpp:3631` CloneMaterial: std::string sourceMaterial@3633 -> luaL_checkstring@3634 (+1); std::string cloneName@3634 -> CheckOptionalResourceGroup@3635
- `src/Game/GameObject.cpp:3650` GetMaterialPassColors: std::string materialName@3652 -> luaL_optint@3653 (+2)
- `src/Game/GameObject.cpp:3690` SetMaterialTexture: std::string materialName@3692 -> luaL_checkstring@3693 (+4); std::string textureName@3693 -> luaL_optint@3694 (+3)
- `src/Game/GameObject.cpp:3730` SetMaterialTextureScroll: std::string materialName@3732 -> luaL_checknumber@3733 (+5)
- `src/Game/GameObject.cpp:3769` SetMaterialTextureRotate: std::string materialName@3771 -> luaL_checknumber@3772 (+4)
- `src/Game/GameObject.cpp:3807` SetMaterialTextureScrollAnimation: std::string materialName@3809 -> luaL_checknumber@3810 (+5)
- `src/Game/GameObject.cpp:3846` SetMaterialTextureRotateAnimation: std::string materialName@3848 -> luaL_checknumber@3849 (+4)
- `src/Game/GameObject.cpp:3884` GetTerrainMaterialName: std::string trnFilename@3886 -> luaL_checkstring@3889
- `src/Game/GameObject.cpp:3903` SetTerrainTextureSet: std::string materialName@3907 -> TryReadOptionalStringField@3908 (+9); std::string trnFilename@3913 -> TryReadOptionalStringField@3914 (+1); std::string resourceGroup@3925 -> TryReadOptionalStringField@3926 (+5); std::vector updates@3940 -> TryReadTerrainTextureField@3946; std::string textureName@3945 -> TryReadTerrainTextureField@3946
- `src/Game/GameObject.cpp:4002` SetMaterialPassColors: std::string materialName@4004 -> luaL_checktype@4005 (+9); std::string resourceGroup@4008 -> ReadOptionalPassColorField@4042 (+5)
- `src/Game/GameObject.cpp:4587` SetEntityAnimationEnabled: std::string animationName@4590 -> CheckBool@4591
- `src/Game/GameObject.cpp:4605` SetEntityAnimationLoop: std::string animationName@4608 -> CheckBool@4609
- `src/Game/GameObject.cpp:4623` SetEntityAnimationWeight: std::string animationName@4626 -> luaL_checknumber@4627 (+1)
- `src/Game/GameObject.cpp:4646` SetEntityAnimationTime: std::string animationName@4649 -> luaL_checknumber@4650 (+1)
- `src/Game/GameObject.cpp:5339` GetAiRecycleTaskState: std::string subtaskRawTypeName@5389 -> PushVectorField@5397 (+3)
- `src/Game/Ordnance.cpp:28` BuildOrdnance: std::string requestedOrd@44 -> luaL_argerror@47 (+2)
- `src/Game/RenderEffects.cpp:39` CheckEffectId (helper): std::string name@41 -> luaL_argerror@45
- `src/Game/RenderEffects.cpp:50` CheckParameterId (helper): std::string name@52 -> luaL_argerror@56
- `src/Game/StaticGeometry.cpp:482` Create: std::string name@484 -> luaL_checkstring@485 (+8); std::string mesh@485 -> luaL_checkstring@486 (+7); std::string material@486 -> luaL_checktype@487 (+6); std::vector instances@504 -> luaL_argerror@511 (+2)
- `src/Game/StaticGeometry.cpp:716` SetVisible: std::string name@718 -> CheckBool@719
- `src/Patches/AddScrapCallback.cpp:92` AddScrapSilent: std::string errorMessage@124 -> luaL_error@126
- `src/Patches/KillMessages.cpp:109` SetCustomKillMessage: std::string message@120 -> luaL_argerror@124
- `src/Patches/UnitVo.cpp:1026` CheckUnitVoFilename (helper): std::string filename@1029 -> luaL_argerror@1032
- `src/Patches/UnitVo.cpp:1165` SetUnitVoAlternates: std::string normalized@1167 -> luaL_checktype@1177 (+3); std::vector alternates@1185 -> luaL_argerror@1194 (+1)
- `src/UI/Overlay.cpp:2775` SetOverlayZOrder: std::string name@2777 -> luaL_checkinteger@2778 (+1)
- `src/UI/Overlay.cpp:2794` SetOverlayScroll: std::string name@2796 -> luaL_checknumber@2797 (+3)
- `src/UI/Overlay.cpp:2818` CreateOverlayElement: std::string typeName@2820 -> luaL_checkstring@2821
- `src/UI/Overlay.cpp:2882` AddOverlay2D: std::string overlayName@2884 -> luaL_checkstring@2885
- `src/UI/Overlay.cpp:2910` RemoveOverlay2D: std::string overlayName@2912 -> luaL_checkstring@2913
- `src/UI/Overlay.cpp:2926` AddOverlayElementChild: std::string parentName@2928 -> luaL_checkstring@2929
- `src/UI/Overlay.cpp:2942` RemoveOverlayElementChild: std::string parentName@2944 -> luaL_checkstring@2945
- `src/UI/Overlay.cpp:2985` SetOverlayMetricsMode: std::string name@2987 -> luaL_checkinteger@2988 (+1)
- `src/UI/Overlay.cpp:3004` SetOverlayPosition: std::string name@3006 -> luaL_checknumber@3007 (+3)
- `src/UI/Overlay.cpp:3028` SetOverlayDimensions: std::string name@3030 -> luaL_checknumber@3031 (+3)
- `src/UI/Overlay.cpp:3052` SetOverlayMaterial: std::string name@3054 -> luaL_checkstring@3055
- `src/UI/Overlay.cpp:3069` SetOverlayParameter: std::string elementName@3071 -> luaL_checkstring@3072 (+1); std::string parameterName@3072 -> luaL_argerror@3076; std::string parameterValue@3073 -> luaL_argerror@3076
- `src/UI/Overlay.cpp:3091` SetOverlayColor: std::string name@3093 -> CheckColorOrSingles@3094 (+1)
- `src/UI/Overlay.cpp:3111` SetOverlayCaption: std::string name@3113 -> luaL_checkstring@3114
- `src/UI/Overlay.cpp:3131` SetOverlayTextFont: std::string name@3133 -> luaL_checkstring@3134
- `src/UI/Overlay.cpp:3160` SetOverlayTextColor: std::string name@3162 -> CheckColorOrSingles@3163 (+1)
- `src/UI/Overlay.cpp:3187` SetOverlayTextCharHeight: std::string name@3189 -> luaL_checknumber@3190 (+1)
- `src/Util/IO.cpp:24` GetGameKey: std::string key@26 -> luaL_argerror@38
- `src/Util/OS.cpp:753` SaveGame: std::string filename@755 -> luaL_checkinteger@759 (+4); std::string description@782 -> luaL_error@799 (+1)
- `src/Util/StorageApi.h:780` CheckNamespace (helper): std::string name@784 -> luaL_argerror@787
- `src/Util/StorageApi.h:793` Save: std::string name@795 -> luaL_checkinteger@798 (+1)

`ContinuityApi.h`, `CommandReplacement.cpp`, `Camera.cpp`, `Radar.cpp` and the
remaining `StorageApi.h` bindings are clean by this sweep.

## 10. Sweep 4: `lua_call` vs `lua_pcall`

`lua_pcall`: 26 sites. `lua_call`: 7 sites, plus 22 callers of
`PushVector`/`PushMatrix` (14 + 8), which contain `lua_call`:

| site | context | verdict |
|---|---|---|
| `src/Game/GameObject.cpp:3455` `IsCommTowerPowered` | lua_CFunction | acceptable |
| `src/Game/Multiplayer.cpp:35` `BuildAsyncObject` | lua_CFunction | acceptable |
| `src/Game/Multiplayer.cpp:50` `BuildSyncObject` | lua_CFunction | acceptable |
| `src/luaexport.cpp:131` `CallLuaRegistryFunction` (from `PatchedSanitizedStockStringFunction`, `PatchedSetObjectiveOn/Off`) | lua_CFunction | acceptable |
| `src/Util/OS.cpp:736` `MessageBox` | lua_CFunction | acceptable |
| `src/LuaHelpers.h:70` `PushVector` | 14 callers: all lua_CFunctions, `PushVectorField` (called from lua_CFunctions) or `MakeEnums` (inside `luaopen_exu`) | acceptable |
| `src/LuaHelpers.h:92` `PushMatrix` | 6 lua_CFunction callers, plus **`BulletHitCallback.cpp:81`** and **`BulletInitCallback.cpp:68`** in native hook context | **not acceptable (H-1)** |

Other native-context raising calls outside `lua_pcall`: `lua_getfield(L,-1,…)`
on the value of global `exu` without a table check, at
`BulletHitCallback.cpp:40`, `BulletInitCallback.cpp:37` and
`AddScrapCallback.cpp:40` (H-1). `AiTargetSelect.cpp:150-156,524-530` and every
`CommandReplacement.cpp` path check `lua_istable`/`lua_isfunction` first.

## 11. Sweep 5: exception safety at the Lua boundary; `__try` census

Direct Ogre call expressions (`->::Ogre::…`, `::Ogre::X::y(`, `Ogre::<RVA fn>(`):
87. **Unguarded: 16**:

- `Overlay.cpp:2938` `addChild`, `:2953` `removeChild`, and `:3064` `setMaterialName` (registered bindings; can throw; H-2).
- `Overlay.cpp:2967` `show`, `:2981` `hide`, `:3000` `setMetricsMode`, `:3107` `setColour` (registered bindings; do not throw).
- `Ogre.h:372,377,382,387,394,399,411,416` inline getters; every caller wraps them in `__try`.
- `Culling.cpp:30` raw-RVA `SetVisible`, per unit per tick (H-4).

Every other Ogre call goes through a mangled-name function pointer or an RVA
inside a `Try*` helper with `__try`. With that, no registered `lua_CFunction`
beyond the H-2 three reaches a throwing Ogre call unguarded.

`__try` blocks: 237 textual, 235 parsed. Filters: 222 `EXCEPTION_EXECUTE_HANDLER`,
2 `(exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)`
(`OS.cpp:692,721`), 11 C++-aware `HandleNativeOverlayException`. What they wrap:
129 Ogre calls, 76 engine/other calls, 17 raw memory reads, 11 C++-aware Ogre
wrappers, 2 native save calls. Per file (line, function, [kind:first call]):

- `src/Game/Environment.cpp` (87): 266 TryGetViewportMaterialScheme[mem]; 289 TrySetViewportMaterialScheme[mem]; 398 TryGetRootSingleton[fn]; 424 TryGetRootRenderSystem[fn]; 450 TryGetRenderSystemViewport[fn]; 493 GetSceneManagerCurrentViewport[ogre]; 589/607 Try{Get,Set}SunAmbientColor[ogre]; 621/639 Try{Get,Set}SunDiffuseColor[ogre]; 653/671 Try{Get,Set}SunSpecularColor[ogre]; 685/703 Try{Get,Set}SunDirection[ogre]; 717 TrySetNativeTimeOfDay[engine SetTimeOfDay]; 732 TryRefreshTerrainMasterLight[engine]; 746/761 Try{Get,Set}SunPowerScale[ogre]; 775/790 Try{Get,Set}SunShadowFarDistance[ogre]; 806 TryHasSkyNode[ogre]; 821/834/847 TryGetSky{Box,Dome,Plane}GenParameters[ogre]; 952/971 Try{Get,Set}SkyEnabled[ogre]; 992/1030/1069 TrySetSky{Box,Dome,Plane}[ogre]; 1339 TryHasParticleSystem; 1360 TryHasSceneNode; 1387 TryGetParticleSystem; 1414 TryGetSceneNode; 1456 TryGetParticleMovableObject; 1488 TryDestroyParticleSystemByName; 1514 TryDestroySceneNodeByName; 1561 TryCreateParticleSystemAttachment; 1656/1678 TrySetManagedParticleSceneNode{Position,Direction}; 1699-1854 TrySetParticleSystem{Emitting,Visible,SpeedFactor,KeepLocalSpace,Material,RenderQueueGroup,ParticleQuota,DefaultDimensions} (8); 2139 GetActiveOgreCamera; 2158 TryGetCameraDerivedPosition; 2181 TryConvertSimPositionToRenderSpace[mem]; 2215 GetMovableObjectParentSceneNode; 2240 TryReparentNode; 2271/2291 TrySetNodeInherit{Orientation,Scale}; 2311 TrySetNodePositionDirect; 2333 TryAttachManagedParticleToRoot; 2451 TryAttachManagedParticleToBone; 2489 GetParticleEmitter; 2516 TryGetParticleEmitterCount; 2539 GetParticleAffector; 2566 TryGetParticleAffectorCount; 2650 TryGetStringInterfaceTypeName; 2682 TrySetStringInterfaceParameter; 2715 TryGetStringInterfaceParameter; 2744 TryGetStringInterfaceParameterNames; 2767-2928 TrySetEmitter{Enabled,EmissionRate,Direction,Position,VelocityRange,Angle,TimeToLiveRange,ColourRange}+TryGetEmitterEmissionRate (9); 2960 TryReturnManagedParticleToOwnNode; 2986 TrySetParticleSystemNonVisibleUpdateTimeout; 3058/3084 Try{Get,Set}ViewportOverlaysEnabled; 4795/4819 {Get,Set}ShowBoundingBoxes; 4843/4867 {Get,Set}ShowDebugShadows; 4891/4915 {Get,Set}ViewportShadowsEnabled; 5031 CallSetCompositorEnabledGuarded; 5663/5689 {Get,Set}SceneVisibilityMask.
- `src/Game/GameObject.cpp` (80): 319 TryCopyReadableCString; 410/433 TryGetPolymorphicMetadata[mem]; 477 TryGetBaseClassRawName[mem]; 517 TryGetBaseClassCount[mem]; 988 TryReadPointerField[mem]; 1009 TryReadUInt32Field[mem]; 1064 TryResolveHandleValue; 1098 TryGetHandleFromObject; 1128 TryGetWeaponSelectionState[mem]; 1163 TryGetModeListState[mem]; 2122-2406 sub-entity/material/visibility/flags/render-queue Try* (20, ogre); 2420-2626 skeleton/animation-state Try* (13, ogre); 2674 GetRenderableEntity; 2706 GetLightObject; 2726-2821 light Try* (7, ogre); 2838 TryResolveMaterial; 2860 TryCloneMaterial; 2883 TryResolveMaterialPass; 2984 TryCollectMaterialTintPasses; 3012 TryResolveMaterialTextureUnit; 3038-3108 texture-unit Try* (5, ogre); 3135-3230 pass colour Try* (8); 3261/3275/3289 TrySetLight{Diffuse,Specular}/TrySetSpotlightRange; 3303 TrySetAsUser[engine]; 3317 TryGetCommTowerPowerHandle[mem]; 4673 GetMass; 4695 SetMass; 4804 GetSelectedWeaponMask[**lua_push inside**]; 4853 GetWeaponSelectionInfo[**lua_push inside**]; 4881 GetAiProcess.
- `src/UI/Overlay.cpp` (31): 173 GetOverlayManagerRaw; 193 FindOverlay; 211 FindOverlayElement; 414 TryGetRootSingleton; 625 TryAddRenderQueueListenerWithSeh; 691 GetSceneManagerForOverlay; 722 TryConstructOverlaySystem; 1006 DetachOverlaySystemFromTrackedSceneManagers; 1037 DestroyOverlaySystemInstance; 1077 TryGetRootRenderSystem; 1098 TryGetRenderSystemSharedListener; 1124 TryGetRenderSystemViewport; 1144 GetCurrentViewportForOverlay; 1170/1197 Try{Get,Set}ViewportOverlaysEnabled; 1220 TryCallSetOverlayParameter; 1236 TryShowOverlay; 1252 TryHideOverlay; 1746-2104 TryCall{Panel,BorderPanel,TextArea}* (11); 2476 TryDestroyOverlayByName; 2502 TryDestroyOverlayElementByName.
- `src/Ogre/OgreNativeFontBridge.cpp` (11, **all C++-aware filter**): 310, 337, 367, 411, 445, 490, 540, 606, 621, 635, 649.
- `src/Game/Camera.cpp` (9): 38 GetCurrentOgreCamera; 146 GetClipDistances[**lua_push inside**]; 179 SetClipDistances; 200 GetAspectRatio[**lua_push inside**]; 226 SetAspectRatio; 246 GetProjectionType[**lua_push inside**]; 272 SetProjectionType; 292 GetPolygonMode[**lua_push inside**]; 318 SetPolygonMode.
- `src/Patches/AiTargetSelect.cpp` (7): 261 WriteRelativeCall[mem]; 349 TryReadRttiName[mem]; 373 TryReadSlot[mem]; 399 TryGetProcessOwner[mem]; 413 TryGetHandleForObject; 428 TryResolveHandle; 453 TryGetHorizontalDistanceSq.
- `src/Util/OS.cpp` (4): 504 ResolveMissionSaveFlag; 692/702 InvokeNativeNormalSaveGame; 721 InvokeNativeSaveShellGame.
- `src/UI/ControlPanel.cpp` (3): 550 MatchesCommandMenuRectArray[mem]; 609 MatchesCommandMenuPointArray[mem]; 743 GetCommandMenuRectBounds[mem].
- `src/Game/StaticGeometry.cpp` (1): 218 TryReadWorldRenderOrigin[mem]. (The Ogre destroy path uses C++ `try`, 426-434.)
- `src/Game/game_state.cpp` (1): 66 TryGetPauseMenuDebugState.
- `src/Util/SignatureResolver.h` (1): 115 MatchBytes[memcmp].

## 12. Sweep 8: duplication groups (14)

1. **Ogre proc resolution by mangled name**: `Environment.cpp:859` `ResolveOgreProc`, `StaticGeometry.cpp:106` `ResolveOgreProc`, `OgreNativeFontBridge.cpp:93` `ResolveOgreProc(module,…)`, `OgreMaterialShim.h:82` `Detail::ResolveProc`, and about 25 inline `GetProcAddress` lambdas (`Overlay.cpp:314-399, 617-682, 1738-2096, 2370`; `Environment.cpp:349-383, 3020, 3037`).
2. **OgreMain / OgreOverlay module handle**: `Environment.cpp:333`, `OgreNativeFontBridge.cpp:80,86`, `Overlay.cpp:165,297`, `OgreMaterialShim.h:84`, `BasicScanner.h:37-41`.
3. **Executable-section enumeration**: `CommandReplacement.cpp:89,137`, `UnitVo.cpp:64,120` and `OS.cpp:49,367` (each with a private `ExecutableSection` struct), vs `SignatureResolver.h:223` `GetExecutableSections`. Also `CommandReplacement.cpp:269` `FindMainModuleSection` and `Overlay.cpp:259` `TryGetMainModuleTextSection` vs `SignatureResolver::TryGetModuleSection`.
4. **Pattern scan loops**: `UnitVo.cpp:185`, `OS.cpp:430`, `BuildValidation.h:66` (`CountPatternMatches`), `SignatureResolver.h:298, 330, 366` (three variants).
5. **Relative-call target decode**: `CommandReplacement.cpp:197`, `UnitVo.cpp:207`, inline in `AiTargetSelect.cpp:266-270` and `Radar.cpp:266-268`.
6. **Readable-memory checks**: `SignatureResolver::IsReadableRange` (`:58`), `Overlay.cpp:254` wrapper, `GameObject.cpp:1028` `IsReadablePointer` (own `VirtualQuery`), `Overlay.cpp:278` `MatchBytes` wrapper.
7. **Logging wrappers**: `Logging.h:91` `LogMessage`; `OgreNativeFontBridge.cpp:41` `LogNativeOverlayMessage` (**verbatim copy**); `Environment.cpp:306/323` `WriteEnvironmentDebug`/`LogEnvironmentDebug` (`ofstream`); `GameObject.cpp:270` `LogMaterialDebug`; `OS.cpp:66` `LogNativeSave` (`std::format` + `ofstream`); `BasicPatch.h:109` `LogPatchIssue`.
8. **Handle to object**: `bzr.h:245` `GetObj` (unchecked); `GameObject.cpp:1054` `TryResolveHandleValue` and `AiTargetSelect.cpp:426` `TryResolveHandle` (both validated, duplicated); `GameObject.cpp:1090` `TryGetHandleFromObject` and `AiTargetSelect.cpp:411` `TryGetHandleForObject` (duplicated).
9. **OpenShim optional-export binding**: `OpenShimBridge::Resolve` per call vs the `static fn / static attempted / static loggedMissing` triple copy-pasted about 25 times (`UnitVo.cpp:669-1001`, `ControlPanel.cpp:388-482`), plus `static const auto fn` (`ControlPanel.cpp:503-519`) and a `Detail::Table` (`RenderEffectBridge.h:70`).
10. **Finite checks**: `Environment.cpp:553` `IsFiniteColor` and `Overlay.cpp:236` `IsFiniteColor`; `Environment.cpp:568,575` `IsFiniteVector`/`IsFiniteScalar`; `StaticGeometry.cpp:203` `IsFinite`.
11. **String normalisation**: `GameObject.cpp:582,594,603` `TrimAscii`/`ToLowerAscii`/`EqualsIgnoreCase`; `Overlay.cpp:1614` `ToLowerCopy`; `OS.cpp:143` `TrimAsciiWhitespace`; `CommandReplacement.cpp:215` `NormalizeStockCommandName`; `UnitVo.cpp:45` `NormalizeFilename`; `EngineFlameColor.cpp:40` `NormalizeColor`.
12. **Module / game directory**: `GameObject.cpp:655` `GetMainModuleDirectory` (`filesystem::path`), `OS.cpp:84` `GetMainModuleDirectory` (`std::string`), `Overlay.cpp:426,451,465` `GetDirectoryForModule`/`GetCurrentModuleDirectory`/`GetCurrentGameRootDirectory`, `Logging.h:16` `GetLogFilePath`.
13. **Lua vector/matrix marshalling**: `LuaHelpers.h:56` `AbsoluteStackIndex` vs `Radar.cpp:283` `AbsoluteIndex`; `LuaHelpers.h:310` `CheckMatrix` vs `ContinuityApi.h:288` `TryReadMatrix`; `LuaHelpers.h:74` `PushMatrix` vs `ContinuityApi.h:322,343` `PushPlainMatrix`/`PushNativeMatrix`; `GameObject.cpp:1382` `PushVectorField`.
14. **World render origin**: `Environment.cpp:2183` and `StaticGeometry.cpp:216-225` both read `0x025F8E4C` raw instead of using `OgreRenderSpace.h`. RTTI name read: `AiTargetSelect.cpp:347` `TryReadRttiName` vs `GameObject.cpp:56-520` RTTI structs.

Bool-token parsing appears once (`Overlay.cpp:1661` `TryParseBoolValue`); Lua
side `CheckBool`. Colour parsing: `CheckColorOrSingles`,
`GameObject.cpp:3241` `ReadOptionalPassColorField`, `Overlay.cpp:1705`
`TryParseColourValue` (string), `EngineFlameColor.cpp:55` `TryParseColor`
(string). These accept different inputs by design, so no merge is recommended.

## 13. Sweep 9: markers

`TODO|FIXME|HACK|XXX` (case-insensitive) in `src/`: **1**, `bzr.h:109`
`class BZR_Camera // todo merge camera namespaces`. `#if 0`/`#if false`: **0**.
Commented-out code blocks of 3 or more lines: **5**. They are
`SoundOptions.cpp:166-170` (`GetEffectsVolume`), `:172-181`
(`SetEffectsVolume`), `:183-187` (`GetVoiceVolume`), `:189-198`
(`SetVoiceVolume`), and `luaexport.cpp:936-939` (their registrations). Shorter
related remnants are `SoundOptions.h:31-32` (two scanners) and `:40-43` (four
declarations), plus `bzr.h:78-84`, an old `VECTOR_3D` definition in a comment.
Diagnostic code left in production paths (not markers, but related):
`Radar.cpp:211-225` (per-refresh investigation logging) and `IO.cpp`
`GetPauseMenuDebugState`, which is registered as a public binding.

## 14. Sweep 10: threading

EXU creates **no threads**. There is no `CreateThread`, `std::thread`,
`std::async`, `_beginthread`, `std::atomic` or timer.

- `std::mutex` ×4: `Environment.cpp:48` (env debug log), `UnitVo.cpp:42` (VO config + queue decision; used from Lua bindings and from the `QueueUnitVo` hook, both on the game thread), `OS.cpp:63` (native save log), `Logging.h:61` (reset-path set). They guard only same-thread re-entry. No contention exists, and each costs about 20-40 ns uncontended.
- `Interlocked*` ×5: `Overlay.cpp:1334,1341,1354,1361` (pause/shell wrapper depth), `:2619` (mission simulation state). The only writer from outside EXU's own call paths is `ExuNotifyMissionSimulationState`. OpenShim calls it from its mission-transition hook (`BZR-OpenShim/src/patches/bzr_hooks.cpp:17342-17363`), inside `__try`.
- Cross-module entry points: `EXU_UpdateCullingForUnit` (OpenShim unit-turbo hook, per unit per tick), `EXU_GetTeamEngineFlameColor` (OpenShim flame hook), and `ExuNotifyMissionSimulationState`. All three are invoked from OpenShim's game-thread detours, and OpenShim re-resolves them by module handle, so an exu.dll reload is handled. I could not prove that no OpenShim worker thread ever calls them, but none of the three call sites I read runs on the OpenShim patch thread. The third reaches Ogre through `NotifyMissionSimulationState` → overlay visibility sync.
- No EXU code touches Lua or Ogre from a non-game thread.

## 15. Summary

- Top findings: **H-1 [High/Med]**: BulletHit/BulletInit/AddScrap hook callbacks can raise Lua errors (unchecked `lua_getfield` on `exu`, unprotected `lua_call` in `PushMatrix`) from engine frames, so a script mistake exits the game. **H-2 [High/Med]**: `AddOverlayElementChild`/`RemoveOverlayElementChild`/`SetOverlayMaterial` call throwing Ogre methods with no guard.
- **H-3 [Med/High]**: 0 of 26 static code/vtable patch sites carry an expected-byte preimage (the parameter has 0 callers), and none is in `exu.json`. **H-4 [Med/Med]**: 71 OgreMain RVAs (65 function pointers) are called with no OgreMain identity check, 57 of them uncatalogued.
- **H-5 [Med/High]**: `GetObj` never validates the handle; 30 of 32 sites trust it, and 2 validated resolvers exist and duplicate each other. **H-6 [Med/High]**: the `SetSun*` setters log 3–6 file-open lines per call, and the shipped Workshop weather controller calls them every frame. **H-7 [Med/High]**: per-unit per-tick `VirtualProtect` code flipping for unit turbo.
- **H-8 [Med/Med]**: 90 functions (79 bindings) keep `std::string`/`vector` live across raising Lua calls, a leak class; no lock or patch guard is ever at risk. **H-9 [Med/Med]**: about 130 heavy static initialisers, including 2 `.text` scans and file logging, run in `DllMain` every mission. **H-10 to H-12 [Med]**: frozen fog pointer, 25 restoring Scanners (8 read-only), SEH swallowing C++ exceptions (222 of 235 `__try`). **H-13 to H-15 [Low]**: docs/example drift, dead code, duplication.
- Counts: dead code **25 items + 19 unused addresses/offsets/RVAs + 5 comment blocks**. Raw addresses **149 literals / 144 distinct in 17 files**, 71 absent from `exu.json`, 0 `exu.json` entries unused by `src/`, 4 duplicated addresses, plus 71 OgreMain RVAs (57 uncatalogued). longjmp sweep **90 functions**. `lua_call` **7 sites + 22 helper callers, 2 unsafe**. `__try` **237** (222 catch-all). Unguarded direct Ogre calls **16 (3 throwing)**. Hot-path census **775 sites**. Duplication **14 groups**. Markers **1 TODO, 0 `#if 0`, 5 commented blocks**. Threads **0**, mutexes 4, `Interlocked` 5.
- Not verified: the Ogre 1.10 throw behaviour for H-2 (from source knowledge; the headers are silent); whether BZR swaps scene managers mid-mission (H-10); the exact destructor behaviour of MSVC x86 `longjmp` under `/EHsc` for these frames (H-8, reasoned); no in-game or build run was done (read-only audit).
