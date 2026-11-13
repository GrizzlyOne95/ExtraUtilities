# Worksheet A: core runtime, Lua bindings and patch engine (2026-09-27)

Reviewer scope (all read end to end): `src/dllmain.cpp` 53, `src/PublicAPI.cpp` 173, `src/LuaState.h` 89,
`src/LuaHelpers.h` 342, `src/luaexport.cpp` 982, `src/Exports.h` 44, `src/About.h` 29,
`src/StockExtensions.cpp` 97 / `.h` 29, `src/bzr.h` 564, `include/ExtraUtils.h` 123, `src/OpenShimBridge.h` 115,
`src/RenderEffectBridge.h` 172, `src/RenderProfileBridge.h` 132, `src/Game/RenderEffects.cpp` 153 / `.h` 37,
`src/Game/RenderEffectNames.h` 160, `src/BasicPatch.h` 379, `src/Hook.h` 121, `src/InlinePatch.h` 147,
`src/Scanner.h` 180, `src/BasicScanner.h` 85, `src/Util/BuildValidation.h` 258, `src/Util/SignatureResolver.h` 393,
`src/Util/BzrBuildProfile.generated.h` 48, `src/Util/Logging.h` 122, `src/Patches.h` 36 (5,138 lines including ARCHITECTURE.md).
Also read for context: `tools/validate_hardening.py`, `tests/hardening_smoke.cpp`, every `Hook`/`InlinePatch`/`Scanner`
declaration in `src/`, and the OpenShim export surface at BZR-OpenShim `origin/main` 821fa3f5
(`include/openshim_sdk_exports.inc`, `src/engine/openshim_sdk_thunks.cpp`, `src/winmm.def`, `scripts/patches.json`).
Base: origin/main aec8c0a. I checked binaries read-only (pefile + capstone) against the untracked `Release/exu.dll`
built 2026-09-27 10:04 from this tree, and against the installed Steam `battlezone98redux.exe`.

Working lifetime model (several findings depend on it): Lua 5.1 `loadlib` puts a `__gc` on the library handle, so
`exu.dll` is `FreeLibrary`'d when the mission Lua state closes. Every static then re-runs on the next `require("exu")`.
The code relies on this (`src/Patches/AiTargetSelect.cpp:593-597`), but `ARCHITECTURE.md` §Lifetimes does not state it.
No sibling repo pins `exu.dll` today: I grepped every repo under `Documents/GIT` for `exu.lib`, `LoadLibrary("exu` and
`EXU_GetLuaState` and found nothing. However, `include/ExtraUtils.h:22-25,98-103` invites C++ consumers to link `exu.lib` and to call
`luaopen_exu` directly. Either would pin the DLL.

## 1. Section map

`src/luaexport.cpp` (982 lines; the only file in scope over 800):

| Lines | What | Status |
|---|---|---|
| 1-44 | includes, `MessageBox` macro push | production |
| 45-191 | anon ns: NUL-trimming wrappers for stock `GetBase/GetClassSig/GetOdf/GetPilotClass/GetWeaponClass` (registry refs + C closures) | production |
| 193-274 | anon ns: `SetObjectiveOn/Off` wrappers + replacement `ObjectiveObjects` iterator (process-global handle vector) | production |
| 277-287 | `ReleaseLuaStateBindings` (unrefs the two groups above) | production |
| 289-467 | `MakeEnums` (CAMERA, DEFAULTS, DIFFICULTY, OGRE, OVERLAY_METRICS, ORDNANCE, RADAR, SATELLITE) | production |
| 469-472 | `DoEventHooks`: empty body, still called from `Init` | dead stub |
| 474-518 | `Init`: attach state, reset per-state modules, **build gate** (`EnableDeferredPatchActivation`), install stock wrappers, radar/viewport call-site patches, reset Ogre binding | production |
| 520-952 | `luaopen_exu` + `exuExports[]` (373 live rows, 4 commented-out rows at 936-939) | production |
| 954-979 | `luaL_register`, `Init`, one-shot `print("exu.dll loaded")` under `lua_pcall` | production |

## 2. Findings

| ID | Sev/Conf | Where | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| A-1 | [High/High] | `src/Patches/ShotConvergence.cpp:160-164`, root cause `src/InlinePatch.h:63-88` | `playerReticleShotConvergence` passes a **function pointer** as the `const void* payload` of the buffer overload. The patch therefore copies the first 4 **code bytes** of `HoverCraftUpdateWeaponAimForReticle` into the hovercraft `UpdateWeaponAim` vtable slot `0x00889418`, not the function's address. | `exu.SetPlayerReticleShotConvergence(true)` without the OpenShim bridge export (no OpenShim, or one that predates the bridge) sends the next hovercraft weapon-aim virtual call to `0xF3EC8B55`. That is a guaranteed access violation. MSVC accepts the function-pointer-to-`void*` conversion silently, so this overload set will catch the next vtable or pointer patch too. | Pass the address the way the walker twin does: a `static constexpr uintptr_t` holding the function address, passed as `&thatConstant`. Add `expectedBytes` for the stock slot values, read from the Steam exe: `30 09 5F 00` at 0x00889418 and `90 B5 4E 00` at 0x0088A4FC. Make InlinePatch reject function pointers, either with a `Pointer(address, fn)` factory that `static_assert`s or with a deleted template overload for `R(*)(A...)`. | Disassembled `Release/exu.dll`. The hovercraft static initializer at `0x100056f0` pushes `0x10076820` (inside `.text`, bytes `55 8B EC F3`) to ctor `0x100101a0`. The walker initializer at `0x100056a0` calls the same ctor with `0x100b3a04` (`.rdata`, bytes `20 F3 60 00` = 0x0060F320). The ctor copies 4 bytes from that pointer. Re-read the call path at `ShotConvergence.cpp:205-222`. |
| A-2 | [Med/High] | `src/OpenShimBridge.h:15-27`, `src/RenderProfileBridge.h:75-86`, `src/RenderEffectBridge.h:102-144`, `src/PublicAPI.cpp:166-172` | **Export presence no longer means OpenShim is present.** On OpenShim main, every one of the 65 `OpenShim*` exports EXU resolves is a winmm thunk. When the plugin provider is not installed, each thunk returns a fixed "unavailable" value. EXU treats `HasExport` as the capability and ownership test. | Case: `winmm.dll` is present but `plugins\openshim.dll` is absent or failed to load. `EXU_SetMultiplayerNickname` returns 0 = `AppliedLive`. `RenderProfileBridge::Forward` sees 0 = AppliedLive and returns true, so `Environment` skips its legacy lighting fallback. `SetRenderEffectEnabled/Float` return true (0 = `kResultAccepted`). EXU also stands its own fallbacks down: HUD text colour hooks `ControlPanel.cpp:837`, unit turbo `GlobalTurbo.cpp:38`, viewport scheme hooks `Environment.cpp:5619`, radar layout `Radar.cpp:245`. Nothing implements those features. This fails **open**, contrary to every bridge header comment. | EXU-side, no policy move. Add `OpenShimBridge::IsProviderLive()`: true when the `OpenShimGetApi` export exists and `OpenShimGetApi(2) != nullptr`. The thunk returns `nullptr` when unavailable; pre-thunk shims return non-null. Cache it per module handle and make `Resolve/HasExport` return null when it is false. Ownership note for OpenShim: status-returning exports should use distinct unavailable sentinels, not 0. | Checked all 65 distinct names from `rg '"OpenShim[A-Za-z0-9_]+"' src` against `openshim_sdk_exports.inc` and `winmm.def` on OpenShim `origin/main`. All 65 are `OPENSHIM_SDK_EXPORT` entries. The thunk body `openshim_sdk_thunks.cpp:18-29` returns `unavail`. Unavailable values: Nickname 0, RequestRenderProfile 0, SetRenderEffectEnabled/Float 0, GetApi nullptr. |
| A-3 | [Med/High] | `src/BasicPatch.h:192-211`, `src/bzr.h` (called pointers), `src/LuaHelpers.h:51`, `src/luaexport.cpp:485-486` | **The build gate covers only `BasicPatch` activation.** `IsSupportedBzr2301()` has one production caller. None of the following consult it: raw engine calls (`Set_View` 0x0061D120, `Matrix_Inverse` 0x008203F0, `Vector_Unrotate` 0x00440300, `SelectOne/None/Add`, `SetTimeOfDay` 0x0068A230, `RefreshTerrainMasterLight`, `UpdateLives`, `OrdnanceClass::Build`, `SetAsUser`, `GetHandle`, four radar functions, `LuaCheckStatus` 0x004FF600, 8 OgreMain offsets), every `Scanner` write, and the raw `VirtualProtect` writers (`AiTargetSelect.cpp:288/387`, `CommandReplacement.cpp:413`). There is no public accessor for the gate result (`patchActivationEnabled` is protected). `Init` ignores the return value and always logs "deferred patches activated". The refusal goes only to `OutputDebugStringA`. | ARCHITECTURE.md says failing closed is preferable to patching an unqualified build. On a new Redux build, ordinary Lua bindings (e.g. `exu.SetCameraView`, `exu.BuildOrdnance`, `exu.MatrixInverse`) would call arbitrary code, while exu.log claims activation succeeded. | Cache the gate result once in `Init` (`BuildValidation::IsSupported()`) and expose it. Every binding that calls a raw pointer or writes a `Scanner` should return nil/false when it is false. Log the refusal through `Logging::LogMessage`, and log "activated" only on success. | Grepped `IsSupportedBzr2301`: 1 production caller plus tests. Grepped the callers of each `bzr.h` function pointer (`Camera.cpp:353`, `ControlPanel.cpp:1273-1287`, `Environment.cpp:720,734`, `Ordnance.cpp:55`, `GameObject.cpp:3305`, `Radar.cpp:177-196,444`, `StockExtensions.cpp:46-92`). None checks a gate. |
| A-4 | [Med/High] | `tools/validate_hardening.py:23-52`, `Definitions/ExtraUtils.lua:2405-2423`, `src/luaexport.cpp:936-939` | **The API parity check counts commented-out rows.** The regex `\{\s*"(name)"\s*,` matches `//{ "GetEffectsVolume", ...}`. As a result `exu.GetEffectsVolume/SetEffectsVolume/GetVoiceVolume/SetVoiceVolume` are documented but nil at runtime, and the check prints "377 Lua functions" (the real count is 373). It also never checks `exu.animation` (12), `exu.storage` (6) or `exu.continuity` (5), which are registered by `AnimationApi/StorageApi/ContinuityApi::Install`, nor `exu.VERSION` or the enum tables. | Missions written against the definitions call nil functions, and the only guard reports success. `TargetLocalFirstPerson` is documented only as an `@field` in ExtraUtils.lua, not in `Definitions/Animation.lua`. | Strip `//` comments before matching. Parse the three `Install` tables and `Definitions/{Animation,Storage,Continuity}.lua` with the same both-direction check. Delete the four stale definitions. | Ran `python tools/validate_hardening.py`: it passes and reports 377. Counted live rows: 373, no duplicates. Read `Install` in `AnimationApi.h:481`, `StorageApi.h:977` and `ContinuityApi.h:1075`. |
| A-5 | [Med/High] | `src/luaexport.cpp:67-94,172-191,242-274` | **The stock-function wrappers are neither idempotent nor tied to an owning state.** A second `luaopen_exu` on the same state (the C++ path documented at `include/ExtraUtils.h:98-103`) runs `luaL_unref(ref)` and then `luaL_ref` on the already-wrapped global. Lua reuses the freed slot, so each wrapper's upvalue ref now points at itself. Separately, if a new state attaches while an old one is still alive (pinned DLL), EXU unrefs the **old** VM's ref numbers in the **new** registry. | Double open on the same state: `GetBase/GetClassSig/GetOdf/GetPilotClass/GetWeaponClass/SetObjectiveOn/SetObjectiveOff` recurse until "C stack overflow" on every call. Overlapping states: the new registry's freelist is corrupted and whatever holds that slot is overwritten. `CommandReplacement` already gets this right with `g_ownerState` (`CommandReplacement.cpp:737-770`). | Record the owning `lua_State*` and unref only when it matches. Skip the install when the global is already EXU's wrapper (`lua_tocfunction(L,-1) == PatchedSetObjectiveOn`, or an upvalue check for the closure). | Traced the `luaL_unref`/`luaL_ref` freelist order for both groups. Confirmed that `HandleLuaStateAttached` is skipped for the same state (`LuaState.h:50-53`) while `Init` still reinstalls. The trigger needs the documented direct call, and no in-repo caller makes it today. |
| A-6 | [Med/Med] | `src/Scanner.h:141-151`; declarations in `Camera.h`, `Environment.h:29`, `PlayOption.h:29,31`, `Radar.h`, `Multiplayer.h`, `Reticle.h`, `Ordnance.h`, `Satellite.h`, `Steam.h` | **`Restore::ENABLED`, the default, writes the load-time value back at DLL unload whether or not EXU ever wrote**, and the DLL unloads at every mission end. 25 scanners are ENABLED. 11 of them are `const` read-only scanners that can never be written through (`Reticle::position/object/matrix`, `Satellite::state/cursorPos/camPos/clickPos`, `Steam::steam64`, ...). | At each mission close EXU silently rewrites live engine state captured at `require` time. That includes play options (`*0x0094672C+0x30`, so auto-level/TLI/reverse-mouse changes made in the pause menu are reverted in memory), difficulty `0x025CFA1C`, satellite and reticle globals, lives and radar state. Whether the option revert reaches the user depends on when the engine persists the profile; not verified in game. | Track `m_written` and restore only when `Write()` was called. Default `const` scanners to `Restore::DISABLED`. Do intentional mission-scope reverts explicitly in `HandleLuaStateClosing`. | Enumerated all 38 `Scanner` declarations (`rg '\bScanner\b' src`) and re-read the destructor. Per-mission unload follows from Lua 5.1 loadlib semantics plus `AiTargetSelect.cpp:593`. |
| A-7 | [Med/High] | all 35 patch instances (list in section 4); `src/BasicPatch.h:226-264,157-182` | **No patch passes `expectedBytes`.** 25 fixed-address sites have no per-site identity check: AddScrap, BulletHit, BulletInit, Cheats x2, GlobalTurbo x4, KillMessages, OrdnanceVelocity x4, ShotConvergence x2, WeaponMask, fogReset, Multiplayer x3, ControlPanel x5. Their "preimage" is whatever bytes were there at DLL load. `RestorePatch` writes the originals back without checking that the site still holds EXU's payload. | Fail-closed depends entirely on three global anchors. If OpenShim patched a site before EXU loads, EXU records OpenShim's bytes as "original" and overwrites them. If OpenShim re-patches a site after EXU, EXU clobbers it on unload. | Add `expectedBytes` from `exu.json`. Three sites have no catalog entry yet: `InfiniteAmmoAddr` 0x004A7709, `InfiniteScrapAddr` 0x005E10D7, `WeaponMaskCaptureAddr` 0x0060A8C6. In `RestorePatch`, check that the current bytes equal the payload (or the Hook's `FF 15 &p_function` form) before restoring; otherwise log and leave the site alone. | Extracted every constructor call with a script: 35 hits, none with a 5th/6th argument. Cross-checked the six sites that do their own byte or signature check before construction (Overlay x4, Radar, Viewport, Wingman Hunt, UnitVo x2). |
| A-8 | [Med/Med] | `src/Patches/BulletHitCallback.cpp:81`, `src/Patches/BulletInitCallback.cpp:68` via `src/LuaHelpers.h:62-93` | **Native per-shot hooks make an unprotected `lua_call`.** `PushVector/PushMatrix` do `lua_getglobal("SetVector"/"SetMatrix")` followed by `lua_call`. The two bullet hooks call `PushMatrix` before their `lua_pcall`, outside any protected frame. | Any of these becomes a Lua panic (process exit) during bullet simulation: a mission that shadows or nils `SetMatrix`, a strict-globals `__index` that errors, or an allocation error. | In hook context, build the matrix inside the protected call: `lua_cpcall` a small C function that does `PushMatrix` plus the callback. Alternatively, check `lua_isfunction` and call the constructor with `lua_pcall`. | Grepped all 30 `PushVector/PushMatrix` callers. Only these two run in native-hook context. The rest run inside Lua-called bindings, where raising is fine. |
| A-9 | [Med/Med] | `src/LuaHelpers.h:196-203`, `src/bzr.h:245-248` | `CheckHandle` accepts **any** userdata, including full-userdata vectors and matrices, and `GetObj` ignores the handle's low 20 bits. The engine's own lookup (FUN_004da060) rejects a slot unless `[object+0x15C] == handle & 0xFFFFF` (OpenShim `patches.json` "GameObject::Arena" identity note). | A stale handle resolves to whatever object now occupies the slot. A vector passed by mistake turns a heap address into a handle. `GameObject.cpp` has 60 `CheckHandle` uses, which then write through `GetScanner()/GetJammer()` (e.g. `SetRadarPeriod`, `GameObject.cpp:5429-5440`). The result is memory corruption instead of a Lua error. | In `CheckHandle`, require `lua_islightuserdata`. Add `GetObjChecked(h)`, which returns null unless the slot's `+0x15C` matches, and use it in bindings. | Re-read `CheckHandle`, `GetObj` and `GetRadarPeriod/SetRadarPeriod`; read OpenShim `scripts/patches.json:543-551`. |
| A-10 | [Med/Med] | static initializers: `src/Game/CommandReplacement.cpp:689-696`, `src/Patches/UnitVo.cpp:620-625`, 38 `Scanner` ctors, `src/Ogre/Ogre.h:100+`; checker `tools/validate_hardening.py:166-175` | **Loader-lock work was moved, not removed.** `dllmain.cpp` and the validator forbid logging in `DllMain`, but C++ static initializers run inside `DLL_PROCESS_ATTACH` anyway. They do two full-`.text` signature scans (4.6 MB each) that call `Logging::LogMessage` (fopen, `CreateDirectoryA`, `std::mutex`), about 38 `VirtualProtect` calls and `GetModuleHandleA`. This repeats every mission because the DLL reloads. The validator only greps the text of `dllmain.cpp`. | File I/O under the loader lock is the class of bug OpenShim fixed as F4: antivirus or filter drivers that load DLLs on file open can deadlock. It also makes the validator's "minimal DllMain" check a false pass. | Resolve signatures lazily on the first `Init` (or first use) and keep namespace-scope initializers trivial. Extend the validator to flag dynamic initializers in the `inline ... = Initialize...()` style that log or scan. | Read both initializer functions (`CommandReplacement.cpp:640-687`, `UnitVo.cpp:604-619`). `.text` size (0x46737B) taken from the Steam exe's PE header. |
| A-11 | [Low/Med] | `src/Scanner.h:59-78,104-134`; `src/Game/Environment.h:30` | Scanner resolves pointer chains **once, at construction**. `Environment::fog` likewise captures `SceneManager+0x128` at static init. This is correct only under the undocumented per-state DLL reload. | If `exu.dll` is ever pinned (see the lifetime note at the top), `Get/SetFog` read and write a freed per-mission SceneManager from mission 2 onward; `luaexport.cpp:511-514` says the SceneManager is per mission. The `playOption`/`musicVolume` chains go stale if the profile object is reallocated. | Resolve chains on each access, or compute fog from `sceneManager.Read()` on each call. Document the DLL-per-Lua-state lifetime in `ARCHITECTURE.md` §Lifetimes. | Read `Ogre.h:77-92`, `Environment.h:29-30`, and the callers at `Environment.cpp:3166,3184`. Latent: no pinning consumer exists today. |
| A-12 | [Low/High] | `src/Util/Logging.h:53-120` and all callers | Every `LogMessage` call does 2x `GetModuleFileNameA`, 2x `CreateDirectoryA`, a mutex-guarded lookup of a hashed `std::string`, and `fopen/fprintf/fclose`. `LogEnvironmentDebug` adds a `std::ofstream`. There are no levels and no budgets across 188 `LogMessage`, 159 `LogEnvironmentDebug` and 117 `LogMaterialDebug` sites. `ResetLogFileForCurrentProcess` actually truncates once per DLL load (= per mission), because its static set dies with the DLL. | Some Lua-driven bindings log on every call: the HUD sprite setters at `ControlPanel.cpp:1188-1252`, and `ShowOverlay` at `Overlay.cpp:2701-2730`, twice per call. Each line costs roughly 0.1-1 ms on Windows, more when antivirus scans on close. A mission that animates HUD sprites every frame pays that every frame. The previous mission's logs are lost when the next mission loads. | Resolve the log path once per process and cache the string. Keep one `FILE*` open with `fflush`. Add a verbosity switch and a per-site once/budget for success-path lines. Rename or fix the "per process" reset. | Counted with `rg -c` and traced the four per-call logging bindings. No threads exist in `src/` (grep for `CreateThread`, `std::thread`, `_beginthread` finds none), so the mutex is uncontended. |

### Quotes for High / Med items

A-1 (`src/Patches/ShotConvergence.cpp:31,160-164`; the first call is right, the second copies code bytes):
```cpp
InlinePatch shotConvergence(wingmanWeaponAimVftableEntry, &walkerUpdateWeaponAim, 4, InlinePatch::Status::INACTIVE);
// walkerUpdateWeaponAim is `constexpr uintptr_t = 0x0060F320`, so &it points at the value
InlinePatch playerReticleShotConvergence(
    hovercraftWeaponAimVftableEntry,
    &HoverCraftUpdateWeaponAimForReticle,   // a function: &it points at its code
    4,
    InlinePatch::Status::INACTIVE);
```

A-2 (`OpenShimBridge.h:102-113` vs OpenShim `openshim_sdk_exports.inc:60`):
```cpp
const SetBzrNetNicknameFn setter = Resolve<SetBzrNetNicknameFn>("OpenShimSetBZRNetNickname");
if (!setter) { return BzrNetNicknameResult::OpenShimUnavailable; }
return static_cast<BzrNetNicknameResult>(setter(nickname));
// OpenShim: OPENSHIM_SDK_EXPORT(DWORD, WINAPI, OpenShimSetBZRNetNickname, ..., (nickname), 0)  -> 0 == AppliedLive
```

A-3 (`src/luaexport.cpp:485-486`):
```cpp
BasicPatch::EnableDeferredPatchActivation();
Logging::LogMessage("exu: deferred patches activated");
```

A-4 (`tools/validate_hardening.py:34` vs `src/luaexport.cpp:936`):
```python
runtime = set(re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,', table))
```
```cpp
//{ "GetEffectsVolume", &SoundOptions::GetEffectsVolume },
```

A-5 (`src/luaexport.cpp:174-189`):
```cpp
ResetSanitizedStockStringPatchState(L);        // luaL_unref(L, ..., patch.originalRef)
...
lua_getglobal(L, patch.name);                  // already our closure on a second open
patch.originalRef = luaL_ref(L, LUA_REGISTRYINDEX);   // gets the slot just freed
lua_pushinteger(L, patch.originalRef);
lua_pushcclosure(L, PatchedSanitizedStockStringFunction, 1);
```

A-6 (`src/Scanner.h:141-148` and, for example, `src/Game/Steam.h:28`):
```cpp
~Scanner() {
    if (m_address != nullptr && m_restoreData == Restore::ENABLED && ...)
        *m_address = m_originalData;          // value captured at require time
```
```cpp
inline const Scanner steam64(BZR::Steam::steam64);   // const, Restore::ENABLED by default
```

A-7 (`src/Patches/AddScrapCallback.cpp:87`, typical of the 25 sites):
```cpp
Hook addScrapHook(0x005E1016, &AddScrapCallback, 6, BasicPatch::Status::ACTIVE);   // no expectedBytes
```

A-8 (`src/LuaHelpers.h:74-92`, reached from `BulletHitCallback.cpp:81` before its `lua_pcall` at :94):
```cpp
lua_getglobal(L, "SetMatrix");
...
lua_call(L, 12, 1);
```

A-9 (`src/LuaHelpers.h:196-203`, `src/bzr.h:245-248`):
```cpp
if (!lua_isuserdata(L, idx)) { luaL_typerror(L, idx, "handle"); }
return reinterpret_cast<BZR::handle>(lua_touserdata(L, idx));
...
return (GameObject*)(((h >> 0x14) * 0x400) + 0x260DB20);
```

A-10 (`src/Game/CommandReplacement.cpp:689-696`):
```cpp
inline uintptr_t g_wingmanHuntActivationHookInitialized = InitializeWingmanHuntActivationHook(); // scans .text + LogMessage
inline std::unique_ptr<Hook> g_wingmanHuntActivationHook = ... std::make_unique<Hook>(...)
```

## 3. Dead / unreferenced code

Verified with `rg -w <symbol> src tests include tools`, excluding the defining file.

| Symbol | Where | Evidence |
|---|---|---|
| `BZR::Ogre::getSkyBoxGenParametersOffset`, `getSkyBoxNodeOffset`, `getSkyDomeGenParametersOffset`, `getSkyDomeNodeOffset`, `getSkyPlaneGenParametersOffset`, `getSkyPlaneNodeOffset` | `src/bzr.h:376-381` | 0 refs (the sky getters now go through the `OgreSceneManagerShim.h` dllimport) |
| `BZR::Camera::viewFrustum` | `bzr.h:143` | 0 refs |
| `BZR::Cheats::editMode` | `bzr.h:180` | 0 refs |
| `BZR::Environment::sunDirection` | `bzr.h:205` | 0 refs |
| `BZR::Reticle::angle` | `bzr.h:558` | 0 refs |
| `BZR::Radar::cockpitWireframeProjectionRadius`, `radarLeftBase`, `cockpitWireframeCenterBase`, `edgeMinX`, `edgeMaxX`, `edgeMinZ`, `edgeMaxZ` | `bzr.h:513-526` | 0 refs |
| `BZR::SoundOptions::soundStruct2`, `sfxOffset`, `voiceOffset` | `bzr.h:497-500` | only commented-out refs (`SoundOptions.h:31-32`) |
| `DoEventHooks` | `luaexport.cpp:469-472` | empty body; one call at :506 |
| `OpenShimBridge::HasBzrNetNicknameBridge` | `OpenShimBridge.h:97` | 0 refs |
| `RenderEffectBridge::ApiVersion`, `RenderProfileBridge::ApiVersion`, `RenderProfileBridge::GetActiveBackend` | `RenderEffectBridge.h:114`, `RenderProfileBridge.h:66,115` | 0 refs |
| `SignatureResolver::FindUniqueMaskedPattern` | `SignatureResolver.h:352` | 0 refs |
| `BasicPatch::m_oldProtect` | `BasicPatch.h:55` | only copied in the move ctor; never read |
| `BasicScanner::m_bzrModuleBase`, `m_ogreMainModuleBase` | `BasicScanner.h:34-35` | written by `RefreshModuleBase`, never read (the "cache" always re-queries) |
| Test-only in production code: `BuildValidation::GetBzrDistribution`, `IsSteamBuild`, `IsGogBuild`, `Detail::HasSteamStubBindSection`, `OpenShimBridge::GetBzrDistribution` (reached only through the former), `ScopedPatchDisable` | `BuildValidation.h:131-257`, `OpenShimBridge.h:61`, `BasicPatch.h:352` | referenced only from `tests/hardening_smoke.cpp` |
| Commented out: 4 `exuExports` rows (`luaexport.cpp:936-939`) and the matching 4 functions in `SoundOptions.h/.cpp` | | see A-4 |

Count: 29 unreferenced symbols in scope (20 of them in `bzr.h`), plus 6 production symbols used only by tests and 1 empty stub.

## 4. Raw address census

In-scope literals matching `0x00[4-9A-F]xxxxx` / `0x02xxxxxx`: **80** (`src/bzr.h` 79, `src/LuaHelpers.h` 1). No other scope file has any.
`BzrBuildProfile.generated.h` carries 2 anchor VAs; they are generated from `exu.json`, so I excluded them. `bzr.h` also holds 14 OgreMain-relative
offsets (`bzr.h:368-381`; 8 used by `Ogre.h`, 6 dead). Per AGENTS.md these belong under `src/Ogre/`, and no OgreMain identity check
covers them.

Called (function pointers): `Set_View` 0x0061D120 (in exu.json, with pattern), `Matrix_Inverse` 0x008203F0, `Vector_Unrotate` 0x00440300,
`ControlPanel::SelectOne/None/Add` 0x004A6CD0/0x004A6D50/0x004a6c70, `SetTimeOfDay` 0x0068A230, `RefreshTerrainMasterLight` 0x0067E0E0,
`GameObject::GetHandle` 0x00462380, `SetAsUser` 0x004DB930, `UpdateLives` 0x006260f0, `OrdnanceClass::Build` 0x00586ff0,
`Radar::RefreshCockpitWireframeAnchor` 0x00404CF0, `RefreshLayout` 0x00492EC0, `FindNamedPath` 0x00460FC0, `RefreshEdgePathBounds` 0x0046AF20.
All of these are in exu.json, but only Set_View has a pattern; the rest are `"pattern": null`. `LuaCheckStatus` 0x004FF600 is **not in exu.json**.
None of the called pointers is build-gated (A-3).

Written: through Scanners (Camera zoom/min/max, gravity, play option, difficulty, radar state/scale, lives, scoreboard, reticle range,
coeffBallistic, satellite pan/zoom) and directly (`Environment::timeOfDay` at `Environment.cpp:719`, radar globals at `Radar.cpp:204-206`;
`worldRenderOriginAddress` is only read). Patched code sites: `Cheats::InfiniteAmmoAddr` 0x004A7709, `InfiniteScrapAddr` 0x005E10D7,
`WeaponMaskCaptureAddr` 0x0060A8C6 (**none in exu.json**).

Seven literals are not in exu.json at all: 0x004FF600 (LuaCheckStatus), 0x004A7709, 0x005E10D7, 0x0060A8C6, 0x025F8E4C (`worldRenderOriginAddress`),
and two that appear only in comments (0x00917AF8, the old satellite state; 0x00493330). Of the literals that are in exu.json, 3 have a signature
(`View_Record_MainCam`, `zoomFactorFPP`, `Set_View`) and the other 70 hits (73 in-catalog hits, several addresses repeated) have `"pattern": null`.

Catalog drift: exu.json names 0x008E7918/24/28 `commandPanelLeftBase/Left/Bottom`, while `bzr.h:517-522` calls them the cockpit wireframe
centre and justifies that from the code. 0x008E77A8 is both `GraphicsOptions::uiScaling` and `Radar::radarLeft` (exu.json notes the
alias), so `exu.GetUIScaling` probably reports the radar's left pixel.

Patch instance census (question 2): 35 `Hook`/`InlinePatch` objects.
- Static storage, never moved: 25 namespace-scope objects, plus 5 `inline Hook` in `ControlPanel.cpp:842-866`.
- On the heap, never moved:
  - `CommandReplacement.cpp:690`: `unique_ptr`, built at static init.
  - `Overlay.cpp:1539-1596`: 4 `unique_ptr`, built after activation. `DestroyOverlayPauseHooks` restores them before destroying them.
  - `Environment.cpp:5644` and `Radar.cpp:279`: intentionally leaked `new InlinePatch`.
- **None is a local or lives in a reallocating container.** `Hook` deletes its move ctor.
- Every static instance is constructed before `EnableDeferredPatchActivation`. That is by design and correct: status is forced to INACTIVE, activation is deferred, and `ValidatePreimage` re-checks the bytes at activation.
- Raw writers outside the engine:
  - `AiTargetSelect.cpp` (vtable slots): value-checked, but restored only at DLL unload, not in `UnloadAllPatches`.
  - `CommandReplacement.cpp:413`: the hunt label pointer.

## 5. Lifetime / ownership notes

Question 1 trace:
1. `luaopen_exu` calls `luaL_register`, then `Init`.
2. `Init` does `state = L`. `LuaStateHandle::operator=` bumps the generation and calls `HandleLuaStateAttached`, which resets the logs, puts the sentinel userdata in the registry, and installs the animation/storage/continuity subtables.
3. `Init` then runs the build gate and activates the patches.
4. On `lua_close`, Lua 5.1 runs finalizers newest-first. The sentinel (created inside `luaopen_exu`) is therefore finalized before the older loadlib handle, whose `__gc` calls `FreeLibrary`.
5. `HandleLuaStateClosing` releases the luaexport refs and the CommandReplacement refs, unloads every `BasicPatch`, shuts down overlays and static geometry, and then clears `state`. The VM is still valid at that point.
6. `DllMain` DETACH then finds `state == nullptr` and does nothing. Static destructors restore any still-active patch and the Scanner values (A-6).

Findings from the trace:
- **In the normal order, no hook can run with `state` pointing at a closed VM.** Every hook that calls Lua is a `BasicPatch`, unloaded before `Clear`. The two exceptions both null-check `state`: AiTargetSelect's vtable slots (`AiTargetSelect.cpp:134-138`) and CommandReplacement dispatch (`CommandReplacement.cpp:774-778`). `BulletHitCallback` has no null check (`BulletHitCallback.cpp:36`), but it cannot run after unload.
- **New state before the old `__gc`.** This needs the DLL to stay mapped (overlapping states or a pinned DLL). I found no documentation of BZR's actual ordering, so this case is unverified.
  - `operator=` attaches the new state.
  - The old sentinel then hits the `state.Get() != L` early return, so the old VM's bindings are never released.
  - CommandReplacement is safe because of `g_ownerState`; luaexport is not (A-5).
  - Patches stay active for the new state, which is correct.
- **No registry ref is missed.** `ReleaseLuaStateBindings` releases the 5 sanitized-string refs and the 2 objective refs. The only other `luaL_ref` in `src/` is CommandReplacement's `callbackRef`, released by `CommandReplacement::ReleaseState`.

State by lifetime:
- **Process lifetime (nominally):** `deferredPatches`, `patchActivationEnabled`, the OpenShim resolve cache in `RenderEffectBridge`, and Logging's reset set. With the per-state DLL reload, all of these are effectively Lua-state lifetime.
- **Lua-state lifetime:** the sentinel, the luaexport refs, and `g_activeObjectiveHandles`. The handle list is cleared on reset, but it returns handles of objects that died without `SetObjectiveOff`. Its iterator reads the live vector, so calling `SetObjectiveOff` inside the loop skips entries.
- **Mission lifetime baked into static init:** Scanner chain resolution and `Environment::fog` (A-11).
- **Temporary-patch lifetime:** `ScopedPatchDisable` exists with the right longjmp warning, but nothing in production uses it.

Question 3: `deferredPatches` is `static inline std::vector<BasicPatch*>{}`.
- Under `/std:c++latest` the default ctor is `constexpr`, so the vector is constant-initialized before any dynamic initializer in any TU.
- Every TU that constructs a patch includes the definition first, so the vector is destroyed after all patches.
- No thread exists in `src/`, so only the game thread touches the list.
- `Reload` and `Unload` never construct patches, so nothing registers during iteration.

No hazard found.

## 6. Performance notes

- **Question 8: Scanner accesses are cheap and off the hot paths.** There are 64 `Scanner::Read/Write` call sites: Camera 11, Satellite 12, Environment 7, Multiplayer 6, Reticle 5, PlayOption 5, SoundOptions 5, Radar 4, GraphicsOptions 2, Ordnance 2, Ogre.h 1, Overlay 1, Renderer 1, StaticGeometry 1, Steam 1.
  - **None is on a native per-object or per-frame hook path**; all run once per Lua call.
  - The busiest is `Ogre::sceneManager.Read()`, reached through `Environment::GetSceneManager()` (61 call sites). It runs every frame only if a mission calls something like `UpdateParticleFollowers` every frame.
  - Each access costs one `VirtualQuery`: sub-microsecond natively, and handled in-process on Wine. Even 100 calls per frame stay under 0.1 ms. Not worth changing.
- **Logging is the real cost (A-12).**
- **Signature scans add up to roughly four full-`.text` scans per mission load, estimated at 20-60 ms total.**
  - `IsSupportedBzr2301` runs a `UniqueExecutable` anchor over the whole 4.6 MB `.text` on every `Init`.
  - Static init adds two more whole-section scans (the Wingman Hunt pattern and UnitVo's two patterns).
  - Overlay resolves its patterns on first use.
  - This is acceptable. If the DLL is ever pinned, cache the gate result per process/exe identity.
- **`OpenShimBridge::Resolve` calls `GetModuleHandleA` + `GetProcAddress` every time.** This affects the RenderProfile getters and most Patches bindings, and costs a few microseconds per Lua call. `RenderEffectBridge` shows the right pattern: a cache keyed on the module handle.
- **`GameViewportSetMaterialSchemeHook` (`Environment.cpp:5095`) makes 2-3 `std::string` copies per call.** It runs only when a viewport's scheme changes, not per frame.

## 7. Patterns worth keeping

- `BasicPatch` deferred activation, with `m_requestedStatus` and `ValidatePreimage` at activation time. A foreign patch applied between load and activation is refused rather than overwritten.
- `Hook` and `Scanner` delete their move constructors, and `hardening_smoke.cpp` `static_assert`s it.
- The Lua-state sentinel `__gc`, plus a generation counter exposed as `EXU_GetLuaStateGeneration()` for native consumers.
- `CommandReplacement`'s `g_ownerState` guard on registry refs. Copy this into luaexport (A-5).
- `RenderEffectBridge::Detail::Resolve` caches exports keyed on the module handle, and `GetStatus` distinguishes "OpenShim absent" from "OpenShim refused".
- `RenderEffectNames.h` is a closed, host-testable name set with a static-asserted POD ABI.
- `SignatureResolver::IsReadableRange` checks every region of a range, not just the first `VirtualQuery`.
- `MatchBytes` keeps its SEH guard around a plain `memcmp`, with no C++ objects in the frame.
- The Overlay, Radar, Viewport and Wingman Hunt sites verify bytes before constructing the patch.

## 8. Low-severity items

- `StockExtensions::ScreenToWorld` (`StockExtensions.cpp:55-84`) has three problems:
  - `mainCam.Get()` is not null-checked, so an unsupported layout dereferences null.
  - The bounds use `GetSystemMetrics` for the primary monitor, cached in a function `static`. They are wrong in windowed mode or after a resolution change.
  - The `std::format` temporary is alive across `luaL_argerror`, so it leaks on longjmp.
- `StockExtensions::DoString` (`:34-39`) silently discards `luaL_dostring` errors and returns nothing.
- `bzr.h:279-331`: `GetScanner/GetJammer/GetOgreEntity/GetLight` use inline asm that assumes `ecx == this`.
  - This holds only while MSVC emits them out of line. I verified they are out of line in the current build, at `0x10023790`, `0x100237b0`, `0x1000d470` and `0x100237d0`, but `/O2` may inline functions containing `__asm`.
  - They also do no null checks.
  - Replace them with plain pointer arithmetic.
- `RenderProfileBridge::GetApiVersionFn` is `__cdecl`, but OpenShim exports it as `WINAPI`. This is harmless (no arguments) and in dead code.
- `Scanner` sets `PAGE_EXECUTE_READWRITE` on data, `.rdata` and (through pointer chains) heap pages for the DLL's lifetime, including for `const` read-only scanners. Request `PAGE_READWRITE`, and only for writable scanners.
- `InlinePatch(address, nullptr, len, ...)` leaves a registered, zero-filled payload that a later `SetStatus(true)` would write.
- `BasicPatch::LogPatchIssue` writes only to `OutputDebugStringA`, so preimage refusals never reach exu.log.
- There are three private copies of `GetExecutableSections`/`FindPattern` (`CommandReplacement.cpp:137-194`, `UnitVo.cpp:120-177`, `OS.cpp:367-422`) that duplicate `SignatureResolver`.
- In `About.h`, `inline std::string version` could be `constexpr const char*`. `EXU_GetVersion` returns a pointer into a DLL that unloads every mission, so callers must not cache it.
- `LuaStateHandle` has an implicit `operator lua_State*`, so `lua_State* L = Lua::state;` compiles without the generation.
- Filename case mismatches:
  - `LuaHelpers.h` includes `"BZR.h"`, but the file is `bzr.h`.
  - The vcxproj lists `src\LuaExport.cpp`, but the file is `luaexport.cpp`.
  - The vcxproj also compiles the headers `src\Util\OS.h` and `src\Util\Vec3.h` as `ClCompile`.
- The comment on the Radar/Viewport `new InlinePatch` objects ("must outlive every mission/Lua reload") assumes a persistent DLL. With the per-state reload, this is a harmless leak of one object per mission.
- `InstallGameViewportSchemeHooks` and the radar call-site installer read fixed code bytes before the build gate is consulted. These are reads only; the writes stay deferred.
- Cross-scope census for the owners of those files: there are 112 `std::string x = luaL_checkstring(...)` sites (Environment 41, Overlay 34, GameObject 21, AnimationApi 9, StaticGeometry 5, IO 1, Ordnance 1). Many are followed by a raising `luaL_check*`/`luaL_argerror`, which leaks the string on error (heap only above the SSO size).
- `BulletHitCallback.cpp:47-50`: `strncpy(formattedODF /*16*/, odf, len - 4)` trusts `len >= 4 && len <= 20`. That is safe given the engine's 16-byte ODF field, but unchecked. Flagged for the Patches owner.
