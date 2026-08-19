# ExtraUtilities (EXU)

Script extender and native utility library for Battlezone 98 Redux. **EXU always means `GrizzlyOne95/ExtraUtilities`.** This repo should own reusable Lua/native runtime features rather than low-level engine patch policy or campaign-specific behavior.

## Local Environment
- Sibling Battlezone repos normally live under `%USERPROFILE%\Documents\GIT`. Prefer local sibling source for reference when present; verify its `origin` before editing because historical folder names may differ.
- Use the relevant sibling repo's `AGENTS.md` for authoritative source/deploy rules; do not infer them from installed runtime copies.

## BZR Bundle
- **EXU / ExtraUtilities** — `GrizzlyOne95/ExtraUtilities` (this repo): reusable native/Lua-facing runtime features.
- **OpenShim** — `GrizzlyOne95/Battlezone98Redux_Shim`: low-level hooks, patches, RE, SDK/native engine integration.
- **Campaign Reimagined / CR** — `GrizzlyOne95/Battlezone98Redux_CampaignReimagined`: addon content, Lua consumers, assets, packaging, and end-user integration/validation.
- **bzfile** — `GrizzlyOne95/bzfile`: Lua-accessible file I/O and update/deployment support.

Cross-repo reading is encouraged to avoid duplicate APIs or repeated RE. Do not edit another repo merely because it was consulted; read that repo's `AGENTS.md` before coordinated changes.

Reference/tooling repos under `%USERPROFILE%\Documents\GIT` (reference, not default edit targets): `BZ98RBlenderToolKit`, `Battlezone98Redux_DedicatedServer`, `BZ1-GameWatcher`, `BZ1_Source`, `BZ2_Source`, `Battlezone_LobbyMonitor`, `BZNTools`, `Battlezone98Redux_AudioTool`, `Battlezone98Redux_WorldBuilder`, `Battlezone98Redux_ZFSSpecialist`.

## Git Workflow
- Before editing, inspect `git status -sb` and the relevant diff; preserve pre-existing user changes.
- Normal work goes on a task branch, usually `agent/<short-description>`, never directly on the default/protected branch.
- Agents may commit and push coherent task-owned checkpoints without repeatedly asking. Prefer validated milestones; a clearly labeled `WIP:` checkpoint is acceptable when preserving valuable intermediate work.
- Stage only task-owned files. Never blanket-stage, clean, restore, or otherwise absorb/destroy unrelated changes in a mixed worktree.
- Do not rewrite shared history or force-push unless explicitly requested.
- PR merges, releases/tags, Workshop publication, and other external release/deployment actions require explicit user instruction.
- Do not commit secrets, machine credentials, transient build/runtime output, crash dumps, or scratch artifacts the repo does not intentionally track.

## Architecture / Task Routing
- For dependency direction, runtime ownership, patch placement, lifetimes, or supported-build work, read `ARCHITECTURE.md` before changing architecture.
- Keep version-specific BZR addresses/signatures in `exu.json` / `profiles/` and build-validation tooling rather than scattering raw addresses through features.
- Keep Ogre ABI/runtime assumptions under `src/Ogre/` (or the runtime layer); features should consume helpers instead of duplicating offsets/signatures.
- Prefer reusable feature/runtime C++ operations with thin Lua bindings rather than embedding substantial game-memory logic in Lua stack-manipulation functions.
- Native patches/hooks must fail closed when expected build/target assumptions are not satisfied.
- For OpenShim-vs-EXU ownership/config questions, consult `BZR_Shim_EXU_Ownership_and_Config_Strategy.md` only when that boundary is relevant.
- For a new Redux executable/build, follow the supported-build workflow in `ARCHITECTURE.md`; do not invent replacement signatures for failed qualification targets.
