# Cockpit glass (sub-entity render queue)

Windshield layers (rain, dirt, cracks) can sit on the cockpit's own glass
instead of a screen overlay. The glass is a submesh of the cockpit mesh; EXU
moves only that sub-entity into a late render queue group.

## Engine facts (GOG 2.2.301, verified 2026-10-05)

- The first-person/cockpit entity (render bridge `GameObject+0xC0`) is set up
  at `0x0067E6C6..0x0067E6FD`: `setCastShadows(false)`, then the virtual
  `Entity::setRenderQueueGroup` (vtable `+0xA8`) with the byte at `0x008ED6A8`
  (= 10). Terrain is group 40 (OpenShim's terrain proxy uses 0x28), so the
  cockpit draws before the world and writes depth.
- Ogre 1.10 `Entity::_updateRenderQueue` adds each visible sub-entity with
  the sub-entity's own group/priority when set, before the entity's. The
  engine only sets the entity's group, so a sub-entity group survives it.
- OgreMain exports `SubEntity::setRenderQueueGroup`,
  `setRenderQueueGroupAndPriority`, `getRenderQueueGroup`,
  `isRenderQueueGroupSet` and `setVisible`, and
  `TextureUnitState::setTextureScale`.

A translucent glass sub-entity in a group after the world (ISDFC uses 90:
after the main queue and effects, before overlays at 100), with
`depth_check on` and `depth_write off`, is therefore hidden wherever the
cockpit frame or dash is nearer the eye and shows through the openings. It is
skinned with the cockpit, so it follows every bone for free.

## API

- `exu.SetSubEntityRenderQueueGroup(target, indexOrMaterial, group[, priority])`
  (alias `exu.animation.SetSubEntityRenderQueue`), with
  `exu.animation.TargetCockpit(h)`. Re-resolved per call; the cockpit entity
  is rebuilt on vehicle and view changes and the setting goes with it, so
  callers re-apply it while the cockpit view is up.
- `exu.GetSubEntityRenderQueueGroup(target, index)` -> group, isOwnGroup.
- `exu.SetMaterialTextureWindow(material, u0, v0, du, dv[, technique, pass, unit])`
  sets scale and scroll so UV 0..1 samples a window, like overlay
  `uv_coords`; technique -1 = all techniques. Math: `src/Game/TextureWindowMath.h`
  (host test `tests/host/texture_window_math_tests.cpp`).
- `exu.SetMaterialTextureScale(material, su, sv[, technique, pass, unit])`.
- `exu.animation.GetCapabilities().glass` / `.subEntityRenderQueue`.

Opacity uses the existing `exu.SetMaterialPassColors` (pass diffuse alpha is
the lit vertex alpha in the fixed pipeline and in ISDFC's DX11 stand-ins).

## Also fixed

`exu.SetSubEntityVisible` passed a `SubEntity*` to
`MovableObject::setVisible`, which writes through the wrong object layout. It
now calls `SubEntity::setVisible`.
