# EXU-driven native HUD placement

Date: 2026-09-30. EXU baseline: `68991210d613d76e20f5fc6bd1531c9012dfbcc2`.
OpenShim baseline: `fd328e0087e3ca90900024aea2719f88787d46ce`.

## Implementation follow-up

The subsequent controls checkpoint is documented in
[`../NATIVE_HUD_LAYOUT_API.md`](../NATIVE_HUD_LAYOUT_API.md): EXU Lua/C++ controls,
mission ownership/reset, an authored-slot/live-value helper, and OpenShim's
geometry/provider contract are now implemented and host-tested. The native
render interception is still absent and unqualified; capabilities remain zero
and the API does not yet move live bars. The research evidence below retains
its original static-only qualification status.

## Result and confidence

Native hull/ammo relocation is feasible. Keep the mod-facing layout API in EXU
and the engine render interception in OpenShim. The released GOG corpus contains
separate screen-space meter bounds, label draws, smoothed fill ratios, an ammo
cost marker, and a shots-remaining readout. Reposition those existing draws so
the engine continues to drive the animation, colors, textures and values.

**High confidence in the static render-path identification and ownership design;
medium confidence in an implementation before an in-game capture.** No new
production Lua API or native layout hook is implemented by this investigation.
The included instrument is a qualification prototype, tested against host
fixtures but not attached to Redux here. This environment is Linux and cannot
perform the required native Windows game/harness qualification.

## Existing work to reuse

| Existing surface | Ownership and use |
| --- | --- |
| Scrap/pilot positions and colors | EXU `src/UI/ControlPanel.cpp` delegates to OpenShim when available. OpenShim `src/patches/scrap_pilot_hud.cpp` owns persistent stock/legacy policy and draw-time alignment. Follow that bridge and reset pattern. |
| `GetHudSpriteRect`, `SetHudSpriteRect`, `SetHudSpriteVisible`, restores | EXU delegates to OpenShim `src/patches/hud_sprite_rects.cpp`. These operate on sprite-table records and atlas dimensions/UVs; they are not a semantic screen-space layout API. Preserve their existing behavior. |
| Radar state and size | EXU `src/UI/Radar.cpp` delegates size policy to OpenShim and stands down its overlapping layout wrapper. Keep one native owner. |
| Custom numeric text | Stock `GetCurHealth`, `GetMaxHealth`, `GetCurAmmo`, `GetMaxAmmo` plus EXU's existing overlay TextArea functions already provide live custom readouts. No new gameplay-memory reader is needed for these values. |

The current sprite record is `0x24` bytes at `0x025F8F40 + id*0x24`.
The older March research note contains superseded strides/addresses; the current
implementation is authoritative. Screen coordinates are arguments to the native
sprite submitter, and live meter clipping uses the graphic buffer pane. Do not
reinterpret an atlas record's `x/y` as a meter's screen position.

For custom numbers, a mod can already update its own named TextArea elements
after creating them with EXU's overlay API. Reacquire the local controlled
object so ejection, vehicle changes and respawn do not retain an old handle:

```lua
local function UpdateCockpitValues()
    local h = GetPlayerHandle()
    if h == nil or not IsValid(h) then return end
    exu.SetOverlayCaption("cockpit_hull_value",
        string.format("%.0f / %.0f", GetCurHealth(h), GetMaxHealth(h)))
    exu.SetOverlayCaption("cockpit_ammo_value",
        string.format("%.0f / %.0f", GetCurAmmo(h), GetMaxAmmo(h)))
end
```

These are live simulation values. The relocatable native bars would continue
using their own smoothed display values. Custom overlay visibility must also
follow the mod's existing gameplay, pause and camera-mode rules.

## Released GOG static trace

Evidence baseline: the private source repository's GOG corpus manifest and
binary checksum list both identify SHA-256
`8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
The corpus explicitly says the advisory Redux PDB does not match. The following
addresses are static candidates from released-build decompilation, not leaked
PDB RVAs, not live-byte captures, and not Steam qualification.

| Candidate VA | Observed role |
| --- | --- |
| `0x005DBD70` | Status-display initialization: resolves `hulltext`/`shulltext`, `ammotext`/`sammotext`, `hull_bar`, `ammo_bar`, and weapon underlays. |
| `0x005DBEA0` | Updates two smoothed display ratios using the current user object. Decompiled calling conventions/types need disassembly confirmation. |
| `0x005DC300` | Status render: chooses faction label variants, calculates screen bounds, draws weapons, labels and clipped live bars, restores the original pane, draws shots remaining and the ammo cost marker. |
| `0x0068CA30` | Sprite submission with destination coordinates/dimensions and flags. Bit `0x200000` adds the graphic buffer's pane origin and clips the quad/UVs. |
| `0x0068AF70` | Filled-rectangle wrapper used for the three-line ammo cost marker. |
| `0x00689D10` | Native font draw used for weapon names and shots remaining. |
| `0x004C0000` | Horizontal filled gauge helper. Its body matches the legacy helper semantics, but this is not the player hull/ammo vertical-bar path. Keep command/selection gauges a separate scope. |

Observed object fields within the live status-render scope: hull bar ID at
`+0x14`, displayed hull ratio at `+0x18`, ammo bar ID at `+0x20`, displayed ammo
ratio at `+0x24`; label IDs at `+0x10` and `+0x1C`. Do not retain this object
pointer between frames, missions, or Lua states.

Screen placement is calculated every render from viewport dimensions, the live
UI scale, and design coordinates. The common meter anchor uses `0x008EA360` /
`0x008EA364` and anchor multipliers `0x008EA368` / `0x008EA36C`. Four hull edge
design values are read at `0x008EA378..0x008EA384`; four ammo edge values at
`0x008EA388..0x008EA394`. These are candidate inputs for qualification, not an
approved block of globals for a Lua script to overwrite.

Each bar advances the pane's top by the missing fill height, then submits its
sprite with `y = -missing`. The pane-relative submit puts the complete textured
bar at its full origin; clipping reveals the engine's current fill. The hull
uses `0x200005` and ammo uses `0x200001`. The original pane is restored after
each draw. Mutating just the sprite destination, just the clip, or just the atlas
dimensions will break this relationship.

The ammo companion draws are significant: three white fill rows mark the cost
of the selected weapons' next shot, and native text prints current ammo divided
by their combined ammo cost. Preserve this calculation and its placement. A
stock raw ammo fraction and the smoothed displayed ammo ratio are different
values and should be named explicitly if both are exposed.

## Proposed public contract — not yet exported

Start with two semantic IDs, `"hull"` and `"ammo"`. Each means the full meter
rectangle plus its native label and companions; it does not identify an atlas
sprite or every HUD draw sharing that sprite.

| Proposed EXU operation | Contract |
| --- | --- |
| `GetNativeHudMeterRect(id)` | Returns `x, y, width, height` of the effective full meter, before fill clipping, in physical viewport pixels; `nil` if unsupported or not sampled for the current viewport/mission. |
| `SetNativeHudMeterRect(id, x, y, width, height)` | Validated finite integer pixels, positive bounded sizes. Stores mission-local layout intent; returns `false` with a reason if the provider/capability is unavailable. Applies on the next native render. |
| `SetNativeHudMeterVisible(id, visible)` | Toggles only this meter group's draw output, retaining layout intent and all native status update/warning behavior. |
| `RestoreNativeHudMeter(id)` | Removes the override and visibility mask. The engine calculates its current default again. |
| `RestoreAllNativeHudMeters()` | Removes this API's mission-owned overrides. It does not restore other callers' sprite, scrap/pilot, radar or persistent player settings. |

Unknown IDs or invalid arguments should produce argument errors. An unsupported
build, missing OpenShim provider or absent live render state should be a normal
capability/availability failure. Definitions must document these distinctions.
Allow valid setters to queue intent before the first HUD render once the native
provider is qualified; do not claim a guessed getter rectangle is available.

First version: physical-pixel rectangles and the native vertical fill direction.
A later EXU layout helper can resolve normalized or design-canvas slots against
the current viewport without adding coordinate ambiguities to the native ABI.
Arbitrary rotation, new fill direction, per-weapon HUD rows and a relocated radar
require additional qualification and are separate capability IDs.

## Implementation sequence

1. Run the included observational instrument through the existing native
   Windows `BZRHarness.ps1` workflow. Capture GOG full, partial and empty fills,
   faction variants, different aspect ratios, HUD scales, vehicle/pilot changes,
   F1/F2/F5, and mission reloads. Verify object/argument ABI in disassembly.
   Record the exact call-return RVAs for labels, fills, markers and counter.
2. Add qualified named resolves and whole-instruction guards to OpenShim
   `scripts/patches.json`. Follow `Docs/AGENT_PATCH_WORKFLOW.md`; register the
   runtime patch and all resolves. Do not scatter these candidates into EXU
   feature code or trust a PDB's register/stack locations.
3. Implement a small native meter module. Intercept only the proven player
   status-render scope and exact companion call sites. For each meter, apply an
   affine transform from the native full rectangle to the authored rectangle
   to both its submitted quad and its active clip edges. Apply the same transform
   to label/marker/readout offsets. Preserve UV clipping, colors, sprite z,
   faction choice, smoothing and font/material state. Restore temporary pane
   and other draw state on every exit. Never zero atlas sizes to hide a live
   meter, and never skip the entire status renderer: it also emits native warnings.
4. Expose the operations through OpenShim's existing SDK/export mechanism:
   `include/openshim_sdk_exports.inc`, the provider, bridge/thunks and SDK v2
   documentation as appropriate. Use a capability/version check and primitive
   or size/versioned POD records. Avoid C++ objects across the DLL boundary.
5. Add a reusable EXU native wrapper using `src/OpenShimBridge.h`, then thin Lua
   bindings, `src/luaexport.cpp` registrations, public declarations and
   `Definitions/ExtraUtils.lua`. Reset mission intent explicitly through
   `src/PublicAPI.cpp`'s Lua-state closing path and OpenShim's mission lifecycle;
   module unload alone is insufficient. Existing scrap/pilot and sprite APIs
   keep their ownership and behavior.
6. Verify the resulting bars and native companions against screenshots/video
   at multiple fills and resolutions. Then qualify settled Steam runtime bytes
   separately. Windows/Steam, Windows/GOG, Proton and Wine support must be
   reported independently. Keep unsupported configurations disabled.

Do not store or reload absolute stock rectangles as a permanent baseline. The
engine must reflow defaults after resolution/HUD-scale changes. Invalidate
sampled getters when the viewport or mission changes and refresh the native
source geometry before applying persistent intent. Screen-space presentation
stays local; do not network-replicate this layout.

## Radar and other stock HUD elements

Hull/ammo changes must leave the radar backdrop, wireframe projection center,
blips, scanning area, command buttons and selection gauges usable. The existing
radar scale feature adjusts more than a background sprite. Full radar movement
would need a coordinated native radar transform and any related interaction
coordinates; it cannot be implemented with `SetHudSpriteRect` alone.

Use the existing scrap/pilot bridge for those readouts and existing sprite
visibility operations for art-only components. A later composition helper may
combine these capabilities, but should not take ownership of the player's
persistent OpenShim preferences implicitly.

## Instrument and validation

`tools/trace_native_hud.py` attaches only to an existing PID. It checks the exact
on-disk GOG checksum, verifies x86 executable section mapping, compares 32 live
entry bytes for every target before instrumentation, and emits bounded JSONL.
It observes draws within the status renderer on the same thread, logs native
pane/quad inputs and displayed ratios, and labels reconstructed full bounds as
**inferred**. Frida temporarily instruments function entries; the tool does not
write layout data or replace draw arguments. Changed entry bytes, including a
foreign entry detour, cause refusal. Do not weaken that guard to collect a trace.

Use native Windows with Frida already available. Dot-source OpenShim's
`reverse_engineering/BZRHarness.ps1`, launch with `BZR_FORCE_WINDOWED=1` using
the harness, then pass its current PID and exact executable path:

```powershell
python tools/trace_native_hud.py --pid <harness-PID> `
  --exe "C:/Program Files (x86)/GOG Galaxy/Games/Battlezone 98 Redux/battlezone98redux.exe" `
  --output "native-hud-gog-01.jsonl" --seconds 30 --every 30
```

Exit the game using `Stop-BZRGame -Id <harness-PID>`. The instrument neither
launches nor terminates it. Raw captures are transient/private evidence; do not
commit them. To examine changing fills, capture during damage/ammo use and
repeat with a new output filename. No Steam/Proton/Wine trace profile is supplied.

Host checks, which do not qualify the game:

```sh
python tools/test_native_hud_trace.py
node tools/native_hud_trace/test_capture.js
node --check tools/native_hud_trace/capture.js
```

The Python checks cover wrong hashes/architectures, truncated images, executable
section mapping and unbacked target rejection. The JavaScript fixture checks
render/thread isolation, half/empty-fill inference, bounded string reads,
fingerprint refusal, partial-attach rollback and record limits. Neither is a
substitute for Windows ABI verification, an EXU DLL build or visual game testing.

Validation in this session: six Python qualification checks and the JavaScript
behavior fixtures passed; JavaScript syntax, Python byte-compilation and diff
whitespace checks passed. The broader Linux suite passed its catalog/hardening,
generated-asset and C++ checks, then stopped because this container has no Lua
interpreter for its existing weather-controller test. The CI workflow installs
Lua and now also declares Node for the instrument fixtures. Windows/GOG runtime
attachment, Windows/Steam, Proton and Wine remain unverified.

## Source references

- [EXU ControlPanel bridge](https://github.com/GrizzlyOne95/ExtraUtilities/blob/68991210d613d76e20f5fc6bd1531c9012dfbcc2/src/UI/ControlPanel.cpp)
- [EXU radar ownership](https://github.com/GrizzlyOne95/ExtraUtilities/blob/68991210d613d76e20f5fc6bd1531c9012dfbcc2/src/UI/Radar.cpp)
- [OpenShim native sprite records](https://github.com/GrizzlyOne95/Battlezone98Redux_Shim/blob/fd328e0087e3ca90900024aea2719f88787d46ce/src/patches/hud_sprite_rects.cpp)
- [OpenShim scrap/pilot draw integration](https://github.com/GrizzlyOne95/Battlezone98Redux_Shim/blob/fd328e0087e3ca90900024aea2719f88787d46ce/src/patches/scrap_pilot_hud.cpp)
- Private evidence, accessed through the authorized GitHub connection:
  `Battlezone_Source/BZ1/Redux/openshim_re_corpus/bzr_gog_best_effort/current_manifest.json`,
  `BZ1/Redux/bin/SHA256SUMS.txt`, and released-corpus functions listed above.
  Legacy `StatusDisplay::Init/Render` and `DrawHorizontalGauge` were semantic
  references. No decompiler output, private PDB files or binaries are included.
- [Frida instrumentation API](https://frida.re/docs/javascript-api/)
