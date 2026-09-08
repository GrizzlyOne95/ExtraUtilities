# BZR OpenShim vs EXU Ownership and Config Strategy

Date: 2026-07-07

This document compiles the ownership/migration recommendations for **Battlezone98Redux_Shim / OpenShim** and **ExtraUtilities / EXU**, with a practical config-file strategy for user-facing toggles until native menu UI hooks exist.

## Executive Summary

Use this split as the guiding rule:

```text
OpenShim = global native patch layer + stock-game/user-facing fixes + shell/network/crash prevention
EXU      = Lua/modding API + per-mission runtime scripting + optional helpers exposed to authors
Shared   = Shim owns unsafe native hook; EXU exposes safe Lua wrapper when scripts need control
```

OpenShim should **protect and repair the game**. EXU should **expose power to scripts and mod authors**.

The most important standout migration is that several systems currently shaped like EXU features should either move to Shim or be duplicated as Shim-owned persistent player settings:

1. Unit VO queue throttling core
2. Radar scale / HUD placement
3. Target reticle popup mode
4. `ObjectiveObjects()` stock Lua bug fix, eventually
5. AI ODF tuning application hooks
6. Howitzer/minelayer weapon-mask actual fix
7. Deep command-menu native UI plumbing, if arbitrary native buttons are implemented

A config file is a very good interim solution for Shim settings. OpenShim already uses startup patching, environment variables, and `net.ini`-based network controls, so a dedicated `openshim.ini` / `bzr_openshim.ini` is technically aligned with the current design.

---

# 1. Repo Roles

## OpenShim / Battlezone98Redux_Shim

OpenShim should own:

- Global stock-game fixes
- Startup-time native patches
- Shell/menu patches
- Multiplayer lobby/session fixes
- Winsock/netcode hooks
- Crash guards and diagnostics
- Renderer/input/audio global patches
- User-facing persistent settings
- Native hook exports that EXU can optionally resolve

It is loaded as `winmm.dll`, proxies the real Windows `winmm.dll`, spawns a patch thread, initializes startup-time Winsock hooks, and runs patching logic before mission Lua is necessarily involved.

## EXU / ExtraUtilities

EXU should own:

- Lua APIs for mission/mod authors
- Custom UI overlays
- Runtime material/environment scripting
- Mission-specific AI inspection/control
- Custom HUD/event behavior
- Script-level compatibility helpers
- Safe Lua wrappers around OpenShim exports

It is loaded from Lua with:

```lua
local exu = require("exu")
```

That makes it ideal for intentional mission/mod behavior, but not ideal as the only home for stock-game fixes that should work before any mission script loads.

---

# 2. Ownership Decision Rules

## Use OpenShim when the feature:

- Affects stock players without any mod script opt-in
- Needs to run before Lua exists
- Touches the shell, menus, load screen, input, renderer, audio backend, or netcode
- Prevents hard crashes
- Enforces multiplayer fairness
- Applies native class behavior globally
- Needs persistent user settings
- Must fail closed if a hook site drifts

## Use EXU when the feature:

- Is mainly for modders or mission authors
- Is intentionally called from Lua
- Is content-specific or mission-specific
- Exposes runtime state or helpers to scripts
- Creates custom UI/widgets/material effects
- Implements optional behavior that should not globally change stock gameplay

## Use Shared Shim + EXU bridge when:

- The unsafe part is native and global
- Lua scripts still need a clean API
- The feature should work without EXU for stock users, but become scriptable when EXU is present

Pattern:

```text
OpenShim:
  - Finds native function / installs hook / owns crash guards
  - Exports a small C ABI or shared bridge

EXU:
  - Resolves OpenShim export if available
  - Exposes Lua helper
  - Fails closed if Shim bridge is absent
```

The music bridge is the model to follow.

---

# 3. Standout Items to Move or Recenter

## 3.1 Unit VO Queue Throttling

**Recommendation:** Move the core from EXU-style ownership to Shim-owned global behavior, while keeping EXU controls.

Current center of gravity: EXU / Lua-facing.

Better ownership:

```text
OpenShim:
- Native Say -> QueueCB hook
- Duplicate bark suppression
- Stale queue compaction
- Queue depth limit
- Global config/user setting: Reduced Unit Chatter

EXU:
- exu.SetUnitVoAlternates()
- exu.SetUnitVoMuted()
- exu.SetUnitVoThrottle() runtime override
- Mission-specific bark customization
```

**Reasoning:** Delayed/stale unit bark spam is a normal-player quality issue. It should improve stock and modded play without requiring a mission script. EXU remains the correct place for custom alternates and mission-specific muting.

**Feasibility:** High to medium. The hook path has already been identified and implemented once. Main work is moving/duplicating the stock-safe queue policy into Shim and adding config defaults.

---

## 3.2 Radar Scale / HUD Placement

**Recommendation:** Duplicate into Shim as persistent player settings; keep EXU APIs.

Current center of gravity: EXU.

Better ownership:

```text
OpenShim:
- Persistent HUD config
- Radar scale
- Radar/background origin correction
- Legacy scrap/pilot layout setting

EXU:
- exu.GetRadarSizeScale()
- exu.SetRadarSizeScale()
- exu.GetCommandMenuRect()
- Per-mission custom HUD layout helpers
```

**Reasoning:** 4K UI/radar scale is an accessibility/display issue. Players should not need a Lua mission to set it.

**Feasibility:** Medium. The scale address and refresh helper are known; the origin misalignment needs live layout work.

---

## 3.3 Target Reticle Popup Mode

**Recommendation:** Shim primary, EXU optional control.

Better ownership:

```text
OpenShim:
- Target popup suppression/filter hook
- Persistent setting:
  - Default
  - ExplicitOnly
  - DisableRecentHit
  - NeutralOnly, once safe
  - Off

EXU:
- exu.GetTargetPopupMode()
- exu.SetTargetPopupMode()
- PDA/menu integration for mods
```

**Reasoning:** This is stock HUD/render behavior and a player preference. A PDA option is useful, but the setting should exist at Shim level first.

**Feasibility:** Medium. Default/explicit suppression appears feasible. Neutral-only needs live crash debugging before becoming a normal setting.

---

## 3.4 `ObjectiveObjects()` Iterator Bug

**Recommendation:** EXU short-term is acceptable, but eventually move to Shim/native Lua patch.

Current center of gravity: EXU Lua-side replacement.

Better ownership:

```text
OpenShim:
- Install safe replacement into Lua global environment when Lua initializes
- Or patch native iterator helper directly if feasible

EXU:
- Keep compatibility wrapper
- Detect if Shim fix is present and avoid double-wrapping
```

**Reasoning:** This is a stock Lua API correctness bug. Missions should not hang simply because EXU was not loaded early enough.

**Feasibility:** Medium. EXU proves the replacement behavior. Native Lua environment timing/hook safety is the remaining work.

---

## 3.5 Engine Flame Color

**Recommendation:** Current split is correct, but formalize Shim ownership.

Better ownership:

```text
OpenShim:
- Hovercraft exhaust callsite hooks
- EngineFlame manager clone/reroute
- VTable Control/Submit hooks
- Optional global team-color policy

EXU:
- exu.GetTeamEngineFlameColor()
- exu.SetTeamEngineFlameColor()
- exu.ClearTeamEngineFlameColor()
```

**Reasoning:** Dangerous native callsite/vtable routing belongs in Shim. Lua-facing team/handle control belongs in EXU.

**Feasibility:** Medium. First-pass implementation exists, but live gameplay validation is still needed.

---

## 3.6 OGG / Music Control

**Recommendation:** Keep the bridge pattern. Do not move fully either way.

Better ownership:

```text
OpenShim:
- Resolve soundtrack manager
- Native Set/Stop/Pause/Resume bridge
- Optional filename/track override if safe

EXU:
- exu.SetMusicTrack()
- exu.StopMusic()
- exu.PauseMusic()
- exu.ResumeMusic()
- exu.GetMusicTrack()
```

**Reasoning:** Native soundtrack calls are unsafe and should live in Shim. Lua scripts need ergonomic helpers in EXU.

**Feasibility:** Medium. `SetMusicTrack` is the strongest current path. Stop/pause/resume should continue to fail closed until native targets are validated.

---

## 3.7 Howitzer / Minelayer Weapon Mask Fix

**Recommendation:** Actual fix should be Shim-only; EXU should keep diagnostics only.

Better ownership:

```text
OpenShim:
- Actual weapon-mask/carrier/mode-list fix
- Crash-safe guards
- Default disabled or guarded until July 2026 crash is reverified
- Trace logging

EXU:
- exu.GetSelectedWeaponMask()
- exu.GetWeaponSelectionInfo()
- Debug/reporting only
```

**Reasoning:** This is native class/process behavior. The actual patch should not require Lua and should not be controlled primarily through EXU.

**Feasibility:** Medium-risk. The concept is mapped, but the July 2026 crash means this should be revalidated before being default-on.

---

## 3.8 AI ODF Tuning

**Recommendation:** Shim applies ODF-driven behavior; EXU exposes inspection/runtime experimentation.

Better ownership:

```text
OpenShim:
- Parse ODF AI fields
- Apply native process/task hooks
- Default-on safe fields
- Trace logs for engage range / retarget / stuck / scavenger selection

EXU:
- exu.GetAiProcessState()
- exu.GetAiTaskState()
- exu.SetAiTaskState() for scripted experiments
- Debug overlays / inspection tools
```

**Reasoning:** ODF-driven behavior should work without mission Lua. Runtime inspection and manual AI task manipulation are EXU strengths.

**Feasibility:** Medium. Engage range path is already partially proven. Retarget, stuck, and scavenger path scoring need live hook validation.

---

## 3.9 Command Menu Replacement

**Recommendation:** Keep Lua-facing API in EXU, but move deeper native UI plumbing to Shim if arbitrary real buttons happen.

Better ownership:

```text
OpenShim:
- Safe native control-panel/menu hook primitives
- Optional generic command slot injection backend
- Crash guards and stock fallback

EXU:
- exu.ReplaceStockCmd()
- exu.AddUnitCommand()
- exu.RemoveUnitCommand()
- Lua callbacks
```

**Reasoning:** The command action is Lua-defined, so EXU is the correct user-facing API. But true arbitrary buttons will likely require deeper native menu allocation/draw/input work, which is Shim territory.

**Feasibility:** Medium-low. Hunt-slot replacement is feasible; arbitrary button injection is a deeper UI patch.

---

# 4. Items That Should Stay Shim

These are too global, too early, too crash-oriented, or too multiplayer-sensitive for EXU to own:

```text
15  MP .tga whitelist
16  UI/cursor/loading-screen replacement loading
22  BMP thumbnail crash
24  satellite fog of war
26  TCP/IP/LAN exposure
27  nickname shell UI
28  netcode buffers/reordering
29  Steam map list jumping
32  ban feature UI/enforcement
45  keybinding shell UI rewrite
49  weaponMask=00000 follow crash
52  mod loading separation
56  targeting camera bug
57  earthquake/dayquake save bug
58  cinematic camera bug
59  walker cockpit jitter
61  howitzer retaliation bug
62  native MP sync correctness
63  freecam MP exploit
64  MagnetClass divide by zero
65  renderCount allocation crash
66  pilot hardpoint crash
67  random crash diagnostics/fixes
70  8-character filename limit investigation
71  custom MP faction shell lists
74  MP Cloak / Any Nation options
78  satellite/F9 targeting camera bug
79  mission briefing text cutoff
80  netcode/custom matchmaking
```

**Reasoning:** Lua may not be loaded, and even when it is, a script extender should not be responsible for protecting the game from startup/menu/network crashes.

---

# 5. Items That Should Stay EXU

These are primarily mission/mod APIs or content-driven scripting features:

```text
1   Lua command callbacks and script-level command replacement API
2   Custom Ogre UI overlays responding to Lua
3   Material hotswapping APIs
5   Runtime edge_path/minimap bounds scripting
6   Runtime planet/environment swap scripting
7   AI process inspection and runtime control APIs
8   Nested build menus as Lua/mod-facing API, with Shim backend if needed
11  Mission-level stats APIs
17  Lua wrappers for music bridge
36  Emissives off in empty craft, if mission/content controlled
37  Pulsing/motion emissive effects
38  Twinkling stars, if map/content controlled
42  Constructor selected build index
48  Reactive reticles/hit markers as custom HUD feature
50  Dynamic HUD effects based on in-game events
69  draw_geom helpers/docs if native renderer support exists
75  Runtime terrain wave controls if added on top of TRN-native support
```

**Reasoning:** These are authoring features. They should be safe, discoverable Lua APIs, not mandatory global changes to stock gameplay.

---

# 6. Shared Bridge Candidates

These should deliberately use both repos:

| Feature | Shim Responsibility | EXU Responsibility |
|---|---|---|
| Music / OGG soundtrack control | Native soundtrack manager bridge and fail-closed exports | Lua helpers |
| Engine flame color | Callsite/vtable hooks and default policy | Lua color APIs |
| Radar/HUD | Persistent setting and stock layout fix | Lua HUD helpers |
| Target popup mode | Native render-path filter and persistent setting | Lua/PDA control |
| Unit VO | Global queue policy | Alternates and mission override |
| AI ODF tuning | Native ODF parse/apply hooks | Inspection and debug APIs |
| Command menu | Deep control-panel hook primitives, if needed | Lua callbacks and modder API |
| MP objective/state sync | Native engine-level fixes | Script-level sync helper patterns |

---

# 7. Config File Strategy for OpenShim

## Short answer

Yes, a config file is a good fit for Shim right now.

OpenShim already has the right shape for this:

- It loads at process startup as `winmm.dll`.
- It already applies native patches from a patcher thread.
- It already uses environment variables for feature gates and diagnostics.
- It already uses a `net.ini` file for packet reorder/buffer logging controls.
- Many settings need to be known before hooks are installed.

A dedicated config file would reduce environment-variable sprawl and make it easier to later add shell/menu UI toggles that write to the same backing store.

## Recommended file name and location

Use one dedicated file next to the game executable:

```text
<Battlezone 98 Redux game folder>\openshim.ini
```

Alternative if you want a more game-specific name:

```text
<Battlezone 98 Redux game folder>\bzr_openshim.ini
```

I would not keep expanding `net.ini` for everything. Leave `net.ini` for existing netcode diagnostics if you want backward compatibility, but make `openshim.ini` the unified future config.

## Recommended precedence

```text
1. Environment variables
2. openshim.ini / bzr_openshim.ini
3. Built-in defaults
```

Environment variables should remain useful for testers, CI, one-off debugging, and emergency overrides.

## Recommended lifecycle

- Load config once during Shim initialization.
- Apply startup-only patch gates before installing hooks.
- Allow runtime-read settings only for systems that already poll safely.
- Log the resolved settings once at startup.
- Do not hot-reload initially unless there is a specific need.
- Menu UI can later write this same file and mark some settings as “restart required.”

## Default policy

Your existing rule should stay:

```text
If a feature is gated with a variable or boolean to be enabled, it should default to ON so it can be verified, unless it is experimental, crash-prone, multiplayer-risky, or unvalidated.
```

Practical version:

```text
Safe stock fixes: default ON
Crash guards: default ON
Diagnostics: default OFF
Trace logging: default OFF
Experimental hooks: default OFF
MP compatibility-risk hooks: default OFF until validated
Behavior-changing balance options: default conservative / stock-compatible
```

## Safety requirements

Every config-backed setting should:

- Clamp numeric ranges.
- Validate enum strings.
- Log final resolved value.
- Fail closed if invalid.
- Prefer stock behavior if hook validation fails.
- Support an emergency disable flag for risky patches.
- Keep old environment-variable aliases where they already exist.

---

# 8. Suggested `openshim.ini` Skeleton

```ini
; BZR OpenShim configuration
; Place next to BZR.exe or battlezone98redux.exe.
; Environment variables override this file.

[Core]
Enabled=1
ConfigVersion=1
LogResolvedSettings=1

[Logging]
TracePatches=0
TraceHookValidation=0
TraceCrashes=1
WriteMiniDumps=1
DedicatedLogsFolder=1

[Network]
EnablePacketReorder=1
PacketReorderWindowMs=45
PacketReorderDepth=8
PacketReorderPeers=32
PacketReorderDrainCap=96
LogPacketReorder=0
EnableBufferLog=0
BufferLogPayloadBytes=32
BufferLogRingRecords=65536

[Audio]
MaxSoundChannels=150
TraceSoundChannels=0
ReducedUnitChatter=1
UnitVoThrottleMs=750
UnitVoQueueDepthLimit=4
UnitVoStaleMs=3000
UnderAttackThrottle=1

[HUD]
RadarScale=1.0
LegacyScrapPilotLayout=1
TargetPopupMode=Default
; Valid TargetPopupMode values:
; Default, ExplicitOnly, DisableRecentHit, NeutralOnly, Off

[Input]
RawInput=0
TraceRawInput=0

[GameplayFixes]
ConstructorRemoteBuildFix=1
PerceivedTeamAttackRevealFix=1
SplinterUndeadFix=1
WeaponMaskZeroFollowCrashGuard=1
MagnetClassRangeZeroGuard=1
TugCargoFix=1
APCDeployTargetFix=1
HowitzerRetaliationFix=1

[AI]
EnableAiOdfTuning=1
TraceAiRange=0
TraceBomberRange=0
EnableBomberStandoff=0

[Renderer]
TerrainType0DeformFix=1
TerrainCenterScaleFix=0
TileBlendToggleSupport=0
LegacySatelliteFogOfWar=1
WalkerCockpitJitterFix=0

[Shell]
AutosaveLoadButton=1
SteamMapListHopFix=1
MissionBriefingScrollFix=1
ModLoadingSeparation=0
NicknameUi=0
KeybindUiRewrite=0

[Multiplayer]
FreecamExploitGuard=1
AllowTgaInMpWhitelist=1
EnableBanUi=0
EnableTcpLanUi=0
EnableMpTeamFlags=0
EnableAnyNationOption=0
EnableCloakOption=0

[Experimental]
ChunkRenderBridge=0
ChunkMeshSelfTest=
CommandMenuNativeInjection=0
NeutralTargetPopupMode=0
HowitzerMinelayerWeaponMaskFix=0
CustomProducerSubmenus=0
```

---

# 9. Config Implementation Notes

## Parser choice

For a Win32 C++ shim, a simple INI parser is enough. Avoid JSON unless the repo already depends heavily on it in the runtime path.

Recommended parser behavior:

```text
- Case-insensitive section/key names
- Ignore unknown keys, but log them once in verbose mode
- Trim whitespace
- Accept 1/0, true/false, yes/no, on/off for booleans
- Clamp numbers
- Keep comments when possible only if writing later; initial implementation can be read-only
```

## Suggested internal structure

```cpp
struct OpenShimConfig {
    bool enabled = true;

    struct Network {
        bool enablePacketReorder = true;
        int reorderWindowMs = 45;
        int reorderDepth = 8;
    } network;

    struct Audio {
        int maxSoundChannels = 150;
        bool reducedUnitChatter = true;
        int unitVoThrottleMs = 750;
        int unitVoQueueDepthLimit = 4;
        int unitVoStaleMs = 3000;
    } audio;

    struct Hud {
        float radarScale = 1.0f;
        bool legacyScrapPilotLayout = true;
        TargetPopupMode targetPopupMode = TargetPopupMode::Default;
    } hud;

    struct Experimental {
        bool chunkRenderBridge = false;
        bool commandMenuNativeInjection = false;
        bool howitzerMinelayerWeaponMaskFix = false;
    } experimental;
};
```

## Startup flow

```text
DllMain
  -> proxy setup only; keep it minimal
  -> spawn patch thread

Patch thread
  -> resolve game folder
  -> load openshim.ini
  -> apply environment-variable overrides
  -> log resolved settings
  -> validate executable/version/sites
  -> install hooks according to config
  -> fail closed per hook if validation fails
```

## Later menu UI flow

When native menu toggles are added later:

```text
Menu UI reads OpenShimConfig snapshot
User changes setting
UI writes openshim.ini atomically
If runtime-safe: apply immediately
If startup-only: show/rely on "Restart required" marker
```

Atomic write pattern:

```text
write openshim.ini.tmp
flush/close
MoveFileEx(tmp, openshim.ini, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
```

---

# 10. Suggested First Config-Backed Settings

Start with settings that already exist as environment variables or have low-risk defaults:

1. Packet reorder settings
2. Buffer logging settings
3. Max sound channels
4. Constructor remote-build cleanup toggle/trace
5. Splinter undead fix toggle/trace
6. Under-attack alert throttle
7. Target popup mode, excluding NeutralOnly until validated
8. Radar scale, if Shim-side support is added
9. Legacy scrap/pilot HUD layout
10. Crash dump/logging settings

Avoid making highly experimental settings look like normal user options until validated:

- Chunk render bridge
- GPU skinning / shader rewrite experiments
- Neutral-only target popup
- Howitzer/minelayer weapon-mask fix until July 2026 crash is resolved
- Producer submenu native injection
- Mod loading separation, until safe enough for default-on testing

---

# 11. Practical Backlog for Ownership Cleanup

## Phase 1 — Config foundation

- Add `openshim.ini` loader.
- Preserve environment-variable overrides.
- Log resolved config once.
- Migrate `net.ini` reading into unified config or support both with `openshim.ini` taking precedence.
- Add `[Logging]`, `[Network]`, `[Audio]`, `[Experimental]` sections first.

## Phase 2 — Move normal-player settings into Shim

- Add Shim-owned unit VO queue policy.
- Add Shim-owned HUD/radar persistent settings.
- Add Shim-owned target popup mode.
- Add crash dump/log settings.

## Phase 3 — Bridge cleanup

- Add OpenShim exports for settings EXU needs to read/write.
- Add EXU wrappers that detect availability and fail closed.
- Avoid duplicate ownership where EXU and Shim both patch the same native behavior.

## Phase 4 — Native menu UI later

- Add a simple OpenShim settings shell page or pause/options hook.
- Write to `openshim.ini`.
- Mark startup-only settings clearly.
- Keep environment variables for testing.

---

# 12. Final Recommendation

Use OpenShim as the home for all global, stock-game, shell, multiplayer, crash, renderer, input, audio, and persistent player-setting work.

Use EXU as the home for Lua APIs, author-facing power tools, mission-specific effects, custom overlays, material/environment scripting, and runtime inspection helpers.

For shared features, use this pattern:

```text
OpenShim owns the native hook.
OpenShim exposes a tiny safe bridge.
EXU exposes the Lua API.
EXU fails closed when the bridge is missing.
```

And yes: add `openshim.ini` now. It is the right stepping stone before native UI toggles exist.
