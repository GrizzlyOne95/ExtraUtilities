# ExtraUtilities G1 — Improvement Suggestions
*Comparison against VTrider's ExtraUtilities2 (BZCC)*

---

## Context

ExtraUtilities2 targets Battlezone Combat Commander, a different game from our BZR target, but it was written by the same original author (VTrider) and solves the same class of problem — a DLL-based native script extender. Because BZCC is a living game that patches frequently, EXU2 was designed from the ground up for maintainability and clean integration. That environment pressure produced several architectural decisions that G1 would benefit from adopting regardless of how static BZR's game binary is.

---

## 1. Offset Management: hardcoded constants vs. pattern scans

**EXU2 approach:** All memory addresses are expressed as IDA-style byte patterns in `exu2.json`. A tooling pass resolves them at build time and emits `Offsets.h`. There are also per-game-version header files (`Offsets185.h`, `Offsets200.h`, `Offsets204.0.h`, `Offsets204.1.h`) so the right set is selected at compile time.

**G1 current:** `src/BZR.h` contains ~300 lines of hardcoded hex constants (`0x008EAAE0`, `0x004A6CD0`, etc.) that are either manually transcribed from the PDB or arrived at via ad-hoc reverse engineering sessions.

**Why this matters:** The `Patches.h` / BZR shim work we have in progress already relies on having a matched PDB/EXE pair to verify these addresses. Any time BZR ships an update, every constant is a potential regression. Pattern scans survive a relocating binary; hardcoded addresses do not.

**Suggestion:** Add an `exu.json` (or equivalent) that stores at least the most fragile addresses as byte patterns alongside their known hardcoded values. Even if BZR never updates again, this serves as machine-readable documentation of *why* each address is correct, and a future contributor can verify or re-resolve them without re-doing the PDB analysis from scratch.

---

## 2. vcxproj: eliminate external path dependencies

**EXU2 approach:** The vcxproj only references paths inside the repository — `$(ProjectDir)\src` and `$(ProjectDir)\include`. All dependencies are vendored in `lib/` and `include/`. Build works from a fresh checkout with zero machine-specific setup.

**G1 current:** The Release configuration's `AdditionalIncludeDirectories` references `$(ProjectDir)..\ogre-1.10.0\OgreMain\include` and `$(ProjectDir)..\ogre-1.10.0\Components\Overlay\include` — paths that exist on developer machines but not on CI runners or a fresh contributor checkout. This is what caused the first two CI failures today, requiring a sparse Ogre clone workaround in the workflow.

**Suggestion:** Move the Ogre include dependency inside the repo. The `third_party\ogre-1.10.0-bzr` directory is already present; it just doesn't contain the headers. The options, in order of preference:

- Add the required Ogre header subset (`OgreMain/include`, `Components/Overlay/include`) directly to `third_party\ogre-1.10.0-bzr\` and update the vcxproj path to `$(ProjectDir)third_party\ogre-1.10.0-bzr\OgreMain\include`. This removes the CI workaround entirely.
- Alternatively, commit a `build-setup.ps1` script that sets up the sibling directory on first clone, so the dev environment is one command away rather than tacit knowledge.

---

## 3. C++ standard and toolset version

**EXU2:** C++23, toolset v145 (VS2022 17.x latest).

**G1:** C++20, toolset v143.

The functional impact is minimal for our codebase today, but v145 is the current default for new VS2022 projects and C++23 unlocks `std::print`, improved `constexpr`, `std::flat_map`, and `std::expected` — the last of which would be genuinely useful for our many `bool TryX(...)` return patterns. The upgrade is low-risk and mostly a project property change.

---

## 4. Public C++ consumer API

**EXU2:** Ships a clean `include/ExtraUtils.h` public header and `ExtraUtilities2.lib` import library. DLL mission authors can include the header, link the lib with delay-load, call `exu2::ProcessAttach()` / `exu2::ProcessDetach()`, and use the full API. The DLL is properly unloadable. This is documented step-by-step in the README.

**G1:** There is no equivalent. The Lua API is well-exposed through `Definitions/ExtraUtils.lua`, but there is no supported mechanism for another DLL mission to consume G1's API from C++. The `Exports.h` file exists but is internal.

**Suggestion:** Define a stable public header (analogous to EXU2's `include/ExtraUtils.h`) that exposes the subset of G1's API that's useful to native DLL consumers. This is especially relevant given the `Battlezone98Redux_Shim` sibling project — that shim would be a natural first consumer, and having a clean import contract would decouple the two repos properly.

---

## 5. Source file organisation

**EXU2:** 10 source files grouped by broad domain: `Camera.cpp`, `Console.cpp`, `ExtraUtils.cpp`, `Graphics.cpp`, `LuaAPI.cpp`, `Mission.cpp`, `Steam.cpp`, `Terrain.cpp`, `VarSys.cpp`, and `dllmain.cpp`.

**G1:** ~40 source files, one per feature (`ShotConvergence.cpp`, `GlobalTurbo.cpp`, `EngineFlameColor.cpp`, `BulletInitCallback.cpp`, etc.). This made sense when each feature was a novel addition, but the file count is now at a level where onboarding a new contributor means navigating a flat list of 40 items to find where a given piece of game state lives.

**Suggestion:** Consider a light consolidation pass — not a full rewrite, just grouping closely related features. For example: `Patches.cpp` for all inline patches (turbo, flame color, velocity), `Callbacks.cpp` for event hooks (bullet init, bullet hit, scrap), `HUD.cpp` for all HUD and radar state, `Rendering.cpp` for Ogre-adjacent code outside of the dedicated Overlay module. This mirrors how EXU2 thinks about the problem space and makes the project easier to reason about at a glance.

---

## 6. Documentation: README quality and update process

**EXU2 README includes:**
- A version compatibility matrix (which game versions support LuaMission vs DLL Mission)
- Step-by-step setup instructions for Lua consumers and C++ DLL consumers separately
- A documented update process for maintainers when the game patches (extract .def → create .lib → run pattern scans from exu2.json → update Offsets.h → bump version string)
- An explicit anti-vendoring policy explaining why users should depend on the Workshop item rather than copying the DLL, and the consequences of not doing so

**G1 README:** Primarily a feature list and recent-additions changelog. Good at describing *what* EXU does; thin on *how to use it*, *how to build it from scratch*, and *what to do when BZR updates*.

**Suggestions:**
- Add a Usage section that mirrors EXU2's clarity: how to `require()` the DLL from Lua with a minimal working example, and what `Definitions/ExtraUtils.lua` is for.
- Add a Maintenance section describing what needs to change when a new BZR binary ships (which BZR.h addresses to verify, how the Shim PDB workflow feeds into that, and how CI validates the result).
- Adopt an explicit stance on vendoring — should mod authors depend on the Workshop item, or is shipping a copy of `exu.dll` with their mod acceptable? EXU2's reasoning is well-articulated and worth adapting.

---

## 7. GitHub Actions CI (where G1 is actually ahead)

EXU2 has no CI pipeline at all — the Actions tab shows a blank slate. This is presumably a deliberate trade-off given how frequently BZCC patches and how quickly a manual build is expected to follow.

G1 just shipped a working CI pipeline that builds on every `v*` tag and publishes a GitHub Release. That is a genuine advantage worth preserving and expanding. Potential additions:

- **Build-on-PR:** Add a `push: branches: [main]` trigger that runs the build step only (no release) on every push, so regressions in the source are caught before they reach a tag. This is the most common CI pattern and would have caught the `OgreNativeFontBridge.cpp` break well before the first release attempt.
- **Artifact upload for development builds:** Upload `exu.dll` as a workflow artifact on non-tag builds so collaborators can download a build from any commit without needing to build locally.
- **Cache the Ogre header clone:** The sparse Ogre checkout currently re-clones on every run. Wrapping it in `actions/cache` keyed on the commit hash would cut CI time on most runs.

---

## Summary Table

| Area | EXU2 | G1 Current | Suggested Action |
|---|---|---|---|
| Offset management | Pattern scans + versioned headers | Hardcoded constants in BZR.h | Add exu.json with patterns alongside constants |
| vcxproj deps | Fully repo-local | External Ogre path breaks fresh checkout | Move Ogre headers inside repo |
| C++ standard | C++23 | C++20 | Upgrade to C++23 (low risk) |
| Public C++ API | Clean include/ExtraUtils.h + .lib | None | Define stable public header |
| Source organisation | 10 domain-grouped files | ~40 feature-per-file | Light consolidation into domain groups |
| README | Comprehensive with update guide | Feature list only | Add usage and maintenance sections |
| CI/CD | None | Working (just added) | Add PR build trigger and artifact upload |
