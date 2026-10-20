# Original small and heavy muzzle flashes

Two texture-free, original meshes for `exu.flash` and OpenShim's weapon ODF
trial. The geometry and shaders are authored here; no BZ2/BZCC art is included.
The muzzle is at the mesh origin and fire extends along render **-Z**, matching
EXU's forward conversion.

| Mesh | Nominal length | Maximum gas radius |
|---|---:|---:|
| `exu_small_flash.mesh` | 0.85 m | 0.24 m |
| `exu_heavy_flash.mesh` | 2.35 m | 0.88 m |

The mesh combines a narrow hot core, filled gas sheets and a small frontal
veil. The HLSL material uses temperature colours, three-octave object-space
turbulence, soft edges and view-dependent optical thickness. Additive emission
and disabled depth writes keep it translucent; depth testing hides fire behind
the barrel and other objects. Static turbulence has no separate wall clock:
EXU owns lifetime and scale, including pause behavior. This material renders
the flash itself; scene lighting and smoke are separate effects.

## Use with OpenShim ODF flags

Copy both `.mesh` files and the `.material`, `.program`, and `.hlsl` files into
the active mod's Ogre resources. No textures are required. The mission must
load the matching EXU companion. Enable OpenShim Settings > Video > Muzzle
Flashes (INI `[SinglePlayer] MuzzleFlash = 1`) and restart. The setting defaults
OFF and gates the exact-GOG single-player cannon ODF path; the old developer
environment flags no longer bypass it. No generic effect is added to weapons
without authored configuration/assets.

```ini
[WeaponClass]
flashMesh = "exu_small_flash.mesh"
flashMaterial = "EXU/CannonFlash/Amber"
flashDuration = 0.08
flashScale = 1.0
flashEndScale = 0.15
flashOffset = "0.20 0.10 2.41"
flashRoll = 0
```

That offset is an **avtank/gatstab visual placement preset**, not a universal
offset. The stock mesh's barrel reaches beyond its native weapon hardpoint.
The preset moves the visual effect to the visible barrel tip; it does not move
the projectile. Measure other craft's muzzles and tune right/up/forward metres
independently. Scale is relative to the dimensions above and does not scale
the offset. The renderer follows the firing craft over the short lifetime;
it does not continuously reattach to a subsequently moving turret/barrel.

The small mesh replaces the BZ2-style `isdfc_sflash.mesh` selection in this
trial. Select it in the weapon ODF rather than overwriting a third-party mesh.
For an existing ISDFC Lua profile, set its `exu.flash` mesh/material fields
instead, or disable that Lua flash producer when using the OpenShim ODF path.
Using both producers would draw two flashes for one shot.

The same weapon ODF can add `muzzleRender = "oshmuzz.render"` and
`muzzleRenderDuration = 0.09` for an independently timed smoke/light layer.
OpenShim's `examples/odf_muzzle_render` supplies the optional original engine
render definition and authoring guide. This native layer does not require EXU;
the mesh still does. Both channels share the Muzzle Flashes master switch.

Use `exu_heavy_flash.mesh` for large ordnance, or select the cooler
`EXU/CannonFlash/Blue` material. All three mesh layers encode their role in UV0
and support either material override; keep the mesh and shader versions matched.

## What is tunable where

| Control | Location |
|---|---|
| Mesh and material preset | Weapon ODF `flashMesh`, `flashMaterial` |
| Duration and start/end size | Weapon ODF `flashDuration`, `flashScale`, `flashEndScale` |
| Muzzle placement and axial roll | Weapon ODF `flashOffset`, `flashRoll` |
| Brightness, turbulence, noise frequency, soft edge exponent | Shader `flashParams` in `.program` or a material override |
| Cool outer gas and hot core RGB | `flashCoolColour`, `flashHotColour` material parameters |
| Shape, length, radius and sheet count | `tools/build_cannon_flash.py`, then regenerate |

`flashParams` defaults to `1.25 0.85 8.0 1.35`: brightness multiplier,
turbulence amount (0..1), noise frequency, and positive edge exponent.
Copy the Blue material's named-technique override pattern to make another
colour/brightness preset; set the corresponding ODF `flashMaterial` to its
name. These shader parameters are **not additional ODF flags**.

The GPU program identifiers include `v1`. When changing shader code or its
uniform layout, increment that suffix in both `.program` and `.material`.
Redux can reuse compiled shader-cache entries for an unchanged program name;
an old entry can silently render old code or reject newly added parameters.
Version the program names rather than clearing a player's whole shader cache.

## Regenerate and validate

```powershell
python tools/build_cannon_flash.py
```

The builder uses the sibling `Battlezone98_Modding_Toolbox` Ogre mesh codec.
`--toolbox` and `--output` override those locations. It checks finite vertices,
triangle area and byte-identical mesh round trips. The small/heavy assets
have 840/931 vertices and 1296/1440 triangles respectively.
`--preview-data <local-directory>` optionally emits geometry JSON for DCC
previews; generated previews belong outside the repository.

The HLSL compiles as `vs_4_0`/`ps_4_0` for DX11 and, with `EXU_DX9=1`, as
`vs_3_0`/`ps_3_0` for DX9. Runtime visual evidence must identify the backend;
compilation alone does not qualify DX9, Steam, Proton or Wine.

Windowed Windows/GOG 2.2.301 DX11 live checks on 2026-10-20 loaded these
resources through the weapon ODF, with the matching OpenShim/EXU trial DLLs
and no Lua flash producer. The small/Amber run counted 81 spawns; heavy/Amber
and heavy/Blue runs each counted 82. All reported zero failed/orphaned flashes
and zero live flashes at shutdown. Captures confirm the two sizes and the
Amber heavy flash emerging at the visible avtank barrel tip with the preset
above. The Blue check used an exploratory 3.40 m forward offset, not the final
placement preset. A disabled-trial comparison confirmed that the orange
projectile flare inside the barrel is a pre-existing ordnance effect. The
custom resources produced no DX11 shader/uniform/material parse errors;
the unused DX9 alternative reports unsupported targets on DX11 as expected.
All 61 temporary deployment/resource/mission/weapon files and Ogre settings
were restored after each run. Wider lifecycle/platform qualification remains
pending. These artwork checks predate the settings-based master switches and
do not qualify broader native rendering or platform support.

## BZ2/BZCC reference findings

The installed BZCC baked weapon definitions and extracted BZ2 demo use the
same `g_sflash.xsi` geometry for miniguns and AT-Stab cannons. BZCC miniguns
start at radius 3; combat/assault AT-Stab use 10/15, and plasma cannon fore
flashes use 8/10 with another texture/light layer. Thus their cannon appearance
comes from scaling, colour, texture and layering rather than a distinct cannon
flash mesh. The existing ISDFC `isdfc_sflash.mesh` already ports that shape.
The BZ2 `g_fflash.xsi` mortar cone is another existing port.

The raw art reconstruction also has unverified `muzzle_FLASH-sphere1.*` OBJ
exports; no link from the inspected installed cannon flash definitions to those
exports was found. They were not used to author this asset.
