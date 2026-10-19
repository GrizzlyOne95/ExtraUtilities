# Point lights

`exu.CreatePointLight` and friends add free-standing coloured point lights to
the world: an Ogre `Light` (type `LT_POINT`) on its own scene node under the
root scene node. The first consumer is Campaign Reimagined mission 17's
volcanic vents.

## API

| Function | Returns |
| --- | --- |
| `CreatePointLight(name, pos, colour?, range?)` | `true`, or `false` if refused |
| `SetPointLightPosition(name, pos)` | `true`/`false` |
| `SetPointLightColour(name, diffuse, specular?)` | `true`/`false` (specular defaults to diffuse) |
| `SetPointLightRange(name, range)` | `true`/`false` (resets attenuation to the default curve) |
| `SetPointLightAttenuation(name, range, constant, linear, quadratic)` | `true`/`false` |
| `SetPointLightPowerScale(name, scale)` | `true`/`false` |
| `SetPointLightVisible(name, visible)` | `true`/`false` |
| `DestroyPointLight(name)` | `true` if a light was destroyed |
| `DestroyAllPointLights()` | number destroyed |
| `HasPointLight(name)` | `bool` |
| `GetPointLightInfo(name)` | table or `nil` |
| `GetPointLightStats()` | `{count, max, sceneManager, frustumLights?, ownedInFrustum?}` |

- **`pos`** is a sim-space vector (anything that `GetPosition` returns). EXU converts it to render space.
- **`colour`** is `{r, g, b}`, with components from 0 to 100; values above 1 overbright. The default is white.
- **`range`** is in metres, at most 2000. The default is 50.
- **Default falloff** is `(range, 1, 0, 15 / range^2)`, the same curve the engine uses for its own lights. Intensity is 1/16 of full at `range`, and Ogre culls the light beyond it.
- **`SetPointLightPowerScale`** multiplies the diffuse and specular colours on the EXU side, because no Redux shader reads Ogre's `light_power`.
- **Bad arguments** raise a Lua error: a bad name, position, colour, range or scale, or attenuation terms that are all zero.
- **Runtime refusals** return `false`, with a reason written once to `exu.log`. Refusals cover:
  - no published scene manager
  - an Ogre export missing
  - a duplicate name
  - the cap is reached
  - no render origin
  - an Ogre fault

## Lifetime and limits

- **Cap:** each mission Lua state can have at most 8 lights (`GetPointLightStats().max`).
- **Mission-scoped:**
  - Every light and its scene node are destroyed when the Lua state closes.
  - At shutdown, EXU calls into Ogre only for lights it can still find by name in the same scene manager they were created in. If the scene manager has changed, the record is dropped without touching Ogre.
- **Not saved:** a mission that wants its lights after a save/load must create them again.
- **No shadows:** each light gets `setCastShadows(false)`. Redux's shadow budget is three sun cascades, and there is no slot for another caster.
- **Raw pointers:** EXU never hands Ogre pointers to Lua. Every call looks the light up again by name and checks that its pointer matches.

## Slot competition

Lights reach a material only through its per-renderable light list. Ogre fills that list per renderable, and stock Ogre sorts it nearest first. The material's pass decides how many lights it actually uses:

| Material set | Lights used per renderable |
| --- | --- |
| Stock Redux high materials | 8 (sun + 7) |
| Stock low materials | sun only |
| CR Enhanced `en-high` | 24 |
| CR Enhanced `en-medium` | 8 |
| CR Enhanced `en-low` | 1 |

On `en-*` schemes, OpenShim's EnhancedLightSelectionV2 (`ogre_enhanced_light_selection.cpp`) re-ranks candidates by influence rather than distance, and keeps the previous selection with hysteresis.

**Competitors:** EXU point lights compete with:

- the player's headlight
- weapon and explosion flashes
- engine lights from other craft
- any other scripted lights

When a renderable has more candidates than slots, the furthest or weakest lose. Memory note: a headlight was once evicted from a busy terrain patch by explosion lights.

**Recommendations for mission authors:**

- Keep ranges as small as the effect allows. Range is what puts a light into the candidate list of every terrain patch and craft within it.
- Use 2-4 vent lights per area, not 8.
- Hide lights that are off-screen or idle. A hidden light is excluded from Ogre's frustum light list, so it takes no slot. This was measured live: hidden, `ownedInFrustum = 0`; shown, `ownedInFrustum = 2`.
- On stock low materials or `en-low`, point lights do nothing: only the sun is lit.

## Live validation (2026-10-18, GOG, D3D11, CR Enhanced `en-high-noshadow`)

**Setup:**

- `lcbench.bzn`, driven by a temporary Lua driver.
- An `avtank` 60 m ahead of the player, framed with a cinematic camera.
- Sun, ambient and specular darkened through exu.
- Captured with PrintWindow, with no focus or input.

**Lights:**

- Orange `(3, 1.3, 0.35)`, range 30, 5 m above the tank.
- Blue `(0.3, 0.7, 3)`, range 22, on the ground 12 m to the side.

**Results:**

- **Off:** only the player's headlight is visible.
- **On:** an orange pool on the terrain and on the tank hull, and a separate blue pool on the slope.
- **Modified:** at runtime, the orange light changed to green at range 45, and the blue light moved to the other side with power ×2 and custom attenuation. Both changes showed up in the next capture.
- **Refusals:**
  - A duplicate name returned `false`.
  - The ninth light returned `false` ("cap of 8 lights reached").
  - Range `-1` and an empty name raised argument errors.
  - Destroying, or setting on, an unknown name returned `false`.
- **Shutdown:** the exu.log line was `[EXU::PointLights] shutdown: released 7 of 7 lights`.

**Not measured live:**

- The influence ranking of EnhancedLightSelectionV2 with EXU lights present. Its trace stores only its first 64 records, and those were all spent before the lights were shown.
- Behaviour on DX9.
- Behaviour on stock (non-CR) materials.
- Frame-time cost.
