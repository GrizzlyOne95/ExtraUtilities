# Betty ammo warning (`exu.SetUserAmmoWarning`)

Betty says "ammo depleted" each time the player's ammo drops below the cost of one shot for every enabled weapon (a rising edge in `StatusDisplay::Render`). Scripts that hold the engine ammo at 0 as a dead trigger (first-person pilot guns during reloads and bolt cycling, as in ISDF Chronicles) make her speak every time. These functions let a script silence the line for the player's own object and play it itself when the pilot is truly dry.

## Lua

| Function | Result |
|---|---|
| `exu.SetUserAmmoWarning(enabled)` | `false` drops Betty's ammo-depleted line for the player's own object; `true` restores stock behaviour. Returns `ok`, `false` when unavailable (unsupported build, Steam, or entry bytes did not qualify). The detour is installed on the first `false`, so missions that never call it are untouched. |
| `exu.GetUserAmmoWarning()` | `true` while Betty speaks as stock |
| `exu.PlayUserAmmoWarning()` | Queues the stock line now (voice event 6) whether or not it is silenced. Returns `false` when there is no user object or the function is unavailable. |

Own-team units (voice event 4) are never affected. The setting is mission-scoped: mission end and Lua state teardown turn Betty back on and remove the detour.

## Engine (GOG 2.2.301)

- `BettyVoice::AmmoWarning` at `0x0047C330`, `void __thiscall(BettyVoice*, GameObject*)`, `ret 4`. If `obj == GameObject::userObject` it queues voice event 6; otherwise, if the object's team is the user team, event 4. Only caller: `StatusDisplay::Render` (`0x005DCB90`), on a rising edge of its ammo-depleted flag.
- The `BettyVoice` global object is at `0x0094F4B0` (passed as `this`). `GameObject::userObject` is `0x00917AFC` (`p_userObject`).
- Catalog: `BettyVoice.*` in `exu.json`.

## Hook

One `EntryDetour32` steals the first 8 bytes (`55 8B EC 51 56 89 4D FC`: whole, position-independent instructions; the `E8 rel32` at +8 is not stolen). The hook returns without calling the original when suppression is on and `obj` is non-null and equals the user object; every other call runs the original unchanged. The first nine bytes are verified before the detour is built, and the feature fails closed (logged once) on a mismatch or unsupported runtime. `PlayUserAmmoWarning` calls the original through the trampoline (or the function directly when not hooked) with the `BettyVoice` object and the user object, under SEH.
