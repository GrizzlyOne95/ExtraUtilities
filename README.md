# Extra Utilities

Extra Utilities is the only public script extender for Battlezone 98 Redux 2.2.301. Support open modding!

Usage:
Build the DLL or download the latest release from the releases tab. Then simply require() exu.dll from lua and refer to the function documentation on the wiki page.

Compatibility:
- Windows: Yes
- Mac: No
- Linux: No
- Steam: Yes
- GOG: No
- Battlezone 1.5: of course not lol

Forked from VTRider's initial project. Massive kudos to him for figuring out how to implement EXU in the first place!

I am working on adding additional features as I can such as set/get Material names, bundling OgreMain.lib, fixing a few bugs with calling sunlight parameters, adding more function calls, and more.

Recent additions:
- Viewport retro-lighting toggles via `exu.GetRetroLightingMode()` and `exu.SetRetroLightingMode(enabled)`, which switch the active viewport between the stock modern material schemes and the `og-*` retro variants.
- Local smart-reticle hovercraft shot convergence via `exu.GetPlayerReticleShotConvergence()` and `exu.SetPlayerReticleShotConvergence(enabled)`.
- Hardened environment/material-scheme access so bad viewport state is logged instead of crashing the caller when Ogre data is unavailable.
