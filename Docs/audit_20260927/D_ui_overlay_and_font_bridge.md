# Worksheet D: UI overlay, HUD/radar bindings, Ogre font bridge (2026-09-27)

Reviewer scope (actual `wc -l`): `src/UI/Overlay.cpp` 3215, `Overlay.h` 57, `src/UI/ControlPanel.cpp` 1290, `ControlPanel.h` 55, `src/UI/Radar.cpp` 482, `Radar.h` 44, `src/UI/Renderer.cpp` 84 / `.h` 31, `src/UI/Reticle.cpp` 91 / `.h` 39, `src/Ogre/OgreNativeFontBridge.cpp` 1330 / `.h` 35, `src/Ogre/OgreOverlayShim.h` 171. Ground truth: `third_party/ogre-1.10.0-bzr/include/{OgreMain,Components/Overlay}`, `third_party/ogre-1.10.0-bzr/ABI_NOTES.md`, the shipped Steam `OgreMain.dll` (export strings only), the OpenShim GOG decompilation corpus (`BZR-OpenShim-cmpB/reverse_engineering/.../decomps/`). The Steam `.text` is SteamStub-encrypted on disk, so I did not verify hook preimages against file bytes. Base: origin/main aec8c0a.

Notes on method:
- Ogre 1.10 throw behaviour cited below (`createOverlayElement`, `OverlayContainer::addChild/removeChild`, `OverlayElement::setMaterialName`, `TextAreaOverlayElement::setFontName`) comes from upstream Ogre 1.10 `.cpp` source, which is not in-tree. The in-tree headers confirm the signatures and overrides but not the throws.
- Lua errors are treated as `longjmp`, per the brief.

## 1. Section map

### `src/UI/Overlay.cpp` (3215)

| Lines | What | Status |
|---|---|---|
| 1-135 | Includes and anonymous-namespace state: name-keyed `knownElements`/`overlayVisibilityStates`, `attachedOverlaySceneManagers`, `overlaySystemInstance`, 4 `unique_ptr<Hook>`, CR-branded runtime-font constants, and the pause/shell wrapper VA/pattern/preimage constants | production |
| 136-290 | Element-kind helpers; SEH-wrapped `FindOverlay`/`FindOverlayElement`/`FindOverlayContainer`; `SignatureResolver` pass-throughs (the `IsReadableRange` wrapper at 254 is unused); `IsOverlaySuppressedByGameUi` | production (+1 dead wrapper) |
| 291-404 | `GetProcAddress` resolvers for OgreMain: Viewport get/setOverlaysEnabled, Root singleton, getRenderSystem, getSharedListener, `_getViewport` | mixed: diagnostic, plus the dead Set path |
| 405-605 | Root singleton; module and game-root directory resolution; filesystem font-candidate discovery (ancestor walk, recursive walk of addon/mods/packaged_mods/workshop) | production (one-shot) |
| 606-778 | `add/removeRenderQueueListener` and `OverlaySystem` ctor/dtor resolvers; `TryConstructOverlaySystem` (malloc 256 + exported ctor); attach to the SceneManager; `EnsureOverlaySupport` | production (runtime layer) |
| 780-983 | `EnsureOverlayRuntimeResources` / `EnsureOverlayRuntimeFont` (script, then TrueType, then sprite-table image fallback) | production |
| 985-1061 | Detach and destroy the OverlaySystem; `GetOverlayManager` (also installs the wrapper hooks) | production |
| 1063-1207 | Root/RenderSystem/Viewport diagnostic getters; `TryGet/TrySetViewportOverlaysEnabled` (the Set half is dead and duplicates `Environment.cpp:3009-3080`) | diagnostic / dead |
| 1209-1262 | SEH wrappers for `StringInterface::setParameter` and `Overlay::show/hide` | production |
| 1264-1372 | Visibility/suppression state machine; C++ callbacks for the pause and shell wrapper hooks | production |
| 1374-1446 | 4 naked asm hook thunks (they hard-code `0x00887A64`, `0x0091812B`, `0x00918324`) | production |
| 1448-1612 | Hook teardown; wrapper address resolution (stock VA, then a whole-`.text` masked scan as fallback); hook install | production (the fallback scan is effectively dead, see §3) |
| 1614-1722 | String, float, bool, alignment and colour parsing helpers | production |
| 1724-2114 | 11 near-identical `TryCall*` wrappers that resolve mangled Panel/BorderPanel/TextArea setters via `GetProcAddress` under SEH | production (boilerplate) |
| 2116-2356 | `TrySetOverlayParameterDirect`: kind-checked typed parameter setters | production |
| 2358-2457 | Generic `SetOverlayParameter` via `StringInterface::setParameter`; Lua value coercion | production |
| 2459-2604 | Cache reset; name-based destroy helpers; `ResetOverlaySupportInternal` (destroys children first) | production |
| 2607-2640 | `ShutdownOverlaySupport`, `NotifyMissionSimulationState`, `exu.ResetOverlaySupport` | production |
| 2642-2773 | Create/Destroy/Show/Hide bindings. `ShowOverlay` 2716-2739 is diagnostic logging on every call | production + diagnostic |
| 2775-3207 | Remaining 20 Lua bindings | production |
| 3210-3215 | `extern "C" ExuNotifyMissionSimulationState` export (called by OpenShim) | production |

Candidate split boundaries:
- (a) `src/Ogre/OgreOverlayRuntime.{h,cpp}`: 291-404, 606-778, 985-1207. These are OverlaySystem lifecycle and OgreMain proc resolvers. They are Ogre ABI knowledge and belong under `src/Ogre/` per ARCHITECTURE.md.
- (b) `src/Ogre/OgreOverlayElementOps.cpp`: 1209-1262 and 1724-2356. Replace the 11 `TryCall*` copies with one templated SEH invoker.
- (c) `src/UI/OverlayFontAssets.cpp`: 405-605 and 780-983.
- (d) `src/UI/OverlaySuppression.cpp`: 1264-1612 and the C export. Move the constants into `exu.json`.
- (e) `Overlay.cpp`: Lua bindings only (2635-3207).

### `src/UI/ControlPanel.cpp` (1290)

| Lines | What | Status |
|---|---|---|
| 1-108 | Constants: 5 hook VAs; palette selector `0x0047C070`/`0x0094F4B0`; 8 HUD-text int globals `0x0091826C..0x009182A0`; OpenShim bridge typedefs; state globals | production |
| 110-384 | Scrap/pilot HUD baseline and offset engine (OpenShim bridge first, raw global writes as fallback) | production |
| 386-523 | 8 copy-pasted OpenShim export resolvers | production (boilerplate) |
| 525-764 | Heuristic discovery of the command-menu rect table: linear scan of the exe `.data` raw bytes with an SEH probe per 4-byte offset | production (read-only) |
| 766-866 | 5 naked hooks and 5 `inline Hook` globals, constructed at DLL static init | production |
| 869-907 | C++ API used by Radar | production |
| 909-1140 | HUD offset, colour and command-rect Lua bindings | production |
| 1142-1267 | HUD sprite bindings (pure OpenShim bridge) | production |
| 1269-1290 | `SelectAdd/None/One` | production |

### `src/Ogre/OgreNativeFontBridge.cpp` (1330)

| Lines | What | Status |
|---|---|---|
| 1-35 | Real Ogre headers behind the `#define register` and `_STLP_MSVC` hacks (C++14 TU) | production |
| 36-123 | Duplicated logger, `FontPtrPod`, module and proc resolvers (mangled names) | production |
| 125-658 | C++ implementation functions plus SEH shells. The filter passes `0xE06D7363` through with `CONTINUE_SEARCH` | production |
| 660-1327 | Public `noexcept` API: each call is `try{}catch(Ogre::Exception/std::exception/...)` around an SEH shell | production |

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| D-1 | [High/High] | Overlay.cpp:3131-3207, 3111-3129; OgreNativeFontBridge.cpp:550-600 | `SetOverlayTextFont`, `SetOverlayTextColor` and `SetOverlayTextCharHeight` call `TextAreaOverlayElement::setFontName/setColour/setCharHeight` on **any** element found by name. They do not check that `GetKnownElementKind(name) == TextArea`, so a Panel, a BorderPanel or a foreign element all pass. `SetOverlayCaption` does the same, but TextArea's `setCaption` only touches base members, so it is benign. | This is type confusion: TextArea members (`mFont` SharedPtr, `mCharHeight`, `mColourTop/Bottom`, `mColoursChanged`; see OgreTextAreaOverlayElement.h:236-248) are written into a `PanelOverlayElement`/`OverlayContainer` layout. `setColour` then calls `updateColours()`, which locks a vertex buffer through the wrong offsets. Result: heap corruption or a crash from an ordinary Lua mistake. `TrySetOverlayParameterDirect` (2129-2165) already does this kind check correctly. | Reject with `false` unless the kind is TextArea, and fall back to `SetOverlayParameter` only for TextArea. Do the same in the Caption path. | Read all four bindings and the bridge; grepped every `Native::TrySetTextArea*` caller (Overlay.cpp only); compared against the header member list. |
| D-2 | [High/High] | Overlay.cpp:2836, 2938, 2953, 3064 | Throwing Ogre calls sit in `lua_CFunction`s with no try/catch and no SEH: `createOverlayElement(typeName,...)` with an unknown type ("Cannot locate factory"); `OverlayContainer::addChild` for a child name already present (`addChildImpl` ERR_DUPLICATE_ITEM); `removeChild` for a missing child (ERR_ITEM_NOT_FOUND); `OverlayElement::setMaterialName` for a missing material (ERR_ITEM_NOT_FOUND). | Every trigger is a user-supplied string or an ordinary double call. An uncaught C++ exception crosses the C Lua frame and terminates BZR. | Route these four calls through a noexcept helper that uses try/catch (or the bridge's CONTINUE_SEARCH + outer `catch` pattern), then return `false`. Pre-check with `getChild`, the factory map, or `MaterialManager::resourceExists`. | Grepped every raw `manager->`/`element->`/`parent->` call in the bindings (full list in Q7). Throw sites come from upstream 1.10 source. The signatures and virtual status come from OgreOverlayContainer.h:77-85 and OgreOverlayManager.h:115-149. |
| D-3 | [High/High] | Radar.cpp:160-233, 238-280 | When OpenShim's radar bridge is absent, EXU retargets both `RefreshLayout` call sites. One of them, `0x0049405B`, is inside `CockpitRadar::Render` (`FUN_00493EC0`), which runs **every frame** in radar mode (GOG decomp line 246; OpenShim `bzr_hooks.cpp:15671-15674` says the same). `RefreshLayoutConcentric` then calls `Logging::LogMessage` every frame: the "passthrough" line at scale 1 and the long line otherwise. At scale != 1 it also runs `RefreshLayout` twice and `RefreshCockpitWireframeAnchor` twice, and toggles `*scale` to 1.0 and back, every frame. The comment at 215 ("Once per layout refresh, not per frame") is wrong. | Each frame pays a file open, fprintf and close, plus `GetModuleFileNameA` x2, `CreateDirectoryA` and a mutex. `exu.log` grows without bound (~60 lines/s). The mid-frame scale toggle is visible to any concurrent reader. | Remove the per-call logs, or log on change only. Cache the reference-pass geometry per (screenHeight, scale), or adopt OpenShim's closed-form approach. | Read Radar.cpp in full; checked the GOG decomp of `FUN_00493EC0` (per-frame body, guarded by `DAT_008eaaac != 0`) and `FUN_00493250`; cross-read OpenShim's comment. |
| D-4 | [Med/High] | OgreNativeFontBridge.cpp:1172, 1222, 1264, 1306; Overlay.cpp:2419-2424, 3065, 2728-2749, 2154ff | Overlay setters log **on every success**. `SetOverlayCaption` logs through the bridge (`native setCaption element=%p text=%s`). `SetOverlayTextColor`, `SetOverlayTextCharHeight`, `SetOverlayParameter`, `SetOverlayMaterial` and `ShowOverlay` (2 lines plus 5 diagnostic Ogre calls) do the same. `LogNativeOverlayMessage` (41-69) re-implements `Logging::LogMessage`. | HUD scripts update captions and colours from `Update()`, i.e. every frame, so each update costs a log-file open/close. | Log failures only, or keep success logging behind a debug flag. Delete `LogNativeOverlayMessage` and call `Logging::LogMessage`. | Traced SetOverlayCaption -> `Native::TrySetTextAreaCaption` -> log; read `Logging.h:77-120`. |
| D-5 | [Med/Med] | Overlay.cpp:1264-1330, 1359-1372, 2617-2633, 2701-2714 | Overlay resurrection. Mission exit and shell entry hide overlays through suppression, but they leave `requestedVisible=true`. The next refresh with `synchronizeVisibility=true` re-shows **every** tracked overlay whose `requestedVisible` is still set, including the previous mission's HUD. Such refreshes come from `CreateOverlay` (2662), `ShowOverlay` (2714), and pause-wrapper enter/exit. The comments at 1369-1370 and 2627-2629 ("the new mission's first ShowOverlay will synchronize them normally") describe exactly this resurrection. | Stale overlays from mission A appear in mission B. It happens whenever the tracking maps survive between missions: the Lua state is not closed per mission (the dated research `RADAR_LIGHTING_PARK_20260809.md` reports a single `luaopen_exu` across 3 missions), or OpenShim is absent (`overlayMissionSimulationState == -1`). | On `mission-simulation-exit` and `game-shell-wrapper-enter`, clear `requestedVisible` (or tag entries with a mission generation and skip older ones in `SyncOverlayVisibilityState`). | Traced every `RefreshOverlaySuppressionState` caller and its `synchronizeVisibility` argument. Whether the Lua state survives between missions was not verified at runtime. |
| D-6 | [Med/Med] | Overlay.cpp:2607-2615, 985-1048 | `ShutdownOverlaySupport` clears `knownElements`/`overlayVisibilityStates` but destroys Ogre overlays and elements **only indirectly**, by destroying EXU's own `OverlaySystem`. If the OverlayManager belongs to someone else (`GetOverlayManagerRaw()` was non-null, so EXU constructed nothing), the mission's overlays and elements survive, untracked and unreachable by `ResetOverlaySupport`. Then `CreateOverlay`/`CreateOverlayElement` with the same names return `false` and never re-populate `knownElements`, so `FindOverlayContainer` returns null and `AddOverlay2D`/`AddOverlayElementChild` silently no-op. Separately, `DetachOverlaySystemFromTrackedSceneManagers` returns early when `removeRenderQueueListener` is unresolved, and still clears and continues after an SEH crash. `DestroyOverlaySystemInstance` then frees the listener object anyway (fail-open, dangling listener in the SceneManager). | This is a lifetime leak across the Lua-state boundary, plus a fail-open teardown order. | In Shutdown, call `ResetOverlaySupportInternal("lua-state-close")` (it destroys by name, children first) instead of only clearing maps. Skip `DestroyOverlaySystemInstance` (deliberately leak it) when detach did not succeed for every tracked SceneManager. | Read both paths; grepped callers (`PublicAPI.cpp:136` only). |
| D-7 | [Med/High] | Overlay.cpp:2967, 2981, 3000, 3064, 3107; OgreOverlayShim.h:71-81 | Because the shim cannot dispatch virtually, calls are qualified to the **base** implementation (`element->::Ogre::OverlayElement::setMetricsMode/setMaterialName/setColour`). That bypasses real overrides: `TextAreaOverlayElement::setMetricsMode/setMaterialName/setColour` (OgreTextAreaOverlayElement.h:83, 91, 125), `BorderPanelOverlayElement::setMetricsMode` (:187) and `PanelOverlayElement::setMaterialName` (:107). | `SetOverlayMetricsMode` on a TextArea or BorderPanel skips the pixel char-height/space-width/border conversions. `SetOverlayColor` on a TextArea never reaches its top/bottom colours. `SetOverlayMaterial` on a Panel skips Panel's override. Wrong HUD rendering that looks like a mission bug. | Dispatch by `ElementKind` to the derived exports (as `TrySetOverlayParameterDirect` does), or call through the real headers in the C++14 bridge TU. | Grepped the three derived headers for overrides; read the shim. |
| D-8 | [Med/High] | ControlPanel.cpp:97-108, 1099-1121; Radar.cpp:375-422; Reticle.cpp:204-218; Renderer.cpp:77-87 | Without OpenShim, mission-set UI state lives for the whole process. Nothing resets it in `HandleLuaStateClosing` or `Init`: scrap/pilot HUD offsets (the draw hook re-applies them every HUD frame after the next `Init` re-activates it), scrap/pilot colours, radar size scale (engine global plus the call-site wrapper), reticle range and wireframe polygon mode. | Mission A's HUD layout, colour and radar size carry into mission B and the shell. Mission-lifetime state is kept at process lifetime (ARCHITECTURE.md Lifetimes). | Add `ControlPanel::ResetMissionState()`, `Radar::ResetMissionState()` and friends. Call them from `HandleLuaStateClosing` and from the mission-simulation-exit seam when OpenShim is absent. | Grepped all writers of these globals; `ResetOpenShimMissionOverrides` (UnitVo.cpp:1625) resets only OpenShim-side state. |
| D-9 | [Med/High] | Radar.cpp:377-379, 355-360; Reticle.cpp:206-216 | Lua input validation gaps. `SetRadarSizeScale(0/0)`: NaN passes `newScale <= 0.f` and is written to `0x008E77B0` (or passed to OpenShim); `+inf` passes too. `SetRadarState(256)` is truncated to `uint8` **before** the range check, so it becomes 0 and is accepted. `SetReticleRange` accepts NaN or negative values and ignores the bridge's `BOOL`. | NaN in a layout global propagates into every radar projection each frame. | `if (!std::isfinite(s) \|\| s <= 0 \|\| s > kMax) luaL_argerror`. Range-check `lua_Integer` before narrowing. Validate range and return the bridge result. | Read the code; the NaN comparison semantics are standard. |
| D-10 | [Med/Med] | Overlay.cpp:1332-1372 | The hook callbacks `OnOverlayPause*`/`OnOverlayGameShell*` are neither `noexcept` nor try-guarded. They run on the game main thread, inside the exe's blocking Escape-UI and shell loops, called from naked asm. They call `Logging::LogMessage` (std::string, `std::mutex`, so `bad_alloc`/`system_error` are possible), iterate an `unordered_map`, and can reach `GetOverlayManager()` -> `EnsureOverlaySupport()` -> **construct an OverlaySystem inside the hook**. Only the Ogre show/hide calls are SEH-guarded. | A C++ exception unwinding through a naked thunk into BZR code is undefined behaviour. | Make the four callbacks `noexcept` and wrap the body in `try{}catch(...){}`. In hook context, use a `FindOverlay` variant that never calls `EnsureOverlaySupport`. | Read the thunks, callbacks and every function they reach; checked the pause-wrapper decomp (`FUN_005d4690`). |
| D-11 | [Med/Med] | ControlPanel.cpp:36-41, 842-866 | The 5 ControlPanel hooks are `inline Hook` globals constructed during DLL static init (under the loader lock, before `DllMain`) with **no `expectedBytes`**. The preimage is whatever bytes are present at load time. Activation is gated by `BuildValidation` (good). The colour-hook initial status calls `OpenShimBridge::HasExport` (GetModuleHandle/GetProcAddress) from a static initializer, so that decision is frozen at load. OpenShim patches the same colour sites (`bzr_hooks.cpp:1621-1626`); if OpenShim patched them first and lacked the stand-down export, EXU would capture OpenShim's bytes as "original". None of the 5 VAs, the palette selector or the 8 HUD-text globals are in `exu.json`. | This goes against the brief's "every write byte-verified" rule, and against ARCHITECTURE.md's no raw addresses in feature code. | Pass `expectedBytes` (taken from qualification), construct lazily in `Init`, and move the addresses into `exu.json` -> `ControlPanel.*`. | Read `Hook.h`/`BasicPatch.h` (the ctor captures bytes with no expected set); grepped `exu.json`. |
| D-12 | [Med/Med] | Overlay.cpp:99-134, 1392-1446; profiles/bzr_2.2.301.json:40-60 | The pause-wrapper pattern exists twice, inline in the profile and in Overlay.cpp, and it is **not in `exu.json`**. The shell-wrapper pattern exists in `exu.json` and again in Overlay.cpp. The asm hard-codes `0x00887A64`, `0x0091812B` and `0x00918324`. `validate_hardening.py` does not check that the profile and the source agree. | Two sources of truth for a required runtime gate anchor. | Add `GameUI.EscapeWrapper` with entry/exit offsets and preimages to `exu.json`, generate the constants, and have `validate_hardening.py` assert equality. | Read the profile, exu.json and validate_hardening.py:95-140. |
| D-13 | [Med/Low] | Overlay.cpp:704-735, 1020-1048; OgreOverlaySystem.h (`OverlaySystem : RenderQueueListener, RenderSystem::Listener`) | EXU constructs a process-wide `Ogre::OverlaySystem` whenever `OverlayManager::getSingletonPtr()` is null. In upstream 1.10 its ctor calls `RenderSystem::setSharedListener(this)` and its dtor calls `setSharedListener(0)`. That silently replaces, and later nulls, any shared listener the game or another DLL installed. The `ShowOverlay` diagnostics already log `getSharedListener` (2721-2736), which suggests someone suspected this. | If Redux or OpenShim relies on the shared listener (device-lost handling), overlay use breaks it. | Before constructing, record `getSharedListener()`. After construction, restore it if it was non-null. After destruction, restore the saved value. Document in `Docs/`. | The header confirms the listener base. The `setSharedListener` call is from upstream 1.10 source (not in-tree), so confidence is Low. |
| D-14 | [Med/Low] | OgreNativeFontBridge.cpp:73-77, 103, 401-402 | `FontManager::create` is an instance method returning `SharedPtr<Font>`. It is called through a free `__thiscall` pointer typed to return an 8-byte POD (`FontPtrPod`). That only works if MSVC uses the hidden-return-pointer convention for this pointer type. If it returns in EDX:EAX instead, the stack and arguments shift. The returned `pInfo` ref is also never released, so the Font is never destroyed. Its generated `Fonts/<name>` material and texture then outlive the FontManager. In TrueType mode the font sits in `EXUOverlayRuntime`, which `TryResetFontResourceGroupIfStale` never clears, so after a Reset/Shutdown and re-creation, the texture name collides. | A latent ABI hazard plus a leak / duplicate-resource failure after reset. | Declare the call with an explicit out-pointer (`void(__thiscall*)(FontManager*, FontPtrPod* ret, ...)`), or call the real header's `create` in this TU using a matching `NameValuePairList`. Release the ref through the exported SharedPtr dtor. | Read the code. I could not disassemble the Steam build to confirm which return convention MSVC used. |
| D-15 | [Med/Med] | ControlPanel.cpp:1269-1289; Radar.cpp:375-481; ControlPanel.cpp:196-205, 274-282 | Engine-facing Lua APIs run whether or not `BuildValidation::IsSupportedBzr2301()` passed. Examples: raw calls to `0x004A6C70`/`0x004A6CD0`, `RefreshLayout`, `FindNamedPath`, `RefreshEdgePathBounds`, and fallback writes to the HUD-text globals. `SelectAdd`/`SelectOne` pass `GetObj(h)` for any userdata, including full userdata or a stale handle, with no liveness check. | Patches fail closed on an unqualified exe, but these direct calls do not. | Gate the non-patch engine calls on the same build check (return nil/false). Validate handles through the GameObject helper used elsewhere. The cross-cutting part belongs to the Scanner/GameObject reviewers. | Read `Init` (luaexport.cpp:474-518) and `CheckHandle` (LuaHelpers.h:196-203). |
| D-16 | [Low/High] | Overlay.cpp:2777-2781, 2796-2805, 2820-2821, 2884-2885, 2912-2913, 2928-2929, 2944-2945, 2987-2991, 3006-3015, 3030-3039, 3054-3055, 3071-3076, 3093-3097, 3113-3114, 3133-3134, 3162-3166, 3189-3193 | `const std::string name = luaL_checkstring(L,1)` is followed by another `luaL_check*`/`luaL_argerror`/`CheckColorOrSingles`. On a Lua error the `longjmp` skips `~basic_string`, leaking the heap buffer for names over 15 characters. | A small leak per misuse, and a pattern that new code will copy. | Read all arguments as `const char*`/numbers first, build `std::string` after the last check (ControlPanel.cpp already does this). | Read each binding. |

Quoted lines for High/Med items:

```cpp
// D-1  Overlay.cpp:3136-3151 - no ElementKind check before a TextArea-only native call
::Ogre::OverlayElement* element = FindOverlayElement(name);
...
const bool success = Native::TrySetTextAreaFontName(element, fontName.c_str());
// OgreNativeFontBridge.cpp:552
auto* textArea = static_cast<Ogre::TextAreaOverlayElement*>(overlayElement);
```
```cpp
// D-2
::Ogre::OverlayElement* element = manager->createOverlayElement(typeName, instanceName, false); // 2836
parent->::Ogre::OverlayContainer::addChild(child);                                            // 2938
parent->::Ogre::OverlayContainer::removeChild(childName);                                     // 2953
element->::Ogre::OverlayElement::setMaterialName(materialName);                               // 3064
```
```cpp
// D-3  Radar.cpp:172-177 (runs from CockpitRadar::Render every frame at scale 1)
Logging::LogMessage("[EXU::Radar] refreshLayout passthrough screenHeight=%d scale=%.4f base=%.6f", ...);
BZR::Radar::RefreshLayout(screenHeight);
```
```cpp
// D-5  Overlay.cpp:1369-1371 / 2712-2714
// A new mission's first ShowOverlay call will synchronize them normally.
RefreshOverlaySuppressionState("game-shell-wrapper-exit", false);
...
visibilityState.requestedVisible = true;
RefreshOverlaySuppressionState("show-overlay");   // syncs ALL tracked overlays
```
```cpp
// D-6  Overlay.cpp:2607-2615
void ShutdownOverlaySupport() noexcept {
    DestroyOverlayPauseHooks();
    DetachOverlaySystemFromTrackedSceneManagers(overlaySystemInstance);
    DestroyOverlaySystemInstance();      // frees even if detach bailed out
    ResetOverlayRuntimeCaches();
    knownElements.clear();               // Ogre elements not destroyed if manager isn't EXU's
    overlayVisibilityStates.clear();
}
```
```cpp
// D-9  Radar.cpp:377-379, 355
float newScale = static_cast<float>(luaL_checknumber(L, 1));
if (newScale <= 0.f) { luaL_error(...); }          // NaN passes
uint8_t newState = static_cast<uint8_t>(luaL_checkinteger(L, 1));  // 256 -> 0
```
```cpp
// D-11  ControlPanel.cpp:847-851 - no expectedBytes; static init
inline Hook g_scrapLabelColorHook(kScrapLabelColorHookAddress, &ScrapLabelColorHook,
    kScrapHudTextColorHookLength, ScrapPilotColorHookInitialStatus());
```

### Answers to the scoped questions

**Q2: pause and shell wrapper hooks.**
- Pause entry is `FUN_005D4690+0x26`. It is the first instruction after the re-entrancy guards (`[0x91832C]==0 && [0x91812B]==0`) and before `DAT_0091812b=1` and the blocking `while` loop over `FUN_005d5150` (the UI frames). Pause exit is `+0x1CA`, which is `DAT_0091812b=0`.
- Shell entry and exit (`FUN_005D42E0+0x32`/`+0x332`) are the writes of `uiWrapperActive` (`0x00918324`) 1 and 0, and they bracket the whole shell loop.
- Frame phase: main game thread, outside Ogre `renderOneFrame` (between frames, at a modal-loop boundary).
- Callbacks: Ogre `Overlay::show/hide` (SEH-guarded), `Logging`, `GameState::IsGameUiOpen` (reads `0x918310`/`0x918324` and calls `GetCursorInfo`). There is **no Lua**.
- Not `noexcept` (D-10).
- After `ShutdownOverlaySupport` the hooks are restored and destroyed first (1450-1453), so they cannot fire unless the restore failed (D-L7). Depths are reset to 0 and the maps cleared.
- Quitting from inside the pause menu, where the Lua state closes inside the loop, is safe: the exit site is restored before it is reached. The next Lua call re-installs lazily (the `attempted` flags reset), and the clamping handles an exit-without-entry.
- The entry thunk clobbers EAX (`pop eax; ...; jmp eax`). The GOG decomp shows the next instructions push arguments and then call `FUN_0081e820`, so EAX is dead at +0x26 (Low; see D-L9).

**Q3: Ogre objects EXU creates.**
- **OverlaySystem**: `malloc(256)` plus the exported ctor, only when no OverlayManager exists. It is attached as a RenderQueueListener to each SceneManager seen (raw pointers in `attachedOverlaySceneManagers`), detached and destroyed in Reset/Shutdown (fail-open, D-6).
- **Overlays and elements**: Lua-chosen names in Ogre's single global namespace (no EXU prefix, so they can collide with any other user of the OverlayManager). They are tracked **by name only**. No raw Overlay or Element pointers are cached, which is good.
- `ResetOverlaySupportInternal` destroys overlays first, then non-containers, then containers (stable-sorted). Upstream 1.10 destructors unlink parent and child either way, so order is safe.
- `Shutdown` does not destroy them by name (D-6).
- A mission reload with the same names gets `false` from Create* while the old objects persist (D-5/D-6).
- **Resource groups** `EXUOverlayRuntime` and `EXUOverlayFontRuntime` are created once, locations added idempotently, and never removed (process lifetime; acceptable).
- **Font** `CRBZoneOverlayFont`: its SharedPtr ref is leaked (D-14). It creates a material and texture named after the font.
- The pause/shell `Hook` objects are heap-owned via `unique_ptr`, so they never move (correct for FF 15).

**Q4: OgreNativeFontBridge.**
- It touches `ResourceGroupManager` and `FontManager` singletons, `Font` configuration, `TextAreaOverlayElement::setFontName/setCaption/setCharHeight/setColour`, `UTFString` ctor/dtor and `ColourValue` ctor.
- It uses **no raw addresses and no vtable slots**; everything goes through exported mangled names (`GetProcAddress`) or the hand-made `lib/OgreOverlay.lib`/`OgreMain.lib` imports.
- It is not gated on the BZR build check. It does not need to be, because it touches no exe code and a missing export returns `false`.
- It fails closed: every entry point is `noexcept`, with an SEH shell (C++ exceptions pass through via `CONTINUE_SEARCH`) inside `try/catch`. This is the best pattern in scope.
- Layout assumptions:
  - `sizeof(Ogre::UTFString)` and `sizeof(ColourValue)` come from the in-tree headers (compiled with the v14x STL; basic_string layout is 24 bytes in both, so it matches).
  - `src/Ogre/OgreBuildSettings.h:16` sets `OGRE_CONTAINERS_USE_CUSTOM_MEMORY_ALLOCATOR 0`, which contradicts ABI_NOTES (TRUE). This is presumably deliberate, and it is why `FontManager::create` must go through `GetProcAddress`, since its mangled name contains `STLAllocator<...CategorisedAllocPolicy<0>>`. See D-L2.
- **C++14 pin.** It was introduced in commit 4304829 and is documented nowhere. It is required because `OgreString.h:211` uses `::std::tr1::hash` for MSVC >= 1600. MSVC's STL drops `std::tr1` in `/std:c++17` and later modes.
  - `register` (OgreBitwise.h:309-363, OgreMemorySTLAllocator.h:130) is already neutralised by the `#define register` hack at bridge lines 11-14.
  - `_STLP_MSVC` (line 16) steers `OgrePrerequisites.h:98` away from `std::tr1::unordered_map`, but does not cover `OgreString.h:211`.
  - `OgreUTFString.h:215` (`std::iterator`) only produces deprecation warnings.
  - It can likely be lifted with a pre-include shim (`namespace std { namespace tr1 { using std::hash; } }`) plus warning suppression for the external headers. It is unverified that `_HAS_TR1_NAMESPACE` still exists in the installed toolset.

**Q5: engine writes.** OpenShim radar ownership: **no dual write today.** When `OpenShimSetRadarSizeScale` exists, EXU skips its call-site hooks (Radar.cpp:246-256), and `SetSizeScale` delegates, raising a Lua error if OpenShim returns FALSE (post-P0-5 OpenShim fails closed, so EXU does not fall back). The residual gap is that getter and setter are resolved independently: an OpenShim exporting only the getter would leave EXU writing and OpenShim reading. No stock value is restored at mission end or Lua close (D-8).

| Written by | Target | Mechanism | Guard |
|---|---|---|---|
| Radar | `state` `0x008EAAAC` | Scanner (VirtualQuery + VirtualProtect per write) | none beyond readability |
| Radar | `scale` `0x008E77B0`, `cockpitWireframeProjectionBase` `0x008E7754`, `radarLeft` `0x008E77A8`, `cockpitWireframeCenterX/Y` `0x008E7924/28` | raw pointer writes | none |
| Radar | `edge_path` points (heap, via `FindNamedPath`), then `RefreshEdgePathBounds` | raw writes | none |
| Radar | 2 call-site `rel32`s | `InlinePatch` | E8-opcode and target check, then BuildValidation |
| ControlPanel | 8 HUD-text ints `0x0091826C-0x009182A0` | raw writes: every HUD frame via the draw hook (only when the OpenShim bridge is absent), or on the Lua call | none |
| ControlPanel | 5 code hooks | `Hook` | BuildValidation only (D-11) |
| ControlPanel | command-menu rect | read only | - |

HUD colour hooks stand down when the OpenShim export exists (decided at static init). The draw hook stays active in both cases but early-outs when OpenShim is present.

**Q6.** No native per-frame overlay work. Per-frame EXU code in scope:
- The scrap/pilot draw hook: 1 indirect call; without OpenShim, 8 int compares and 8 writes. Cheap.
- `RefreshLayoutConcentric` without OpenShim (D-3). Expensive.
- Anything the Lua HUD calls from `Update`.
  - A typical `SetOverlayCaption(name,text)` costs: two `std::string` constructions (heap if over 15 characters); `GetOverlayManager` (flag checks plus an SEH frame); `hasOverlayElement` plus `getOverlayElement` (two `std::map<string>` lookups); `UTFString` conversion and allocation; `setCaption`; and a **log line with a file open/close** (D-4).
  - There is no `getByName` per native frame, and `IsReadableRange` is not on any overlay path (the Overlay.cpp wrapper is unused).
  - `Reticle::GetRange/SetRange` and `Radar::Get/SetSizeScale` call `GetModuleHandleA`+`GetProcAddress` on every Lua call (uncached).

**Q7: raw Ogre calls in `lua_CFunction`s with no try/SEH.**
- Throwing: 2836, 2938, 2953, 3064 (D-2).
- Non-throwing in 1.10 but unguarded: 2654 `getByName`, 2660 `create` (throws on duplicate, pre-checked), 2790 `setZOrder`, 2814 `setScroll`, 2830/2878 `hasOverlayElement`, 2900 `add2D` (a duplicate add double-registers the root), 2922 `remove2D`, 2967/2981 `show/hide`, 3000 `setMetricsMode`, 3024 `setPosition`, 3048 `setDimensions`, 3107 `setColour`.
- `Renderer.cpp:83` calls an RVA-resolved `SetCameraPolygonMode` unguarded.
- Everything else goes through SEH wrappers or the bridge.

## 3. Dead / unreferenced code

| Symbol | Location | Verification |
|---|---|---|
| `IsReadableRange` wrapper | Overlay.cpp:254 | Defined only; grep of Overlay.cpp shows no call. |
| `TrySetViewportOverlaysEnabled` + `ResolveSetViewportOverlaysEnabled` | Overlay.cpp:1183, 320 | Only self-referenced; the live copies are in `Environment.cpp:3026-3115`. |
| Whole-`.text` fallback scan in `ResolvePauseWrapperFunctionAddress`/`ResolveGameShellWrapperFunctionAddress` | Overlay.cpp:1470-1484, 1494-1508 | Both wrappers are required runtime-gate anchors at their stock VA (profile). If the VA fails, `patchActivationEnabled` is false, so a relocated match can never install. |
| `overlayPauseHooksReady` / `overlayGameShellHooksReady` | Overlay.cpp:90, 92 | Write plus log only; never read for control flow. |
| `CockpitWireframeScaleBaselines::initialized` | Radar.cpp:51, 117 | Written, never read. |
| `TryParseFontScript` `scriptName` parameter | OgreNativeFontBridge.cpp:320-331 | Ignored; `initialiseResourceGroup` parses every script in the group. |
| Shim declarations `SharedPtr`, `Resource`, `Font`, `FontType`, `PanelOverlayElement::setTiling/setUV/setTransparent`, `BorderPanel::setBorderSize x3/setBorderMaterialName`, `TextArea::setSpaceWidth/setColourTop/setColourBottom/setAlignment` | OgreOverlayShim.h:30-49, 83-106, 118-145 | Overlay.cpp calls these through `GetProcAddress`; the bridge uses the real headers. The class types are only used as cast targets. |
| `exu.DrawLine` / `DrawBox` / `ClearVisuals` | Renderer.cpp:95-114 | Exported (luaexport.cpp:804-806) but no-ops that only validate arguments. Definitions (2463-2469) declares them as `(...)` with no "not implemented" note. |

Duplicates:
- `GetOgreMainModule`/`GetOgreOverlayModule`: Overlay.cpp:165, 297 and bridge:80, 86.
- Viewport-overlay resolvers: Overlay.cpp vs Environment.cpp.
- `LogNativeOverlayMessage` vs `Logging::LogMessage`.
- 8 OpenShim resolvers in ControlPanel.cpp:386-523.
- 11 `TryCall*` bodies in Overlay.cpp:1724-2114.

## 4. Raw address census (`0x00[4-9A-F]xxxxx` literals)

| File | Count | Literal (line) | Use | In exu.json? |
|---|---|---|---|---|
| Overlay.cpp | 6 | `0x005D4690` (99) | hook site base (byte-compared, then **Hook-written**) | no (profile inline only) |
| | | `0x005D42E0` (118) | hook site base (compared, then written) | yes, `GameUI.MainShellWrapper` |
| | | `0x00887A64` (1392) | pushed immediate, replicated from the preimage | no |
| | | `0x0091812B` (1401) | **written** (replicated `mov byte`) | no |
| | | `0x00918324` (1418, 1435) | **written** (replicated `mov dword`) | no (only `game_state.cpp` has it) |
| ControlPanel.cpp | 15 | `0x005C6FF0`, `0x005C712B`, `0x005C719B`, `0x005C72F1`, `0x005C7361` (36-41) | **Hook-written** code sites | no |
| | | `0x0047C070` (89) | **called** | no |
| | | `0x0094F4B0` (90) | passed as `this` | no |
| | | `0x0091829C`, `0x009182A0`, `0x0091826C`, `0x00918270`, `0x00918280`, `0x00918284`, `0x00918278`, `0x0091827C` (92-95) | **written** and read | no |
| Radar.cpp | 3 | `0x0049325F`, `0x0049405B` (238) | **InlinePatch-written** after an E8/target compare | no |
| | | `0x00492EC0` (239) | compared as the expected call target | yes, `Radar.RefreshLayout` |
| Renderer, Reticle, NativeFontBridge, shim, all headers | 0 | - | - | - |

Total: **24 literals, 23 distinct**. 2 are in `exu.json`. Radar, Reticle and ControlPanel additionally consume `bzr.h` addresses (`Radar.*`, `Reticle.*`, `ControlPanel.*`, `Camera.View_Record_MainCam`), which are catalogued. Note: `SetSizeScale` reads `View_Record_MainCam` (`0x008EAAE0`), and the 2.2.301 profile marks that catalog entry's reference pattern as stale (see D-L10).

## 5. Lifetime / ownership notes

- **Process lifetime.** Most state here lives for the whole process:
  - Overlay: static `GetProcAddress` caches; `HMODULE` statics (never reloaded; fine because the Ogre DLLs never unload).
  - ControlPanel: `inline Hook` globals, HUD offset/colour globals, the command-rect table pointer, and OpenShim resolver statics (frozen at first resolve; `winmm` is never unloaded).
  - Radar: the leaked `InlinePatch` objects (restored by `UnloadAllPatches`, re-armed by `EnableDeferredPatchActivation`), `stockProjectionBase`, `s_refreshOrdinal`.
  - Renderer: `g_isWireframe`.
- **Runtime (Ogre) lifetime.** The EXU `OverlaySystem`, `attachedOverlaySceneManagers` (raw SceneManager pointers; safe only because Redux never recreates the SM, per research), the resource groups, and the leaked Font ref (D-14).
- **Lua-state lifetime.** Overlay hooks, the tracking maps and the runtime-font flags are cleared in `ShutdownOverlaySupport` (from the `__gc` sentinel). The Ogre objects are *not* destroyed by name (D-6). `ExuNotifyMissionSimulationState` can arrive at any time; it is safe after Shutdown (empty maps).
- **Mission lifetime.** EXU has none of its own in this scope. It relies on OpenShim's `SetRunning` seam (`NotifyMissionSimulationState`) for overlays, and on OpenShim ownership for HUD, radar and reticle. Without OpenShim, mission state leaks (D-8). With OpenShim, overlays are hidden but can be resurrected (D-5).
- **Temporary-patch lifetime.** None in scope.

## 6. Performance notes

- D-3 (radar wrapper, per frame, with a log line) is the only native per-frame hot spot found, and it is serious when OpenShim is absent.
- D-4: Lua-driven overlay updates pay a log-file open/close per call. This dominates the cost of a caption update.
- `EnsureOverlayRuntimeResources` (Overlay.cpp:780-874) runs once per Lua state or reset, synchronously on the main thread inside the first `SetOverlayParameter("font_name")`/`SetOverlayTextFont`. It does a `recursive_directory_iterator` over `addon`, `mods` and `packaged_mods` (depth 2) and the entire `workshop/content/301650` (depth 3), with 2 `stat`s per directory. With many Workshop items this is a visible hitch.
- `DiscoverCommandMenuRects` (ControlPanel.cpp:645-729) runs once: a 4-byte-stride scan of ~188 KB of raw `.data`, with an SEH frame per candidate. Cheap, and cached.
- The scrap/pilot draw hook is per HUD frame and cheap.
- `IsReadableRange` sits on Scanner Read/Write only (per Lua call); none is per frame in scope.

## 7. Patterns worth keeping

- The OgreNativeFontBridge exception layering. The public API is `noexcept` and wraps `try{...}catch(Ogre::Exception&/std::exception&/...)` around an SEH shell whose filter returns `CONTINUE_SEARCH` for `0xE06D7363`. Access violations are caught, C++ exceptions reach real `catch` clauses, and nothing crosses into Lua. Overlay.cpp's `__except(EXCEPTION_EXECUTE_HANDLER)` wrappers should adopt this filter instead of swallowing C++ exceptions.
- Overlay tracking stores **names, not pointers**. Every access re-resolves through the OverlayManager, so there are no stale Ogre pointers.
- `TrySetOverlayParameterDirect` does kind-checked, parsed, finite-validated dispatch. Apply it to D-1 and D-7.
- `ResetOverlaySupportInternal` destroys by name, children before containers, copying the key lists before mutating the maps.
- Radar's OpenShim stand-down: a single owner decided by export presence, failing closed (Lua error) rather than falling back to a second writer.
- `ResolveStockProjectionBase` derives the idempotent baseline (`currentBase / currentScale`) instead of snapshotting a mutable global.
- `InstallRefreshLayoutCallSiteHooks` verifies the opcode *and* the decoded call target before patching.
- Heap-owned `unique_ptr<Hook>` for FF 15 hooks, which must not move.

## 8. Low-severity items

- D-L1: The C++14 pin (vcxproj:163-165) is undocumented. Add a vcxproj comment or a `Docs/` note naming `OgreString.h:211` `std::tr1::hash` as the reason (see Q4).
- D-L2: `src/Ogre/OgreBuildSettings.h:15-17` (`CONTAINERS_USE_CUSTOM_MEMORY_ALLOCATOR 0`) contradicts ABI_NOTES (TRUE). Inline header container code compiled in EXU (e.g. `Font::setGlyphTexCoords` map inserts at OgreFont.h:295-313 and `addCodePointRange` at :355-358) allocates with `std::allocator` into containers the DLL frees through `STLAllocator<StdAllocPolicy>`. It works only because the shipped OgreMain uses `StdAllocPolicy` (verified in its export strings) and both MSVCR120 and the UCRT allocate from the process heap. Document this as an ABI invariant. Also, `OgreBuildSettings.h` is found only through the including file's directory (quoted-include search), which is fragile.
- D-L3: `SetEdgePathCoords` (Radar.cpp:470-477) writes points one by one while `CheckEdgePathPoint` can raise mid-loop, leaving a partially updated path with stale bounds. Validate everything first, then write.
- D-L4: Several `__except(EXCEPTION_EXECUTE_HANDLER)` blocks in Overlay.cpp (173-223, 1220-1262, 2476-2518, ...) swallow C++ exceptions: the exception object leaks and there is no diagnostic distinction. Use the bridge's filter.
- D-L5: Ogre ABI knowledge (about 30 mangled export names, OverlaySystem alloc size, listener wiring) lives in `src/UI/Overlay.cpp`, contrary to ARCHITECTURE.md (Ogre runtime knowledge). `kOverlaySystemAllocSize = 256` has no `static_assert` against `sizeof(Ogre::OverlaySystem)`, which could be computed in the C++14 TU.
- D-L6: CR-branded asset names (`CRBZoneOverlayFont`, `CRBZoneOverlay.fontdef`) and a Workshop scan hard-coded in EXU feature code. Campaign content belongs to CR. `SetOverlayTextFont` returns false for any valid Ogre font unless this CR asset is found (Overlay.cpp:3143-3149), and `Definitions/ExtraUtils.lua:1301-1305` does not document that.
- D-L7: `Hook`/`BasicPatch` destructor: if `RestorePatch`'s `VirtualProtect` fails, the object is still freed while the FF 15 still points at its `m_function` slot, leaving a dangling indirect call. This is shared infrastructure; it affects Overlay's `unique_ptr<Hook>` reset.
- D-L8: Colour hooks do not preserve stock register semantics. At `0x005C719B` stock is `mov edx,[white]; push edx` (OpenShim `bzr_hooks.cpp:1623`), but the hook loads EAX. At `0x005C7361` stock is `mov ecx`, but the hook loads EDX and leaves ECX = return address. This is safe only if those registers are dead; the adjacent argument pushes suggest they are. Not disassembly-verified.
- D-L9: The pause-entry thunk clobbers EAX. The GOG decomp shows `push offset FUN_004bc8c0` before +0x26 and `call FUN_0081e820` after, so EAX is dead and this is fine. Keep a comment stating that. The `.text` fallback scan is unreachable in practice (see §3).
- D-L10: `Radar::SetSizeScale` (400-403) guards with `cam != nullptr` on a constant pointer (always true), and derives `screenHeight` from `View_Record_MainCam->Orig_y`, whose catalog signature the 2.2.301 profile flags as stale.
- D-L11: `AddOverlay2D` twice registers the same root container twice (1.10 `add2D` is a `push_back`). Consider `remove2D` before `add2D`.
- D-L12: `ShowOverlay` performs 5 diagnostic Root/RenderSystem/Viewport calls every time (2716-2739). Move them to a debug build or a one-shot.
- D-L13: `EnsureOverlayRuntimeResources` adds whole ancestor directories of `exu.dll` (up to 5 levels) as Ogre `FileSystem` resource locations whenever a `BZONE.ttf` or fontdef exists there. This is broad resource-namespace pollution, and it is unreviewed against `Docs/BZR_PLATFORM_COMPATIBILITY.md` for GOG, Proton and Wine layouts.
- D-L14: `GetCommandMenuRect` relies on a heuristic `.data` scan instead of a catalogued address. It is read-only, but a false positive would return plausible wrong bounds. Once found, record the address in `exu.json`.
- D-L15: `Definitions/ExtraUtils.lua` declares `GetViewportOverlaysEnabled`/`SetViewportOverlaysEnabled`/`ResetOverlaySupport`/`GetHudSpriteRect`... as `(...)`, with no parameter docs. `CreateOverlayElement` doesn't warn that an unknown type currently crashes (D-2).
