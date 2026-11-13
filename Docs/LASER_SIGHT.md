# OpenShim laser-sight mode (`exu.SetLaserSightMode`)

Thin Lua binding for two optional OpenShim exports (`OpenShimSetLaserSightMode`, `OpenShimGetLaserSightMode`, resolved lazily from `openshim.dll`). Source: `src/Game/LaserSight.cpp`.

| Function | Result |
| --- | --- |
| `exu.SetLaserSightMode(mode)` | mode in effect (integer), or nil. `mode` must be integer `0` or `1`; anything else raises a Lua error. |
| `exu.GetLaserSightMode()` | current mode (integer), or nil |

- `0` (default): follows `openshim.ini` `LaserSight`, which is off in network games.
- `1`: the laser sight is also allowed in network games.
- OpenShim resets the mode to `0` whenever the game leaves a mission, so a script must set it every mission.
- Requires OpenShim with the laser-sight feature (Battlezone98Redux_Shim PR #822). With no OpenShim, or an older one, both functions return nil.
