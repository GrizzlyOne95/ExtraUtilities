# Material cache groups and owning references

Material lookup and entity/subentity assignment now use both the requested
resource group and material name as cache identity. Autodetect is a separate
requested-group key; cache misses still use Ogre's native lookup and fallback
rules. Reading a subentity's material name no longer seeds a name-only cache
entry with an unknown group.

Native getByName and clone returns now enter the existing retail-aware
RuntimeReference holder. Cache insertion/copy acquires a reference; replacement,
temporary destruction and mission reset release it. The holder uses the
released x86 control-block count at +14h, rather than the SDK's different ABI.
Borrowed SharedPtr views stay trivial; assignment supplies a borrowed view to
the native setter while retaining the caller's owning handle.

Offline validation on 2026-10-27:

- Release x86 DLL build and hardening smoke passed.
- All 42 MSVC host programs passed, including material cache tests.
- The material tests pass with Linux g++ too. Group A/B, missing-group and
  Autodetect isolation, replacement, and four mission-reset cycles are covered.
- The Windows test also loads a synthetic OgreMain.dll from the test output
  directory. Its two-pointer returns and +14h control blocks exercise the
  actual native-return wrappers, owning copy/move, final destruction/free,
  cloning, and borrowed outbound entity/subentity assignment. Four reset
  cycles leave zero fixture blocks; clone/source teardown also balances.
- The full Linux script reached and passed its Python qualification checks,
  then stopped because this WSL environment has no Node.js executable.

The fixture is not the released game DLL. No live-game memory/VRAM measurement,
game launch or deployment occurred. GOG/Steam/Proton/Wine runtime acceptance is
unverified, and these defects are not established causes of the harness GPU
hangs. Cache lifetime remains mission-scoped through ClearMaterialCache.
