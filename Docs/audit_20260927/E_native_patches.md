# Worksheet E: Native patches and hooks (`src/Patches/`) (2026-09-27)

Reviewer scope (all read end to end): `src/Patches/AddScrapCallback.cpp` (131) / `.h` (25), `AiTargetSelect.cpp` (740) / `.h` (54), `BulletHitCallback.cpp` (168) / `.h` (23), `BulletInitCallback.cpp` (128) / `.h` (23), `Cheats.cpp` (78) / `.h` (30), `EngineFlameColor.cpp` (155) / `.h` (34), `GlobalTurbo.cpp` (238) / `.h` (54), `KillMessages.cpp` (134) / `.h` (26), `OrdnanceVelocity.cpp` (351) / `.h` (34), `ShotConvergence.cpp` (226) / `.h` (45), `ShotConvergenceMath.h` (173), `WeaponConvergenceMath.h` (487), `UnitVo.cpp` (1634) / `.h` (90), `WeaponMask.cpp` (57) / `.h` (27), `src/Patches.h` (36), root `unit_vo_notes.md` (91). Engine contract read first: `src/BasicPatch.h` (379), `src/Hook.h` (121), `src/InlinePatch.h` (147). Base: origin/main aec8c0a.

**Verification method.**
- Every patch site was disassembled with pefile+capstone from the unpacked GOG 2.2.301 image at `BZR-OpenShim/BZR64_RESEARCH/windows_x86/Battlezone98Redux_GOG.exe`. It has the same section layout as the installed GOG exe.
- The installed Steam exe is SteamStub-encrypted on disk, so it could not be used.
- Claims about built binaries were checked with `llvm-pdbutil` against:
  - `Release/exu.dll` + `exu.pdb`, built 2026-09-27 10:04 (newer than every file in scope).
  - `BZR-OpenShim/bin/Release/plugins/openshim.dll` + `.pdb`.
- Callers were grepped across `src/`, `include/`, `tests/`, `tools/`, the `exuExports[]` table and the sibling OpenShim tree.

**Lifetime fact used below.** `exu.dll` is loaded by Lua 5.1 `require` and is **not pinned**:
- There is no `GET_MODULE_HANDLE_EX_FLAG_PIN` and no self-`LoadLibrary`.
- `Overlay.cpp:454` uses `UNCHANGED_REFCOUNT`.
- So the `_LOADLIB` `__gc` frees the DLL when the mission state closes. `AiTargetSelect.cpp:593-597` relies on this explicitly.

Consequences:
- All namespace-scope state in this worksheet normally lives for exactly one Lua state.
- Every `Hook` and `InlinePatch` re-captures its "original" bytes each time a mission loads the DLL.

## 1. Section map

`src/Patches/UnitVo.cpp` (1634 lines). Status: production (queue interposer plus OpenShim bridge setters).

| Lines | What | Status |
|---|---|---|
| 40-60 | `g_unitVoMutex`, `g_lastUnitVoAttemptTick`, `NormalizeFilename` (lower-cases into a `std::string`) | production |
| 62-118 | anon ns: `ExecutableSection`; engine mirror `UnitVoQueueItem {char name[16]; owner; sound; priority; time; next}`; inspection and decision structs; the 52-byte and 35-byte call-site signatures; resolved-address globals | production |
| 120-262 | Private PE section walker, masked `FindPattern`, rel32 resolver, and `ResolveCallSite` (first match wins, no uniqueness check) | production; duplicates `SignatureResolver::GetExecutableSections/FindPattern/FindUniqueMaskedPattern` |
| 264-367 | Heuristic QueueCB prologue probe for the `q_list` storage (`83 3D imm32 00` within the first 0x30 bytes) and a `KillCBQueue` shape probe | production, fragile (E-4) |
| 369-549 | Filename heuristic, native queue walk, alternate selection (`std::rand`), throttle/dup/stale/depth decision | production |
| 551-602 | `QueueUnitVo` plus two identical naked thunks (`ret 0x0C`) | production |
| 604-625 | `InitializeUnitVoQueueHooks()`, run from an `inline` namespace-scope initializer at DLL load, then two `Hook`s of length 8 | production; loader-lock work (E-3) |
| 628-1037 | OpenShim bridge typedefs and 15 near-identical cached `Resolve*Bridge()` functions; limits; `CheckUnitVoFilename` | production, boilerplate |
| 1039-1217 | Lua get/set for throttle, depth, stale, mute and alternates | production |
| 1219-1305 | Thin OpenShim bridge toggles: under-attack alert, reticle popup, bomber range, howitzer volley, carrier bias, ODF tuning | production (misplaced in a "UnitVo" file) |
| 1307-1561 | `Set/Get/Clear/ClearAll AiUnitTuning` plus a handle-keyed mirror map | production (misplaced) |
| 1563-1633 | Turret pitch, attack reveal, jump-snipe crouch default, `ResetMissionHookOverrides`, `ResetOpenShimMissionOverrides` | production (misplaced) |

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| E-1 | [High/High] | `ShotConvergence.cpp:160-164` | `playerReticleShotConvergence` passes `&HoverCraftUpdateWeaponAimForReticle` to the **buffer** ctor `InlinePatch(uintptr_t, const void* payload, size_t, Status)`, which `memcpy`s 4 bytes *from* that address. Vtable slot `0x00889418` therefore receives the first four code bytes of the function (`55 8B EC F3`, i.e. `0xF3EC8B55`), not its address. | Calling `exu.SetPlayerReticleShotConvergence(true)` without the OpenShim bridge export crashes on the next TurretCraft `UpdateWeaponAim` virtual call. `GetPlayerReticleShotConvergence` then reports `true`. The sibling `shotConvergence` (line 31) only works because it passes `&walkerUpdateWeaponAim`, the address of a *data* constant that holds `0x0060F320`. | Use the templated value ctor with an explicit `uintptr_t`: `InlinePatch(slot, reinterpret_cast<uintptr_t>(&Fn), Status::INACTIVE, {0x30,0x09,0x5F,0x00})`, which also adds an expected preimage. Delete the `const void*` buffer overload for function pointers or make it `explicit`. | In `Release/exu.dll`, both dynamic initializers call the same ctor at `0x100101A0`. The playerReticle initializer pushes payload `0x10076820`, which the PDB names `HoverCraftUpdateWeaponAimForReticle`; the bytes there are `558BECF3`. The shotConvergence initializer pushes `0x100B3A04`, which holds `0x0060F320`. The GOG slot currently holds `0x005F0930`. |
| E-2 | [High/Med] | Every `Hook`/`InlinePatch` in scope (table in §4); `OrdnanceVelocity.cpp:163,281,302`; `UnitVo.cpp:624-625` | **No patch in `src/Patches/` supplies `expectedBytes`.** The DLL reloads per mission, so `BasicPatch` re-captures the "original" bytes at every mission load. If OpenShim has already detoured a site, its bytes become EXU's preimage. EXU then silently overwrites OpenShim's detour when it activates and restores it when it unloads. OpenShim detours the same 8/6/6-byte ordnance sites (`bzr_hooks.cpp:1237-1253`, SP-only, with a runtime owner-chain check), the UnitVo call sites, and the turbo bytes and hooks. `OrdnanceVelocity` has **no OpenShim deference at all**. | `ValidatePreimage` is defeated, and two owners write one site with no arbitration. While EXU's copy is active, OpenShim's MP gate and owner-chain validation are lost. If OpenShim installs after EXU, its expected-bytes check fails and it may exhaust its 200 retries. | Add the exact stock preimages; all of them are listed in §4 and verified against the GOG exe. EXU would then fail closed when another owner holds the site. For ordnance velocity, add an OpenShim ownership-export check as `GlobalTurbo` does (`OpenShimHasUnitTurboHooks`), or bridge to OpenShim. | Disassembled every site. Grepped OpenShim for each address. Overlaps: ordnance 3/3, turbo 4/4, convergence slots 2/2. No overlap: AddScrap, BulletHit/Init, KillMessages, WeaponMask, cheats, AiTargetSelect. |
| E-3 | [Med/High] | `UnitVo.cpp:604-621` | `inline uintptr_t g_unitVoQueueHooksInitialized = InitializeUnitVoQueueHooks();` runs in the CRT initializer, **inside `DllMain(PROCESS_ATTACH)` under the loader lock**, on every mission load. It performs two scans of all executable sections (GOG `.text` is 0x46737B bytes). It makes 4-6 `Logging::LogMessage` calls, each of which does `CreateDirectoryA`, `fopen_s` and `fclose` and initializes a function-static `std::mutex` and `unordered_set`. | `tools/validate_hardening.py:166-175` forbids exactly this work (`Logging::LogMessage`, log reset) in `dllmain.cpp`, but static initializers bypass that check. It adds tens of ms of scanning plus file I/O under the loader lock on every mission start. | Resolve lazily from `HandleLuaStateAttached` or `Init`, then construct or activate the hooks. Reuse `SignatureResolver::FindUniqueMaskedPattern` and require a unique match. Move the signatures into `exu.json` and the build profile. | Read `UnitVo.cpp:604-625` and `Logging.h:53-120`. `dllmain.cpp` does nothing else, so this work happens during static init. Both patterns match exactly once in GOG (`0x005F9321` and `0x005B656C`); both target QueueCB `0x0043D1D0`. |
| E-4 | [Med/Med] | `UnitVo.cpp:233-300, 612-617, 624-625` | EXU never stands down its Say/Recycle hooks when OpenShim owns the unit-VO policy. OpenShim writes `E8 → UnitVoQueueIntercept` at startup (`bzr_hooks.cpp:18400-18463`). EXU's wildcard signature still matches, so EXU resolves "QueueCB" to **OpenShim's intercept**. EXU then probes that intercept's prologue for `83 3D imm32 00`. In the current `openshim.dll`, that pattern (`cmp dword [g_BzrFn_UnitVoQueue],0`) is at +0x2A, one byte outside EXU's window (`offset+7 <= 0x30`). | A one-byte OpenShim codegen shift would make EXU treat `&g_BzrFn_UnitVoQueue - 4` as the native `q_list` head. EXU would then walk OpenShim `.data` as a `UnitVoQueueItem` linked list on every bark, causing an access violation or an endless loop. Today the effect is two stacked policies: EXU's defaults (depth 2, stale 2000) run on top of OpenShim's configured policy, while EXU's queue inspection is blind because the storage resolves to 0. | Stand down when OpenShim exports `OpenShimGetUnitVoThrottle`. Alternates are EXU-only and would need a bridge or a pre-filter. At minimum, reject a resolved rel32 target that lies outside the BZR image, and apply the RecycleTask==Say target-equality check that OpenShim uses. | Disassembled `openshim.dll` RVA `0x7A130` (PDB symbol `UnitVoQueueIntercept`) and found the pattern at +0x2A. Re-read the loop bounds in `ResolveQueueListStorageAddress`. Confirmed there is no `HasExport` gate before the `Hook`s are constructed. |
| E-5 | [Med/Med] | `BulletHitCallback.cpp:36-40,81`; `BulletInitCallback.cpp:33-37,68`; `AddScrapCallback.cpp:39-40`; `LuaHelpers.h:62-93` | The hook callbacks make Lua calls that can raise **outside any pcall**. `lua_getfield(L,-1,"BulletHit")` runs without a `lua_istable` check on `exu`. `Lua::PushMatrix` makes an unprotected `lua_call(SetMatrix)`, which raises if `SetMatrix` is nil or shadowed or `__index` errors. Bullet init and hit fire from simulation, not from Lua. With no protected frame, `lua_error` goes through `luaD_throw` to panic and `exit()`. If a pcall *is* on the Lua stack, the error longjmps across the naked-asm and game frames and skips `StackGuard`. BulletHit/BulletInit also lack the `L == nullptr` check that AddScrap has. | A mission script can crash the game with `SetMatrix = nil` or a strict-globals metatable. When the unwind happens, it also skips `popad/popfd`. | Build each callback as a `lua_CFunction` and run the whole body under `lua_cpcall`. Alternatively, build the matrix table directly instead of calling `SetMatrix`. Add `lua_istable` guards (AiTargetSelect already has them) and a null check on `L`. | `LuaHelpers.h:62-93` uses `lua_call`. `exu` is a global via `luaL_register` (`luaexport.cpp:955`). The hooks are ACTIVE after `Init` (`luaexport.cpp:485`). |
| E-6 | [Med/Med] | `AiTargetSelect.cpp:258-319, 384-395, 598-633` | Raw `VirtualProtect`+write outside the patch engine: 6 rel32 call rewrites and 5 vtable-slot writes. They are not gated by `BuildValidation::IsSupportedBzr2301()` and not registered in `deferredPatches`. `UnloadAllPatches()` in `HandleLuaStateClosing` therefore does **not** restore them; only `SlotRestoreGuard`'s static dtor at `FreeLibrary` does. `RestoreScoreCalls` rewrites the saved bytes without checking that the site still holds EXU's rel32. The slot restore does check. | This contradicts the engine rule that all game-code writes go through `BasicPatch`. If the DLL is ever pinned (E-9), these patches outlive the state. The unconditional restore can clobber a later owner's patch. | Express both kinds as `InlinePatch`es with `expectedBytes`. The call-site preimage is `E8 <rel to 0x00462070>` and the slot preimage is the expected implementation address. That gives the build gate and unload-at-close for free. Keep the RTTI check as an extra guard. | Read the whole file. The only caller of `Uninstall` is the static guard. All 6 call sites are `E8 → 0x00462070` followed by `83 C4 04`. GOG slot values are `0x583500` (×4) and `0x614020`. |
| E-7 | [Med/Med] | `GlobalTurbo.cpp:168-237`; `Cheats.cpp:59-77`; `OrdnanceVelocity.cpp:314-330` | EXU's fallback paths change gameplay with **no multiplayer gate**. This covers global and per-unit turbo when OpenShim lacks the exports, infinite ammo, infinite scrap, and ordnance velocity inheritance. OpenShim's own copies of turbo and ordnance velocity are hard SP-only (`bzr_hooks.cpp:18080` and `18009`). | Every client that runs the script changes unit physics or weapon state locally, which risks desync or one-sided cheating in network games. This is EXU's own feature policy, not a stock-fix ownership question. | Refuse or return `false` when `Multiplayer::isNetGame` (`Game/Multiplayer.h:29`) is set, or document these APIs as SP-only in `Definitions`. | Grepped `isNetGame` across `src/`: there are no uses in `src/Patches/`. |
| E-8 | [Med/High] | `GlobalTurbo.cpp:56-81, 83-142` | Per-unit turbo toggles **self-modifying code inside the per-unit simulation loop**. For each overridden unit whose override differs from the global setting, the BEGIN and END hooks call `SetStatus` on two patches. Each `SetStatus` does `ValidatePreimage` (VirtualQuery + memcmp), two `VirtualProtect`s and one `FlushInstructionCache`. That is about 4 VirtualProtect, 2 VirtualQuery and 4 flushes per unit per tick. The BEGIN thunk also runs C++ code (`Culling::UpdateUnit` and an `unordered_map` lookup) while the clamp result from `0x00447ED0` is still live in x87 `ST0`, because the stolen `fstp [eax+8]` only executes afterwards. | Syscall cost scales with the number of overridden units (estimated 0.3-0.6 ms/tick at 50 units). The x87 state breaks the ABI, since callees assume an empty FPU stack. `setTurboUnits` is keyed by handle and never pruned, so a recycled handle inherits a dead unit's override. | Use a data-driven gate instead of rewriting code: patch once so the `comiss` operand points at a per-unit float that the BEGIN hook sets (OpenShim reconciles the same way). Handle the pending `fstp`, or save and restore x87 state in the thunk. Prune `setTurboUnits` when `GetObj(h)` fails. | Disassembled `0x601C60-0x601CD9` and `0x447ED0`. `0x447ED0` leaves `eax = [ebp-0x68]` (set at `0x601C39`), which is why `mov eax,[eax+0x10]` works despite the comment saying "ecx". Read `BasicPatch::Reload/RestorePatch` and `SignatureResolver::MatchBytes`. |
| E-9 | [Med/Low] | `BasicPatch.h:192-211` with `Cheats.cpp:62,75`; `GlobalTurbo.h:44-46`; `UnitVo.h:32-36`; `KillMessages.cpp:29`; `EngineFlameColor.cpp:38`; `AiTargetSelect.h:36-41` | Mission reset of EXU-side patch state relies entirely on the DLL being unloaded. `ReleaseLuaStateBindings` and `Init` reset none of it; only `aiUnitTuning` is cleared (`UnitVo.cpp:1628`). `include/ExtraUtils.h:69` documents a static `dllimport` consumer model, which would pin the DLL. If a consumer does that: `m_requestedStatus==ACTIVE` for infinite ammo/scrap survives and `EnableDeferredPatchActivation()` re-applies it in the next mission. `setTurboUnits`, alternates, kill messages, flame colours and the AI dispatch flags also persist. | Gameplay cheats could carry over into the next mission. The README's "mission-scoped hook resets" would be false in that configuration. | In `HandleLuaStateClosing` or `Init`, reset requested statuses explicitly and clear the per-mission maps. Add a test that `Init` on a fresh state sees defaults. | Grepped for `LoadLibrary`, `GetModuleHandleEx` and `PIN`. Read `PublicAPI.cpp:122-146` and `luaexport.cpp:277-287, 474-518`. I did not check whether any shipped consumer actually static-links `exu.dll`, hence Low confidence. |
| E-10 | [Med/Low] | `BulletHitCallback.cpp:47-52`; `BulletInitCallback.cpp:44-49` | The code is `int len = strlen(odf); strncpy(formattedODF /*[16]*/, odf, len - 4); lua_pushlstring(L, formattedODF, len - 4);`. When `len < 4`, `len - 4` becomes a huge `size_t`, so `strncpy` zero-pads far past the 16-byte stack buffer. The code also always strips 4 characters, even when the name has no `.odf` suffix. | The result is a stack smash inside a hook thunk. The source field is `char odf[0x10]` at `OrdnanceClass+0x20` (`bzr.h:411`), so normal names (15 characters or fewer) are safe; only degenerate names trigger it. | Replace the copy with `size_t n = strnlen(odf, 16); if (n >= 4 && _stricmp(odf+n-4, ".odf") == 0) n -= 4; lua_pushlstring(L, odf, n);`. | Checked `bzr.h:399-414`. The asm passes `[OrdnanceClass]+0x20`. |
| E-11 | [Low/High] | `AddScrapCallback.cpp:123-126`; `UnitVo.cpp:1026-1036, 1144-1161, 1167-1215`; `KillMessages.cpp:120-124` | `std::string`/`std::vector` objects are live across `luaL_error`, `luaL_argerror` or `luaL_checktype`, so the longjmp skips their dtors and leaks them. Worse, `GetUnitVoAlternates` holds a `std::lock_guard<std::mutex>` across `lua_newtable`, `lua_pushlstring` and `lua_rawseti`. A `LUA_ERRMEM` at that point leaves `g_unitVoMutex` locked, and the next Say hook self-deadlocks or throws. | The argument-error cases only leak. The mutex case happens only on OOM, but it would hang the game thread. | Validate and copy inputs into plain locals before constructing any C++ object. Copy the alternates under the lock, release it, then push to Lua. Build error text with `lua_pushfstring` + `lua_error`. | Re-read each span and confirmed that every raising call comes after an object is constructed. The hardening validator still passes because `lua_pcall(L, 2, 1, 0)` is present. |
| E-12 | [Low/Med] | `BulletInitCallback.cpp:86-127`; `AddScrapCallback.cpp:54-86`; `KillMessages.cpp:53-87`; `GlobalTurbo.cpp:83-137`; `BulletHitCallback.cpp:111-160` | Thunk ABI hygiene has two gaps. (1) The callees are not `noexcept` and have no `try/catch(...)`. `QueueUnitVo` allocates and locks, `DoSelectiveTurboPatch` and `ProcessKillMessage` use STL containers, and Lua calls can allocate. A C++ exception would unwind into naked-asm and game frames and call terminate. (2) XMM preservation is inconsistent: BulletHit saves xmm0/2/3, OrdnanceVelocity saves xmm0-2, and the rest save none. | (1) Low-probability (OOM-driven) crashes that are hard to diagnose. (2) Register clobbering only matters where the host function keeps xmm values live across the site. | Mark every C++ function called from a thunk `noexcept` and give it a `try/catch(...)`. Save xmm0-7 in thunks that call Lua. | Traced each callee. There are no `noexcept` specifiers anywhere in `src/Patches`. |
| E-13 | [Low/Med] | `UnitVo.cpp:507-515` | The throttle is a sliding debounce: a throttled attempt still updates `g_lastUnitVoAttemptTick`. Steady chatter faster than `unitVoThrottleMs` therefore suppresses *all* barks indefinitely. OpenShim mirrors this logic (`bzr_hooks.cpp:18287-18290`). | The behaviour contradicts the documented "drop inside the window" semantics in `unit_vo_notes.md`. | Update the tick only for accepted barks, or document the behaviour as a debounce. | Re-read both copies. |
| E-14 | [Low/Med] | `OrdnanceVelocity.cpp:130-133, 250-253` | The thunks use `pextrd`, an SSE4.1 instruction. On pre-SSE4.1 CPUs this raises `#UD` and crashes on the first projectile spawn after the feature is enabled. | The game itself only requires SSE2. | Replace it with `movss` plus `shufps`/`movhlps` stores. | Read the asm. |
| E-15 | [Low/Med] | `OrdnanceVelocity.cpp:199-209` | CannonLead dereferences `[cannon+0x18]`, then `+0x8C`, then `+0x12C` on trust. OpenShim notes that this `+0x18` owner offset "could NOT be corroborated statically" and proves it at runtime instead (`bzr_hooks.cpp:1274-1281`). | A non-null but wrong pointer gives garbage velocity, which produces a wrong lead or an access violation. | Adopt OpenShim's runtime chain validation, or defer to OpenShim. | Cross-read OpenShim's comments at the same address. |
| E-16 | [Low/Low] | `AiTargetSelect.cpp:485-496, 562-579` | `*rangeLimit` is rewritten to the native candidate's horizontal distance **before** the Lua override is applied. An override target is then returned with the old candidate's range, and a veto returns `nullptr` with an already-modified range. | Downstream engine code sees an inconsistent target/range pair. | Recompute the range after the override, or leave it untouched on veto. | Confirmed `0x00462070` computes `x²+z²`, so without an override the rewrite matches the stock metric (good). |
| E-17 | [Low/Med] | `WeaponMask.cpp`, `KillMessages.cpp`, `Cheats.cpp` (via `bzr.h:181-183`) | Patch addresses live in `bzr.h` and in feature files, not in `exu.json`. This contradicts `ARCHITECTURE.md`: "Do not add new raw BZR addresses ... inside feature code". | New-build qualification cannot see these 29 code addresses. | Catalogue them with signatures. §4 lists the verified bytes. | §4 grep. |

Offending code for the High and Med findings:

```cpp
// E-1  ShotConvergence.cpp:160
InlinePatch playerReticleShotConvergence(
    hovercraftWeaponAimVftableEntry,
    &HoverCraftUpdateWeaponAimForReticle,   // -> const void* payload: memcpy of CODE bytes
    4,
    InlinePatch::Status::INACTIVE);
```
```cpp
// E-2  (representative) OrdnanceVelocity.cpp:163 - no preimage, no OpenShim deference
Hook ordnanceVelocityPatch(0x004803D4, &OrdnanceVelocityPatch, 8, BasicPatch::Status::INACTIVE);
```
```cpp
// E-3  UnitVo.cpp:621 - CRT static init == DllMain loader lock; full .text scans + file logging
inline uintptr_t g_unitVoQueueHooksInitialized = InitializeUnitVoQueueHooks();
```
```cpp
// E-4  UnitVo.cpp:280 - window stops 1 byte short of OpenShim's matching cmp at +0x2A
for (size_t offset = 0; offset + 7 <= kCmpScanWindow; ++offset)
```
```cpp
// E-5  BulletHitCallback.cpp:39-40 / LuaHelpers.h:70
lua_getglobal(L, "exu");
lua_getfield(L, -1, "BulletHit");      // raises if exu is not indexable - no pcall active
...
lua_call(L, 12, 1);                    // PushMatrix -> SetMatrix, unprotected
```
```cpp
// E-6  AiTargetSelect.cpp:287-295 - raw write outside BasicPatch, not unloaded at Lua close
if (!VirtualProtect(site, patch.original.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) ...
std::memcpy(site + 1, &relative, sizeof(relative));
```
```cpp
// E-8  GlobalTurbo.cpp:67-70 - code rewrite per unit per tick
if (setTurboUnits.contains(h)) {
    turboPatch1.SetStatus(setTurboUnits.at(h));
    turboPatch2.SetStatus(setTurboUnits.at(h));
}
```
```cpp
// E-10 BulletHitCallback.cpp:47-52
int len = strlen(odf);
char formattedODF[16];
strncpy(formattedODF, odf, len - 4);
```

### Answers to the task's specific questions

**Q1. Patch census.**
- The full table is in §4: 19 engine-managed instances plus 11 raw writes (AiTargetSelect).
- All of them are static namespace-scope objects; none are local or temporary.
- **None supply `expectedBytes`.**
- None of the in-exe addresses has an `exu.json` entry. The UnitVo sites are signature-resolved in code.
- Raw `VirtualProtect` + write outside the engine exists only in `AiTargetSelect.cpp` (E-6).

**Q2. Hook thunks.**

| Thunk | Convention and stack discipline | Lua and exception behaviour |
|---|---|---|
| AddScrap | Naked. Runs the stolen `mov [ebp-4],ecx; mov ecx,[ebp-4]` first, then `pushad/pushfd`, a cdecl call, and `ret`. | Callback via `lua_pcall(L,2,0,0)`. The `getfield` is unguarded (E-5). |
| BulletHit | Naked. Stolen bytes run first. Saves xmm0/2/3. | Callback via `pcall(5)`. `PushMatrix` does an unprotected `lua_call` (E-5). |
| BulletInit | Naked. Stolen bytes are re-executed after `popad`. No xmm save. | Same as BulletHit (E-5). |
| KillMessages | Naked. Calls cdecl C++ that does `strncpy` into the 33-byte native buffers. | No Lua. Safe: the game NUL-terminates both names at index 0x20 (verified at `0x6261E1` and `0x626282`), and the Lua setter limits input to 32 bytes. |
| UnitVo Say/Recycle | Naked. Re-pushes the 3 args, calls cdecl `QueueUnitVo`, and returns with `ret 0x0C`, which replaces the stolen `add esp,0Ch`. The return value in `eax` is preserved. | No Lua. Uses `std::mutex` and `std::string`; not `noexcept` (E-12). |
| AiTargetSelect slots | `__fastcall` stand-ins for `__thiscall` (ecx=this, edx dummy, stack arg popped by the callee). Correct. | Lua under `lua_pcall` with `lua_istable`/`lua_isfunction` guards and a re-entrancy flag. |
| AiTargetSelect score stubs | Naked. Leave the caller's `add esp,4` in place and return a float in ST0, like `0x00462070`. | Same guards as the slot hooks. The Lua callback runs **inside** the stock candidate loop, so a script that builds or removes objects there may invalidate the engine iterator (unverified hazard). |

EngineFlameColor has **no hook in EXU**:
- It is only a handle-free `std::unordered_map<int,color>` plus an `extern "C"` cdecl export, `EXU_GetTeamEngineFlameColor`.
- OpenShim does the call-site routing. It re-resolves the export via `GetModuleHandleA` + `GetProcAddress` on every lookup, checks module identity, and wraps the call in SEH (`bzr_hooks.cpp:27188-27260`).
- The ABI matches: `int(__cdecl*)(int)`, values 0-3.

**Q3. Hooks that call Lua.**
- **Registry refs:** no hook holds one. Each event looks up `exu.<Name>` in `_G` of the current `Lua::state`. So `ReleaseLuaStateBindings` correctly has nothing to release, and no reference can go stale across state generations. The cost is two table lookups per event.
- **Callback errors:** caught by `lua_pcall` and routed to the game's handler `LuaCheckStatus` (`0x004FF600`, a raw uncatalogued address). There is no disable-after-N-errors, so a broken `AiTargetScore` reports once per candidate per scan.
- **Before a callback is registered:** the lookup yields nil and the hook returns immediately.
- **Null state:** AddScrap and AiTargetSelect check `L == nullptr`. BulletHit/Init do not. They are safe only because hooks are activated in `Init` after `state = L`, and `UnloadAllPatches` runs before `state.Clear`.

**Q4. Multiplayer gating and OpenShim overlap.** Neither the cheats nor the turbo and ordnance fallbacks are MP-gated (E-7). The shared-site overlap is E-2. Ownership of engine sites that both repos can write:

| Site | Does EXU defer to OpenShim? |
|---|---|
| Turbo bytes `0x601CA3` / `0x601CB5` | Partial. EXU defers only if OpenShim exports `OpenShimSetGlobalTurbo`. The EXU END hook still writes these bytes when OpenShim exports SetGlobalTurbo but not `OpenShimHasUnitTurboHooks`. |
| Turbo hooks `0x601C92` / `0x601CCD` | Yes, via export presence. |
| Ordnance `0x4803D4` / `0x48F658` / `0x48F639` | **No.** |
| Convergence vtable slots `0x88A4FC` / `0x889418` | Yes, via bridge exports. |
| UnitVo call sites | **No.** EXU layers its hooks over OpenShim's (E-4). |
| AI process vtables | No clash. AiTargetSelect patches slot +0xE4; OpenShim patches slot 11 (+0x2C) on the same vtables. |

**Q5. UnitVo.** Covered in §1, E-3, E-4, E-11 and E-13. Queue data structure, bounds and pruning:
- The native queue is walked read-only; EXU keeps no queue of its own.
- Alternates are bounded to 16 entries per filename of 1-15 characters, keyed by the normalized filename, and never pruned except by `nil` or DLL unload.
- The Say hook runs on the game main thread, so the mutex is uncontended.
- Queue names are read from the fixed `char[16]` engine buffer. There is no `strcpy`; `std::string` is used throughout.

**Q6. AiTargetSelect per-tick cost and stock fallback.**
- **Slot hook, per scan:** 1 SEH owner read, 2 SEH `GetHandle` calls, `getglobal`/`getfield`, and 1 pcall when enabled. No allocations and no `VirtualQuery`.
- **Score stubs, per candidate distance evaluation while scoring is enabled:** the stock magnitude, 2 SEH `GetHandle` calls, then 1 pcall. The `GetHandle` calls happen **before** checking whether `exu.AiTargetScore` exists. Check for the function first, and cache a "present" flag per frame.
- **Fallback when disabled or no Lua callback:** the hook calls the original implementation directly. Scores return the stock `x²+z²` and the stock hard range gates are preserved, including the 70 m lane-2 cap. The `rangeLimit` rewrite equals the stock metric, so this is the exact stock path apart from E-16.

**Q7. Float math.**
- Ordnance velocity and convergence null-check their pointer chains in asm.
- There is no NaN check on the inherited velocity. The values come from the engine and the ratio is fixed at 1.0.
- `ShotConvergenceMath::Normalize` rejects non-finite values and lengths below epsilon squared, so NaN cannot reach the engine from the reticle path.
- `DispatchScore` rejects non-finite scores and clamps before squaring.
- `DotProduct` has no division.

**Q8. Mission-scoped resets.** See §5 and E-9.

**Q9. Raw address census.** See §4.

## 3. Dead / unreferenced code

| Symbol | Location | Verification |
|---|---|---|
| `mortarLeadPositionPatch` Hook, the `MortarLeadPositionPatch` thunk, and the global `float x = 500.0f` (an external-linkage symbol named `x`) | `OrdnanceVelocity.cpp:282-299` | Grepped `src`, `include`, `tests` and `tools`: the only reference is the definition. The hook is never activated. |
| `GetOrdnanceVelocRatio`, `SetOrdnanceVelocRatio` | `OrdnanceVelocity.h:33-34` | Declared but never defined; absent from `exuExports[]` and `Definitions`. |
| `velocInheritRatio` | `OrdnanceVelocity.cpp:33` | Never written after initialization, so it is effectively a 1.0 constant read by the asm. |
| `WeaponConvergenceMath.h` (487 lines, `wip(convergence)` commit 2cd3bf2) | whole file | Not included by any `.cpp` and not in the vcxproj. Its header cites `tests/host/weapon_convergence_math_tests.cpp`, which does not exist. |
| `BulletHitCallback.h`, `BulletInitCallback.h` | whole files | Each contains only an empty `namespace ExtraUtilities {}` and is included only by `Patches.h`. |
| `#include <fstream>` | `KillMessages.cpp:25` | Unused. |
| Value of `g_unitVoQueueHooksInitialized` | `UnitVo.cpp:621` | Never read. The variable exists only for its initializer's side effect (E-3). |

Count: 7 items, about 520 lines, most of it in `WeaponConvergenceMath.h`.

## 4. Raw address census

Literal `0x00[4-9A-F]xxxxx` count per file in scope:

| File | Count |
|---|---|
| `AiTargetSelect.cpp` | 14 |
| `WeaponConvergenceMath.h` | 11 (all in comments; the file is dead) |
| `ShotConvergence.h` | 6 |
| `GlobalTurbo.h` | 4 |
| `OrdnanceVelocity.cpp` | 4 |
| `AddScrapCallback.cpp` | 1 |
| `BulletHitCallback.cpp` | 1 |
| `BulletInitCallback.cpp` | 1 |
| `KillMessages.cpp` | 1 |
| Every other file in scope | 0 |
| **Total** | **43** |

These files also consume addresses from `bzr.h` and `LuaHelpers.h`: `0x004A7709`, `0x005E10D7`, `0x0060A8C6`, `0x004FF600`, `0x00462380`, `0x00917AFC`, `0x025CE79C`.

`exu.json` coverage:
- **No code address used by these files is catalogued with a signature.**
- The only literal that appears in `exu.json` is `0x008203F0`, which occurs only in a comment in the dead `WeaponConvergenceMath.h`.
- `GameObject.GetHandle` (`0x00462380`), `p_userObject` and `Reticle.position` are catalogued, but without signatures.

**Patch/hook census.**
- All entries are namespace-scope static objects.
- `expectedBytes` is supplied for **none** of them.
- The "verified preimage" column gives the stolen bytes checked against GOG 2.2.301. Each can be supplied as `expectedBytes` as-is.

| # | file:line | Class | Target | Len | Initial status | Verified preimage |
|---|---|---|---|---|---|---|
| 1 | `AddScrapCallback.cpp:87` | Hook | `0x005E1016` | 6 | ACTIVE | `89 4D FC 8B 4D FC` |
| 2 | `BulletHitCallback.cpp:168` | Hook | `0x00480771` | 6 | ACTIVE | `8B 48 14 83 C1 38` |
| 3 | `BulletInitCallback.cpp:128` | Hook | `0x00480363` | 6 | ACTIVE | `8B 55 E0 8B 42 14` |
| 4 | `Cheats.cpp:47` | Hook | `0x004A7709` | 8 | INACTIVE | `8B 45 08 35 33 33 33 33` |
| 5 | `Cheats.cpp:48` | Hook | `0x005E10D7` | 8 | INACTIVE | `8B 45 08 35 33 33 33 33` |
| 6 | `GlobalTurbo.cpp:47` | InlinePatch<float*> | `0x00601CA3` | 4 | INACTIVE | `04 26 8A 00` |
| 7 | `GlobalTurbo.cpp:48` | InlinePatch (NOP) | `0x00601CB5` | 2 | INACTIVE | `76 0C` |
| 8 | `GlobalTurbo.cpp:109` | Hook | `0x00601C92` | 6 | ACTIVE unless OpenShim owns turbo | `8B 45 90 D9 58 08` |
| 9 | `GlobalTurbo.cpp:138` | Hook | `0x00601CCD` | 9 | ACTIVE unless OpenShim owns turbo | `8B 55 90 8B 85 78 FF FF FF` |
| 10 | `KillMessages.cpp:88` | Hook | `0x0062627F` | 8 | ACTIVE | `8B 55 A8 C6 44 15 B4 00` |
| 11 | `OrdnanceVelocity.cpp:163` | Hook | `0x004803D4` | 8 | INACTIVE | `8B 4D F0 89 08 8B 55 F4` |
| 12 | `OrdnanceVelocity.cpp:281` | Hook | `0x0048F658` | 6 | INACTIVE | `8B 45 E0 8B 48 0C` |
| 13 | `OrdnanceVelocity.cpp:299` | Hook (dead) | `0x0056B254` | 6 | INACTIVE | `89 41 08 8B 4D 0C` |
| 14 | `OrdnanceVelocity.cpp:302` | InlinePatch (NOP) | `0x0048F639` | 6 | INACTIVE | `0F 86 F0 01 00 00` |
| 15 | `ShotConvergence.cpp:31` | InlinePatch (buffer) | vtable slot `0x0088A4FC` | 4 | INACTIVE | `90 B5 4E 00` (`0x004EB590`) |
| 16 | `ShotConvergence.cpp:160` | InlinePatch (buffer), **bug E-1** | vtable slot `0x00889418` | 4 | INACTIVE | `30 09 5F 00` (`0x005F0930`) |
| 17 | `UnitVo.cpp:624` | Hook | scanned; GOG `0x005F9321` | 8 | ACTIVE | `E8 rel32(→0x0043D1D0) 83 C4 0C`; signature-guarded, not catalogued |
| 18 | `UnitVo.cpp:625` | Hook | scanned; GOG `0x005B656C` | 8 | ACTIVE | same shape as #17 |
| 19 | `WeaponMask.cpp:42` | Hook | `0x0060A8C6` | 9 | ACTIVE | `89 51 1C 8B 85 F0 FE FF FF` |
| R1-R6 | `AiTargetSelect.cpp:76-79` | raw rel32 write | `0x004634A5`, `0x00463593`, `0x00463670`, `0x00463A46`, `0x00463B34`, `0x00463C11` | 5 | on Lua enable | self-checked: `E8 → 0x00462070` |
| R7-R11 | `AiTargetSelect.cpp:94-100` | raw vtable-slot write | slot +0xE4 of `0x0088A6EC`, `0x0088A5C0`, `0x0088AB9C`, `0x0088B178`, `0x0088AF98` | 4 | on Lua enable | self-checked: RTTI name + current value `0x583500` / `0x614020` |

Every stolen length ends on an instruction boundary, and every thunk replays or replaces exactly the stolen instructions.

For hooks longer than 6 bytes, the return address (site + 6) falls mid-instruction in the original bytes. This is harmless only because none of those thunks can trigger its own `Unload`. It is a documented invariant, not an enforced one.

Addresses that are CALLED rather than merely compared:
- `0x00583500` and `0x00614020`: the ChooseAttackTarget originals.
- `0x00462070`: VectorMagnitude.
- `0x00462380`: GameObject::GetHandle.
- `0x005F0930`: TurretCraft::UpdateWeaponAim per OpenShim RTTI; "HoverCraft" in EXU is a misnomer.
- `0x00417F60`: Carrier::GetWeapon.
- `0x00681A00`: RefreshWeaponTransform.
- `0x004FF600`: LuaCheckStatus.
- `0x0043D1D0` (QueueCB) and KillCBQueue: resolved at runtime.

## 5. Lifetime / ownership notes

| State | Declared lifetime | Actual lifetime | Reset at the right boundary? |
|---|---|---|---|
| All `Hook`/`InlinePatch` objects and their `m_originalBytes` | process (static) | one DLL load, i.e. one mission | Preimages are re-captured every mission, which is why the missing `expectedBytes` is dangerous (E-2). `UnloadAllPatches` in `HandleLuaStateClosing` restores the patches before `state.Clear`. |
| `m_requestedStatus` for cheats, ordnance and turbo (note: `Reload`/`Unload` bypass `m_requestedStatus`) | process | DLL load | Only if the DLL unloads (E-9). |
| AiTargetSelect slots and score calls, plus `dispatchEnabled`/`scoreDispatchEnabled` | process | DLL load | Restored only by the static dtor, not at Lua-state close (E-6). Between state close and `FreeLibrary`, the hooks see `Lua::state == nullptr` and pass through, which is safe. |
| `setTurboUnits` (keyed by handle) | process | DLL load | Never pruned within a mission, so recycled handles inherit stale overrides (E-8). |
| `unitVoAlternates`, throttle, depth, stale, muted, `g_lastUnitVoAttemptTick` | process | DLL load | These are EXU-local copies. OpenShim's side is reset by `ResetOpenShimMissionOverrides()` at `Init`, so the two sides agree only while the DLL actually unloads. |
| `aiUnitTuning` mirror | process | cleared at `Init` | Yes. |
| `messageMap`, `teamEngineFlameColors` | process | DLL load | Reset only by DLL unload. OpenShim re-resolves the flame export on every call, so it never holds a stale pointer. |
| Cached OpenShim `Resolve*Bridge()` pointers (`static attempted`) | DLL load | DLL load | Safe, because `winmm.dll` is never unloaded. A missing export stays cached as null for the mission (fails closed). |
| Lua callback lookups | per event | per event | No registry refs, so nothing to release. |

**`unit_vo_notes.md` (repo root).** It is a dated investigation (2026-03-16).
- Its "Verification" section is stale: it says the full solution build was blocked.
- Its description of queue behaviour still matches the code, and its API list matches `exuExports[]`.
- Recommendation: move it to `Docs/Research/UNIT_VO_QUEUE_HOOK_20260316.md`. Add a one-line status header saying the global policy is now owned by OpenShim and EXU layers alternates and overrides on top (E-4).
- It should not stay at the repo root.

## 6. Performance notes

| Path | Cost per event | Notes |
|---|---|---|
| Per-unit turbo | Up to about 4 `VirtualProtect`, 2 `VirtualQuery` and 4 `FlushInstructionCache` per overridden unit per sim tick | E-8. Even units without an override pay one `Culling::UpdateUnit` and one `unordered_map` lookup per tick in the BEGIN hook, which is the intended culling piggyback. |
| AiTargetScore | One pcall per candidate distance evaluation per target scan while enabled | The 2 SEH `GetHandle` calls run before the "is `AiTargetScore` defined" check. |
| BulletInit / BulletHit, no callback defined | 2 table lookups per ordnance spawn or hit | |
| BulletInit / BulletHit, callback defined | Per bullet: an extra Lua call to `SetMatrix` with 12 args, 2-3 lightuserdata pushes, and the pcall | This is the dominant per-shot cost in missions that define `exu.BulletInit`. |
| UnitVo bark | 2-3 `std::string` allocations, a walk of the short native queue, and one mutex lock | Low frequency, which is fine. The one-time cost is two full `.text` linear scans per mission load under the loader lock (E-3). |
| `Logging::LogMessage` | n/a | In this scope it is called only on install paths, never per frame. |

## 7. Patterns worth keeping

- **AiTargetSelect preimage discipline.** It checks the RTTI name (e.g. `.?AVWingmanProcess@@`) and the expected current implementation before writing a vtable slot. It restores a slot on uninstall only if the slot still holds EXU's hook. It verifies each rel32 call target (`E8 → 0x00462070`) before rewriting it. This is the discipline the rest of the folder lacks.
- **GlobalTurbo's ownership decision.** It decides ownership once, before constructing the hooks, from OpenShim export presence (`OpenShimHasUnitTurboHooks`). It also documents why it will not race a late OpenShim install. UnitVo and ordnance velocity should copy this.
- **`DispatchScore` safety.** It preserves the stock hard range gates (including the 70 m lane-2 cap), rejects non-finite results, clamps before squaring, and guards against re-entrancy. Lua can only adjust the comparison metric, never eligibility.
- **`ShotConvergenceMath.h`.** Pure, Windows-free math with host tests (`tests/host/shot_convergence_math_tests.cpp`). It keeps the mount-local translation, following the engine convention.
- **`AddScrapSilent` sequencing.** It calls pcall, restores the hook, and only then re-raises. That is the right shape for a temporary-patch lifetime; only the `std::string` needs to go (E-11).
- **UnitVo input handling.** The Lua setters bound every input, and the 1-15 character filename limit matches the engine's `char name[16]`. It never flushes non-unit radio chatter.
- **Stolen-byte placement.** Thunks that replay stolen bytes place them consistently and correctly: either before `pushad` (AddScrap, BulletHit) or after `popad` (BulletInit, turbo, ordnance).

## 8. Low-severity items

- **Wrong register in a comment** (`GlobalTurbo.cpp:88-94`). The comment says "ecx has the unit task", but the code uses `eax`. It only works because `0x00447ED0` preserves `eax = [ebp-0x68]`, a fragile register-flow dependency that should be commented.
- **Misnomers** (`ShotConvergence.h:31-32`). `hovercraftWeaponAimVftableEntry` and `HoverCraftUpdateWeaponAim` are misnamed: per OpenShim RTTI, `0x005F0930` is `TurretCraft::UpdateWeaponAim`.
- **Idiom drift in patch toggling.** `SetOrdnanceVelocInheritance` and the fallback paths of `SetGlobalTurbo`/`SetShotConvergence` call `Reload()`/`Unload()` directly, bypassing `m_requestedStatus`. `Cheats` uses `SetStatus`.
- **`SetOrdnanceVelocMode` style.** It calls `luaL_argerror` without `return`. This is harmless because the call longjmps, but inconsistent. It also does `lua_pushinteger(L, bool)` for the mode.
- **`AddScrapSilent` return value.** It is documented as `@return number newScrapCount`, but it returns whatever stock `AddScrap` returns. Not verified.
- **Missing callback docs** (`Definitions/ExtraUtils.lua`). There is no signature documentation for the callbacks `exu.AddScrap`, `exu.BulletHit`, `exu.BulletInit`, `exu.AiTargetSelect` and `exu.AiTargetScore`. About 20 patch APIs in scope are `(...)` stubs (`:2465-2575`).
- **Misplaced code in `UnitVo.cpp`.** It hosts about 600 lines of unrelated OpenShim AI and HUD bridge setters (§1), which should be split into e.g. `OpenShimAiBridge.cpp`. The 15 copy-pasted `Resolve*Bridge()` functions could become one templated cached resolver.
- **Include case.** Includes use both `"BZR.h"` and `"bzr.h"` for the same file. This is harmless on NTFS but breaks case-sensitive host builds.
- **vcxproj.** `ShotConvergenceMath.h` is missing from the `ClInclude` list (cosmetic).
- **Unguarded aim calls.** `ApplyReticleConvergence` calls `CarrierGetWeapon` and `RefreshWeaponTransform` raw on every aim tick with no SEH guard. The pointer chain comes from engine state and is only null-checked.
- **Shared RNG.** `SelectUnitVoFilenameLocked` uses `std::rand()`, which shares CRT state with the game (minor).
- **No circuit breaker.** An erroring AiTargetSelect Lua callback is reported through `LuaCheckStatus` once per candidate per scan, with nothing to disable it after repeated failures.

## Unverified

- Steam runtime bytes. The on-disk Steam exe is encrypted, so all preimages were checked against the GOG image only.
- Whether any shipped consumer statically imports `exu.dll` and so pins it (E-9).
- What stock `AddScrap` returns.
- Whether a script that mutates objects inside `AiTargetScore` or `BulletHit` corrupts engine iteration. This was reasoned, not tested.
