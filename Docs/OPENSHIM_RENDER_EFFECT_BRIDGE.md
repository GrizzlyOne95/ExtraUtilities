# Optional OpenShim render-effect intent bridge

Status: design/research anchor; no runtime API implementation in this change.

Related work:

- OpenShim PR #191: https://github.com/GrizzlyOne95/Battlezone98Redux_Shim/pull/191
- EXU PR #24 (Kenshi-inspired Ogre weather): https://github.com/GrizzlyOne95/ExtraUtilities/pull/24

## Purpose

Define the **EXU side** of future renderer-level effects without making ExtraUtilities responsible for renderer internals.

The ownership rule is deliberately strict:

> EXU expresses mission-scoped intent. OpenShim decides whether a renderer feature is supported/effective and owns every D3D11/Ogre render resource required to implement it.

This keeps baseline EXU weather/particle functionality independent from OpenShim while still allowing Enhanced-only features such as soft-particle depth fading, SSAO tuning, or depth haze when OpenShim provides them.

## Why this is separate from EXU weather

PR #24 correctly keeps ordinary weather in EXU/Ogre ParticleFX:

- rain;
- dust;
- ash;
- mist particles;
- haboob/cloud walls;
- emitter/affector control;
- wind;
- fog/light interpolation;
- weather scheduling.

None of those should require OpenShim.

Renderer-dependent features are different:

- depth-aware soft-particle intersection fading;
- screen-space ambient occlusion/obscurance;
- depth-aware post haze;
- renderer-owned fog/intersection composition;
- future water/depth intersection effects.

Those require scene depth, render-target ordering, shader resources, or D3D11 state that EXU should not own.

## External reference context

The OpenShim investigation records two supplied archives by exact hash:

| Archive | SHA-256 |
| --- | --- |
| `ShaderSSAO-215-3-0-1549132333.zip` | `4cb66a9c59e4308838dae916abbcb0a9f36900e664f5b533a51d69c2c7d8dbe3` |
| `KENSHI ENHANCED 1.0-945-1-1-1777749127.zip` | `a27a3a3a61849d7d76a191c2734c9ee15d663774c93765cea8a848d1a1d00080` |

Relevant behaviors in those references include depth-derived normals, AO/SSDO, depth-bilateral filtering, haze, and UI/effect separation. EXU should **not** import or ship those third-party shaders/assets. The OpenShim PR contains the detailed licensing and clean implementation constraints.

## Existing EXU architecture to preserve

Current EXU already exposes mission-facing Ogre/environment functionality through:

- `src/Game/Environment.cpp`
- `src/Game/Environment.h`
- `src/luaexport.cpp`
- `Definitions/ExtraUtils.lua`

Particle APIs such as `HasParticleSystem`, `CreateParticleSystem`, and `SetParticleSystemPosition` are a good example of the mission-facing style: Lua requests behavior, native EXU owns the Ogre object interaction, and the editor definition file mirrors the runtime export surface.

Renderer effects should use the same **Lua ergonomics**, but the actual rendering implementation should remain behind an optional OpenShim bridge.

## Existing OpenShim bridge pattern

OpenShim already exports narrow C-callable controls/capability helpers from its `winmm.dll` proxy, including:

- `OpenShimGetActiveRendererBackend`
- `OpenShimGetRenderCapabilities`
- `OpenShimSupportsRenderProfile`
- `OpenShimSetUnderAttackAlertMode`
- `OpenShimSetTargetReticlePopupMode`

OpenShim's own research notes already identify the intended cross-module pattern as an EXU bridge to `winmm.dll` through `GetProcAddress` plus a narrow exported setter.

Use that pattern rather than linking EXU directly against OpenShim.

### Required optional-dependency behavior

EXU must continue to load and work when OpenShim is absent.

Preferred resolution pattern:

```text
EXU
  -> GetModuleHandleW("winmm.dll")
  -> GetProcAddress("OpenShim...")
  -> cache validated function pointers
```

Do not create a hard link-time dependency on OpenShim. Do not assume that a loaded `winmm.dll` is OpenShim; successful lookup of the expected OpenShim exports is the capability test.

Do not repeatedly resolve exports every frame.

## Public Lua API proposal

Names are provisional until the OpenShim ABI exists.

Prefer a small generic mission-facing API rather than one EXU function per renderer implementation detail:

```lua
local ok = exu.SetRenderEffectEnabled("ssao", true)
local ok = exu.SetRenderEffectFloat("ssao", "strength", 0.65)
local ok = exu.SetRenderEffectFloat("ssao", "radius", 1.25)

local requested, supported, effective, reason =
    exu.GetRenderEffectStatus("ssao")
```

Weather could then request an optional renderer enhancement without depending on it:

```lua
exu.SetRenderEffectEnabled("soft_particles", true)
```

or, if soft particles are better represented as a capability consumed automatically by OpenShim's particle/material path, EXU may expose only the status query and not a user toggle.

### Initial logical effect names

Do not expose an unbounded arbitrary string namespace to OpenShim. EXU should map friendly Lua names to a fixed native enum/ID set.

Candidate initial names:

```text
ssao
depth_haze
soft_particles
```

Only add names after the OpenShim implementation/capability actually exists.

### Initial parameter names

Keep parameters semantic rather than shader-specific.

For AO, likely candidates are:

```text
strength
radius
fade_start
fade_end
quality
```

Do not expose implementation details such as sample texture names, SRV slots, shader constants, kernel arrays, or compositor pass indices.

## Proposed OpenShim C ABI shape

EXU should not dictate the final OpenShim ABI, but a versioned POD/enumeration interface is preferable to passing C++ or Ogre objects across the DLL boundary.

Illustrative shape:

```cpp
enum OpenShimRenderEffectId : uint32_t
{
    OPENSHIM_RENDER_EFFECT_SSAO = 1,
    OPENSHIM_RENDER_EFFECT_DEPTH_HAZE = 2,
    OPENSHIM_RENDER_EFFECT_SOFT_PARTICLES = 3,
};

enum OpenShimRenderEffectParamId : uint32_t
{
    OPENSHIM_RENDER_PARAM_STRENGTH = 1,
    OPENSHIM_RENDER_PARAM_RADIUS = 2,
    OPENSHIM_RENDER_PARAM_FADE_START = 3,
    OPENSHIM_RENDER_PARAM_FADE_END = 4,
    OPENSHIM_RENDER_PARAM_QUALITY = 5,
};

struct OpenShimRenderEffectStatusV1
{
    uint32_t size;
    uint32_t version;
    BOOL requested;
    BOOL supported;
    BOOL effective;
    uint32_t reasonCode;
};
```

Possible exports:

```text
OpenShimSetRenderEffectEnabled(effectId, enabled)
OpenShimSetRenderEffectFloat(effectId, paramId, value)
OpenShimGetRenderEffectStatus(effectId, status, statusSize)
```

Exact names are provisional. The important constraints are:

- C ABI;
- POD only;
- explicit sizes/versions for structs;
- stable integer IDs;
- no STL across DLLs;
- no Ogre types;
- no COM/D3D11 pointers;
- no raw shader/material names;
- no borrowed renderer-owned memory with ambiguous lifetime.

A status `reasonCode` should be mapped by EXU to a stable human-readable Lua reason string where useful.

## Requested / supported / effective contract

This distinction is mandatory.

Example: mission asks for SSAO, but OpenShim is running DX9:

```text
requested = true
supported = false
effective = false
reason = "unsupported-renderer"
```

Example: DX11 is active, but scene depth failed qualification:

```text
requested = true
supported = false
effective = false
reason = "scene-depth-unavailable"
```

Example: capability exists but mission did not request it:

```text
requested = false
supported = true
effective = false
reason = "not-requested"
```

Do not overload a single boolean with all three meanings.

## Fail-open behavior

All EXU renderer-effect APIs must be optional and non-fatal.

If OpenShim is absent, too old, missing an export, on DX9, or reports the effect unsupported:

- return failure/status to Lua;
- leave baseline EXU functionality running;
- do not throw or abort mission startup;
- do not silently substitute a different visual effect;
- log once per missing/unsupported capability, not once per frame.

For weather specifically:

```text
EXU rain/dust/mist particles     -> continue
OpenShim soft-particle enhancement -> unavailable, clean fallback
```

The mission must never need to branch around OpenShim merely to get ordinary weather.

## Mission lifecycle

Render-effect requests from EXU are mission-scoped unless explicitly designed otherwise.

A mission that enables/tunes an effect must not accidentally leave that request active in the next mission.

Implementation must define reset behavior for:

- mission start;
- mission unload/cleanup;
- shell return;
- mission A -> shell -> mission B;
- script reload/restart where applicable.

Preferred rule:

> EXU clears its mission-owned renderer requests during normal mission teardown, and OpenShim also has a safe default/reset state so stale EXU state cannot survive an abnormal unload.

Do not make Campaign Reimagined responsible for cleanup correctness.

## Proposed EXU implementation locations

Once OpenShim PR #191 (or its implementation follow-up) exposes a stable ABI, likely EXU work is:

### Native bridge

Add a small dedicated bridge module rather than putting Win32 symbol lookup into `Environment.cpp`, for example:

```text
src/Integration/OpenShimBridge.h
src/Integration/OpenShimBridge.cpp
```

Responsibilities:

- resolve/cached optional OpenShim exports;
- report bridge version/capabilities;
- validate ABI/status struct versions;
- map EXU enums to OpenShim enums;
- centralize one-time warning/log behavior;
- reset mission-owned requests.

### Lua exports

Add wrappers to an appropriate EXU module and register them in:

```text
src/luaexport.cpp
```

Update editor definitions in:

```text
Definitions/ExtraUtils.lua
```

The definitions file is tooling-only; runtime registration remains authoritative.

### Tests

Keep bridge mapping/status logic testable without Battlezone or OpenShim loaded. Abstract symbol/function resolution enough that tests can inject a fake OpenShim export table.

## Suggested Lua semantics

### `exu.SetRenderEffectEnabled(name, enabled)`

- validates known effect name;
- returns `true` when OpenShim accepted the request;
- returns `false` if unavailable/unsupported/invalid;
- does not crash or create a hard dependency.

### `exu.SetRenderEffectFloat(name, parameter, value)`

- validates effect + parameter combination;
- rejects NaN/Inf;
- clamps only where the contract explicitly defines a clamp;
- otherwise rejects out-of-range values rather than silently changing mission intent;
- returns success/failure.

### `exu.GetRenderEffectStatus(name)`

Suggested multi-return form for Lua 5.1:

```lua
requested, supported, effective, reason =
    exu.GetRenderEffectStatus("ssao")
```

This avoids allocating a new table every frame for scripts that poll status. Scripts should still be encouraged not to poll every frame unless necessary.

Unknown names should return a clear failure reason such as `unknown-effect`.

## Weather integration guidance

PR #24 should remain implementation-independent from this bridge.

Recommended order:

1. finish baseline Ogre ParticleFX weather in EXU;
2. benchmark/tune it without OpenShim depth features;
3. OpenShim qualifies scene depth and an effect ABI in/after PR #191;
4. add EXU bridge;
5. opt weather materials/particles into soft-depth behavior only where it visibly helps;
6. preserve ordinary hard-intersection particle rendering as fallback.

Potential soft-particle consumers:

- mist/cloud sprites;
- dust sheets;
- haboob particles;
- smoke-like weather layers.

Rain streaks may not need soft-particle fading; qualify by effect rather than enabling it indiscriminately.

## SSAO guidance from EXU's perspective

SSAO itself belongs entirely in OpenShim.

EXU should only need to say things like:

```lua
exu.SetRenderEffectEnabled("ssao", true)
exu.SetRenderEffectFloat("ssao", "strength", 0.6)
```

EXU must not:

- capture the depth buffer;
- create AO render targets;
- reconstruct normals;
- own AO shaders;
- run fullscreen passes;
- know whether AO is half/full resolution;
- know whether OpenShim uses SSAO, GTAO-like logic, or another future implementation internally.

This abstraction lets OpenShim replace the implementation later without breaking mission Lua.

## Campaign Reimagined ownership

CR may eventually own artistic mission/planet defaults, for example:

```lua
-- illustrative only
if planet == "Mars" then
    exu.SetRenderEffectFloat("ssao", "strength", 0.55)
end
```

But CR should not be required for generic EXU/OpenShim integration and should not carry renderer implementation code.

Do not open a CR implementation dependency until the generic bridge exists and there is a real tuning need.

## Diagnostics

EXU should log the bridge at mission startup only when used or diagnostic mode is enabled.

Useful examples:

```text
[EXU/OpenShim] bridge present abi=1 backend=dx11
[EXU/OpenShim] ssao requested=1 supported=1 effective=1
```

Fallback:

```text
[EXU/OpenShim] soft_particles requested=1 supported=0 effective=0 reason=scene-depth-unavailable
```

Avoid per-frame log spam.

## Validation matrix

### OpenShim presence

- current OpenShim with full render-effect ABI;
- older OpenShim with no new ABI;
- no OpenShim / system `winmm.dll` only;
- malformed/partial export set handled safely.

### Renderer/profile

- DX11 Enhanced;
- DX11 Redux profile;
- Retro where applicable;
- DX9;
- unsupported feature status.

### Mission lifecycle

- single mission start/exit;
- mission A -> shell -> mission B;
- repeated restart;
- EXU script error/early mission exit;
- OpenShim effect unavailable mid-development build.

### Lua validation

- known/unknown effect names;
- known/unknown parameter names;
- invalid types;
- NaN/Inf;
- boundary values;
- status reason mapping;
- calls before renderer capability is ready.

### Weather regression

With OpenShim absent or soft particles unsupported:

- rain still renders;
- dust still renders;
- mist still renders;
- weather scheduling still runs;
- fog/light interpolation still runs;
- no mission script changes are required for fallback.

## Implementation checklist

- [ ] Wait for OpenShim PR #191 investigation to establish a useful stable capability.
- [ ] Agree on versioned C ABI and effect/parameter/status IDs in OpenShim.
- [ ] Add `OpenShimBridge` optional symbol resolver in EXU.
- [ ] Unit-test bridge resolution and status mapping with fake exports.
- [ ] Add Lua wrappers and runtime registration.
- [ ] Add `Definitions/ExtraUtils.lua` annotations.
- [ ] Add mission teardown/reset behavior.
- [ ] Add once-only bridge/capability diagnostics.
- [ ] Validate old/no OpenShim fallback.
- [ ] Integrate weather soft particles only after baseline PR #24 work is functional.
- [ ] Add CR tuning only after the generic path is stable.

## Non-goals

- no OpenShim hard dependency;
- no D3D11/Ogre render-resource ownership in EXU;
- no SSAO implementation in EXU;
- no third-party shader import;
- no ReShade dependency;
- no requirement that baseline weather use OpenShim;
- no public Lua exposure of renderer internals;
- no CR dependency for generic capability.
