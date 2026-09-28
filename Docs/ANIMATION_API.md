# EXU Animation API

`exu.animation` is the high-level animation-control layer for Ogre animation states exposed through Extra Utilities.

For local pilot/viewmodel code that does not need to manage a target descriptor,
see [FPS_API.md](FPS_API.md). `exu.fps` is a thin convenience facade over this
same implementation, not a separate animation runtime.

It deliberately sits above the existing low-level functions (`HasEntityAnimation`, `GetEntityAnimationInfo`, `SetEntityAnimationEnabled`, `SetEntityAnimationLoop`, `SetEntityAnimationWeight`, and `SetEntityAnimationTime`) rather than replacing them.

## Design rules

- EXU never stores `Ogre::Entity*` or `Ogre::AnimationState*` between calls.
- Every operation reuses the existing SEH-guarded GameObject/Ogre resolver.
- The presentation API itself installs no animation-evaluation hook. EXU's separate
  `exu.fps` logical-pilot layer may expose an observe-only `Person::Simulate`
  seam; it does not change these Ogre animation semantics.
- Redux/Ogre remains responsible for animation evaluation and time advancement.
- Unsupported or temporarily unavailable targets fail closed.
- `TargetLocalFirstPerson()` resolves the dedicated pilot FP target afresh for every operation. OpenShim remains preferred when installed; standalone EXU falls back to the validated Redux `Person` render-bridge path. Neither EXU nor Lua caches its Ogre pointer.

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

Enumerate the target's complete current animation inventory:

```lua
local states = exu.animation.List(player)
if states then
    for _, state in ipairs(states) do
        print(state.name, state.length, state.enabled, state.timePosition)
    end
end
```

`List` returns the same metadata shape as `GetInfo` for each state and sorts
the snapshot by animation name for deterministic script/tool output. It returns
`nil` when the target cannot be resolved or Ogre enumeration fails; a valid
entity with no animation states returns an empty table.

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

When OpenShim is installed, EXU preserves the existing OpenShim resolver and its generation/lifetime qualification.

Without OpenShim, EXU now resolves the target directly from the current user-controlled object on every operation:

```text
p_userObject -> RTTI Person -> Person+0x0F0 render bridge
                              -> bridge+0x094 WORLD entity
                              -> bridge+0x0C0 FP entity
```

The native resolver fails closed unless the current user object is a `Person`, the FP pointer is non-null and distinct from WORLD, and the FP entity exposes a skeleton with the stock `idle` and `stand2Kneel` vocabulary. The `+0xC0` FP field was independently runtime-qualified in the 2026-09-05 pilot flashlight investigation as the live `aspilo_fp.mesh` entity. Because the chain is re-read per operation, boarding, destruction, respawn, mission changes, and entity recreation cannot leave EXU holding a stale Ogre pointer. If no qualified FP entity exists, operations return `false` (`GetInfo` returns `nil`) without falling back to WORLD.

The standalone EXU resolver still needs an isolated runtime matrix with OpenShim absent before it is labeled `PROVEN-RUNTIME`; the original public control matrix below used OpenShim as the resolver.

## Capability probe

```lua
local caps = exu.animation.GetCapabilities()
```

Current expected values:

```lua
caps.gameObjectTarget == true
caps.localFirstPersonTarget == true -- with OpenShim or EXU's native supported-build resolver
caps.animationInventory == true
caps.pilotStateInspection == true
caps.pilotFsmIntercept == true -- when the verified observe-only entry detour is active
caps.pilotAnimationOverrides == false -- no non-stock pilot animation policy can be applied yet
caps.managedClock == false
caps.nativeAdvancement == "unvalidated"
```

`pilotStateInspection`, `pilotFsmIntercept`, and `pilotAnimationOverrides`
describe the logical pilot/FSM tooling used by `exu.fps`; they are independent of whether the first-person
Ogre target is currently resolvable.

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

This proves stock `Play`, `Stop`, and `Seek` through the complete Lua → EXU → OpenShim tracker → Ogre `AnimationState` path. The native EXU fallback intentionally reuses the same downstream `GameObject::HasAnimation` / `AnimationState` operations; only target discovery changes. It does not prove autonomous native advancement of an externally selected clip, so `managedClock` remains `false` and `nativeAdvancement` remains `"unvalidated"`.

## Animation inventory implementation

`List` snapshots Ogre's `AnimationStateSet` through the vendored Ogre 1.10
ABI rather than guessing internal STL/container offsets. The iterator work is
isolated in a C++14 bridge, matching EXU's existing native Ogre bridge strategy.
The snapshot contains only names and one-operation state pointers; metadata is
then read through EXU's existing guarded animation getters and no Ogre pointer
is retained after the public call returns.

The two Ogre entry points the bridge needs (`Entity::getAllAnimationStates` and
`AnimationStateSet::getAnimationStateIterator`) are resolved from the loaded
`OgreMain.dll` by mangled name at run time (`Ogre/OgreProc.h`), like the other
native Ogre bridges, and are not added to the hand-made `lib/OgreMain.lib`
import subset. A load-time import of a name the shipped `OgreMain.dll` does not
export would stop `exu.dll` loading at all; a missing export here only makes
`List` return `nil`. Each animation's name is the `AnimationStateSet` map key,
which is exactly the name `Has`, `GetInfo`, and `Play` look states up by.

This is deliberately read-only. Enumerating states does not enable, seek,
weight, or otherwise mutate them.

## Why there is no speed control yet

Speed control would require either a proven native Ogre/Redux time-scale mechanism for the target or an EXU-owned animation clock. Adding a second clock before confirming how Redux advances the state risks double-advancement and frame-order bugs. The API therefore exposes only operations whose semantics are already grounded in the existing Ogre `AnimationState` bridge.

Once the live ownership/advancement experiment is complete, speed and first-person helpers can be added behind the same API without breaking existing scripts.
