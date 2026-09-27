# Worksheet G: build, CI, tooling, tests, catalog and repository hygiene (2026-09-27)

Reviewer scope: `ExtraUtilities.sln` (34), `ExtraUtilities.vcxproj` (257), `.vcxproj.filters` (296), `Lua5.1-BZR/Lua5.1-BZR.vcxproj` (165) + `Lua5.1-BZR/src` (53 files, 13,246 lines of `.c`), `lib/` (6 tracked), `Resource/` (Resource.rc 99 UTF-16LE, resource.h 14), `third_party/ogre-1.10.0-bzr/` (README 95, ABI_NOTES 148, Build-Ogre-BZR.ps1 425, Compare-Ogre-ABI.ps1 139, 2 patches 49/51), `tests/` (HardeningSmoke.vcxproj 45, hardening_smoke.cpp 175, 5 host `.cpp` 833, weather_controller_test.lua 515, linux/run.sh 169), `.github/workflows/` (linux.yml 40, release.yml 110), `tools/` (qualify_bzr_build.py 427, generate_bzr_build_profile.py 137, test_bzr_qualification.py 192, validate_hardening.py 189, generate_weather_textures.py 223, Validate-BZRBuild.bat 25), root scripts (squish.py 93, clean.py 19, upload_workshop.py 99/.bat 1, install_requirements.bat 4/.sh 19, requirements.txt 1), `scripts/*.sh` (274/103/103), `exu.json` (500), `profiles/bzr_2.2.301.json` (72), `.gitignore` (103), `.gitattributes` (65), README (116), ARCHITECTURE (75), AGENTS (36), `Docs/*.md` + `Docs/Research/*` (skimmed for drift; shared docs hashed), `examples/*.lua` (12), `Definitions/*.lua` (API-parity only), workshop metadata. Base: origin/main aec8c0a.

Commands run (all read-only): `python tools/generate_bzr_build_profile.py --check` -> "BZR build profile generation check passed" (rc 0); `python tools/test_bzr_qualification.py` -> 4 tests OK (rc 0); `python tools/validate_hardening.py` -> "API parity OK: 377 Lua functions / Version parity OK: 1.2.0 / Address catalog ... OK (3 required anchors) / Hardening markers OK" (rc 0, but see G-2); `python tools/generate_weather_textures.py --check` -> up to date (Pillow 12.3.0, numpy 2.5.3); all five `tests/host/*.cpp` built with clang++ 19 `-std=c++17 -Wall -Wextra -Werror -Wshadow -I src` into the scratchpad and passed; `lua tests/host/weather_controller_test.lua` (Lua 5.4.6) passed; `qualify_bzr_build.py` against the installed GOG exe -> `SUPPORTED_PROFILE_MATCH` (rc 0, ~10 s) and against the installed Steam exe -> `UNKNOWN_OR_CHANGED_BUILD`, every anchor MISS (see G-6). Shared-doc hashes: see section 8 header.

## 1. File disposition table (repo root, scripts/, tools/, tests/, Docs/)

(No file in scope exceeds 800 lines except `Definitions/ExtraUtils.lua` 2,594 and the shared `Docs/BZR_LUA_AGENT_REFERENCE.md` 1,214, both reference material; no section map needed.)

| Path | Purpose (1 line) | Referenced by | Verdict |
|---|---|---|---|
| `.gitattributes` | `text=auto`, LF pin for `*.sh`, linguist-vendored | git | keep; fix `src/BZR.h` case (G-10), add `*.patch -text`, `*.bat eol=crlf`, UTF-16 `.rc` handling |
| `.gitignore` | build/py/local-state ignores | git | keep; add `EXU_BZR_Compatibility_*.txt` (qualify `--write-report` output lands in cwd) |
| `AGENTS.md`, `ARCHITECTURE.md` | project rules / ownership | everything | keep; ARCHITECTURE should name the two-Lua-cores model (G-1) |
| `COPYING`, `COPYING.LESSER` | licence | README | keep |
| `ExtraUtilities.sln` | 2 projects (EXU + Lua5.1-BZR) | CI | keep; HardeningSmoke not in sln (Low) |
| `ExtraUtilities.vcxproj` / `.filters` | exu.dll build | CI | keep, clean up (G-7, G-9, G-10, G-11) |
| `README.md` | user/dev entry | - | keep; fix drift (G-10, section 8) |
| `exu.json` | address catalog | profiles, tools, validate_hardening | keep; see G-20 / section 4 |
| `squish.py` | flatten Definitions+Release+Workshop into `Build/` | Docs/WORKSHOP_RELEASE.md | keep, fix (G-15) |
| `clean.py` | `rmtree(Build)` + `pause` | none (WORKSHOP_RELEASE does not mention it) | fold into `squish.py --clean` or remove |
| `upload_workshop.py` / `.bat` | write `workshop.vdf`, run steamcmd | WORKSHOP_RELEASE | keep, fix (G-5) |
| `install_requirements.bat` / `.sh` / `requirements.txt` | venv with python-dotenv for upload | upload_workshop docstring, run.sh (`bash -n`) | keep; move the three (plus squish/clean/upload) under `tools/workshop/` |
| `unit_vo_notes.md` | dated (2026-03-16) Unit VO hook research note | none | move to `Docs/Research/UNIT_VO_NOTES_20260316.md` (AGENTS.md: dated investigations go there) |
| `workshop_description.txt`, `workshop_changenote.txt` | Workshop metadata | upload_workshop.py | keep; changenote is stale (last edited 2026-03-17, describes retro lighting) |
| `parameter_value_tests.obj`, `render_space_tests.obj` (root) | untracked cl.exe litter (2026-09-19), ignored by `*.obj` | none | local only: delete; not tracked (`git ls-files` confirms) |
| `ExtraUtilities/Release/`, `Release/`, `tests/bin`, `tests/obj`, `tools/__pycache__`, `Lua5.1-BZR/{Release,Lua5.1-BZR}/`, `lib/Lua5.1-BZR.{lib,pdb}` | build output / IntDir | - | all ignored (`git status --ignored`), none tracked. OK |
| `scripts/install_linux.sh` | one-line Proton installer, release DLL + SHA256SUMS | README | keep (G-23 notes) |
| `scripts/steam_game_paths.sh` | Steam library discovery (sourced) | install_linux, deploy_linux_proton, run.sh | keep |
| `scripts/deploy_linux_proton.sh` | copy local `Release/exu.dll` into Proton installs | README:66, run.sh | keep-or-remove: `install_linux.sh` already uses `../Release/exu.dll` when run from a checkout (install_linux.sh:244-246) and duplicates `is_exu_dll`/deploy verbatim; make it a 3-line wrapper or delete |
| `tools/qualify_bzr_build.py` | offline PE qualification vs profile | ARCHITECTURE, README, Validate-BZRBuild.bat, generator, tests | keep, fix (G-6) |
| `tools/generate_bzr_build_profile.py` | render `BzrBuildProfile.generated.h` | CI (`--check`) | keep (Low: header comment hard-codes source path, line 32) |
| `tools/test_bzr_qualification.py` | 4 unittest cases for the qualifier | CI, run.sh | keep; extend (section 8) |
| `tools/validate_hardening.py` | API/version/catalog/hardening tripwires | CI, run.sh | keep, fix (G-2, G-4, G-17) |
| `tools/generate_weather_textures.py` | procedural Workshop PNGs + `--check` | run.sh | keep (G-19) |
| `tools/Validate-BZRBuild.bat` | Windows wrapper for qualify | none (README/ARCHITECTURE call the .py) | keep; fix example exe name (G-6) |
| `tests/linux/run.sh` | Linux host lane | linux.yml | keep |
| `tests/host/*.cpp` (5) | pure-header tests on real `src/` headers | run.sh | keep; not run on Windows or on tag (G-3) |
| `tests/host/weather_controller_test.lua` | fake-`exu` test of `Workshop/exu_weather.lua` | run.sh | keep (its fake API names all exist in `exuExports`, verified) |
| `tests/hardening_smoke.cpp` / `HardeningSmoke.vcxproj` | Win32 BasicPatch/Scanner/BuildValidation smoke | release.yml | keep; align flags (Low) |
| `Docs/ANIMATION_API.md`, `Docs/PERSISTENCE_AND_CONTINUITY.md` | API docs | README | keep |
| `Docs/OPENSHIM_RENDER_EFFECT_BRIDGE.md` | bridge design + later "Implementation status" | README? no; code comments | keep; fix stale status line (G-18); move the pre-implementation design half to Research |
| `Docs/WORKSHOP_RELEASE.md` | publication notes | AGENTS | keep |
| `Docs/BZR_LUA_AGENT_REFERENCE.md`, `Docs/BZR_PLATFORM_COMPATIBILITY.md` | shared cross-repo docs | AGENTS | keep; byte-identical across all four repos (section 8) |
| `Docs/Research/*` (4) | dated research | - | keep; `KENSHI...` and `SHIM_EXU...` reference CR/OpenShim files and not-yet-existing APIs by design |
| `examples/*.lua` (12) | mission examples | README | keep; fix `PhysicsImpact.lua` (G-13); `openshim_coop_sync.lua` uses no `exu.*` at all (pure stock Send/Receive helper) -- fine, but say so in its header |

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| G-1 | [Med/High] | `Lua5.1-BZR/src/ltable.c:72-74` | EXU statically links its own Lua 5.1.5 core and reconciles the one cross-copy sentinel that matters by hard-wiring the exe's `dummynode_` VA: `#define dummynode (0x86EEF0)`. This raw engine address is not in `exu.json`, not a profile anchor, invisible to the address census regex (6 hex digits), and is live for every table op the moment `luaopen_exu` runs, regardless of `BuildValidation::IsSupportedBzr2301()`. | On any exe whose `dummynode_` moved, EXU's `luaH_resize` would `luaM_freearray` the host's static node (`nold != dummynode`), i.e. the "infamous heap corruption" this fixed returns silently; no fail-closed path exists. It is also the single most important architectural fact about how EXU talks to the game's Lua and it is documented only in a code comment. | Add `Lua.dummynode` to `exu.json` with a signature taken from a Lua-core function that embeds the address (GOG exe has 7 imm32 refs at 0x00830D9D..0x00831649), add it as a required `runtime_gate` anchor, and have `luaopen_exu` refuse (return a Lua error before creating any table) when the anchor fails. Document the two-core model (only `dummynode` shared; `luaO_nilobject_` comparisons are intra-copy) in ARCHITECTURE.md. | Read ltable.c 60-80 and all 16 `luaO_nilobject` uses (all intra-copy); on the installed GOG exe: 32 zero bytes at 0x0086EEF0 and 7 imm32 refs in `.text` (Lua core region). Steam exe on disk is SteamStub-encrypted (0 refs), so Steam could not be verified offline. `git log` shows no Lua source change since import 2ef8611. |
| G-2 | [Med/High] | `tools/validate_hardening.py:29-36`; `src/luaexport.cpp:936-939`; `Definitions/ExtraUtils.lua:2409-2422` | API-parity regex does not skip `//` comments, so four commented-out registrations count as runtime exports. `GetEffectsVolume`, `SetEffectsVolume`, `GetVoiceVolume`, `SetVoiceVolume` are documented in Definitions but not registered. Tool reports "377 Lua functions"; the live table has 373. | Mission authors following the editor annotations get `attempt to call a nil value`; CI claims parity it does not have. | Match only lines starting with `{` (`^\s*\{\s*"..."` with `re.M`) or strip `//...` first; delete or `---@deprecated`-mark the four Definitions (the C++ bodies are also commented out in SoundOptions.cpp:166-195). | Parsed the table with both regexes (377 vs 373); grepped all four names. |
| G-3 | [Med/High] | `.github/workflows/linux.yml:3-11`; `release.yml:80-83` | Release is not gated by the host tests. `linux.yml` triggers on push to `main`/`agent/**` and PRs, never on tags; `release.yml`'s `release` job `needs: build` only, and the Windows `build` job runs none of `tests/host/*.cpp`, the weather Lua test, or `generate_weather_textures.py --check`. | A tag can publish a DLL whose render-effect ABI ids, missionSave decode or render-space math regressed (the exact contracts those tests pin). Same shape as OpenShim P2-8 (release lane skipping a PR-lane test). | Add a `host-tests` job (ubuntu, `bash tests/linux/run.sh`) to release.yml and make `release: needs: [build, host-tests]`; optionally compile the five host tests with MSVC in the Windows job too (production is `/std:c++latest` MSVC, tests are g++ `-std=c++17`). | Read both workflows end to end. |
| G-4 | [Med/High] | `Resource/Resource.rc:53-54,71,76`; `src/About.h:29`; `release.yml:33-45` | DLL version resource says `1,1,0,0` / `"1.1.0.0"` while source says 1.2.0. No check covers it: `validate_hardening.check_versions` reads About.h/ExtraUtils.h/Definitions only (UTF-8; the .rc is UTF-16LE), the tag check reads About.h only. Separately, 35 non-merge commits (render-effect bridge, particle emitter/affector API, static geometry, weather, animation fixes) landed after tag `v1.2.0` (d64ce82) with the version still 1.2.0. | Explorer/crash dumps/`qualify`-style tooling report 1.1.0.0; `EXU_GetVersion()` cannot distinguish a 1.2.0 DLL with or without the new exports, so missions cannot feature-detect by version. | Generate the VERSIONINFO from About.h (or add a UTF-16 decode check to validate_hardening), and bump to a pre-release version (e.g. 1.3.0-dev) once new exports land. Keep workshop_changenote in the same check list. | Decoded the .rc with Python (`FILEVERSION 1,1,0,0`), `git log v1.2.0..HEAD --no-merges` (35). |
| G-5 | [Med/High] | `upload_workshop.py:37-41,61-62` | Description and changenote are pasted raw into a quoted VDF value. The current `workshop_description.txt` contains `require("exu")`, so the `"description"` string terminates early; files are opened without `encoding=` (cp1252 on Windows; the em-dashes survive only because every UTF-8 byte happens to be defined in cp1252). | Next steamcmd publish either truncates the Workshop description at `require(` or fails to parse the manifest; the failure is silent (`subprocess.run` result ignored, line 89). | Escape `\` and `"` (`value.replace('\\','\\\\').replace('"','\\"')`), open with `encoding="utf-8"`, check `returncode`. | Read both files; `grep -c '"'` = 1 in description; quote added 2026-09-08 (cc5931c), after the last upload-script change. |
| G-6 | [Med/High] | `tools/qualify_bzr_build.py:280-326`; `tools/Validate-BZRBuild.bat:6`; `profiles/bzr_2.2.301.json:6`; `exu.json:11-12`; `ARCHITECTURE.md:69-73`; `README.md:97-100` | The "new build" workflow is not supported by the tool for the common case. (a) On the Steam exe (SteamStub `.bind`, `.text` encrypted on disk) every anchor reports MISS, and the tool does not detect or explain this although `BuildValidation::HasSteamStubBindSection` knows the pattern. (b) Only the 6 profile anchors are checked; the catalog is merely counted (75 address entries, 5 with signatures, 3 of those known-stale), so README's "Revalidate the addresses and signatures documented in exu.json" has no tool behind it. (c) The .bat example and profile/catalog name the exe `bzr.exe`; the shipped file is `battlezone98redux.exe`. | A maintainer following ARCHITECTURE.md step 1 on a Steam install concludes the build changed; on GOG they get a pass that says nothing about the 146 raw addresses in `src/`. | Detect `.bind` and print "SteamStub-packed: qualify the GOG exe or an unpacked dump"; add a `--catalog` mode that checks every catalog `pattern` at its `address` (MATCH/RELOCATED/MISSING/AMBIGUOUS) and lists address-only entries; fix the example path and the `executable`/`exe`/`pdb` fields. | Ran the tool on both installed exes (GOG: SUPPORTED, Steam sha256 d298782f...: all MISS); read qualify() 280-326. |
| G-7 | [Low/High] | `ExtraUtilities.vcxproj:170,183` | `src\Util\OS.h` and `src\Util\Vec3.h` are `ClCompile` items. Under `/std:c++latest` MSBuild compiles them as C++20 header units (`/exportHeader`), producing `OS.h.ifc` (4.3 MB), `Vec3.h.ifc` (4.2 MB), and links `OS.h.obj`/`Vec3.h.obj` into exu.dll. Nothing imports the header units (all consumers `#include`). | Two extra compilations per build, 8.5 MB of IFC, extra objects in the DLL, and a latent ODR hazard if either header gains a non-inline definition. | Change both to `ClInclude` (filters too). | Read `ExtraUtilities/Release/ExtraUtilities.tlog/CL.command.1.tlog` (`/exportHeader /TP /Fo"...OS.H.OBJ"`) and `link.command.1.tlog` (both `.h.obj` linked). |
| G-8 | [Low/Med] | `ExtraUtilities.vcxproj:72-102` vs `103-139`; `src/Util/IO.h:30-31` | Debug config drifted from Release and is built nowhere: W4+`/WX` without `TreatAngleIncludeAsExternal`/`ExternalWarningLevel` (Release uses them to silence Ogre headers included by OgreNativeFontBridge.cpp), no `GC_PATCH`, no `PreferredToolArchitecture`, `LinkStatus`; `IO.h` disables C4244 only under `_DEBUG`. | Debug is likely broken by Ogre header warnings (unverified: ground rules forbid building), so the one config with `_DEBUG`-only code (`InitializeDebugConsole`, PublicAPI.cpp:80) is untested. | Share one ItemDefinitionGroup for common ClCompile settings; add a Debug build to CI or delete the config. | Diffed the two groups; did not build. |
| G-9 | [Low/High] | `ExtraUtilities.vcxproj:35,56,61-71,80,99,109,113,132` | Stale settings: `ExternalIncludePath` `$(ProjectDir)\minhook;$(ProjectDir)\discord` (neither exists since the imgui/discord era), include dirs `\lib` (no headers) and `\bin` (does not exist; `[Bb]in/` is gitignored), `/NODEFAULTLIB:library` (literal placeholder from the property page; ignores a lib named `library.lib`), `VcpkgUse*` groups with `VcpkgEnabled=false`, `GC_PATCH` define (no reference in `src/`), `LargeAddressAware` on a DLL (ignored by the loader), `PreferredToolArchitecture x86` in Release only. | Noise that hides the settings that matter; `bin` on the include path means a stray local `bin/` header would silently shadow `src/`. | Delete them. | `ls` of repo root; `grep -rn GC_PATCH src` (none); link tlog shows `/NODEFAULTLIB:library` verbatim. |
| G-10 | [Low/High] | `ExtraUtilities.vcxproj:160,195`; `.filters:33,149`; `.gitattributes:48`; `README.md:98`; `unit_vo_notes.md` | Case drift: project items `src\LuaExport.cpp` / `src\BZR.h` vs tracked `src/luaexport.cpp` / `src/bzr.h`. Harmless on Windows, but `.gitattributes` `src/BZR.h linguist-vendored` never matches on GitHub (case-sensitive), and README tells maintainers to edit `src/BZR.h`. `bzr.h` is first-party and should not be vendored anyway; `include/**` also vendors first-party `ExtraUtils.h`. | Linguist stats and "vendored" diff collapsing are wrong; case-sensitive tooling (Linux CI, grep scripts) will miss the path as spelled in docs. | Fix spelling in vcxproj/filters/README; drop the `src/BZR.h` rule and narrow `include/**` to `include/lua*.h`. | `git ls-files` vs parsed project items (script in scratchpad). |
| G-11 | [Low/High] | `ExtraUtilities.vcxproj:185-245`; `.filters` | 12 tracked headers are not in the project: `Game/AnimationApi.h`, `Game/ContinuityApi.h`, `Ogre/OgreMaterialShim.h`, `Ogre/OgreSceneManagerShim.h`, `Patches/ShotConvergenceMath.h`, `Patches/WeaponConvergenceMath.h`, `Util/BuildValidation.h`, `Util/BzrBuildProfile.generated.h`, `Util/Logging.h`, `Util/NativeSaveFlag.h`, `Util/SignatureResolver.h`, `Util/StorageApi.h`; 15 project items are missing from `.filters` (RenderEffects.*, Ogre shims, AiTargetSelect.*, Overlay.*, bridges). No item points at a missing file. | 2,100 lines of header-only runtime (Storage/Continuity APIs) and the build gate are invisible in Solution Explorer. | Add them; consider making StorageApi/ContinuityApi `.cpp` (other worksheets). | Scripted comparison (case-insensitive). |
| G-12 | [Low/High] | `src/Patches/WeaponConvergenceMath.h:1-487` | Dead WIP header (commit 2cd3bf2 "wip(convergence)"): included by nothing, not in the vcxproj, namespace `ExtraUtilities::WeaponConvergence` referenced nowhere, and its header cites `tests/host/weapon_convergence_math_tests.cpp`, which does not exist. Contributes 11 of the raw-address literals in the census (comments). | Looks like tested production math; it is neither. | Finish (wire + add the named test) or delete; `ShotConvergenceMath.h` is the live, tested version. | `grep -rn WeaponConvergence` (only itself). |
| G-13 | [Low/High] | `examples/PhysicsImpact.lua:178` | Calls `exu.SetOrdnanceVelocity`, which is not registered (`Ordnance::SetOrdnanceVelocity` exists at `src/Game/Ordnance.cpp:129` but has no `exuExports` row or Definitions entry). All other `exu.*` uses in the 12 examples resolve (BulletHit/BulletInit are callbacks the mission defines). | Copying the example raises inside the BulletHit callback whenever `strength ~= 1.0`. | Register it (and document) or change the example. | Scripted cross-check of every `exu.X` in examples, Workshop Lua and the weather test fake vs the live table. |
| G-14 | [Low/High] | `third_party/ogre-1.10.0-bzr/patches/0001-...patch:32`; `Build-Ogre-BZR.ps1:210-218`; `README.md:7-10` | Patch 0001 is malformed: hunk `@@ -734,10 +735,16 @@` has 9/15 body lines (trailing context missing), so `git apply --check` fails with "corrupt patch at line 50" on any tree; the build script applies it unconditionally and throws unless `-SkipPatch`. README lists only 0001, not 0002. Also: unpinned `zziplib master.zip`, no archive hashes, and `Ensure-OgreFreeImageImportMode` edits the caller's Ogre `CMakeLists.txt` in place. Ownership: rebuilding the game's render-system DLLs is stock-game work (OpenShim), not EXU runtime. | The optional Ogre rebuild tooling cannot run as committed. | Regenerate 0001 with `git diff`; list 0002; pin/hashed downloads; raise the ownership question with OpenShim rather than growing it here. | `git show HEAD:...0001.patch | git apply --check` against the local `ogre-1.10.0` tree (parse error, tree-independent); counted hunk lines. |
| G-15 | [Low/High] | `squish.py:43-50,63-65,37-39` | Flattens Definitions/Release/Workshop by basename (a future name collision silently overwrites), `os.makedirs(.../"Bin")` without `exist_ok` (second run crashes after copying), leaves `Animation.lua`/`Storage.lua`/`Continuity.lua` (editor meta, no `error()` guard unlike ExtraUtils.lua:6) at the Workshop root under generic names, and ships `exu.pdb` whose CodeView path is the maintainer's absolute path (`C:\Users\<name>\Documents\GIT\...`). Uses cwd, not script dir. | After `RequireFix.Initialize(<exu id>)` a mission `require("Storage")` can resolve to EXU's meta stub; local username leaks into the Workshop PDB. | Copy into a declared layout (Definitions -> `Definitions/`), fail on collisions, `exist_ok=True`, link with `/PDBALTPATH:%_PDB%`, resolve paths from `__file__`. | Read squish.py; parsed `Release/exu.pdb` path from the built DLL's debug directory. |
| G-16 | [Low/Med] | `release.yml:16-19,22,48,71,90,104`; `linux.yml:28` | Actions pinned by major tag, including third-party `softprops/action-gh-release@v2` running with `contents: write`; `build` job has no `timeout-minutes` (default 360) and no `concurrency`; checksums are computed after download from the same run (integrity only, which is fine) but nothing ties the tag commit to `main`. `if-no-files-found: error` is present (good). | Supply-chain and runaway-runner exposure on the only job that can write releases. | Pin by SHA (dependabot keeps them fresh), add `timeout-minutes: 30`, add a concurrency group per ref. | Read both workflows. |
| G-17 | [Low/High] | `tools/validate_hardening.py:23-52`; `Definitions/Animation.lua`; `src/Game/AnimationApi.h:497-510`; `Definitions/ExtraUtils.lua:104-121` | Sub-APIs are not parity-checked: `exu.animation` (12), `exu.storage` (6), `exu.continuity` (5) are registered by `*Api::Install`, and `Definitions/Animation.lua` lacks `animation.TargetLocalFirstPerson`. `exu.animation` is declared twice with different class names (`AnimationApi` in ExtraUtils.lua, `ExuAnimationApi` in Animation.lua). | Drift in the three newest APIs goes unnoticed. | Parse each `luaL_Reg functions[]` in the three headers and compare with `function <table>.<Name>(` in the matching Definitions file; drop the duplicate class. | Scripted comparison. |
| G-18 | [Low/High] | `Docs/OPENSHIM_RENDER_EFFECT_BRIDGE.md:3`; root `unit_vo_notes.md` | The render-effect doc's first line says "no runtime API implementation in this change"; line 10 says both halves are implemented. The design half (lines 100-594) is a pre-implementation plan. `unit_vo_notes.md` is a dated research note at the root. | Readers stop at line 3. | Replace the status line; move the plan to `Docs/Research/`; move the VO note to `Docs/Research/`. | Read both. |
| G-19 | [Low/Med] | `tools/generate_weather_textures.py:60-70,169-180` | `--check` requires exact decoded-pixel equality of a Pillow `BICUBIC` resize; CI installs whatever `python3-pil` ubuntu-latest ships. A resampling change in Pillow fails CI with "out of date" art that nobody edited. | Flaky gate on a runner-image bump. | Compare with a tolerance (max abs diff <= 1) or implement the upscale in numpy. | Read code; passes today with Pillow 12.3. |
| G-20 | [Low/High] | `profiles/bzr_2.2.301.json:12-39`; `exu.json:32-79` | Three of the five catalog signatures are known-stale and kept as always-failing diagnostic anchors: `Camera.Set_View` (bytes at 0x0061D120 are `55 8B EC 51 83 3D D8 AA 8E 00 05 75 05 E9 ...`, not the catalog's `55 8B EC 8B 45 08 50 ...`), `Camera.View_Record_MainCam` (`A1 E0 AA 8E 00` absent; imm32 0x008EAAE0 occurs 7 times with other opcodes), `Camera.zoomFactorFPP` (same; 17 imm32 occurrences). The addresses themselves are still used by `src/bzr.h:137-170` and `exu.SetCameraView` calls 0x0061D120. 70 of 75 entries have no signature; 2 lack the `pattern` key entirely (`Ogre.terrain_masterlight`, `Ogre.sceneManagerStructure`). | Every qualification run prints three expected failures, training maintainers to ignore MISS lines. | Derive real signatures from the GOG exe and flip them to passing diagnostics; make `pattern` mandatory (null allowed) in a schema check. | Read bytes from the installed GOG exe. |
| G-21 | [Low/Med] | `ExtraUtilities.vcxproj:55,116`; README / Workshop description / platform doc | exu.dll imports `MSVCP140.dll`, `VCRUNTIME140.dll` and 9 `api-ms-win-crt-*` sets (dynamic CRT, `CopyCppRuntimeToOutputDir=false`), i.e. requires the VC++ 2015-2022 x86 redistributable; the game itself ships only VC2013 dependencies. Not mentioned in README, Workshop text or `BZR_PLATFORM_COMPATIBILITY.md`; under Proton Wine's builtin msvcp140 is used. | `require("exu")` fails with "module not found" on a machine lacking the redist, which reads like a path problem. | Document the requirement (or evaluate `/MT`; EXU already cannot share a heap with the game's MSVCR120, so static CRT changes nothing at the boundary). | Parsed the built DLL's import table. |
| G-22 | [Low/High] | 17 first-party files | Licence header inconsistency: no LGPL block in `Game/Culling.{h,cpp}`, `Game/game_state.{h,cpp}`, `Ogre/OgreBuildSettings.h`, `Ogre/OgreNativeFontBridge.{h,cpp}`, `OpenShimBridge.h`, `Patches/ShotConvergenceMath.h`, `RenderEffectBridge.h`, `RenderProfileBridge.h`, `Util/Logging.h`, `tests/hardening_smoke.cpp`, `tests/host/shot_convergence_math_tests.cpp`, and the five `tools/*.py`; two wording variants ("This program is distributed" x93, "Extra Utilities is distributed" x5). (BuildValidation.h and SignatureResolver.h do carry it.) | Licence clarity for a public LGPL repo. | Add the block (generated header excepted). | `head -20 | grep` over tracked files. |
| G-23 | [Low/Med] | `scripts/install_linux.sh:106-126,199-216`; `deploy_linux_proton.sh:68-91` | When piped from curl, `steam_game_paths.sh` is fetched from `main` (unpinned, unhashed) and `source`d; SHA256SUMS comes from the same release as the DLL, so the check proves integrity, not authenticity ("tampered with" wording overstates it). Each deploy leaves `exu.dll.bak-<stamp>` in the game dir forever; no uninstaller; the "is this EXU?" probe greps `exu.dll loaded`, a Lua return string (`luaexport.cpp:964`) no test pins. | Low-probability, but these scripts write into player installs. | Inline `steam_game_paths.sh` into the release or pin `--ref` to the tag; keep last N backups; add a test that the marker string is in luaexport.cpp. | Read the scripts. |

Quoted lines for the Medium items:

G-1 `Lua5.1-BZR/src/ltable.c:72-74`
```c
// THIS IS CRITICAL!!! This reassigns the dummy node to the dummy node
// that BZR uses, this prevents the infamous heap corruption crash! -VT
#define dummynode		(0x86EEF0)
```

G-2 `tools/validate_hardening.py:29-36` and `src/luaexport.cpp:936-939`
```python
end = source.find("{ 0, 0 }", start)
...
runtime = set(re.findall(r'\{\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,', table))
```
```cpp
//{ "GetEffectsVolume", &SoundOptions::GetEffectsVolume },
//{ "SetEffectsVolume", &SoundOptions::SetEffectsVolume },
//{ "GetVoiceVolume",   &SoundOptions::GetVoiceVolume },
//{ "SetVoiceVolume",   &SoundOptions::SetVoiceVolume },
```

G-3 `.github/workflows/linux.yml:3-10` / `release.yml:80-83`
```yaml
on:
  pull_request:
    branches:
      - main
  push:
    branches:
      - main
      - 'agent/**'
  workflow_dispatch:
# (linux.yml: no `tags:` trigger)
...
  release:
    needs: build
    if: startsWith(github.ref, 'refs/tags/v')
```

G-4 `Resource/Resource.rc:53,71` (UTF-16LE) vs `src/About.h:29`
```
FILEVERSION 1,1,0,0
VALUE "FileVersion", "1.1.0.0"
```
```cpp
inline std::string version = "1.2.0";
```

G-5 `upload_workshop.py:61-62`
```python
manifest.write(f'    "description" "{description}"\n')
manifest.write(f'    "changenote" "{changenote}"\n')
```

G-6 `tools/Validate-BZRBuild.bat:6` and qualify output on the Steam exe
```bat
echo Example: %~nx0 "C:\Program Files (x86)\Steam\steamapps\common\Battlezone 98 Redux\bzr.exe"
```
```
[MISS ] Overlay pause wrapper | required | mode=expected_va | expected=0x005D4690 | matches=-
[MISS ] Overlay game shell wrapper | required | ...
[MISS ] Wingman Hunt activation | required | ...
Result: UNKNOWN_OR_CHANGED_BUILD
```

## 3. Dead / unreferenced code

| Symbol / file | Location | Evidence |
|---|---|---|
| `WeaponConvergenceMath.h` (whole file, 487 lines, namespace `ExtraUtilities::WeaponConvergence`) | `src/Patches/` | `grep -rn WeaponConvergence` over repo: only the file itself; not in vcxproj; cited test file absent (G-12) |
| `Ordnance::SetOrdnanceVelocity` | `src/Game/Ordnance.cpp:129`, `.h:48` | not in `exuExports`, no other caller; only an example calls `exu.SetOrdnanceVelocity` (G-13). Owner: C++ worksheet decides register vs delete |
| `SoundOptions::{Get,Set}{Effects,Voice}Volume` (commented-out code in `.h:40-43`, `.cpp:166-195`, `luaexport.cpp:936-939`) | `src/Util/SoundOptions.*` | commented out in all three places but still documented (G-2) |
| `BuildValidation::IsSteamBuild`, `IsGogBuild`, `GetBzrDistribution` (production callers) | `src/Util/BuildValidation.h:231-257` | only `tests/hardening_smoke.cpp:78-81` call them; no `src/` caller. Keep only if intended as API |
| `BZR::Ogre::getSkyBoxGenParametersOffset`, `getSkyBoxNodeOffset`, `getSkyDomeGenParametersOffset`, `getSkyDomeNodeOffset`, `getSkyPlaneGenParametersOffset`, `getSkyPlaneNodeOffset` (+ their 6 `exu.json` `Ogre.*` rows) | `src/bzr.h:376-381`, `exu.json:250-255` | referenced only in bzr.h and exu.json; `src/Ogre/Ogre.h` uses the other 8 RVAs |
| `GC_PATCH` preprocessor define | `ExtraUtilities.vcxproj:109` | no `#if`/`#ifdef` in `src/`, `include/`, `Lua5.1-BZR/` |
| Stale include/lib paths `minhook`, `discord`, `bin`, `/NODEFAULTLIB:library` | `ExtraUtilities.vcxproj:56,80,113,132` | directories do not exist / placeholder (G-9) |
| `dummynode_` static | `Lua5.1-BZR/src/ltable.c:76` | macro redefined to 0x86EEF0, static never referenced |
| `print.c` (`luaU_print`, luac disassembler) | `Lua5.1-BZR/src/print.c`, compiled (vcxproj:43) | no caller in EXU or the Lua lib; dropped by `/OPT:REF`; harmless but can be removed from the project |
| 3 profile anchors that are documented never to match | `profiles/bzr_2.2.301.json:12-39` | own `reason` fields say so; qualification confirms (G-20) |
| `lib/OgreMain.exp`, `lib/OgreOverlay.exp` | `lib/` | `.exp` files are only consumed when building the exporting DLL; EXU links the `.lib` only |
| `clean.py` | root | referenced nowhere (G-15 table row) |

Count: 12 items (1 file, 11 symbols/settings/files).

## 4. Raw address census

Method: every `0x[0-9A-Fa-f]{8}` literal in tracked `src/` + `include/` with value 0x00400000..0x02FFFFFF (brief regex `0x00[4-9A-F]xxxxx` gives 155 literals / 143 distinct in 18 files; the extended range adds the 0x02xxxxxx data globals: 164 / 152 distinct). Compared case-insensitively against the 75 `address` entries in `exu.json` (the 14 `rva` rows are OgreMain RVAs and the 3 `value` rows are struct offsets).

Per file (extended range): `src/bzr.h` 77, `src/UI/ControlPanel.cpp` 15, `src/Patches/AiTargetSelect.cpp` 14, `src/Patches/WeaponConvergenceMath.h` 11 (all comments, dead file), `src/Game/Environment.cpp` 8, `src/Game/game_state.cpp` 7, `src/Patches/ShotConvergence.h` 6, `src/UI/Overlay.cpp` 6, `src/Patches/GlobalTurbo.h` 4, `src/Patches/OrdnanceVelocity.cpp` 4, `src/Game/Multiplayer.h` 3, `src/UI/Radar.cpp` 3, and 1 each in `Game/Environment.h`, `LuaHelpers.h`, `Patches/AddScrapCallback.cpp`, `Patches/BulletHitCallback.cpp`, `Patches/BulletInitCallback.cpp`, `Patches/KillMessages.cpp`. Not caught by either regex: `Lua5.1-BZR/src/ltable.c:74` `0x86EEF0` (G-1), and the 7-digit spellings `0x260DB20` (`bzr.h:247`) and `0x25CFA1C` (`bzr.h:479`).

**Catalog entries whose address appears nowhere in src/: 0.** The two regex misses (`GameObject.GetObj_base`, `PlayOption.difficulty`) are the 7-digit spellings above. Two catalog addresses are intentional aliases (`0x008E77A8` uiScaling/radarLeft, `0x0094672C` userProfilePtr/soundStruct1), both annotated.

**src/ literals not in the catalog: 81 distinct values.** 10 occur only in comments (`00462610 005B1E10 005B2010 005B8FF0 005D6330 00611610 0081FE60` in the dead WeaponConvergenceMath.h; `00493330`, `00917AF8` in bzr.h comments; `00680FE0` in an Environment.cpp comment). The 71 in code, by use (from the declaring line; byte-guard status is for the C++ worksheets):

- Hook / patch / call-site / vtable-slot targets (written), 43: BulletInit `00480363`, OrdnanceVelocity `004803D4 0048F639 0048F658 0056B254`, BulletHit `00480771`, Multiplayer `0056F014 005C833B 005C833D`, AddScrap `005E1016`, KillMessages `0062627F`, ControlPanel HUD colour/draw `005C6FF0 005C712B 005C719B 005C72F1 005C7361`, GlobalTurbo `00601C92 00601CA3 00601CB5 00601CCD`, bzr.h cheats/weapon mask `004A7709 005E10D7 0060A8C6`, AiTargetSelect call sites `004634A5 00463593 00463670 00463A46 00463B34 00463C11`, Radar RefreshLayout call sites `0049325F 0049405B`, Environment viewport scheme call sites `00681585 00682AA0 00682EA7` + IAT slot `00869810`, Overlay pause wrapper `005D4690` (a profile anchor, inline-pattern only, no catalog row), vftable slots `00889418 0088A4FC` (ShotConvergence) and `0088A5C0 0088A6EC 0088AB9C 0088AF98 0088B178` (AiTargetSelect).
- Called as functions, 10: `0047C070` (HUD palette selector), `004FF600` (LuaCheckStatus), `00462070` (vector magnitude), `00583500` / `00614020` (ChooseAttackTarget impls), `00417F60`, `005F0930`, `0060F320`, `00681A00` (ShotConvergence), `00683370` (fogReset).
- Data read/written, 18: ControlPanel HUD rects `0091826C 00918270 00918278 0091827C 00918280 00918284 0091829C 009182A0`, `0094F4B0`; game_state UI/pause globals `00918320 00918324 00918328 009454EC 00945549 0094557C`; Overlay asm `0091812B` (written), `00887A64` (pushed string); bzr.h `025F8E4C` (worldRenderOrigin).

Of the 75 catalog rows, 57 are consumed only through `src/bzr.h`; `Radar.RefreshLayout` 0x00492EC0 is re-spelled in `Radar.cpp`, `Math.Matrix_Inverse` 0x008203F0 in the dead WeaponConvergenceMath.h, `GameUI.MainShellWrapper` in `Overlay.cpp`, `GameUI.EscapeWrapperActive` in `game_state.cpp`. Catalog signatures: 5/75 (2 valid, 3 stale, G-20).

## 5. Lifetime / ownership notes

- Lua core (process lifetime, shared global_State): EXU's static Lua 5.1.5 copy operates on the game's `lua_State`/`global_State`; allocations go through the host's `frealloc`, strings/tables are shared structures. Only `dummynode` is reconciled (to the exe's static); `luaO_nilobject_` is per-copy but every comparison against it is inside the copy that produced the pointer (checked all 16 uses). `ldblib.c` `KEY_HOOK` and `loadlib.c` `sentinel_` would diverge if EXU ever registered its copy's debug/package libraries; it does not (`luaopen_exu` is the only entry point, no `luaL_openlibs`). This model should be in ARCHITECTURE.md (G-1).
- Build profile (compile time): `BzrBuildProfile.generated.h` is reproducible from `profiles/` + `exu.json` (`--check` passes; CI and run.sh both run it). `IsSupportedBzr2301()` is not cached; its only production caller is `BasicPatch::EnableDeferredPatchActivation`, so the two `.text` scans (~4.5 MB, the unique anchor must scan all of it) happen once per activation.
- Workshop packaging (per publish): `Build/` is rebuilt by `squish.py`; `workshop.vdf` and `.env` are gitignored (confirmed).
- Installer state (player machine): `exu.dll.bak-<timestamp>` files accumulate per install in the game dir (G-23).

## 6. Performance notes

- `qualify_bzr_build.py:106-107` scans byte-by-byte in pure Python with a generator per offset: ~10 s wall for 6 anchors on the 5.4 MB GOG exe (measured). Fine for 6 anchors; a `--catalog` mode (G-6) over 75 entries would want `bytes.find` on the longest literal run or `re` with `.` for wildcards.
- `tests/linux/run.sh:79-85` compiles each host test separately (5 g++ invocations, one-file each); negligible.
- Build: the two header-unit compilations (G-7) are the only avoidable build cost found; Release uses `/GL` + `/LTCG:incremental`, Lua lib deliberately non-LTCG (commented in .gitignore as not byte-stable).
- Built DLL hardening (parsed `Release/exu.dll` and link tlog): `/DYNAMICBASE`, `/NXCOMPAT`, `/SAFESEH` (277 handlers), `/GS` cookie, `/sdl`, `/permissive-`, `/EHsc`; no `/guard:cf` (reasonable for a module whose hooks jump into game code), no `/Qspectre`. As in the OpenShim audit, pin `RandomizedBaseAddress`/`DataExecutionPrevention`/`ImageHasSafeExceptionHandlers` explicitly in the Release Link group so a toolset change cannot drop them.

## 7. Patterns worth keeping

- Generated runtime gate from the same JSON the offline tool reads, with a `--check` in CI (`generate_bzr_build_profile.py`); `validate_hardening.py` also asserts `BuildValidation.h` consumes it.
- Release reuses the exact tested artifact instead of rebuilding (`release.yml:68-93`), `if-no-files-found: error`, SHA256SUMS published and verified by the installer, tag-vs-source version check.
- Host tests compile the real production headers (`-I src`), not copies; pure logic was deliberately split into Windows/Ogre/Lua-free headers (NativeSaveFlag, OgreParameterValue, OgreRenderSpace, RenderEffectNames, ShotConvergenceMath) with drift/fail-closed cases (e.g. `native_save_flag_tests.cpp:88-120`).
- Fake-`exu` Lua test that exercises older-EXU degradation (`omit`) and teardown restoration; its fake API matches the live export names exactly.
- Generated art with a decoded-pixel `--check` rather than byte compare.
- `.gitattributes` LF pin for shell scripts with the reason written down; installer refuses to overwrite a non-EXU `exu.dll` and fails closed on checksum problems.
- HardeningSmoke tests the patch lifecycle (deferred activation, preimage mismatch, `ScopedPatchDisable`, `UnloadAllPatches`) on real pages.

## 8. Low-severity items

- **Shared BZR docs:** `Docs/BZR_LUA_AGENT_REFERENCE.md` (sha256 `15732942...`, blob `95a146ae...`) and `Docs/BZR_PLATFORM_COMPATIBILITY.md` (`88cedde9...`, blob `b9af9f64...`) are byte-identical in EXU, BZR-OpenShim (`Docs/`, origin/main), Campaign-Reimagined and bzfile (working trees and `origin/main` blobs). No CI in any repo enforces this; a 5-line hash check in each repo's host lane would.
- `include/luaconf.h` differs from `Lua5.1-BZR/src/luaconf.h` only in `LUA_CPATH_DEFAULT` (LuaBinaries-style `clibs`/`?51.dll` entries); `lua.h`, `lauxlib.h`, `lualib.h` identical. EXU compiles against `include/` (first on the include path). Harmless; replace `include/lua*.h` with an include of `Lua5.1-BZR/src` to remove the second copy.
- Lua5.1-BZR vs stock 5.1.5: only the `dummynode` patch found (plus unused `dummynode_`); `LUA_RELEASE "Lua 5.1.5"`, line counts of `luaconf.h` (763) and `lvm.c` (767) match stock; a byte diff against upstream was not possible offline (unverified for the other files). The Lua project builds at W3 without `/WX`, so the pointer/integer comparisons the macro introduces (`nold != dummynode`) are never surfaced.
- `lib/` provenance undocumented: `OgreMain.def` (18 exports) / `OgreOverlay.def` (35) are hand-maintained import subsets; README says only "included". Document "add the decorated name to the .def and run `lib /def:OgreMain.def /machine:x86`" next to them. `lib/Lua5.1-BZR.lib` is built from source by the ProjectReference and untracked since 87d28c8 (correct).
- `OgreNativeFontBridge.cpp` pinned to `stdcpp14` (vcxproj:163-165) with no comment; the file also `#define register` and `_STLP_MSVC` hacks around Ogre 1.10 headers. It shares `Util/Logging.h` inline functions with static locals (`ResetLogFileForCurrentProcess`) with `/std:c++latest` TUs; OK on MSVC in practice, but record why the pin exists (OpenShim P1-19 had the same question).
- `tests/HardeningSmoke.vcxproj:28-39`: "Release" with `Optimization Disabled`, `/std:c++20` (production `c++latest`), no `/WX`, `/sdl` unset, not in the sln. Align with production flags so the smoke test exercises what ships.
- `tests/host/shot_convergence_math_tests.cpp` uses 4-space indent, `gFailures`, no licence header, and prints nothing on failure count; the other four follow a common pattern. A 20-line `tests/host/test_support.h` would dedupe `Expect`/`g_failures` (5 copies).
- Untested engine-independent source, largest first: `StorageApi.h` pure helpers (`Detail::IsSafeNamespace`, `Crc32`, `EncodeString`/`ReadPod` cursor bounds; note `IsSafeNamespace` accepts reserved device names such as `CON`/`NUL`/`COM1` -> `...\Storage\CON.exudata`, worth a test and a decision by the StorageApi owner), `BuildValidation::Detail::PatternMatches`/`CountPatternMatches` (no parity test with the Python matcher; C++ scans `.text` only, Python all executable sections), `SignatureResolver::FindMaskedPattern`/`FindPattern`/`FindUniqueMaskedPattern` (one positive case in the Windows smoke only), `generate_bzr_build_profile.render` (only the `--check` round trip), `ContinuityApi.h` (Lua-bound throughout; would need extraction). All of these sit in headers that include `<Windows.h>` or `<lua.hpp>`, which is why run.sh cannot reach them; splitting the pure parts out (the pattern the five tested headers already follow) is the fix.
- `generate_bzr_build_profile.py:32` writes "Source: profiles/bzr_2.2.301.json + exu.json" regardless of `--profile`.
- `.gitattributes`: `*.patch` get CRLF in Windows working trees (`text=auto`), which `git apply` then sees as content; mark `*.patch -text` or `eol=lf`. `*.bat` should be `eol=crlf`. `Resource.rc` is UTF-16LE, so git treats it as binary (no diffs); convert to UTF-8 with `#pragma code_page(65001)` or add `working-tree-encoding`.
- Local working tree has CRLF `.sh` files (`install_requirements.sh`, `scripts/*.sh`: `i/lf w/crlf`) because the checkout predates the LF pin (6815088); the index is LF, so CI is unaffected. `git add --renormalize` is not needed; a fresh checkout fixes it.
- `README.md:66` recommends `deploy_linux_proton.sh`; `README.md:41` tells C++ consumers to link "the import library produced by the build" but releases publish no `exu.lib`.
- `README.md:97-102` "Updating for a game patch" omits `python tools/generate_bzr_build_profile.py` (without `--check`, to regenerate) and `validate_hardening.py`, and says "Revalidate ... documented in exu.json" which no tool does (G-6).
- `Workshop/RequireFix.lua:22-24` derives the game dir from `package.cpath` entry 2 and assumes a Steam layout; returns a bogus workshop path on GOG (no Workshop there, so harmless, but the function name promises more).
- `workshop_changenote.txt` last changed 2026-03-17 and describes 1.1-era features; WORKSHOP_RELEASE.md says to update it per publication. Add it to the release checklist check.
- `examples/openshim_coop_sync.lua` calls no `exu.*`; say in its header that it is a stock-Lua helper shipped for OpenShim/EXU co-op missions.
- `Docs/Research/SHIM_EXU_OWNERSHIP_STRATEGY_20260707.md` names `exu.AddUnitCommand`, `RemoveUnitCommand`, `Get/SetTargetPopupMode`, none registered; acceptable for a dated proposal, but ARCHITECTURE's "code wins over Research" rule should be quoted at its top.
- `Docs/Research/KENSHI_WEATHER_PARTICLE_REFERENCE_20260916.md` cites `Docs/FOG_COMPOSITOR_QUALIFICATION_20260908.md`, which is an OpenShim doc; prefix the repo.
- `third_party/ogre-1.10.0-bzr/Compare-Ogre-ABI.ps1:33-36` hard-codes two local MSVC versions before falling back to a recursive `Program Files` search; use `vswhere`.

Could not verify: whether the Debug configuration builds (G-8, building was out of bounds); the Steam exe's `dummynode` address (on-disk `.text` is SteamStub-encrypted, G-1); byte-exact equality of the other 52 Lua files with upstream 5.1.5; whether `Build-Ogre-BZR.ps1` would succeed after 0001 is fixed (the local `ogre-1.10.0` tree is already partially patched).
