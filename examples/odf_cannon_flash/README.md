# Original small and heavy muzzle flashes

Two original meshes for `exu.flash` and OpenShim's weapon ODF trial. Version 2
adds an original grayscale flame mask with preserved transparency, generated
with the built-in image tool; its exact prompt is in `flame_v2.provenance.json`.
No BZ2/BZCC art is included.
The muzzle is at the mesh origin and fire extends along render **-Z**, matching
EXU's forward conversion.

| Mesh | Nominal length | Maximum gas radius |
|---|---:|---:|
| `exu_small_flash.mesh` | 0.85 m | 0.24 m |
| `exu_heavy_flash.mesh` | 2.35 m | 0.88 m |

The mesh combines a narrow hot core, filled gas sheets and a small frontal
veil. The HLSL material uses temperature colours, three-octave object-space
turbulence, textured gas breakup, soft edges and view-dependent optical thickness. Additive emission
and disabled depth writes keep it translucent; depth testing hides fire behind
the barrel and other objects. A short white core gives way to eroding outer gas;
each shot varies its axial rotation, length, brightness and texture direction.
EXU owns lifetime, phase and scale, including pause behavior. A deterministic
visual seed does not consume the game's random number generator. This material renders
the flash itself; scene lighting and smoke are separate effects.

## Use with OpenShim ODF flags

Copy both `.mesh` files, `exu_muzzle_flame_v2.png`, and the `.material`, `.program`,
and `.hlsl` files into the active mod's Ogre resources. The mission must
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
| Axial variation, length variation, growth time, gas fade | `flashAnimation` shader parameter |
| Flame silhouette and filament detail | `exu_muzzle_flame_v2.png` (grayscale RGBA, root at bottom) |
| Shape, length, radius and sheet count | `tools/build_cannon_flash.py`, then regenerate |

`flashParams` defaults to `1.25 0.85 8.0 1.35`: brightness multiplier,
turbulence amount (0..1), noise frequency, and positive edge exponent.
Copy the Blue material's named-technique override pattern to make another
colour/brightness preset; set the corresponding ODF `flashMaterial` to its
name. These shader parameters are **not additional ODF flags**.

`flashAnimation` defaults to `6.283185 0.12 0.22 1.4`: total axial rotation
range in radians, fractional length variation, normalized growth time, and
gas fade exponent. Shrinking a flash to nearly zero with `flashEndScale` can
hide the new gas tail; the v2 trial uses 0.65. The existing ODF controls still
select the material, size, timing and barrel placement.

EXU writes Ogre renderable custom parameter **1344** on every flash subentity:
normalized age, stable seed (0..1), valid marker (1), and duration in seconds.
The values belong to each flash, so simultaneous shots do not animate together
through a shared material uniform. Custom materials can consume this parameter
or ignore it. Legacy EXU can render the new material's static fallback but
requires the updated runtime for lifetime animation and shot variation.

The GPU program identifiers include `v2`. When changing shader code or its
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

For a reversible artwork trial using the installed `lcbench` fixture and
`stock_terrain_hd` resource group, build EXU and current OpenShim, then run
`tools/Test-CannonFlashLive.ps1 -OpenShimRepo <checkout> -Size small` from a
fresh Windows PowerShell 5.1 process. `-Size heavy`, `-Colour Blue`, and `-Smoke`
select variants. It uses the shared launch lock, windowed mode, native ODF
producer and graceful shutdown; captures and hash-checked restoration receipts
stay under ignored `build/flash-v2/`. The helper never stops another game.

### Version 2 trial, 2026-10-20

The windowed Windows/GOG 2.2.301 DX11 checks used OpenShim main `ffde33097`
and the matching EXU v2 build, with the native weapon ODF producer and no Lua
flash producer. The small/Amber run spawned 66 flashes (PID 65112); heavy/Amber
with `oshmuzz.render` spawned 79 (PID 23076); heavy/Blue spawned 80 (PID 64972).
All three initialized per-renderable parameters, loaded the new texture and
finished with zero failed/orphaned flashes and zero live flashes at shutdown.
The heavy smoke/light run logged native render creation and detachment too.
Captures show the hot jet at the visible barrel end, two flash sizes and the
Blue tint distinct from the stock orange projectile. The phase/seed host checks
pass, including invalid times; EXU hardening and Release x86 builds pass, and
both shader stages compile for DX11 and DX9.

Evidence stays locally in `build/flash-v2/run-20261020-165018`,
`run-20261020-165120`, and `run-20261020-165220`. The runs restored 66/67/66
temporary files with original hashes, plus Ogre settings. Useful unedited
frames are heavy Amber `frame-043.png` / `frame-098.png`, and Blue
`frame-098.png`. No flash DX11 shader/uniform/material parse failures occurred;
the unused DX9 alternatives report unsupported targets on DX11 as expected.
The test install has unrelated pre-existing `TestGround.material` references
which still report missing objects. No settings-UI interaction or broad
lifecycle/platform qualification is claimed. This is an artwork/runtime trial,
not a CR asset-package update or release.

### Earlier version 1 trial

Windowed Windows/GOG 2.2.301 DX11 live checks on 2026-10-20 loaded the earlier
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
