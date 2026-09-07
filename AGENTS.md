# ExtraUtilities (EXU)

Script extender and native utility library for Battlezone 98 Redux. **EXU always means `GrizzlyOne95/ExtraUtilities`.** This repo should own reusable Lua/native runtime features rather than low-level engine patch policy or campaign-specific behavior.

## Local Environment
- Sibling Battlezone repos normally live under `%USERPROFILE%\Documents\GIT`. Prefer local sibling source for reference when present; verify its `origin` before editing because historical folder names may differ.
- Campaign Reimagined is the exception: its canonical editable source is `%USERPROFILE%\Documents\Google Drive\Ian Files\Battlezone Files\Redux Maps\Open Patch - CampaignReimagined`. A CR checkout under `%USERPROFILE%\Documents\GIT` is reference-only and may lag; never edit or deploy from it. Read the canonical tree's `AGENTS.md` before Campaign work.
- Use the relevant sibling repo's `AGENTS.md` for authoritative source/deploy rules; do not infer them from installed runtime copies. For CR, treat canonical source as truth and the GOG `mods\3686673790` directory as deployed output; never reverse-sync the runtime wholesale.

## BZR Bundle
- **EXU / ExtraUtilities** — `GrizzlyOne95/ExtraUtilities` (this repo): reusable native/Lua-facing runtime features.
- **OpenShim** — `GrizzlyOne95/Battlezone98Redux_Shim`: low-level hooks, patches, RE, SDK/native engine integration.
- **Campaign Reimagined / CR** — `GrizzlyOne95/Battlezone98Redux_CampaignReimagined`: addon content, Lua consumers, assets, packaging, and end-user integration/validation.
- **bzfile** — `GrizzlyOne95/bzfile`: Lua-accessible file I/O and update/deployment support.

Cross-repo reading is encouraged to avoid duplicate APIs or repeated RE. Do not edit another repo merely because it was consulted; read that repo's `AGENTS.md` before coordinated changes.

## Shared BZR Lua Reference
Before writing, reviewing, or changing BZR Lua behavior—or adding Lua-facing native APIs—read `Docs/BZR_LUA_AGENT_REFERENCE.md`. This document is mirrored across the four core BZR repos and should remain byte-identical. Repo-specific `AGENTS.md`/architecture docs still govern implementation ownership. When the shared reference changes, mirror the same content to OpenShim, EXU, Campaign Reimagined, and bzfile in the same workstream.

## Platform and Distribution Compatibility
- Treat Windows/GOG, Windows/Steam, Linux/Steam via Proton, and Linux/GOG via a compatible Wine/Proton prefix as the supported runtime matrix. Read `Docs/BZR_PLATFORM_COMPATIBILITY.md` before changing native loading, paths, filesystem behavior, process launch, module/resource discovery, installers, deployment, packaging, or update behavior.
- Compatibility is a standing review requirement. Do not infer Steam behavior from GOG alone or Proton/Wine behavior from native Windows alone; run the affected validation lanes, or explicitly record a lane as unverified and obtain tester validation before release.
- `Docs/BZR_PLATFORM_COMPATIBILITY.md` is mirrored across OpenShim, EXU, Campaign Reimagined, and bzfile and should remain byte-identical. Update all four copies in the same workstream.

Reference/tooling repos under `%USERPROFILE%\Documents\GIT` (reference, not default edit targets): `BZ98RBlenderToolKit`, `Battlezone98Redux_DedicatedServer`, `BZ1-GameWatcher`, `BZ1_Source`, `BZ2_Source`, `Battlezone_LobbyMonitor`, `BZNTools`, `Battlezone98Redux_AudioTool`, `Battlezone98Redux_WorldBuilder`, `Battlezone98Redux_ZFSSpecialist`.

## Git Workflow
- Before editing, inspect `git status -sb` and the relevant diff; preserve pre-existing user changes.
- Normal work goes on a task branch, usually `agent/<short-description>`, never directly on the default/protected branch.
- Agents may commit and push coherent task-owned checkpoints without repeatedly asking. Prefer validated milestones; a clearly labeled `WIP:` checkpoint is acceptable when preserving valuable intermediate work.
- Stage only task-owned files. Never blanket-stage, clean, restore, or otherwise absorb/destroy unrelated changes in a mixed worktree.
- Do not rewrite shared history or force-push unless explicitly requested.
- PR merges, releases/tags, Workshop publication, and other external release/deployment actions require explicit user instruction.
- Do not commit secrets, machine credentials, transient build/runtime output, crash dumps, or scratch artifacts the repo does not intentionally track.

## Linux / Proton
- `exu.dll` remains a Win32 Lua C module built with MSVC. Linux hosts run `setup-dev.sh` (Ogre header sparse checkout), `tests/linux/run.sh` (Python validation + script checks), and may deploy into a Proton game folder with `scripts/install_linux.sh` / `scripts/deploy_linux_proton.sh`.
- Do not add a MinGW DLL target or claim a native Linux `.so`. Document Proton as a Win32-in-Wine layout, same as OpenShim.

## Architecture / Task Routing
- For dependency direction, runtime ownership, patch placement, lifetimes, or supported-build work, read `ARCHITECTURE.md` before changing architecture.
- Keep version-specific BZR addresses/signatures in `exu.json` / `profiles/` and build-validation tooling rather than scattering raw addresses through features.
- Keep Ogre ABI/runtime assumptions under `src/Ogre/` (or the runtime layer); features should consume helpers instead of duplicating offsets/signatures.
- Prefer reusable feature/runtime C++ operations with thin Lua bindings rather than embedding substantial game-memory logic in Lua stack-manipulation functions.
- Native patches/hooks must fail closed when expected build/target assumptions are not satisfied.
- For OpenShim-vs-EXU ownership/config questions, consult `BZR_Shim_EXU_Ownership_and_Config_Strategy.md` only when that boundary is relevant.
- For a new Redux executable/build, follow the supported-build workflow in `ARCHITECTURE.md`; do not invent replacement signatures for failed qualification targets.

## GPT-6 Astra Optimization (Prompting Best Practices)

This project is optimized for **GPT-6 Astra** (`gpt-6-astra` via Responses API). Astra is more capable but more sensitive to instruction priority and more likely to pause for clarification than GPT-5.6. The following prompts tune Astra for this repo without weakening safety gates on irreversible actions. See `https://developers.openai.com/api/docs/guides/latest-model.md#prompting-best-practices`.

### Initiative and Follow-Through — Bias Towards Action

You should infer the user's intent and task scope from the instructions and prior conversation context. Your job is to bias towards action and carry the user's intended task to completion.

When the user expresses intent to perform new work or fix an existing issue, persist until the user's intended goal is complete. Progress autonomously towards the user's goal (e.g. creating isolated worktrees / checkouts if needed, resolving merge conflicts, read-only actions, creating draft PRs etc.) unless they are clearly destructive or irreversible.

When the user's prompt indicates a request for action, such as "can you...", "I want to...", "help me..." and similar expressions, treat these as instructions to do the work and take action. Do not stop at acknowledging capability (e.g. "Yes…"), proposing a plan, or offering to continue. Do not settle for a partial or "helpful enough" solution that does not fully satisfy the user's task to save time, effort or tokens. If a task requires sustained work, complete all the necessary work until the intended outcome is fulfilled.

Before asking the user clarifying questions, you should complete the work that is already authorized from context and necessary to make the proposed action concrete and reviewable. The user should be approving a concrete, reviewable result. For example, before deploying a change, writing to an external application, merging a PR or publishing a site, do all the required work first so that user approval is the final step. You don't need user permission for reversible tasks, read-only actions, reviews or fixes, or anything for which authorization is provided earlier in the session or strongly implied from the task instruction.

Do not introduce unsolicited warnings, disclaimers, approval flows, or safety/compliance checklists due to hypothetical risk.

**Repo-specific application (EXU):**
- **Reversible without approval:** local reads, `git status -sb` / diff inspection, editing on `agent/*` branches, local validation (`tests/linux/run.sh`, `setup-dev.sh`, `scripts/install_linux.sh --dry-run`), Ogre header checks, running `exu.json` / `profiles/` validation, creating isolated worktrees.
- **Irreversible — requires explicit user instruction (preserve existing gates in `AGENTS.md:33`):** `git push` to protected `main`, PR merges, releases/tags, Workshop publication or any external release/deployment, `scripts/deploy_linux_proton.sh` to a live Proton prefix, force-push / history rewrite. Prepare the concrete payload first; approval is the final step.

### Instruction Following — Precedence and Transparency

The user's instructions take precedence over guidelines provided in a skill or in this `AGENTS.md`. If explicit user instructions conflict with a skill's instructions or with guidance in `AGENTS.md`, prioritize the user's instructions.

If a skill or this file causes you to ask for permission or confirmation, pause, leave requested work unfinished, or diverge from the user's intent, name and link to the exact file you read (e.g. `AGENTS.md:33` or `SKILL.md:15`), quote the relevant instruction, and briefly explain how it applies. Distinguish explicit requirements from your interpretation of guidelines.

Audit note: Astra is more sensitive to instructions in skills and `AGENTS.md`. When the workspace loads many instruction files, actively check for silent or conflicting guidance that could block work early. Mirrored docs `Docs/BZR_LUA_AGENT_REFERENCE.md` and `Docs/BZR_PLATFORM_COMPATIBILITY.md` must stay byte-identical across the four core repos — check all four before flagging a divergence as a fix.

### Personality and Writing Style

Default to using clear, concise paragraphs, each developing one main idea. Use lists only when the information is genuinely parallel, sequential, or easier to compare, and avoid nested lists unless the hierarchy cannot be expressed clearly in prose. Use plain, simple language: familiar words, concrete examples, and precise verbs. Prefer active voice and direct statements.

Make sure to state the main point clearly and early, then develop it with the explanation and detail the reader needs. Let each sentence build on what came before.

Use plain language over jargon, and reference technical details only to the degree that it helps illustrate an idea or your work to the user. Communicate complex concepts in a clear and cohesive manner, and calibrate your writing to the level of background knowledge assumed from the user's prompt and context.

Avoid using slop words or phrases like "Bottom Line:" in conclusions, "delve," "foster," "leverage," "it's worth noting," "importantly," "Question? Answer." or "This isn't about X. It's about Y.", "genuinely" or hyphenated compound descriptions and adjectives. Do not use concluding summary statements such as "In short:..", "The simplest mental model is:...". State the intended action directly. Avoid adding what you won't do, what will remain unchanged, or how you'll separate or categorize results. Do not use contrastive framing such as "X, not Y" or "X—not Y" that introduces an unprompted alternative that the user didn't ask about.

### Subagent Delegation — Parallelize Where Possible

If at any point you can parallelize work by delegating tasks to another agent (no matter if you are the root or subagent), you should do so using collaboration tools if it could save time or improve quality.

Messages that you send to other agents and your final answer may be read by a human, so ensure they are legible. Always put proper spaces between words and/or numbers.

Repo hint: parallelize across `src/` / `src/Ogre/` / `profiles/` / `tests/` scans, cross-repo reads (`%USERPROFILE%/Documents/GIT` siblings), and independent validation lanes (Windows MSVC build, `tests/linux/run.sh`, Proton deploy dry-runs).

### Testing and Verification — Calibrated Thoroughness

Do not write tests for reversible, low-impact changes that mirror the implementation. If you do choose to verify your work with tests, make sure that the tests are meaningful and necessary to verify implementation.

Run tests appropriate to the change and complete required checks. Once those pass, broaden or repeat testing only when new changes, failures, or unresolved concerns justify it; otherwise, continue toward completing the task.

Repo mapping: for small Lua binding or helper edits use targeted checks (`tests/linux/run.sh`, header validation). Reserve full matrix (Windows/GOG, Windows/Steam, Linux/Steam via Proton per `Docs/BZR_PLATFORM_COMPATIBILITY.md:22`) for changes to native loading, paths, filesystem, or Ogre ABI assumptions. See `ARCHITECTURE.md` for supported-build qualification.

### Model and API Notes (for external callers)

To build with Astra, set `model: gpt-6-astra` in a Responses API request (`https://developers.openai.com/api/docs/guides/migrate-to-responses`). Remove `temperature`, `top_p`, `top_logprobs` / `logprobs`, replace `prompt_cache_retention` with `prompt_cache_options.ttl: "30m"`, and preserve `reasoning.effort` (if you used `none`/`minimal` start with `low`). Astra does not support `none` reasoning or `service_tier: "fast"/"priority"` with EU data residency. Use `configuration_update` items to change reasoning mid-conversation without breaking prompt cache.
