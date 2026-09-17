# Kenshi / Particle Universe weather reference for BZR

Date: 2026-09-16

Status: implementation plan / research reference. This document does not import or redistribute third-party assets.

## Goal

Use the supplied Kenshi **Particle System Override (PSO) 3.4** package as an engineering reference for richer Battlezone 98 Redux weather while keeping BZR implementation native to Ogre 1.10 / `Plugin_ParticleFX` and keeping repository ownership boundaries intact.

The immediate objective is a reusable EXU particle-control layer capable of reproducing the important behaviours behind:

- camera-local rain volumes;
- layered dust / ash / mist;
- haboob / sandstorm walls;
- pulsing compound storm systems;
- wind-driven particle motion;
- colour and scale changes over particle lifetime.

Do **not** attempt to load Kenshi `.pu` files in BZR. Kenshi uses the Particle Universe plugin; Redux ships Ogre 1.10 `Plugin_ParticleFX` instead. Treat the PSO files as behaviour references and translate them to native Ogre `.particle` systems plus EXU runtime controls.

## Reference package provenance

Source mod:

- Particle System Override (PSO), Nexus Mods Kenshi mod 950
- https://www.nexusmods.com/kenshi/mods/950

Inspected archive:

```text
Particle system override (installer)-950-3-4-1701958188.zip
SHA-256: 737b34180bcc2d19c3c8399bbdba3523d1e212377df4a06a940d0aa4b1cbe92b
```

Archive inventory under `PSO/Resources/Particlefix/`:

- `scripts/`: 93 `.pu` files plus one extensionless script;
- `materials/`: 96 `.material` files plus one extensionless material file;
- `Textures/`: 92 `.dds` textures.

The archive contains `PSO/Resources/license.txt`, identifying the work as **CC BY-SA 4.0**, credited to SCARaw. Even though adaptation is permitted under that license, this plan deliberately avoids importing the original textures/scripts into EXU. Author BZR-native assets unless a separate decision is made to ship CC BY-SA-derived content with the required attribution and ShareAlike obligations.

## Existing BZR / EXU facts that matter

1. Redux uses Ogre 1.10 and loads `Plugin_ParticleFX`.
2. EXU already resolves Ogre APIs dynamically from `OgreMain.dll` and exposes runtime particle controls in `src/Game/Environment.cpp`.
3. EXU already exposes particle-system creation/destruction, camera/object/bone attachment, visibility/emission, quota/default dimensions/non-visible timeout, plus emitter emission rate, direction, position, velocity, angle, TTL and colour range.
4. EXU carries the BZR-matched Ogre 1.10 header subset under `third_party/ogre-1.10.0-bzr/include/`.
5. `Ogre::ParticleEmitter` and `Ogre::ParticleAffector` derive from `Ogre::StringInterface`. `StringInterface::setParameter(name, value)` is therefore the preferred generic bridge for type-specific ParticleFX properties.

Relevant EXU files:

- `src/Game/Environment.cpp`
- `src/Game/Environment.h`
- `src/luaexport.cpp`
- `Definitions/ExtraUtils.lua`
- `third_party/ogre-1.10.0-bzr/include/OgreMain/OgreParticleEmitter.h`
- `third_party/ogre-1.10.0-bzr/include/OgreMain/OgreParticleAffector.h`
- `third_party/ogre-1.10.0-bzr/include/OgreMain/OgreStringInterface.h`

Official Ogre references:

- Ogre 1.10 API: https://ogrecave.github.io/ogre/api/1.10/
- Particle scripts / standard ParticleFX emitters and affectors: https://ogrecave.github.io/ogre/api/1.11/_particle-_scripts.html
- `StringInterface::setParameter`: https://ogrecave.github.io/ogre/api/1.10/class_ogre_1_1_string_interface.html

## Ownership decision: EXU vs OpenShim

### EXU owns the weather particle runtime

All of the following belong in EXU:

- creating and destroying Ogre particle systems;
- attaching weather volumes to the active camera;
- selecting / mutating particle materials;
- emitter geometry and emitter parameter control;
- affector inspection / parameter control;
- weather intensity modulation;
- fog / ambient / sun controls already exposed by EXU;
- a reusable Lua weather controller or lower-level APIs consumed by such a controller;
- mission-scoped weather state.

This follows `AGENTS.md` and `ARCHITECTURE.md`: EXU owns reusable Lua/native runtime features and safe mission-scoped controls.

### OpenShim is not required for baseline weather

Do **not** add baseline rain, dust, ash, mist, wind, haboob, or storm scheduling to OpenShim.

OpenShim should only become involved for renderer-level features that cannot be expressed through normal Ogre scene / particle APIs. The main known example is **depth-aware soft particles**.

Kenshi's `Kenshi_Mist_Cloud.material` samples a compositor-provided depth texture (`global_gbuffer`) through a depth-aware particle shader. BZR does not provide an equivalent ready-to-use scene-depth texture. OpenShim's `Docs/FOG_COMPOSITOR_QUALIFICATION_20260908.md` established that, on the shipped DX11 path with MSAA enabled, scene depth is not available as a shader-resource view and render ordering around terrain/cockpit/HUD is non-trivial.

Therefore:

- Phase 1 weather must work without soft particles and without an OpenShim dependency beyond the existing EXU/OpenShim renderer bridge.
- If soft intersection fading is pursued later, treat it as an **OpenShim Enhanced/DX11 renderer feature** and expose only a capability/query path back to EXU.
- DX9 must continue to receive ordinary alpha-blended particles without depth fading.
- Do not make basic weather contingent on Enhanced rendering.
- Any future screen-space volumetric storm pass, depth pre-pass, or compositor-level atmospheric scattering also belongs in OpenShim rather than EXU.

## What the Kenshi systems teach us

The useful lesson is not Particle Universe syntax. It is the construction of the effects.

### 1. Heavy rain: dense, local, short-lived streaks

Reference: `PSO/Resources/Particlefix/scripts/Kenshi_Heavy_Rain.pu`

| Property | Value / behaviour |
|---|---|
| visual particle quota | 5000 |
| material | `Kenshi_rain_basic` |
| default billboard size | `0.85 x 4` |
| billboard type | `oriented_self` |
| billboard origin | `bottom_center` |
| emitter | Box |
| emission rate | 6400 particles/sec |
| TTL | random 0.3-1.0 sec |
| velocity | random 90-182 |
| position | `0 15 0` |
| direction | `0.2 -1 0` |
| box width / height | 120 / 120 |

Interpretation for BZR: precipitation should be a **camera-local volume**, not a map-scale simulation. High emission works because TTL is very short.

The reference also uses an `OnPosition` observer to expire drops below a height threshold. Particle Universe observers do not have a direct Ogre ParticleFX equivalent; treat splash placement as a later feature.

### 2. Basic rain: thin streaks plus ground-trigger idea

Reference: `PSO/Resources/Particlefix/scripts/Kenshi_Rain_Basic01.pu`

Important values:

- quota 3000;
- emission 8000/sec;
- size `0.25 x 2`;
- velocity 60-82;
- direction `0.2 -1 0`;
- Box `100 x 20 x 100`;
- `oriented_self`, `bottom_center`;
- position observer at `Y < -5.85` that emits `Up-splash` and expires the source particle.

Do not copy the fixed Y-plane literally. A BZR splash implementation would need terrain-height sampling / collision-aware placement, not a hard-coded plane.

### 3. Dust: low-speed sprites that expand and fade

Reference: `PSO/Resources/Particlefix/scripts/kenshi_weather_dust1.pu`

The system uses two Box emitters, each at 150 particles/sec, over a very large horizontal region. Initial dimensions are 30-75 and 75-100, TTL is 3-5 sec, velocity only 1-5. The visual bulk comes from:

- a colour-over-lifetime fade from transparent -> visible -> transparent;
- a scale curve from approximately `0.56` to `61.48`;
- random initial texture rotation;
- a linear force vector (`0 10 0`).

This is the primary model for efficient BZR dust: **few soft particles that grow large**, not thousands of tiny grains.

### 4. Haboob: visible storm wall

Reference: `PSO/Resources/Particlefix/scripts/Haboob_cloud.pu`

Important construction:

- very large default billboards (`400 x 200`);
- one Circle emitter, radius 350, emission 111/sec;
- particle dimensions randomized 120-292;
- colour curve: transparent -> dense tan/grey -> transparent;
- strong scale growth;
- second `SphereSurface` layer, emission 30/sec, radius 170.

Particle Universe's `Circle` and `SphereSurface` do not map by name to Ogre ParticleFX. Approximate them with stock emitters:

- `Circle` -> `Ring` with inner factors chosen to concentrate emission near the ring;
- `SphereSurface` -> `HollowEllipsoid` with inner factors near 1.0.

The resulting BZR effect should be a *world-space* storm-wall system, not camera-attached precipitation. The camera-local dust layer can ramp up as the player approaches / enters it.

### 5. Compound desert storm: multiple coordinated techniques

Reference: `PSO/Resources/Particlefix/scripts/DesertCloudStorm.pu`

The system contains multiple techniques (including `Dust-Pump_mid_ring`, centre, outer ring), long TTLs, random velocity/duration, rotation, scale/colour changes, vortex motion, and oscillating emission. One observed emitter uses sinusoidal emission approximately:

```text
base      20
amplitude 20
frequency 0.4
type      sine
```

BZR does not need a custom Particle Universe dynamic-value implementation for this. The weather controller can update `SetParticleEmitterEmissionRate` over time using a sine function. Keep such modulation in reusable EXU/native or Lua feature logic, not in OpenShim.

### 6. Mist: soft depth interaction is the optional renderer enhancement

References:

- `PSO/Resources/Particlefix/scripts/Kenshi_Mist_Cloud.pu`
- `PSO/Resources/Particlefix/materials/Kenshi_Mist_Cloud.material`

The particle system is low quota (100) with 5-10 second TTL, moderate velocity and strong scale / colour changes. The material uses a depth-aware particle fragment program and samples a compositor depth texture.

This is the one major reference feature that is **not baseline EXU work**. Reproduce the volume/alpha behaviour first. Soft depth intersection is an optional later OpenShim Enhanced feature.

## Particle Universe -> Ogre ParticleFX translation matrix

| Kenshi / Particle Universe feature | BZR/Ogre ParticleFX approach | Owner | Notes |
|---|---|---|---|
| Box emitter | `Box` emitter | EXU/content | Direct conceptual match; ParticleFX params are type-specific |
| Circle emitter | `Ring` emitter | EXU/content | Approximation; concentrate emission near ring edge |
| SphereSurface emitter | `HollowEllipsoid` | EXU/content | Approximation; inner factors near 1 create shell-like emission |
| Point emitter | `Point` | EXU/content | Direct |
| emission rate | emitter base property | EXU | Already exposed as typed API |
| TTL / velocity / direction / angle | emitter base properties | EXU | Already exposed as typed APIs |
| Box/ring/etc dimensions | emitter type-specific `StringInterface` params | EXU | Add generic emitter parameter setter |
| LinearForce | `LinearForce` affector | EXU/content | Direct stock ParticleFX equivalent |
| Colour-over-life | `ColourInterpolator` affector | EXU/content | Stock ParticleFX supports staged interpolation |
| simple scale-over-life | `Scaler` affector | EXU/content | Stock scaler is simpler than PU curves |
| texture rotation | `Rotator` affector | EXU/content | Close stock equivalent |
| random motion | `DirectionRandomiser` | EXU/content | Direct stock equivalent |
| texture animation | `TextureAnimator` where registered | EXU/content | Verify BZR's exact factory at runtime |
| Vortex | no direct standard equivalent | EXU future | Optional custom native affector or approximation |
| arbitrary spline scale curve | no exact standard equivalent | EXU future | Approximate with Scaler or add a custom affector later |
| dyn_oscillate emission | controller changes emission rate over time | EXU/Lua | No new Ogre feature required |
| OnPosition observer / child effect | no direct standard equivalent | EXU future | Requires explicit observer/collision/height logic |
| depth-aware soft particle | custom shader + readable scene depth | OpenShim Enhanced | Not baseline weather; DX11-only candidate |
| compositor interior mask | no baseline equivalent | OpenShim/content-specific | Ignore for initial prototype |

## Required EXU API work

Implement this in the existing particle runtime rather than creating a parallel weather-specific native path.

### Phase A: generic emitter parameter bridge

Add:

```lua
---@return string|nil
exu.GetParticleEmitterType(name, emitterIndex)

---@param name string
---@param emitterIndex integer
---@param parameter string
---@param value string|number|boolean
---@return boolean
exu.SetParticleEmitterParameter(name, emitterIndex, parameter, value)

---@param name string
---@param emitterIndex integer
---@param parameter string
---@return string|nil
exu.GetParticleEmitterParameter(name, emitterIndex, parameter)
```

Implementation guidance:

1. Reuse existing particle-system lookup and emitter-index validation helpers in `Environment.cpp`.
2. Resolve the emitter and call Ogre `StringInterface::setParameter()` / `getParameter()` rather than down-casting to `BoxEmitter`, `RingEmitter`, etc.
3. Keep the existing typed wrappers (`SetParticleEmitterVelocity`, etc.). The generic function is an escape hatch for type-specific properties, not a replacement for the typed API.
4. Convert Lua number/bool to Ogre-compatible text before the call; pass strings unchanged.
5. Cap parameter/value string lengths to a conservative bound before crossing the Ogre ABI.
6. Wrap resolved ABI calls in the same SEH/fail-closed pattern already used by particle functions.
7. Log one concise diagnostic on unresolved Ogre exports or rejected parameters; do not spam every frame.
8. A failed/unknown parameter must return `false`, not crash or silently succeed.

Target use (verify the exact registered Box parameter names at runtime before documenting them as stable):

```lua
exu.SetParticleEmitterParameter("weather_rain", 0, "width", 120)
exu.SetParticleEmitterParameter("weather_rain", 0, "height", 120)
exu.SetParticleEmitterParameter("weather_rain", 0, "depth", 120)
```

### Phase B: affector access and generic parameter bridge

Add:

```lua
---@return integer|nil
exu.GetParticleSystemAffectorCount(name)

---@return string|nil
exu.GetParticleAffectorType(name, affectorIndex)

---@param name string
---@param affectorIndex integer
---@param parameter string
---@param value string|number|boolean
---@return boolean
exu.SetParticleAffectorParameter(name, affectorIndex, parameter, value)

---@param name string
---@param affectorIndex integer
---@param parameter string
---@return string|nil
exu.GetParticleAffectorParameter(name, affectorIndex, parameter)
```

Use Ogre `ParticleSystem::getNumAffectors()` and `getAffector(index)`. Particle affectors inherit `StringInterface`, so use the same generic parameter path as emitters.

Primary test cases:

```lua
-- LinearForce
exu.SetParticleAffectorParameter("weather_dust", 0, "force_vector", "20 0 -5")
exu.SetParticleAffectorParameter("weather_dust", 0, "force_application", "add")

-- ColourInterpolator
exu.SetParticleAffectorParameter("weather_dust", 1, "time0", 0.0)
exu.SetParticleAffectorParameter("weather_dust", 1, "colour0", "1 1 1 0")
exu.SetParticleAffectorParameter("weather_dust", 1, "time1", 0.45)
exu.SetParticleAffectorParameter("weather_dust", 1, "colour1", "1 1 1 0.75")
exu.SetParticleAffectorParameter("weather_dust", 1, "time2", 1.0)
exu.SetParticleAffectorParameter("weather_dust", 1, "colour2", "1 1 1 0")
```

Verify actual parameter names against BZR's loaded `Plugin_ParticleFX` before freezing examples in the public Lua reference.

### Phase C: authored BZR `.particle` prototypes

Author **new** BZR-native templates; do not convert or commit PSO textures/scripts verbatim.

Minimum prototypes:

1. `EXU/WeatherRainLight`
2. `EXU/WeatherRainHeavy`
3. `EXU/WeatherDustLocal`
4. `EXU/WeatherHaboobWall`
5. `EXU/WeatherMist`

Prototype rules:

- rain: Box emitter, camera-attached, world-aligned node, short TTL;
- dust: low/moderate emission, large billboards, `ColourInterpolator` + `Scaler` + `LinearForce`;
- haboob: world-space `Ring`/`HollowEllipsoid` layers with large transparent billboards;
- mist: low quota, long TTL, large alpha-blended cards; no depth sampling in baseline implementation;
- use original BZR-authored textures/materials or obvious temporary test assets that are licensed for this repository.

### Phase D: weather controller

Keep the first controller simple and deterministic enough to test. It can live in Lua initially.

Responsibilities:

- maintain current state and target state;
- interpolate intensity over transition time;
- map intensity to emission rate, fog, sun/ambient light, and wind force;
- slowly change wind target rather than snapping direction;
- optionally pulse emission with deterministic sine/noise;
- call `exu.UpdateParticleFollowers()` once per frame for camera followers that could not bind to a real camera SceneNode;
- stop or reduce expensive off-screen systems using the existing non-visible timeout API.

Do not put random weather scheduling into OpenShim.

## Renderer / material guidance

Kenshi materials in the archive commonly use:

```text
lighting off
scene_blend alpha_blend (or add)
depth_write off
```

Those settings are transferable as concepts, but Kenshi shader names and compositor bindings are not:

- `Basic_Coloured_Ambient_VP`
- `Basic_Texture_Clipped_FP_HLSL`
- `Particle_Blend_Depth`
- `rt_interiormask`
- `global_gbuffer`

BZR materials must use BZR/OpenShim-compatible programs and schemes. Do not reference Kenshi program names.

For Enhanced materials, follow the existing OpenShim material-scheme/resource bootstrap rather than inventing a second renderer-selection mechanism.

## OpenShim follow-up (optional, not required for baseline)

Only open a corresponding OpenShim implementation issue/PR if baseline EXU weather demonstrates a concrete rendering limitation.

The likely future feature is **Enhanced DX11 soft particles / depth fade**.

Requirements before implementation:

1. Reconcile with OpenShim `Docs/FOG_COMPOSITOR_QUALIFICATION_20260908.md`.
2. Do not assume the current MSAA depth buffer is shader-readable; it is not under the qualified FSAA=8 configuration.
3. Decide whether to obtain depth through an OpenShim-owned pre-pass / alternate resource rather than fighting Ogre's writable DSV.
4. Expose a capability bit through the existing render-profile bridge.
5. Particle materials must fall back cleanly to ordinary alpha blending when unsupported.
6. DX9 remains fallback-only.

OpenShim should **not** own weather state, emitter creation, wind, timing, or mission-facing particle controls.

## Validation plan

Use a dedicated `lcbench`/sandbox mission so particle behaviour can be reproduced without campaign scripting noise.

### Native API tests

For each new generic setter/getter:

- valid system + valid index + valid parameter;
- valid system + out-of-range index;
- missing system;
- unknown parameter;
- wrong value format;
- destroyed system followed by attempted access;
- scene/mission reload;
- satellite/sniper camera transitions;
- resolution / renderer reset where applicable.

No case may crash.

### Visual tests

#### Heavy rain

- camera attachment stays centred while looking around;
- rain remains world-oriented rather than rotating with camera pitch/yaw;
- 3000-5000 quota range is stable;
- emission/TTL can be reduced to find the lowest visually acceptable cost;
- sniper/satellite views do not leak or duplicate systems.

#### Dust

- verify `LinearForce` can be changed live;
- verify colour interpolation and scaler work through authored ParticleFX affectors;
- verify no hard material clipping regression under DX9/DX11;
- tune large-card overdraw before increasing quota.

#### Haboob

- world-space wall remains stationary/moving in world coordinates rather than following camera;
- local camera dust can crossfade based on distance to the wall;
- no need to fill the complete map with particles.

### Performance telemetry

Record at minimum:

- active particle quota;
- emitter rates;
- number of weather systems;
- CPU frame time before/after;
- GPU frame time if available;
- DX9 and DX11 separately.

Prefer reducing overdraw / lifetime / volume before raising quotas.

## Acceptance criteria for baseline weather support

The feature is ready for higher-level weather authoring when all are true:

1. EXU can mutate type-specific emitter parameters through Ogre's `StringInterface` safely.
2. EXU can enumerate existing affectors and mutate their parameters safely.
3. A camera-local heavy-rain template can be created, attached, wind-steered, intensity-scaled, and destroyed without leaks/crashes.
4. A dust template demonstrates `LinearForce`, colour-over-life, scale-over-life and runtime wind changes.
5. A world-space haboob prototype demonstrates a large moving storm wall without map-scale particle simulation.
6. All baseline effects work on normal Redux rendering without requiring an OpenShim Enhanced feature.
7. Unsupported parameters / missing exports fail closed.
8. Lua definitions/documentation are updated alongside native exports.
9. Windows build and existing Linux host-side tests pass.
10. No PSO/Kenshi third-party asset is committed as part of the implementation unless its licensing/attribution is handled explicitly.

## Non-goals for the first implementation

Do not block baseline weather on:

- Particle Universe compatibility or `.pu` parsing;
- Vortex emulation;
- arbitrary spline-value engines;
- terrain collision / per-drop splash callbacks;
- soft particles;
- volumetric clouds;
- screen-space atmospheric scattering;
- gameplay effects such as traction, damage, or sensor reduction;
- global random weather scheduling.

Those are follow-on features after the native ParticleFX bridge is proven.
