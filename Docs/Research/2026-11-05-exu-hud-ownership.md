# Coordinated HUD presentation ownership

`SetStockStatusHudVisible` and radar presentation now probe the optional
OpenShim provider exports described in OpenShim's `Docs/EXU_HUD_OWNERSHIP.md`.
Claims use hull=1, ammo=2, weapons=4, scrap=8 and radar=16. The setter returns
the granted complete mask; EXU records ownership only when it matches.

On capable providers, OpenShim suppresses its native draws and yields palette
and fading for claimed areas. EXU does not NOP OpenShim's hull/ammo/weapon CALLs.
The scrap block skip remains necessary for its complete sprite/text group.
Existing native radar layout services remain in use. Neither DLL rewrites saved
HUD preferences. On older providers EXU retains checked native patch fallback.

Release occurs when each replacement is disabled and at Lua close, normal or
exceptional. A pinned EXU DLL is reset explicitly between mission Lua states.

## Evidence and limits

Release x86 build, generated address/profile checks, hardening checks and all
47 MSVC host programs passed on 2026-11-05. Native GOG 2.2.301 `isdfms06` logged
claims 0x01, 0x03, 0x07, 0x17, 0x1F and release 0x00 on exit. Read-only native
tracing confirmed the provider owns the hull/ammo/weapon CALL targets and those
adapters execute with mask 31. Native terrain wireframe and scan-ring submission
were observed. Constructor hologram enabling succeeded, but selection, placement,
cancel and build confirmation have not yet been exercised in native gameplay.

Requested larger headless Ogre modes initially produced portrait captures only
604 pixels wide. OpenShim's test-only `OPENSHIM_FRAME_CAPTURE_SIZE=2560x1440`
option corrected that desktop clamp. Verified native 2560x1440 PNGs show the
source rim border following the blue perimeter, with stock status draws absent
behind the EXU status ring. No camera scripts or replacement mission controllers
were used. Steam, Proton and Wine remain untested.
