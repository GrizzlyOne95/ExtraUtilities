# EXU Animation API

`exu.animation` is the high-level animation-control layer for Ogre animation states exposed through Extra Utilities.

It deliberately sits above the existing low-level functions (`HasEntityAnimation`, `GetEntityAnimationInfo`, `SetEntityAnimationEnabled`, `SetEntityAnimationLoop`, `SetEntityAnimationWeight`, and `SetEntityAnimationTime`) rather than replacing them.

## Design rules

- EXU never stores `Ogre::Entity*` or `Ogre::AnimationState*` between calls.
- Every operation reuses the existing SEH-guarded GameObject/Ogre resolver.
- No new executable patch or frame hook is installed by this API.
- Redux/Ogre remains responsible for animation evaluation and time advancement.
- Unsupported or temporarily unavailable targets fail closed.
- `TargetLocalFirstPerson()` resolves the qualified OpenShim `aspilo_fp` target afresh for every operation; neither EXU nor Lua caches its Ogre pointer.

## Basic use

```lua
local player = GetPlayerHandle()

if exu.animation.Has(player, "stand2Kneel") then
    exu.animation.Play(player, "stand2Kneel", {
        restart = true,
        loop = false,
        weight = 1.0,
    })
end
```

Read live state:

```lua
local info = exu.animation.GetInfo(player, "stand2Kneel")
if info then
    print(info.timePosition, info.length, info.normalizedTime, info.atEnd)
end
```

Stop or seek:

```lua
exu.animation.Stop(player, "stand2Kneel", true)
exu.animation.Seek(player, "idle", 0.5)
```

## Explicit targets

A raw BZR handle is shorthand for a normal GameObject animation target:

```lua
local target = exu.animation.Target(GetPlayerHandle())
exu.animation.Play(target, "idle", { loop = true })
```

The descriptor currently has this shape:

```lua
{
    kind = "gameObject",
    handle = GetPlayerHandle(),
}
```

The dedicated local first-person target is selected without a BZR handle:

```lua
local fp = exu.animation.TargetLocalFirstPerson()
if exu.animation.Has(fp, "stand2Kneel") then
    exu.animation.Play(fp, "stand2Kneel", {
        restart = true,
        loop = false,
        weight = 1.0,
    })
end
```

OpenShim revalidates the local world `Person`, SceneManager membership, the strict `_fp` pilot mesh family, skeleton, and stock pilot animation vocabulary before returning the entity. Entering a vehicle, destruction, respawn, mission changes, and entity recreation advance the tracker generation. If no qualified FP entity exists, operations return `false` (`GetInfo` returns `nil`) without falling back to the world entity.

## Capability probe

```lua
local caps = exu.animation.GetCapabilities()
```

Current expected values:

```lua
caps.gameObjectTarget == true
caps.localFirstPersonTarget == true -- when the required OpenShim export is installed
caps.managedClock == false
caps.nativeAdvancement == "unvalidated"
```

`nativeAdvancement` remains `unvalidated` until the stock `Play`/`Stop`/`Seek` runtime matrix is captured. Target qualification proves that the FP entity is independently controllable, but does not by itself prove every public operation's playback semantics.

## Stock first-person runtime qualification (2026-08-28)

The public path is now qualified on GOG Redux 2.2.301 with matching isolated Release builds of OpenShim and EXU. A Lua-only `lcbench` capture proved:

- Before `HopOut`, `Has` and `Play` return `false` and `GetInfo` returns `nil` without a crash or WORLD fallback.
- `TargetLocalFirstPerson()` exposes the stock `idle`, `stand2Kneel`, `kneel2stand`, `fireRecoilSniper`, `jump`, `runForward`, and `landParachute` states on the promoted `aspilo_fp.mesh` entity.
- FP-only `Play` plus `Seek` changed FP while the WORLD state remained disabled at time zero; FP-only `Stop(reset=true)` reset FP without changing WORLD.
- The reciprocal WORLD-only test changed WORLD while FP remained disabled at time zero.
- Stock gameplay reclaimed both entities after the test rather than leaving an override behind.
- Same-process mission replay released generation 1, reacquired a different entity at generation 3, released it at generation 4, and reacquired another at generation 5. No stale pointer was retained or manipulated.
- A synchronized first-person capture showed the FP half-kneel pose; Shift+F3 during the same FP-only hold showed the external WORLD pilot still standing.

This proves stock `Play`, `Stop`, and `Seek` through the complete Lua → EXU → OpenShim tracker → Ogre `AnimationState` path. It does not prove autonomous native advancement of an externally selected clip, so `managedClock` remains `false` and `nativeAdvancement` remains `"unvalidated"`.

## Why there is no speed control yet

Speed control would require either a proven native Ogre/Redux time-scale mechanism for the target or an EXU-owned animation clock. Adding a second clock before confirming how Redux advances the state risks double-advancement and frame-order bugs. The API therefore exposes only operations whose semantics are already grounded in the existing Ogre `AnimationState` bridge.

Once the live ownership/advancement experiment is complete, speed and first-person helpers can be added behind the same API without breaking existing scripts.
