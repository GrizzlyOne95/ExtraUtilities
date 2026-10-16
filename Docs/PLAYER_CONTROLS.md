# Mission player-control masks

```lua
local exu = require("exu")
local owner = "mission:landing"
if not exu.SetPlayerControlMask(owner, exu.CONTROL.ALL) then
    print("Control lock unavailable: " .. exu.GetPlayerControlState().reason)
end
-- At release, abort, restart/load or the mission's terminal transition:
exu.ClearPlayerControlMask(owner)
```

This is opt-in native input filtering for the local player in **single player**.
It blocks the inputs consumed by UserProcess, including primary/auto/special
fire and eject/abandon. It follows the current local pilot or craft; it does not
retain an actor pointer or change weapons, ammo, AI, team or physical velocity.
Scripted actions such as `FireAt` and `EjectPilot` remain separate operations.

Use distinct bits from `exu.CONTROL` to select channels:

| Flag | Value | Native channels suppressed |
| --- | --- | --- |
| THROTTLE | 1 | Analogue and digital forward/reverse |
| STRAFE | 2 | Analogue and digital strafe |
| LOOK | 4 | Steering, pitch and aim-assist input flags |
| FIRE | 8 | Primary, auto and special fire |
| JUMP | 16 | Jump |
| DEPLOY | 32 | Deploy/undeploy |
| EJECT | 64 | Eject |
| ABANDON | 128 | Abandon |
| TURBO | 256 | Explicit and throttle-derived turbo |
| WEAPONS | 512 | Selection, cycle and link |
| NAV | 1024 | Drop nav beacon |
| JETPACK | 2048 | Native jetpack input |
| MOVEMENT | 2323 | Throttle, strafe, jump, turbo and jetpack |
| ALL | 4095 | All listed channels |

Each owner has one replaceable mask. The effective request is the OR of all
owners' masks. A zero mask or `ClearPlayerControlMask(owner)` releases only that
owner. Names contain 1..63 bytes with no embedded NUL; capacity is 16 concurrent
owners. Bad arguments raise a Lua error; capacity or runtime refusal returns
false. Releasing an absent owner succeeds, including after a runtime fault.

`GetPlayerControlState()` distinguishes availability, hook activation, requested
mask and actual applications. `lastAppliedMask` is a historical sample; it is
not a current UI/focus eligibility signal. Unsupported builds, a changed/owned
entry point, multiplayer and native read/write faults stand the feature down.
Do not treat a missing API or false result as a successful lock.

The hook installs lazily on the first accepted nonzero request. The input key
prefix and unrelated UI/command data are preserved. Masked smoothing outputs
are cleared before and after stock processing, including its synthesized turbo.
Owner state resets on Init; Lua-state closure removes the detour before EXU can
unload. Lua saves do not serialize this native policy: reapply the mission's
current lock after Load and clear it when releasing the player or ending.

Validation is split: host tests cover all 4,096 channel combinations, untouched
UI data, cached output clearing and owner isolation/capacity/reset. The x86
release DLL builds, and the GOG 2.2.301 executable matches the new catalog/profile
anchors. **Native gameplay, UI access, pilot/craft swaps and shutdown/restart
qualification still require an in-game run.** See the dated RE note.
