# EXU Animation API

`exu.animation` is the high-level animation-control layer for Ogre animation states exposed through Extra Utilities.

It deliberately sits above the existing low-level functions (`HasEntityAnimation`, `GetEntityAnimationInfo`, `SetEntityAnimationEnabled`, `SetEntityAnimationLoop`, `SetEntityAnimationWeight`, and `SetEntityAnimationTime`) rather than replacing them.

## Design rules

- EXU never stores `Ogre::Entity*` or `Ogre::AnimationState*` between calls.
- Every operation reuses the existing SEH-guarded GameObject/Ogre resolver.
- No new executable patch or frame hook is installed by this API.
- Redux/Ogre remains responsible for animation evaluation and time advancement.
- Unsupported target kinds fail closed.
- The public target abstraction is intentionally broader than the first implementation so a validated first-person `aspilo_fp` resolver can be added later without changing `Play`, `Stop`, `Seek`, etc.

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

The internal target system also reserves `localFirstPerson`, but that target is intentionally unavailable until the `aspilo_fp` ownership path is confirmed in a live Redux run.

## Capability probe

```lua
local caps = exu.animation.GetCapabilities()
```

Current expected values:

```lua
caps.gameObjectTarget == true
caps.localFirstPersonTarget == false
caps.managedClock == false
caps.nativeAdvancement == "unvalidated"
```

`nativeAdvancement` remains `unvalidated` until a direct animation activation test establishes whether Redux advances an externally enabled first-person state without changing `Person::curAnim` / weapon state.

## Why there is no speed control yet

Speed control would require either a proven native Ogre/Redux time-scale mechanism for the target or an EXU-owned animation clock. Adding a second clock before confirming how Redux advances the state risks double-advancement and frame-order bugs. The API therefore exposes only operations whose semantics are already grounded in the existing Ogre `AnimationState` bridge.

Once the live ownership/advancement experiment is complete, speed and first-person helpers can be added behind the same API without breaking existing scripts.
