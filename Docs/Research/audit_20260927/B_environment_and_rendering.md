# Worksheet B: Environment, particles, sky, lighting mode, camera/satellite/culling, Ogre ABI layer (2026-09-27)

Reviewer scope (read end to end): `src/Game/Environment.cpp` (5822 lines; the brief's "5092" is stale), `Environment.h` (168), `Culling.cpp/.h` (66/27), `StaticGeometry.cpp/.h` (761/27), `Satellite.cpp/.h` (108/48), `Camera.cpp/.h` (398/63), `src/Ogre/Ogre.h` (424), `OgreBuildSettings.h` (44), `OgreMaterialShim.h` (363), `OgreSceneManagerShim.h` (72), `OgreStringInterfaceShim.h` (132), `OgreParameterValue.h` (140), `OgreRenderSpace.h` (68); context: `Workshop/exu_weather.lua` (662), `exu_weather.particle` (352), `exu_weather.material` (74), `exu_ogre_particle.program`, `tests/host/weather_controller_test.lua` (515). Base: origin/main aec8c0a.

Method notes. Ogre ABI claims were checked against the committed 1.10 headers and against the shipped Steam `OgreMain.dll` (export table parsed by hand because pefile truncates at 8192 of its 9194 names; capstone for disassembly). All 109 mangled names that my files pass to `GetProcAddress` exist in that DLL, and all 66 raw OgreMain RVAs in `Ogre.h`/`bzr.h` land on the export (or its ILT thunk target) they claim to be, with one misnaming (B-9). GOG's OgreMain was not available; I assume it is byte-identical.

## 1. Section map

### `src/Game/Environment.cpp`

| Lines | What | Status |
|---|---|---|
| 1-43 | includes | - |
| 44-101 | anon ns open; `ViewportLightingMode`, raw `Ogre::Viewport` layout mirror (`ViewportMaterialSchemeLayout`, verified against `OgreViewport.h`) | production (legacy path) |
| 103-256 | scheme-name model: modern/`og-`/`en-` prefixes, normalisation, Lua mode parsing | production (legacy path) |
| 258-304 | read/write `Viewport::mMaterialSchemeName` by raw layout under SEH | production (legacy path) |
| 306-331 | `WriteEnvironmentDebug` / `LogEnvironmentDebug` (ungated, see B-1) | diagnostic |
| 333-531 | Root / RenderSystem / SceneManager viewport discovery (`GetActiveViewports`) | production |
| 532 | anon ns closes; everything from here to 5745 has external linkage | - |
| 534-585 | `DescribeLuaCaller`, finiteness/time-of-day validators | diagnostic + production |
| 587-800 | SEH wrappers over `Ogre.h` raw-RVA light/ambient calls; native `SetTimeOfDay`/`RefreshTerrainMasterLight` (exe calls) | production |
| 802-856 | sky node/params probes (dllimport'd SceneManager getters) | production |
| 858-1142 | sky enable/setSkyBox/Dome/Plane via exported names; `TryReadSkyPlane` | production |
| 1144-1308 | particle: typedefs + ~25 `GetProcAddress` resolvers | production (Ogre ABI that belongs in `src/Ogre/`) |
| 1310-1864 | particle core: has/get/create/destroy, node position/direction, system setters, `TryGetParticleMovableObject` re-basing | production |
| 1866-2102 | attachment/emitter/affector typedefs and resolvers, follower list | production |
| 2104-2476 | followers, active camera, sim->render conversion, reparent, attach to root/camera/object/bone | production |
| 2478-2576 | emitter/affector index getters | production |
| 2578-2757 | generic `StringInterface` bridge (get/set/enumerate) | production |
| 2759-2938 | typed emitter setters | production |
| 2940-3003 | return-to-own-node, non-visible timeout, `ForgetAllParticleCameraFollowers` | production |
| 3005-3137 | viewport overlays get/set + "flip to refresh" hack; `GetSceneManager` etc. | production (legacy path) |
| 3139-3187 | Lua: gravity, fog (`fog` Scanner, B-5) | production |
| 3189-3589 | Lua: sun ambient/diffuse/specular/direction, time of day, power scale, shadow far distance; 4-6 debug lines per call | production API + diagnostic tracing |
| 3591-3842 | Lua: sky getters/setters | production |
| 3844-4532 | Lua: particle bindings (create/destroy/attach/emitter setters) | production (weather path) |
| 4534-4781 | Lua: generic emitter/affector parameter bindings (templates) | production (weather path) |
| 4783-4959 | Lua: bounding boxes, debug shadows, viewport shadows/overlays | diagnostic API |
| 4961-5012 | Lua: `GetRetroLightingMode`/`GetLightingMode` (bridge first, legacy fallback) | production |
| 5014-5070 | Glow compositor gating for retro | production (legacy path) |
| 5072-5147 | game `Viewport::setMaterialScheme` call-site hook (3 sites, IAT 0x00869810) | production (legacy path, stands down under OpenShim render-profile ABI) |
| 5149-5418 | lighting-mode apply/enforce + `SetLightingMode`/`SetRetroLightingMode`/`EnforceLightingMode` | production (legacy path) |
| 5420-5605 | OpenShim render-profile bridge bindings | production |
| 5607-5649 | `InstallGameViewportSchemeHooks` | production (legacy path) |
| 5651-5744 | visibility mask, `HasSky*Node` | diagnostic API |
| 5747-5822 | `Patch::fogResetPatch` (RET at 0x00683370), `TryInitializeOgre`/`ResetOgreInitialization` | production |

No dead or experimental sections; the only dead symbol is one resolver (section 3). "Legacy path" means code that only runs when OpenShim's render-profile ABI is absent.

Candidate split (each boundary is already a clean seam; no shared statics cross them except the log helper and `GetSceneManager`):
1. `src/Ogre/OgreParticleShim.h` / `OgreSceneShim.h`: every mangled name, typedef and `Resolve*` (1144-1308, 1884-2102, 858-942) plus `ViewportMaterialSchemeLayout`. ARCHITECTURE.md puts Ogre ABI under `src/Ogre/`, and StaticGeometry/OgreMaterialShim each carry their own `ResolveOgreProc` copy today.
2. `Game/ParticleRuntime.cpp` (1310-3003, C++ ops, no Lua) + `Game/ParticleBindings.cpp` (3844-4781).
3. `Game/EnvironmentLighting.cpp`: fog, gravity, sun, time of day (587-800, 3139-3589, 5747-5822).
4. `Game/EnvironmentSky.cpp` (802-1142, 3591-3842, 5701-5744).
5. `Game/ViewportLightingMode.cpp`: legacy scheme model, call-site hook, Glow gating, render-profile bindings (44-531, 3005-3137, 4961-5649).
6. `Game/SceneDebug.cpp` (4783-4959, 5651-5699).
7. A shared `EnvironmentLog.h` once B-1 is fixed.

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| B-1 | [High/High] | Environment.cpp:306-331, 3212-3243, 3281-3312; exu_weather.lua:453-468 | `LogEnvironmentDebug` has no gate. `SetSunAmbient`/`SetSunDiffuse` (and specular/direction/power/shadow/time) write 4 lines per call (enter/parsed/calling/completed) plus a `lua_getinfo` for the caller. Each line takes a mutex, calls `OutputDebugStringA` twice, runs `GetLogFilePath` twice (`GetModuleFileNameA` + `CreateDirectoryA` each time, ~6 string allocations), and opens, appends to and closes an `std::ofstream`. Every non-clear weather profile has `ambientScale` != 1, so `Weather.Update` calls `SetAmbientLight` and `SetSunDiffuse` every frame. | That is 8 file open/close cycles and ~80 syscalls per frame from logging alone: ~480 opens/s at 60 fps. `exu_environment_debug.log` grows ~40-60 KB/s (~150-200 MB/h), bounded only by process restart. This is the dominant per-frame cost of the weather runtime and a stutter source with AV on-access scanning. | Gate behind an explicit switch (`exu.SetEnvironmentDebug`/ini), keep one `FILE*` per Lua state, drop the success-path trace lines, and log failures once per site. The same applies to `LogMaterialDebug` (GameObject.cpp:270, another worksheet). | Read `WriteEnvironmentDebug`, `Logging.h` 16-89; traced `Weather.Update` -> `ApplyEnvironment` for all 4 profiles; confirmed no flag or `#ifdef` in any `LogEnvironmentDebug` caller. |
| B-2 | [High/High] | Environment.cpp:1286-1290, 1803-1822, 4054-4074 | `SetParticleSystemRenderQueueGroup` calls `?setRenderQueueGroup@ParticleSystem@Ogre@@UAEXE@Z` with the `ParticleSystem*`. That function overrides a **MovableObject** virtual (`OgreParticleSystem.h:556`, `OgreMovableObject.h:372`), so under the MSVC ABI it takes `this` = the MovableObject subobject (0x20 bytes in, after the `StringInterface` primary base). Disassembly of the shipped function (RVA 0x2cc1d0): it forwards `ecx` unchanged to `MovableObject::setRenderQueueGroup`, which writes `[ecx+0x48]`/`[ecx+0x49]`, then loads `mRenderer` from `[ecx+0x1d4]` and tail-calls vtable slot 0x50 on it. | Every call writes two bytes into the wrong fields of the particle system and makes a virtual call through an unrelated field: silent heap corruption, or an AV that the SEH frame logs as "crashed". This is the exact bug class the file's own comment (1431-1444) guards against for `attachObject`/`setVisible`; that fix missed overrides of MovableObject virtuals. Public Lua API; the shipped weather controller does not call it. | Pass the pointer from `TryGetParticleMovableObject` (as `TrySetParticleSystemVisible` does). Document the rule as "any MovableObject-declared **or overridden** virtual takes the re-based pointer". No other ParticleSystem call in the file overrides a MovableObject virtual (checked `setMaterialName`, `setDefaultDimensions`, and all `QAE` members). | Headers + capstone disassembly of the export and of `MovableObject::setRenderQueueGroup` (RVA 0x2a4d50). |
| B-3 | [Med/High] | StaticGeometry.cpp:484-516 | `Create` reserves `instances` for up to 100,000 x 40 B (4 MB) and then calls `ReadInstance`/`ReadOptions`/`luaL_argerror`, all of which can `longjmp`. It also holds 3 `std::string`s. | One malformed instance leaks the whole reservation plus the strings. Content that retries inside `pcall` leaks 4 MB per attempt. | Parse into a `lua_newuserdata` buffer (GC-owned), or run the reader under `lua_pcall` and build the `std::vector` only after it succeeds. Validate `name`/`mesh` as `const char*` before constructing strings. | Read the frame; `luaL_error` sites at 277, 301, 331, 363, 368, 395, 399, 511. |
| B-4 | [Med/High] | Environment.cpp (≈55 bindings), e.g. 3212-3235, 3852, 3871-3879, 3922-3927, 4583-4594, 4541-4556, 3704-3707, 3819-3821 | Across Lua errors, `std::string` objects are live on the stack: `caller` (from `DescribeLuaCaller`, always >15 chars, so heap-allocated) survives `CheckColorOrSingles`/`luaL_argerror` in every `SetSun*` and `SetTimeOfDay`. `name` survives later `luaL_check*`/`luaL_argerror` in every particle binding. `name`/`parameter`/`value` do the same in `GenericSetParameter`, and `text` is built inside `CheckParameterValue` before its own `luaL_argerror`. `materialName` survives `TryReadSkyPlane`/`luaL_opt*`. | Every Lua argument error leaks one heap block per live string. Weather names are 16 chars (`exu_weather_rain`), one past the SSO limit. `exu_weather.lua` wraps every call in `pcall`, so a bad value computed per frame becomes a per-frame leak with no visible error. No locks or patch guards are held across these calls; the only issue is leaked heap. | Read all arguments first as `const char*` + length (they are stable while on the stack) and validate them, then construct C++ objects once nothing else can raise. Or return `false, msg` instead of `luaL_argerror`. Drop `DescribeLuaCaller` together with B-1. | Grepped every `luaL_argerror`/`luaL_check*`/`luaL_error` after a `std::string` declaration in the file. |
| B-5 | [Med/Med] | Environment.h:30; Ogre.h:81-92; Scanner.h:92-99, 153-173; Environment.cpp:3156-3187 | `fog` is an `inline Scanner` built at DLL static-init from `sceneManager.Read() + 0x128`. The address is resolved once, and the constructor also `VirtualProtect`s that SceneManager heap page to RWX. Every other Environment binding re-reads the SceneManager per call. The 0x128/0x138/0x13c layout is correct: disassembly of `SceneManager::getFogColour/Start/End` shows `lea eax,[ecx+0x128]`, `[ecx+0x138]`, `[ecx+0x13c]`. | (a) If the SceneManager is null at `require` time, `GetFog` returns zeros and `SetFog` silently does nothing for the whole Lua state. (b) Environment.h:162-166 and `ResetOgreInitialization` assume the DLL outlives a mission (and the SceneManager with it); `AiTargetSelect.cpp:593` says the DLL unloads with every Lua state. If the former is ever true, `GetFog`/`SetFog` read and write a freed SceneManager: `IsReadableRange` passes on a committed heap page, giving a use-after-free write. The two lifetime claims in the repo contradict each other; one of them is wrong. | Resolve per call: `sm = GetSceneManager()`, then call `?getFogColour@SceneManager…`/`getFogStart`/`getFogEnd` and `setFog` by export name (all exported), or at least compute `sm+0x128` per call. Settle and document the DLL-lifetime fact in ARCHITECTURE.md §Lifetimes. | Read Scanner ctor/Read/Write; disassembled the three fog getters; grepped for DLL-lifetime statements. |
| B-6 | [Med/Med] | Environment.cpp:3160-3164, 3193-3202, 3262-3271, 3321-3330, 3380-3389; exu_weather.lua:253-285, 435-468 | When the SceneManager or terrain master light is unavailable (or the SEH read fails), the getters return **zero** colours/fog (`DefaultSunColor()` = {0,0,0,1}; `Fog{}`) instead of `nil`. `Weather.CaptureBaseline` stores whatever it gets, and `ApplyEnvironment`/`RestoreBaseline` lerp ambient and sun from it. | If `Weather.Init()` runs before the scene is bound (the "waiting" path in `TryInitializeOgre` shows this state exists), the weather drives ambient and sun diffuse toward black, and "clear"/`Shutdown` restore black. The controller already handles `nil` correctly (`baseline.x ~= nil`), so the problem is only the sentinel. | Return `nil` when unavailable (document it in `Definitions/ExtraUtils.lua`); keep zeros only for a successful read. | Read both sides; host test fakes always return real values, so the test cannot catch this. |
| B-7 | [Med/Med] | Environment.h:158; Environment.cpp:5750, 5789-5821 | `TryInitializeOgre()` runs at the top of 79 bindings, including pure getters (`GetSunAmbient`, `HasParticleSystem`, `GetLightingMode`, `EnforceLightingMode`). On first bind it `Reload()`s `fogResetPatch`, which writes a RET over the stock function at 0x00683370 for the rest of the Lua state. The patch has no `expectedBytes` (its "original" is whatever was there at DLL load), and neither address is in `exu.json`. | Calling a read-only query changes stock fog behaviour for the rest of the mission. That surprises any mod calling e.g. `GetParticleEmitterCount`. Build gating comes only from `EnableDeferredPatchActivation`; target identity is never checked. | Activate the patch only from `SetFog` (its sole consumer), give it an expected-byte preimage, and add a `exu.json` entry with provenance. If the stock reset is itself a bug, the ownership question goes to OpenShim. | Grepped `TryInitializeOgre` callers; read `BasicPatch` ctor/Reload. |
| B-8 | [Med/Med] | Culling.cpp:13-32, 37-53 | `UpdateUnit` returns before touching visibility when `enabled` is false, so `SetCullingEnabled(false)` (or raising `SetCullDistance`) never re-shows entities it already hid. `SetCullDistance` accepts NaN/negative values: NaN makes `distSq < NaN` false, hiding every unit. `Ogre::SetVisible` is the RVA of `Light::setVisible` (B-9); it works only because that function tail-calls `MovableObject::setVisible` (disassembled). | Units stay invisible for the rest of the mission after a script turns culling off. `EXU_UpdateCullingForUnit` is called per unit per tick by OpenShim, so the stale state is never corrected. | Keep a "culled by EXU" flag (or set visible=true once per unit on the disable edge), validate `cullDistance` (finite, >0), and resolve `?setVisible@MovableObject…` by name. | Read file; grepped callers (OpenShim export only); disassembled 0x220EF0. Whether the engine itself re-asserts visibility was not verified in game (hence Med confidence). |
| B-9 | [Med/Med] | Ogre.h:100-358; bzr.h:368-375; exu.json:241-255 | Ogre.h holds 66 raw OgreMain RVAs: 58 literals in `Ogre.h`, 8 via `bzr.h`/`exu.json`. They are turned into call targets at DLL static-init with **no OgreMain identity check** (`BuildValidation` checks only the exe PE). Every one of them is a named export in the shipped DLL; I mapped all 66. `setVisibleOffset` 0x220EF0 is actually `Light::setVisible`, not `MovableObject::setVisible` as `exu.json:249` says. | Any OgreMain rebuild (Redux patch, Proton/Wine packaging, modded runtime) silently turns 66 function pointers into random code. The rest of the file already resolves by mangled name and fails closed. | Replace all 66 with `GetProcAddress` by mangled name (null means the feature is unavailable). Delete the RVAs and the six unused `getSky*Offset` entries (section 3). | Export-table census script (scratchpad `ogre_exports.py`). |
| B-10 | [Med/Med] | Environment.cpp:715-742; Camera.h:28-36; Satellite.h:28-35; Environment.h:29 | No binding in scope checks `BuildValidation::IsSupportedBzr2301()`. Exe functions 0x0068A230 (`SetTimeOfDay`), 0x0067E0E0 (`RefreshTerrainMasterLight`) and `Camera::Set_View` are **called**, and Scanner writes (gravity, zoom, satellite pan/zoom, time of day 0x02CD94E4) happen on any build. Only `BasicPatch` activation is gated. | On an unqualified exe these call or write arbitrary addresses. SEH hides AVs, not wrong-target calls. This contradicts ARCHITECTURE.md "fail closed". Cross-cutting, and likely raised by other worksheets too. | Cache `IsSupportedBzr2301()` once per Lua state; have exe-calling helpers and Scanner `Write` stand down when it is false. | Grepped scope files for `BuildValidation`/`IsSupported`: none. |
| B-11 | [Med/Med] | PublicAPI.cpp:122-146; Environment.cpp (no shutdown) | `HandleLuaStateClosing` shuts down StaticGeometry and Overlay but nothing from Environment. Particle systems and `__exu_ps_node_*` nodes (possibly parented under the engine's camera node or a craft node) outlive the Lua state unless the mission calls `Weather.Shutdown()`. So do viewport scheme / Glow compositor state, sky changes, scene visibility mask, bounding-box/debug-shadow toggles and camera clip/polygon mode, none of which are restored. | If the SceneManager survives the Lua state (not verified; see B-5), orphaned camera-attached systems keep emitting with no owner. Retro mode's Glow-off state can leak into the next mission/shell until the game rebuilds the viewport (the scheme itself self-heals because `UnloadAllPatches` removes the call-site hook). | Record created particle names in a `std::set` and destroy them in a new `Environment::Shutdown()` called from `HandleLuaStateClosing` when the SceneManager identity still matches (the StaticGeometry pattern). Re-enable Glow on shutdown when this process disabled it. | Read lifecycle; name lookups mean no stale pointers are dereferenced (section 5). |
| B-12 | [Low/High] | ~70 `__try` frames, e.g. Environment.cpp:992-1008, 1561-1613, 2451-2475, 2594-2757 | SEH `__except(EXCEPTION_EXECUTE_HANDLER)` wraps Ogre C++ calls that throw `Ogre::Exception` for ordinary bad input: `createParticleSystem` with an unknown template, `setSkyBox/Dome/Plane` with a missing material, `attachObjectToBone` with a bad bone. Same pattern as OpenShim P0-7. The exception is logged as "crashed code=0xE06D7363", its message is lost, and the thrown object is never destroyed. `std::string` assignment inside `__try` (Environment.cpp:269, 292, 2660) also routes `bad_alloc` into the SEH handler. | A mistyped template reads like a crash in the log. Ogre checks these inputs before mutating state, so apart from B-13 nothing is left half-applied. | Filter `0xE06D7363` to `EXCEPTION_CONTINUE_SEARCH` and put a C++ `try { } catch (const std::exception& e)` (or `catch (...)`) in the calling helper, as StaticGeometry.cpp:567-673 does, logging `e.what()`. | Read each frame; checked the 1.10 throw sites. |
| B-13 | [Low/High] | Environment.cpp:2463-2464 | `TryAttachManagedParticleToBone` calls `detachFromParent` and then `attachObjectToBone`. A bad bone name throws after the detach (swallowed per B-12). | The system disappears (attached to nothing) until `DetachParticleSystem` is called. | Check the bone exists (`Skeleton::hasBone` export) first, or re-attach to the own node on failure. | Read. |
| B-14 | [Low/High] | OgreMaterialShim.h:25-64; OgreSceneManagerShim.h:21-72; Ogre/OgreNativeFontBridge.cpp:22-27 | ODR violation: `OgreMaterialShim.h` defines `Ogre::SharedPtr`, `Ogre::ColourValue` and `Ogre::Radian`, and `OgreSceneManagerShim.h` defines `Ogre::SceneManager`/`Viewport`, all differently from the real headers that `OgreNativeFontBridge.cpp` includes in the same DLL. The shim `SharedPtr` has no destructor, so the +1 references returned by `getByName`/`clone` are never released (materials are pinned forever). `GetSubEntityMaterial` copies a `const&` into a non-owning alias. | Currently benign because the inline members that collide (`getPointer`, `isNull`) are identical, but any change to either side becomes a silent miscompile. The reference pinning defeats `MaterialManager::remove`. | Rename shim types into `ExtraUtilities::OgreAbi` (as `OgreStringInterfaceShim.h` does), or use the committed real headers. Release returned references via the exported `SharedPtr` release path, or document them as intentionally pinned. | Grepped includes; compared against `OgreSharedPtr.h:117-134`. |
| B-15 | [Low/High] | src/Ogre/OgreBuildSettings.h:15-16 | Sets `OGRE_CONTAINERS_USE_CUSTOM_MEMORY_ALLOCATOR 0`, but the shipped OgreMain was built with 1: 331 exports mangle `STLAllocator<…>`, including the `getParameters` name EXU itself resolves. The header is also found only because its sole consumer TU (`OgreNativeFontBridge.cpp`) sits in the same directory; `third_party/.../OgreConfig.h:35` includes it by bare name. | Any TU that includes real Ogre headers sees `Ogre::vector<T>::type` = `std::vector<T>` instead of `std::vector<T, STLAllocator<…>>`: link-time name mismatches and inline allocator mismatches. A TU outside `src/Ogre/` fails to compile. | Set it to 1 (verify the rest against the DLL) and move it next to the third_party headers, or add `src/Ogre` to the include path explicitly. | Export census; include-path read in `ExtraUtilities.vcxproj:80`. |

Code for the High/Med items:

B-1 (Environment.cpp:306-321), called 8x per frame by the weather controller:
```cpp
void WriteEnvironmentDebug(const std::string& message)
{
    std::lock_guard lock(g_environmentLogMutex);
    OutputDebugStringA(message.c_str());
    OutputDebugStringA("\n");
    ExtraUtilities::Logging::ResetLogFileForCurrentProcess("exu_environment_debug.log");
    std::ofstream file(
        ExtraUtilities::Logging::GetLogFilePath("exu_environment_debug.log"),
        std::ios::app);
```

B-2 (Environment.cpp:1288, 1814). Shipped callee: `mov esi,ecx; call MovableObject::setRenderQueueGroup; mov ecx,[esi+0x1d4]; ... jmp [eax+0x50]`:
```cpp
static SetParticleSystemRenderQueueGroupFn fn = ResolveOgreProc<...>("?setRenderQueueGroup@ParticleSystem@Ogre@@UAEXE@Z");
...
if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr) ...
fn(particleSystem, renderQueueGroup);   // needs the MovableObject* subobject
```

B-3 (StaticGeometry.cpp:504-515):
```cpp
std::vector<Instance> instances;
instances.reserve(instanceCount);            // up to 4 MB
for (size_t index = 1; index <= instanceCount; ++index)
{
    lua_rawgeti(L, 4, static_cast<int>(index));
    if (!lua_istable(L, -1))
        return luaL_argerror(L, 4, "each instance must be a table");   // longjmp, vector leaked
    instances.push_back(ReadInstance(L, -1));                          // luaL_error inside
```

B-4 (Environment.cpp:3212, 3224, 3235):
```cpp
auto caller = DescribeLuaCaller(L);            // heap std::string
...
auto color = CheckColorOrSingles(L, 1);        // luaL_checknumber -> longjmp
...
return luaL_argerror(L, 1, "SetSunAmbient requires finite numeric color values");
```

B-5 (Environment.h:30 / Ogre.h:81-91):
```cpp
inline Scanner fog(Ogre::GetFog(), BasicScanner::Restore::DISABLED);   // address fixed at DLL load
inline Fog* GetFog() { void* sm = sceneManager.Read(); ... add eax, 0x128 ... }
```

B-6 (Environment.cpp:3160-3164):
```cpp
if (GetSceneManager() == nullptr) { PushFog(L, {}); return 1; }   // zeros, not nil
```

B-7 (Environment.cpp:5810, reached from every getter):
```cpp
fogResetPatch.Reload();   // InlinePatch(0x00683370, RET) with no expected bytes
```

B-8 (Culling.cpp:15):
```cpp
if (!enabled) return;     // previously hidden entities are never shown again
```

B-9 (Ogre.h:124, 152):
```cpp
inline uintptr_t getDirectionAddr = BasicScanner::CalculateAddress(0x14042, OGRE);   // no OgreMain identity check
inline uintptr_t setVisibleAddr   = BasicScanner::CalculateAddress(BZR::Ogre::setVisibleOffset, OGRE); // = Light::setVisible
```

B-10 (Environment.cpp:719-720):
```cpp
*BZR::Environment::timeOfDay = timeOfDay;          // 0x02CD94E4
BZR::Environment::SetTimeOfDay(timeOfDay / 100);   // call 0x0068A230, no build gate
```

B-11 (PublicAPI.cpp:133-137): no `Environment::` call:
```cpp
BasicPatch::UnloadAllPatches();
Overlay::ShutdownOverlaySupport();
StaticGeometry::Shutdown();
```

### Q3: Ogre calls that can throw, reached from a `lua_CFunction`

Every Ogre call in `Environment.cpp` is inside an SEH `__try`. None can terminate the game, but all of them swallow the exception (B-12). The ones that actually throw on user input are listed below. `has*` guards run before every `get*`, so `getParticleSystem`/`getSceneNode`/`getMovableObject` cannot throw on a missing name.

| Call | Throws when | Guard |
|---|---|---|
| `createParticleSystem` 1570 | unknown template, duplicate name | SEH 1603 (duplicate pre-checked 1624) |
| `createChildSceneNode` 1585 | duplicate node name | SEH (pre-checked 1632) |
| `attachObject` 1592, 2967 | object already attached | SEH (detached first at 2966) |
| `setSkyBox/Dome/Plane` 994/1032/1071 | material not found | SEH |
| `attachObjectToBone` 2464 | bad bone, already attached | SEH, leaves system detached (B-13) |
| `addChild` 2253 | node already has a parent | SEH (removed first) |
| `setMaterialName` (particle) 1787 | does not throw in 1.10 (logs) | SEH |
| `CompositorManager::setCompositorEnabled` 5033 | does not throw; creates a chain as a side effect | SEH |

Outside Environment:
- StaticGeometry.cpp: all calls in C++ `try/catch` (good).
- Camera.cpp: SEH around frustum getters/setters, which do not throw.
- Culling.cpp:30: unguarded `SetVisible`, which does not throw.
- Environment.cpp:5061: unguarded `CompositorManager::getSingletonPtr`, which does not throw.
- Unguarded callers into the game: `GameViewportSetMaterialSchemeHook` (5095-5142) is not `noexcept` and allocates `std::string`s, so `bad_alloc` could unwind into exe code (Low).

## 3. Dead / unreferenced code

| Symbol | Where | Verification |
|---|---|---|
| `ResolveEntityDetachObjectFromBone`, `EntityDetachObjectFromBoneFn` | Environment.cpp:1902, 1976-1980 | `grep -rnw` over src/include/tests: definition only |
| `SetRenderQueueGroupSubEntity`, `setRenderQueueGroupSubEntityAddr` | Ogre.h:200-202 | 0 uses outside Ogre.h |
| `getSkyBoxGenParametersOffset`, `getSkyBoxNodeOffset`, `getSkyDomeGenParametersOffset`, `getSkyDomeNodeOffset`, `getSkyPlaneGenParametersOffset`, `getSkyPlaneNodeOffset` | bzr.h:376-381, exu.json:250-255 | 1 hit each (the definition); sky getters use dllimport'd members in OgreSceneManagerShim.h |
| `Ogre::GetFog` inline-asm helper | Ogre.h:81-92 | Only feeds B-5's static; plain pointer arithmetic, asm unnecessary |

Dead-code count: 2 functions/pointers (+1 typedef) in scope, and 6 unused catalog constants mirrored in `exu.json`. All 121 Lua exports from Camera/Satellite/Culling/StaticGeometry/Environment are registered in `exuExports[]` and documented in `Definitions/ExtraUtils.lua`.

## 4. Raw address census

`0x00[4-9A-F]xxxxx` literals: Environment.cpp **8** occurrences (5 unique), Environment.h **1**, every other file in scope **0**.

| Address | Use | exu.json |
|---|---|---|
| 0x00683370 (`fogReset`, Environment.h:158) | **WRITTEN** (1-byte RET via InlinePatch, no expected bytes) | no |
| 0x00681585, 0x00682AA0, 0x00682EA7 (Environment.cpp:5082-5086) | **WRITTEN** (disp32 of `call [imm32]` retargeted); 6-byte `FF 15 <IAT>` identity compared first | no |
| 0x00869810 (IAT slot, 5081) | **READ and CALLED THROUGH** (original `setMaterialScheme`) and byte-compared | no |
| 0x00680FE0 (5074) | comment only | no |

Addresses my files reach through `bzr.h`:
- **Called**: 0x0068A230 `SetTimeOfDay` (json yes), 0x0067E0E0 `RefreshTerrainMasterLight` (yes), `Set_View` (yes).
- **Written**: 0x02CD94E4 timeOfDay (yes), 0x00871A80 gravity (yes); Camera zoom 0x008EAD10 and neighbours (yes); Satellite 0x009C91D0/0x00872400/0x008723F4/0x009C91B0 (yes).
- **Read**: 0x00920CA0/0x00920EA0 (yes), Satellite state/positions (yes), 0x008EAAE0 main camera (yes), 0x00920C78 (yes). 0x025F8E4C `worldRenderOriginAddress` is read by Environment.cpp:2183 and StaticGeometry.cpp:221 and is **not** in exu.json.
- None of these are in `profiles/bzr_2.2.301.json` anchors.

Non-exe raw offsets:
- 58 OgreMain RVAs literal in Ogre.h, plus 8 in bzr.h (listed in exu.json as RVAs). All 66 are **called**, all map to named exports (B-9).
- SceneManager fog offset 0x128 (Ogre.h:88) is **read/written**. It matches the DLL but has no catalog entry.
- `ViewportMaterialSchemeLayout` (Environment.cpp:67-93) is a **written** layout mirror, verified against `OgreViewport.h`.

## 5. Lifetime / ownership notes

- **DLL load (per Lua state if AiTargetSelect.cpp:593 is right):**
  - All `static` resolver function pointers.
  - Ogre.h's 66 RVA pointers (`GetModuleHandleA` under loader lock is benign).
  - Scanners: `fog` (B-5), `gravity` (Restore on), Camera zoom ×4 (Restore on, intentional), Satellite ×8 (Restore on by default). This includes the four read-only `const` satellite scanners (`state`, `cursorPos`, `camPos`, `clickPos`), whose destructors write load-time snapshots back into live game state at unload (Low).
  - `fogResetPatch` captures its "original" byte.
  - Each Scanner `VirtualProtect`s its target to RWX for the DLL's life, including the SceneManager heap page for `fog`.
- **Lua state:**
  - `g_desiredLightingMode`, `g_lastModernMaterialScheme` and `g_lastSchemeRewriteLogged`: never reset except by DLL reload. `g_lastModernMaterialScheme` is also mutated from the game-thread hook.
  - `g_particleCameraFollowers`: cleared in `ResetOgreInitialization` at `Init`. It stores names only.
  - `g_initializedSceneManager`: identity compare only.
  - StaticGeometry `g_records`: destroyed at `HandleLuaStateClosing` with an owner-identity check (ABA on a recycled SceneManager address is theoretical).
  - Culling `enabled`/`cullDistance`: never reset (B-8).
- **Ogre SceneManager (mission):**
  - Particle systems and their `__exu_ps_node_<name>` nodes.
  - TagPoints from bone attach.
  - Sky, fog, ambient and master-light values.
  - Visibility mask, bounding boxes, debug shadows.
  - None of these are released by EXU at Lua close (B-11).
- **Viewport / camera (possibly process lifetime):**
  - Material scheme: self-heals after the hook unloads.
  - Glow compositor enable state: does not self-heal.
  - `CompositorChain` created by `setCompositorEnabled` on any viewport it touches.
  - Overlays/shadows toggles.
  - Camera clip distances, aspect ratio, projection and polygon mode.
  - EXU restores none of these.
- **Temporary patches:**
  - `fogResetPatch`: activated by the first Ogre-touching call (B-7).
  - Scheme call-site patches: installed once (`static bool attempted`), `new InlinePatch` intentionally leaked, restored by `UnloadAllPatches` at Lua close.
- **Stale-pointer check (Q2):**
  - Particle/node APIs look everything up by name on every call and never cache an Ogre pointer; followers hold names.
  - Viewports and cameras are re-resolved per call.
  - The only cached SceneManager-derived address is the `fog` Scanner (B-5).
  - Entities come from `GameObject::ResolveAnimationEntity(h)` per call.
  - If the engine destroys the craft node a system was parented to, Ogre orphans EXU's node without destroying it, and the next call simply finds it unattached. No dangling dereference was found.
- **Q7, terrain re-theme and clones (GameObject.cpp, outside my files; flagged for that reviewer):**
  - `SetTerrainTextureSet` (GameObject.cpp:3903-4000) mutates the shared terrain material's texture units in place, with no snapshot or restore. A later mission using the same material inherits the re-theme unless the engine reloads the material.
  - `CloneMaterial` clones persist in `MaterialManager` for the process. Growth is bounded by distinct clone names, and a same-name re-clone throws and is caught (GameObject.cpp:1754).
  - The shim `SharedPtr` never releases the refs it receives (B-14), so the clones can never be freed.

## 6. Performance notes

Per frame, `Weather.Update` with `rain_heavy` (2 systems) issues 7 EXU calls: `SetParticleEmitterEmissionRate` ×2, `SetParticleAffectorParameter` ×1, `SetFog`, `SetAmbientLight`, `SetSunDiffuse`, `UpdateParticleFollowers`. Reasoned cost:

| Item | Count / frame | Notes |
|---|---|---|
| Debug log lines | **8** | ~10 syscalls + 1 file open/close + ~6 allocations each (B-1); dominant |
| `lua_getinfo` (`DescribeLuaCaller`) | 2 | only for tracing |
| `VirtualQuery` (`IsReadableRange` via `Scanner::Read/Write`) | ~22 | `TryInitializeOgre` = 2 per binding, `GetSceneManager` = 1 per binding, `fog.Write` = 1 |
| `std::string` heap allocations | ~6 | names are 16 chars (SSO is 15); affector value text; `CheckParameterValue` copy |
| Ogre `std::map<String>` lookups | ~12 | `hasParticleSystem` + `getParticleSystem` = 4 per emitter/affector resolve (collection map + object map, twice) |
| `StringInterface::setParameter` | 1 | `ParamDictionary` lookup + `StringConverter::parseVector3` (split + istringstream) |
| `UpdateParticleFollowers` | 1 | 3 VQ and return when the camera had a node; else viewport discovery (3 SEH'd exports) + 3 map lookups + 1 alloc per follower |

- Without B-1 the per-frame cost is small: ~22 `VirtualQuery` plus a dozen map lookups.
- Cheap wins: cache resolved `ParticleSystem*`/emitter pointers per name for one frame, or let `Weather.Update` push a batch.
- `TryInitializeOgre` does 2 VQ even when bound; `GetSceneManager()` re-reads right after.
- `EnforceLightingMode` (documented as "call from per-frame update") does `GetModuleHandleA("winmm.dll")` + `GetProcAddress` per call before anything else. In retro mode without OpenShim it adds per-viewport `setCompositorEnabled` and two `std::string` copies.
- `TryInitializeOgre` logs "waiting" (a full B-1 line) on **every** call while the SceneManager is null, e.g. if content polls a getter from the shell or loading.
- Culling: when disabled, one branch per unit per tick. When enabled: one Scanner `Get` (no VQ), `GetOgreEntity`, and one Ogre call per unit per tick.
- Content: all templates use `cull_each true`. For camera-centred volumes that are always partly in view (rain quota 4200), per-billboard culling costs a sphere test per particle per frame for modest fill savings. Worth profiling `false` for rain.

## 7. Patterns worth keeping

- Particle/node API looks up by **name** on every call and never caches Ogre pointers. That makes it immune to engine-side destruction between calls.
- The ParticleSystem -> MovableObject re-basing is obtained from Ogre itself (`getMovableObject` with `FACTORY_TYPE_NAME` read from OgreMain) instead of a hardcoded offset (1431-1472). Extend it to overrides (B-2).
- Ogre entry points are resolved by mangled export name and fail closed on null. All 109 names in scope exist in the shipped DLL.
- `StaticGeometry.cpp`:
  - C++ `try/catch` around Ogre, with cleanup of partial objects (567-673).
  - SceneManager-identity check before destroy.
  - Shutdown from `HandleLuaStateClosing`.
  - Explicit render-space conversion with a logged origin.
- Pure, host-tested headers: `OgreRenderSpace.h` (conjugation derivation documented) and `OgreParameterValue.h` (bounded names/values, locale-safe number text).
- Bounded, sanity-checked foreign-container read in `InvokeGetStringInterfaceParameterNames` (ordering + `kMaxParameterDefs`) with `static_assert`s on `std::string`/`ParameterDef` sizes.
- `InstallGameViewportSchemeHooks` verifies the whole `FF 15 <IAT>` instruction before patching, goes through `InlinePatch`, and stands down when OpenShim owns scheme policy.
- Finite/range validation of every Lua number before it reaches Ogre (except `SetFog`, section 8).
- `exu_weather.lua`: `pcall`-wrapped `Call()`, one-shot warnings, affector-type check before writing `force_vector`, destroy-before-create by name. Its host test (`weather_controller_test.lua`) uses a fake `exu` and covers older-EXU and missing-template cases.

## 8. Low-severity items

- `SetFog` (3173-3186) does no finiteness check, and writes the `padding` word as 0.0, i.e. fog colour alpha = 0. It bypasses `SceneManager::setFog` (fog mode unchanged).
- `SetSceneVisibilityMask` (5688) goes through `luaL_checkinteger` (int32). `0xFFFFFFFF` passed as 4294967295 converts to 0x80000000 on the x87 `lua_number2integer` path. `GetSceneVisibilityMask` returns negative values for high-bit masks.
- `Camera::GetOrigins/GetTransformMatrix/GetViewMatrix` (Camera.cpp:57, 78, 90) dereference `mainCam.Get()` without a null check (`GetFOV` checks).
- `Camera::SetView` calls `Set_View(userEntity.Read(), view)` even when the entity is null.
- Satellite `const Scanner`s default to `Restore::ENABLED` (Satellite.h:28-31), so read-only state is written back at unload (section 5).
- `GameViewportSetMaterialSchemeHook` (5095) is a hook thunk without `noexcept`/`try`. Its `std::string` allocations can throw into exe code.
- `TrySetViewportMaterialScheme` (292) assigns an EXU-allocated buffer into an Ogre-owned (MSVCP120) `std::string`. This relies on both CRTs using `GetProcessHeap()`; the assumption is documented in `OgreStringInterfaceShim.h:77-82` but not at this site.
- `GetActiveViewports` (525-531) prefers `RenderSystem::_getViewport()`, the last viewport set, which may be an RTT/compositor/shadow viewport between frames. That viewport feeds scheme writes and the camera choice for camera-attached weather. Unverified in game.
- `TrySetGlowCompositorEnabled` creates a `CompositorChain` (and viewport listener) on any viewport it touches.
- `InstallGameViewportSchemeHooks` reads the 6 site bytes without `IsReadableRange`. This is harmless on a non-ASLR exe but inconsistent with `BasicPatch`.
- ~150 helpers after line 532 have external linkage in `ExtraUtilities::Lua::Environment`; wrap them in the anonymous namespace.
- Indentation drifts by one level from 845-3003.
- Duplicated helpers:
  - `ResolveOgreProc` (Environment.cpp:858, StaticGeometry.cpp:105, OgreMaterialShim.h:81).
  - `OgreQuaternionValue` (Environment.cpp:870, StaticGeometry.cpp:52).
  - Two hand-rolled overlay resolvers (3009-3041) that skip `ResolveOgreProc`.
- ~75 mangled names plus a Viewport layout mirror live in feature code, not under `src/Ogre/` (ARCHITECTURE.md §Ogre runtime knowledge).
- `GetRenderCapabilities` (5585-5605) omits `CapEnhancedResources` (bit 8, RenderProfileBridge.h:48) from the named fields; it is only visible in `mask`.
- `Ogre.h:98` `using enum` at namespace scope in a widely included header.
- `Ogre.h:118` casts `getSpecularColorAddr` through `_GetDiffuseColor` (same signature, typo).
- `OgreSceneManagerShim.h` dllimports 7 SceneManager/Viewport members, giving exu.dll a hard load-time dependency on those exports, unlike everything else, which fails closed.
- `TryInitializeOgre` identity check can miss a SceneManager recycled at the same address (ABA). Harmless today: it only re-arms `fogResetPatch`.
- `DescribeLuaCaller` level 1 is `pcall` when called via `exu_weather.lua`'s `Call()`, so the logged caller is always `[C]:-1 (pcall)`.

## 9. Task-specific answers (cross-reference)

1. Section map: §1. Split boundaries: §1 list.
2. Created Ogre objects:
   - ParticleSystem, SceneNode `__exu_ps_node_*` and TagPoint (bone): SceneManager-owned, never released by EXU at Lua close (B-11).
   - StaticGeometry: released at Lua close.
   - No ManualObject, Light, Texture or Overlay is created in scope. Material clones are in GameObject.cpp.
   - Stale-pointer analysis: §5. Only `fog` (B-5) can dereference a stale SceneManager.
3. Throwing getters: §2 Q3 table. None can terminate; all are SEH-swallowed (B-12). The only unguarded ones do not throw.
4. Per-frame paths and costs: §6.
5. Debug logs:
   - `exu_environment_debug.log` is ungated. Its cost when on is the default (B-1); there is no "off".
   - `exu_material_debug.log` (GameObject.cpp:270-286) is likewise ungated, `fopen`/`fclose` per line through `OpenSessionLogFile` (two `GetLogFilePath` calls per line).
6. Raw address census: §4.
7. Terrain re-theme and clones: §5 last bullet.
8. SEH around Ogre C++ calls: yes, 88 `__try` frames in Environment.cpp, ~70 of them around Ogre calls (B-12). Plus SEH around `std::string` assignment (269, 292, 2660).
