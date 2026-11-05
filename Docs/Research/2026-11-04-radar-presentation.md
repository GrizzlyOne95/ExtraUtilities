# Mission-scoped native radar presentation

Implemented `SetRadarPresentation(enabled, tiltDegrees=30, perspectiveDistance=8)`
and `GetRadarLayout()` for ISDFC. This adapts the existing radar and does not
enumerate contacts from Lua or replace its scanner. Stock contact classification,
selection, alerts, sounds, compass, terrain grid and minimap remain native.

## Qualified local evidence

- Current GOG Redux 2.2.301 executable SHA256:
  `8d71f56c1314e69a8ad38f4eeaf20a8ff825965a84cf196e5f77ea4cc3377413`.
- `CockpitRadar::Render` at 0x00493EC0, simulation at 0x004933E0,
  packed screen projector at 0x00493330, backdrop at 0x00493D40.
  Source navigation: Battlezone_Source/BZ1/Redux/openshim_re_corpus/
  bzr_gog_best_effort/ghidrecomp/results/bins/battlezone98redux.exe-6777ca/decomps.
- All twenty instructions are catalogued in exu.json and verified against the
  local PE: nine radar-only projector calls, ten radar-only sine/cosine loads,
  and one argument-free backdrop call. No shared float constant is overwritten.
- BZ2 b131p reference `Render-00528792.c`: dedicated radar view distance 8.
  `_dynamic_initializer_for__RADAR_SIN__` at 0x00415DBC and COS at 0x00415DA6
  read radians at 0x006E94E8: 0x3F060A92, or approximately 30 degrees.
  BZ2 values are visual references; no BZ2 object layout is used by EXU.

## Ownership and fallback

Patches start inactive, activate only by a mission API call, and use the existing
BasicPatch lifecycle and expected-byte checks. If any site cannot activate, all
twenty are restored. Closing the Lua state restores inactive defaults. Repeated
enable calls update projection values without allocating more patches.

The radar-only projector keeps the native cdecl stack arguments and EDX:EAX
packed signed pixel return. Nonfinite/unprojectable points return the radar
center rather than convert invalid floats to integers. `GetRadarLayout` reads
the native center, radius, HUD flag and radar mode. It writes no camera or HUD
toggle state. Other patches, including OpenShim's internal radar color/fade
adapters, are not replaced.

ISDFC draws an EXU overlay rim from its existing d01m029 BZ2 demo radar mesh.
The filled center and baked compass letters are omitted so native terrain,
contact symbols and heading-dependent compass remain visible. This is a BZ2
presentation pass; exact native visual parity has not been accepted.

## Validation boundary

Release x86 build, Windows x86 and Linux projection tests, Lua 5.1 lifecycle/fallback test,
hardening/API/address checks, Lua definition-file syntax and static PE preimages
passed. The complete Linux suite passed with all 45 C++ host tests, Lua 5.1
controllers, JavaScript checks and installer checks. Its initially missing Node
dependency was supplied by a temporary checksum-verified portable runtime,
removed after completion.
Two normal isdfms01 native starts subsequently passed with the installed DLL;
all 20 sites were active in a live read. First-run inspection showed the rim,
native contacts/grid/compass/cone and correct overlay hiding on pause. The review
desktop's 600-pixel width clipped the requested window, and second-run computer
use capture failed (CreateForMonitor 0x80070057; activation retry failed).
Full-size visual acceptance and Steam/Proton/Wine runtime checks remain unrun.
Both owned tests exited gracefully and the original Ogre configuration was
restored byte for byte. No gameplay camera commands or test Lua were used.
