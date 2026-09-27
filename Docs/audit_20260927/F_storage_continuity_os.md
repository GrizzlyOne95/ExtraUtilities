# Worksheet F: Storage, Continuity, OS/native save, options (2026-09-27)

Reviewer scope: `src/Util/StorageApi.h` (1000), `src/Game/ContinuityApi.h` (1097), `src/Util/OS.cpp` (947) / `OS.h` (33), `src/Util/IO.cpp` (115) / `IO.h` (73), `src/Util/NativeSaveFlag.h` (65), `tests/host/native_save_flag_tests.cpp` (163), `src/Util/PlayOption.cpp/.h` (96/40), `src/Util/SoundOptions.cpp/.h` (199/44), `src/Util/GraphicsOptions.cpp/.h` (59/33), `src/Util/Vec3.h` (70), `src/Util/VectorSpider.h` (42); docs `Docs/PERSISTENCE_AND_CONTINUITY.md` (253), `Definitions/Storage.lua` (80), `Definitions/Continuity.lua` (121), `Docs/BZR_PLATFORM_COMPATIBILITY.md` (87). Commits `abcedd4`, `e11a8ce` read in full. Base: origin/main aec8c0a.

Method notes: every file read end to end. Native-save claims were checked against the on-disk GOG 2.2.301 `battlezone98redux.exe` (`C:\Program Files (x86)\GOG Galaxy\Games\Battlezone 98 Redux`) with pefile+capstone, read-only. The Steam exe on disk carries SteamStub (`.bind`) and its `.text` is encrypted, so the OS.cpp signatures can only be judged against GOG bytes offline. On that GOG exe the 54-byte `SAVE_GAME_SIGNATURE` matches exactly once, at 0x004FD190. It has two callers: 0x004FBF48, inside the mission-save wrapper FUN_004FBE90, and 0x004FDD77, inside FUN_004FDC80. FUN_004FDC80 is the function `ResolveNativeSaveShellGame` finds.

Premise correction for Q4: `exu.continuity` does **not** go through native saves. It has no save/load hook, reads no native save structures and uses no EXU-owned Lua globals. It calls public BZR Lua globals (`AllObjects`, `GetOdf`, `GetTransform`, `BuildObject`, ...) through `lua_pcall`, builds plain Lua tables from the results, and expects the mission to persist them with `exu.storage`. Cross-version behaviour is therefore a question for the storage format (F-14), plus the snapshot's own `formatVersion`, which is never checked (F-17). OS.cpp contains no Steam ID code (that lives in `src/Game/Steam.cpp`) and no pause-menu code (`IO.cpp` forwards to `Game/game_state.h`, out of scope).

## 1. Section map

### src/Util/StorageApi.h (1000), status: production
| Lines | What |
|---|---|
| 1-45 | License, design comment, includes (`Windows.h`, `lua.hpp`, STL) |
| 46-117 | `Detail` constants (16 MiB file, 1 MiB string, 100000 entries, depth 32), tag enums, 24-byte `Header`, `Paths`, `ReadStatus`, encode/decode contexts |
| 119-234 | `SetError`, `IsSafeNamespace`, attribute helpers, `EnsureDirectory`, `GetStoragePaths` (`%LOCALAPPDATA%\Battlezone 98 Redux\ExtraUtilities\Storage\<ns>.exudata[.bak/.tmp]`) |
| 236-285 | Bitwise CRC-32 and the bounded append helpers |
| 287-418 | Encoder: `EncodeTable` (identity-set cycle check, `lua_next`) and `EncodeValue` |
| 420-551 | Decoder: `ReadPod`, `DecodeString`, recursive `DecodeValue` |
| 553-627 | Win32 `WriteWholeFile` (CREATE_ALWAYS, write-through, flush) and `ReadWholeFile` (size-capped) |
| 629-712 | `BuildFile` (header + CRC) and `DecodeFile` (magic, version, length, CRC, trailing-byte checks) |
| 714-741 | `SaveAtomic`: tmp, then CopyFile primary to .bak, then MoveFileEx(REPLACE_EXISTING, WRITE_THROUGH) |
| 743-790 | Result/meta pushers and `CheckNamespace` |
| 793-975 | Lua bindings `Save`, `Load` (primary, then backup, then "new"), `Exists`, `Delete`, `GetInfo`, `GetCapabilities` |
| 977-999 | `Install`: creates `exu.storage` |

### src/Game/ContinuityApi.h (1097), status: production
| Lines | What |
|---|---|
| 1-93 | Design comment, constants (default 4096 objects, hard cap 10000), `MatrixData`, capture/restore options |
| 95-272 | Handle push/test helpers and `TryCall*` pcall wrappers around BZR globals; `IsPlayerHandle` compares against `GetPlayerHandle()` and `GetPlayerHandle(team)` |
| 274-409 | Matrix read (`TryReadNumberField` x12), `PushPlainMatrix`, `PushNativeMatrix` (`SetMatrix` with 12 args), `TryBuildObject`, `TrySetHandleNumber` |
| 411-466 | `PushObjectDescriptor` (odf/team/transform/health/ammo/class/isPlayer/isPerson) |
| 468-573 | `ReadCaptureOptions`, `TryGetOffset`, `ReadRestoreOptions` |
| 575-663 | `ResolveOdf` (odfMap) and `ReadDescriptor` |
| 665-706 | Report and snapshot header builders |
| 708-890 | Capture: filter, one handle, handle table, `AllObjects()` iterator protocol |
| 892-917 | `CanRestoreWorld` (IsNetGame/IsHosting guard) |
| 920-1047 | Bindings `CaptureObject`, `CaptureObjects`, `CaptureWorld`, `Restore` |
| 1049-1096 | `GetCapabilities`, `Install` (creates `exu.continuity`) |

### src/Util/OS.cpp (947), status: production (`SaveGame` is the only substantial piece)
| Lines | What |
|---|---|
| 1-61 | Includes, fn typedefs, `SAVE_GAME_SIGNATURE` (54 bytes, build-specific, not in exu.json) |
| 63-82 | `LogNativeSave`: std::format, mutex, reset-check, then open, append and close `logs/exu_native_save.log` per line |
| 84-141 | Path helpers: exe dir via `GetModuleFileNameA`, `BuildSlotSavePath`, `NormalizeSavePath`, `EnsureSaveParentDirectory` |
| 143-255 | Trim, sanitize and hex encode/decode helpers |
| 257-365 | `RewriteTextSaveDescription`: in-place truncating rewrite of the saved file |
| 367-483 | PE section walk, `FindPattern`, `ResolveNativeSaveGame` (cached only on success) |
| 485-526 | `ResolveMissionSaveFlag`: decodes 0x009173B7 from SaveGame+40 via `NativeSaveFlag.h` |
| 528-665 | SaveShellGame discovery: every `E8` in every exec section that targets SaveGame, then prolog backtrack, then `IsSaveShellGameCandidate` (slot 1..10 compare and a pattern embedding 0x008E86D8) |
| 667-729 | SEH-guarded invokers (`InvokeNativeNormalSaveGame` saves and restores missionSave; `InvokeNativeSaveShellGame`) |
| 732-751 | `MessageBox`, `GetScreenResolution` |
| 753-944 | `SaveGame` Lua binding: argument parsing, then SaveShellGame path (slot + description) or direct SaveGame path, plus the description rewrite |

## 2. Findings

| ID | [Sev/Conf] | file:line | Finding | Why it matters | Suggested fix | How verified |
|---|---|---|---|---|---|---|
| F-1 | [High/High] | src/Util/OS.cpp:818-836 | `exu.SaveGame(slot, description)` passes the Lua description to native SaveShellGame (FUN_004FDC80) with no length limit. FUN_004FDC80 copies it byte by byte, unbounded, into the global `saveGameDesc` buffer at 0x008E86D8. That buffer is 0x100 bytes: SaveGame writes the field with `push 0x100; push 0x8e86d8` at 0x004FD54A, and so does 0x004FCAB6. | `exu.SaveGame(1, string.rep("x", 300))` overwrites the .data globals after 0x008E87D8. Any mission script can do this, and the result is silent memory corruption followed by a crash later or a bad save. | Clamp `description` to 255 bytes (or the UI's own limit) before calling the native function. Do it in the binding, next to `TrimAsciiWhitespace`. | Disassembly of GOG 0x004FDC80..0x004FDCCC: a `mov [ebp-0x6c],0x8e86d8` / byte-copy loop until NUL. Field-size pushes found by scanning every reference to 0x008E86D8 (5 refs). Resolver target confirmed: the only non-mission caller of SaveGame is at 0x004FDD77 inside FUN_004FDC80, whose slot compare `cmp [ebp+8],1 / cmp [ebp+8],0xa` matches `SLOT_RANGE_PATTERN`. |
| F-2 | [Med/Med] | src/Util/StorageApi.h:308-366, 493-545; src/Game/ContinuityApi.h:353-365 | Recursive encode and decode push 2 stack slots per nesting level (key+value, or table+key) with no `lua_checkstack`. In this Lua 5.1, `lua_pushnil`, `lua_next` and `lua_createtable` use `api_incr_top`, which never grows the stack. `api_check` is an assert-only no-op (llimits.h:58), and a C function is only guaranteed `LUA_MINSTACK` = 20 slots plus `EXTRA_STACK` = 5. A legal 16-deep Save needs about 35 slots. A crafted or old-format 33-deep file needs about 66 on Load. `Restore`'s `PushNativeMatrix` peaks at slot 22. | Writing past `L->stack + stacksize` corrupts the Lua heap block. The exposure is highest from coroutines, whose stacks start at 45 slots (`BASIC_STACK_SIZE` 40 + 5). No grep hit for `lua_checkstack`/`luaL_checkstack` anywhere in `src/`. | Call `lua_checkstack(L, 4)` at entry of `EncodeTable` and of the Table case of `DecodeValue`, and fail with "nesting too deep" if it returns 0. Call `luaL_checkstack(L, 16, ...)` before `PushNativeMatrix`. | Read Lua5.1-BZR/src/lapi.c:421-426, 578-584, 973-983; lstate.h:29,34; lua.h:87. Counted pushes along the recursion. |
| F-3 | [Med/Med] | src/Util/PlayOption.h:29; PlayOption.cpp:36-41, 69-74, 88-93 | `playOption` is a `Scanner` with the default `Restore::ENABLED` over the packed profile-options byte `*(0x0094672C)+0x30`. The byte is captured when exu.dll loads and written back unconditionally in `~Scanner` (src/Scanner.h:137-146) when the DLL unloads. Lua 5.1 `FreeLibrary`s C modules at `lua_close`, and EXU does not pin itself (no `GET_MODULE_HANDLE_EX_FLAG_PIN` in src/). So any TLI, auto-level or reverse-mouse change the player makes in the in-mission options menu, or any other bit in that byte, is reverted when the mission ends, even if no script called `Set*`. Separately, `Set*` dereference `playOption.Get()` without a null check and without the `IsReadableRange` check that `Write` has. | The player's option changes are silently lost. A null pointer-chain result (profile pointer unset at load) is an immediate access violation. | Use `Restore::DISABLED`. Record only the bits a script actually changed and restore just those bits on the Lua-state closing boundary, not in a static destructor. Null-check `Get()` and return a Lua error. | Read Scanner.h ctor/dtor; grepped for all `userProfilePtr` users (PlayOption.h:29 only); dllmain.cpp has no pin. Whether the menu writes the same byte is inferred from exu.json's description, not traced (hence Med confidence). |
| F-4 | [Med/High] | src/Util/IO.cpp:44 | `GetAsyncKeyState(vKey) ? true : false` also counts the least-significant bit ("pressed since the last query"), which Microsoft documents as unreliable. It also ignores focus. | `exu.GetGameKey` is documented as "whether a key is held" (Definitions/ExtraUtils.lua:1915) but can return true once for a key that has already been released, and returns true for keys typed into another window while alt-tabbed. | `(GetAsyncKeyState(vKey) & 0x8000) != 0`, gated on the game window being foreground. | Read the code and the docs line. |
| F-5 | [Med/Med] | src/Util/OS.cpp:738 | `MessageBoxA(0, ..., MB_APPLMODAL)` runs on the game thread from inside a Lua callback. With a NULL owner, MB_APPLMODAL disables no window. The box's nested modal loop keeps dispatching messages to the game's WndProc (activate, size, paint, input) while the mission Lua frame and any EXU hook frame are on the stack. In exclusive fullscreen the box can sit behind the D3D surface and look like a hang. | Re-entrancy into engine and window code mid-callback. Behaviour differs under Proton/Wine. | Keep it debug-only, or use the game HWND as owner with MB_TASKMODAL|MB_TOPMOST. Better: log plus an on-screen message. | Read the call; grepped for callers (exuExports only, luaexport.cpp:834). |
| F-6 | [Med/High] | src/Util/OS.cpp:56-61, 562-569, 452-483, 600-665 | Build-specific knowledge lives in feature code instead of `exu.json`/`profiles/`: `SAVE_GAME_SIGNATURE`, `SLOT_RANGE_PATTERN`, and `DESCRIPTION_BUFFER_PATTERN`, which hard-codes 0x008E86D8 as bytes `D8 86 8E 00`. The derived addresses 0x004FD190, 0x004FDC80, 0x009173B7 and 0x008E86D8 also appear nowhere in exu.json (grep). There is no ambiguity check (first match wins) and no `BuildValidation` gate. A failed resolution is not cached, so every failing call rescans all executable sections byte by byte, plus a full `E8` sweep for SaveShellGame. | Violates ARCHITECTURE.md §Ownership: `qualify_bzr_build.py` cannot report drift in these targets. Repeated misses cost tens of ms per call on the game thread. | Move the signature(s) to exu.json with provenance and resolve through `SignatureResolver`, requiring uniqueness. Cache a negative result per DLL load. Add the catalog entries to the profile. | grep of exu.json and profiles for each hex; read the resolvers. |
| F-7 | [Low/High] | src/Util/OS.cpp:667-711, 820-866 | The e11a8ce fix is incomplete. (a) The slot+description path goes through FUN_004FDC80, which stores `mov byte [0x9173B7],0` at 0x004FDD38 and never restores it, so the "save during the mission-save modal window" downgrade that e11a8ce guards against still exists on that path. (b) Both stock wrappers set byte 0x009173B6 to `[0x008EAAB4]!=0` before SaveGame and clear it afterwards (0x004FDD5B/0x004FDD8A and 0x004FBF39/0x004FBF56). 0x008EAAB4 is set from the `binarysave` command-line switch (0x007D55BB, string at 0x008A0E94). The field writers (e.g. 0x004CCD86) branch on 0x009173B6. The direct path leaves it at rest (0), so EXU direct saves are always text saves and ignore `-binarysave`. | (a) is only as reachable as the scenario e11a8ce describes, which is unproven: Lua may not tick inside that dialog loop. (b) is a behavioural difference from stock. The description rewrite only works on text saves, so it happens to benefit. | (a) Save and restore missionSave around `InvokeNativeSaveShellGame` too. (b) Either mirror the wrapper (derive 0x009173B6 the same way as missionSave, from the qualified wrapper) or document it. | Disassembly of FUN_004FDC80 and the FUN_004FBE90 tail; xrefs to 0x009173B6/0x008EAAB4. |
| F-8 | [Low/Med] | src/Util/OS.cpp:84-128, 757-772 | Save path policy. (a) Any absolute or `..` path from Lua is accepted, followed by `create_directories`, a native write and a truncating rewrite (F-9) anywhere writable. Contrast storage's sandbox and PERSISTENCE doc line 70. (b) ANSI-only: `GetModuleFileNameA` and `path::string()` fail for install paths not representable in the ACP. (c) The slot base comes from the exe directory, but the engine builds its slot path from 0x02CEEFE0, copied from `_getcwd()` at startup (0x00619001 into 0x02CF1000). They diverge when CWD is not the install dir (custom launchers, some Proton or Wine shortcuts), and then EXU slot saves land where the in-game Load menu does not look. | Correctness and portability (BZR_PLATFORM_COMPATIBILITY §2). Whether BZR mission Lua already has `io` is unverified; if it does, (a) is not a new capability. | Restrict string paths to the save directory. Use W APIs or `GetModuleFileNameW`. For slots, prefer the stock wrapper or the engine's save-dir global. | Disassembly of the `_getcwd` call site and the 0x02CEEFE0 xrefs; code read. |
| F-9 | [Low/High] | src/Util/OS.cpp:349-361 | `RewriteTextSaveDescription` reopens the save it just wrote with `ios::trunc` and rewrites it in place. It also patches the first `saveGameDesc` substring found anywhere in the file. | A crash or full disk mid-rewrite destroys the save the user just made. | Write to `<file>.tmp`, then `MoveFileExA(REPLACE_EXISTING)` (StorageApi already has this). Anchor the match to line start. | Code read. |
| F-10 | [Low/Med] | src/Util/StorageApi.h:201-233 | The storage root comes from `GetEnvironmentVariableA("LOCALAPPDATA")` plus A-suffixed file APIs. Profile paths not representable in the ACP (e.g. a CJK or Cyrillic user name on a Western ACP; under Wine the Linux user name) turn into `?` and every call fails. The environment variable can also be overridden by a launcher. Under Proton the root is per-prefix (`compatdata/301650/pfx/...`), so it is lost if the prefix is deleted, and it is not shared between GOG-Wine and Steam-Proton. | Fails closed (returns an error, no corruption), but the whole API is unusable for those users. | `SHGetKnownFolderPath(FOLDERID_LocalAppData)` plus W APIs, UTF-8 at the Lua boundary. Document the per-prefix location. | Code read against the platform doc. |
| F-11 | [Low/Med] | src/Util/StorageApi.h:723-731 | Backup rotation copies the current primary over `.bak` without validating it. If the primary is corrupt (Load already fell back to the backup) and the following `MoveFileExA` fails, both generations are bad. | A narrow data-loss path in the one place the design promises recovery. | Rotate to `.bak` only when the primary decodes (or header+CRC check), otherwise keep the existing backup. | Code read of `SaveAtomic` and `Load`. |
| F-12 | [Low/High] | src/Util/StorageApi.h:293, 412, 359 | Depth accounting is asymmetric. `EncodeValue(Table)` passes `depth+1` to `EncodeTable`, which passes `depth+1` again to child values, so Save rejects at 17 nested tables. `GetCapabilities().maxDepth` reports 32, and the decoder accepts 33 levels. | Docs and capabilities drift. The decoder limit is looser than anything Save can produce, which widens F-2. | Increment once per table, and make the decoder limit equal the encoder limit. | Traced the recursion by hand. |
| F-13 | [Low/Med] | src/Util/StorageApi.h:127-155 | `IsSafeNamespace` accepts DOS device names (`CON`, `NUL`, `AUX`, `PRN`, `COM1`-`9`, `LPT1`-`9`). `COM1.exudata.tmp` opens a device on Windows 10. Namespaces are also case-insensitive on NTFS and on Wine's drive_c, so `Foo` and `foo` alias. | Odd failures. Opening a serial device on Save or Load. | Reject reserved stems. Lower-case namespaces or document the aliasing. | Code read. |
| F-14 | [Low/Med] | src/Util/StorageApi.h:679-683, 835-859 | Any `formatVersion != 1` counts as corrupt. After a future format bump, an older EXU's Load falls back to `.bak`. If that backup is still v1 it returns stale data with `recovered=true`, and the next Save rotates the newer primary into `.bak` and overwrites the primary. | Silent downgrade and data loss across EXU versions (future). | Report a newer format as a distinct error (`source="unsupported"`, value nil) that neither falls back nor allows Save to rotate. | Code read. |
| F-15 | [Low/High] | src/PublicAPI.cpp:35,41 | Both header-only APIs (about 2100 lines) are included from exactly one TU. `inline` keeps them ODR-safe, but header-only buys nothing: it pulls `Windows.h`/STL/`lua.hpp` into PublicAPI.cpp, recompiles everything on any PublicAPI edit, and neither header (nor `NativeSaveFlag.h`) is listed in `ExtraUtilities.vcxproj`. | Maintainability and build time. | Move them to `StorageApi.cpp`/`ContinuityApi.cpp` with an `Install(lua_State*)` declaration header. Keep only pure codec pieces in headers if host tests want them. | `git grep StorageApi\|ContinuityApi` (only PublicAPI.cpp); vcxproj grep. |
| F-16 | [Low/High] | see §2a | C++ objects are live across raising Lua calls (longjmp skips destructors under /EHsc). Every site is a leak only (no locks or patch guards held), but some are unbounded in size (a caller-supplied namespace or path string). | Memory leak per bad call. | Validate before constructing std::string (check the `const char*`), or wrap bindings in a pcall trampoline. | Listed individually below. |
| F-17 | [Low/Med] | src/Game/ContinuityApi.h:946-1047, 599-663 | `Restore` never checks `snapshot.formatVersion`. `team` is `lua_tointeger` of arbitrary persisted data with no 0..15 range check; `teamOverride` is unchecked too. Transforms only need to be finite (1e300 becomes inf once narrowed to float by `SetMatrix`). The returned `handles[i]` do not map to `objects[i]` (skips compact the list, and `lua_next` order is used). | Snapshots come from disk or user edits. An out-of-range team reaches `BuildObject`, and its engine-side handling is unverified. | Reject unknown `formatVersion`, clamp team to 0..15, bound positions, and return a source-index-keyed map. | Code read. |
| F-18 | [Low/Med] | src/Util/PlayOption.cpp:51-56 | `SetDifficulty` casts any integer to `uint8_t` and writes it to 0x025CFA1C with no 0..4 range check (exu.json: "0=Very Easy .. 4=Very Hard"). | Engine difficulty tables may be indexed with an out-of-range value (unverified). | `luaL_argcheck(L, v >= 0 && v <= 4, ...)`. | Code read and exu.json. |
| F-19 | [Low/High] | tests/host/native_save_flag_tests.cpp | The test compiles the **production** `Util/NativeSaveFlag.h` (not a copy), and run.sh:79 globs it. It covers only the operand decode and the 0/1 plausibility check. It does not cover the save/restore logic that e11a8ce actually changed (`InvokeNativeNormalSaveGame`), the SaveShellGame path, or that `SAVE_GAME_SIGNATURE` really pins bytes 37..39 as literals (the signature lives in OS.cpp's anonymous namespace). StorageApi's codec, which has no Win32 dependency in `Detail::Crc32`/header/tag logic, has no host test at all. | Regression risk. | Move the signature bytes into `NativeSaveFlag.h` and assert them against `RealPrologue()`. Template the invoke on a callable so the host lane can test restore-on-success and restore-on-throw. Factor the storage codec behind an interface so it can be host-tested. | Read the test, run.sh:71-88, OS.cpp:56-61. I checked by hand that signature indices 37..39 are `0x0F,0xB6,0x05`, and that the GOG bytes at 0x004FD190 match `RealPrologue()`: the decode yields 0x009173B7, and the same address is the target of `mov byte [..],0` at 0x004FDD38. |

### High/Med quotes

F-1 (OS.cpp:818-836, and the native side it feeds):
```cpp
description = TrimAsciiWhitespace(description);
if (slot != 0 && !description.empty())
{ ...
    const bool saved = InvokeNativeSaveShellGame(saveShellGame, slot, description.c_str(), exceptionCode);
```
```
0x4fdc99 mov dword ptr [ebp-0x6c], 0x8e86d8     ; dst = saveGameDesc (0x100 bytes)
0x4fdca6 mov edx,[ebp-0x70] / mov al,[edx] / mov [ecx],dl / ... / cmp byte [ebp-0x65],0 / jne 0x4fdca6
```

F-2 (StorageApi.h:308-309, 495):
```cpp
lua_pushnil(L);
while (lua_next(L, absIndex) != 0)   // + recursive EncodeValue per nested table; no lua_checkstack
...
lua_newtable(L);                     // DecodeValue Table case, recursive
```

F-3 (PlayOption.h:29, PlayOption.cpp:36-37; Scanner.h:137-143):
```cpp
inline Scanner playOption((uint8_t*)BZR::PlayOption::userProfilePtr, { BZR::PlayOption::playOptionOffset }); // Restore::ENABLED
uint8_t* p_playOption = playOption.Get();
*p_playOption &= ~(1 << 4);          // no null check
~Scanner() { if (m_address && m_restoreData == Restore::ENABLED && IsReadableRange(...)) *m_address = m_originalData; ... }
```

F-4 (IO.cpp:44):
```cpp
lua_pushboolean(L, GetAsyncKeyState(vKey) ? true : false);
```

F-5 (OS.cpp:738):
```cpp
MessageBoxA(0, message, "Extra Utilities", MB_OK | MB_APPLMODAL);
```

F-6 (OS.cpp:567-569):
```cpp
constexpr std::array<int, 7> DESCRIPTION_BUFFER_PATTERN = {
    0xC7, 0x45, -1, 0xD8, 0x86, 0x8E, 0x00      // 0x008E86D8 hard-coded
};
```

### 2a. Lua error-path sites (Q7; all leak-only)
- StorageApi.h:784-787: `std::string name` is built before `luaL_argerror`, so it leaks a caller-sized string, and only when the name is invalid.
- StorageApi.h:795-801 (`Save`): `name` is live across `luaL_checkinteger(L,3)` and `luaL_argerror(L,3)`.
- StorageApi.h:811-816, 833-859: the vectors hold up to 16 MiB each and are live across `lua_push*`/`lua_newtable`/`lua_settable` in `DecodeFile`, which can only raise on out-of-memory. `std::bad_alloc` from a 16 MiB `resize`/`insert` in a fragmented 32-bit address space is **not caught** in any binding, and would terminate the game.
- ContinuityApi.h:413-455 (`PushObjectDescriptor`): `odf` and `text` are live across `lua_getfield` on the `GetTransform` userdata (its `__index` can raise) and across `lua_setfield`.
- ContinuityApi.h:587-595 (`ResolveOdf`): `mapped` and `original` are live across `lua_getfield` on a user-supplied `odfMap`, where a metatable can raise. `original.c_str()` also truncates at an embedded NUL.
- ContinuityApi.h:951, 995-1003 (`Restore`): `authorityError` and the per-iteration `odf` are live across `lua_getfield` on the user snapshot tables (metatables can raise).
- ContinuityApi.h:806, 850: the error strings are only live across `lua_pushlstring` (out-of-memory only).
- OS.cpp:755-814 (`SaveGame`): `filename` (heap-allocated past 15 chars) and `description` are live across `luaL_error` at 799 and 814. At 759, 762 and 770 `filename` is still empty (SSO), so nothing leaks there.
- IO.cpp:26-38 (`GetGameKey`): `std::string key` is live across `luaL_argerror`.
- No C++ objects are live across raises in PlayOption, SoundOptions, GraphicsOptions or `OS::MessageBox`. Its `lua_call` of `tostring` can raise, but only POD is in scope.
- No `FILE*` or `ifstream` is held across any Lua call: every file operation in scope completes before the results are pushed.

## 3. Dead / unreferenced code
- `Detail::Paths::directory` (StorageApi.h:86, assigned at 229) is never read. Verified with grep of `\.directory` in src/.
- Commented-out sfx/voice API: SoundOptions.h:31-32 and 40-43, SoundOptions.cpp:166-198, luaexport.cpp:936-939. The only remaining users of `BZR::SoundOptions::soundStruct2/sfxOffset/voiceOffset` (bzr.h:497-500) are these comments (grep over src/ and include/).
- Cross-scope note: `src/Patches/WeaponConvergenceMath.h`, the only other includer of `Util/Vec3.h`, is itself included by nothing (`git grep WeaponConvergenceMath`). Flagging it for the patches reviewer.

Count: 3 items in scope.

## 4. Raw address census
Regex for `0x00[4-9A-F]xxxxx` / `0x0[12]xxxxxx` over the in-scope files: **0 in production files**. The only hits are in `tests/host/native_save_flag_tests.cpp`, 3 in total: line 50 (comment 0x004FD190), line 52 (comment 0x009173B7), and line 68 (`0x009173B7u`, compared). Addresses that feature code in scope reaches without a literal:

| Address | Where | Use | In exu.json? |
|---|---|---|---|
| 0x008E86D8 | OS.cpp:568 (as bytes in a pattern) | byte-compared (and written by the native callee, F-1) | **no** |
| 0x004FD190 (SaveGame) | resolved by signature at OS.cpp:452 | **CALLED** | **no** |
| 0x004FDC80 (SaveShellGame) | resolved heuristically at OS.cpp:600 | **CALLED** | **no** |
| 0x009173B7 (missionSave) | decoded at OS.cpp:506 | **WRITTEN** (and restored) | **no** |
| 0x0094672C (+0x30) | PlayOption.h:29 via bzr.h:476 | **WRITTEN** (raw pointer; restored at unload) | yes (PlayOption.userProfilePtr; alias note) |
| 0x0094672C (+0x2A) | SoundOptions.h:30 | read | yes (SoundOptions.soundStruct1) |
| 0x025CFA1C | PlayOption.h:31 | **WRITTEN** (`SetDifficulty`; restored at unload) | yes |
| 0x009183B8 | GraphicsOptions.h:28 | read | yes |
| 0x008E77A8 | GraphicsOptions.h:29 | read (`/5`; this is actually `Radar::radarLeft`, see §8) | yes (alias noted) |
| 0x008EAAE0 (`mainCam`) | GraphicsOptions.cpp:36 | read | yes |
| 0x009C915C | reached by VectorSpider only through Ordnance.cpp | read | yes |

None of the exu.json addresses above appear in `profiles/bzr_2.2.301.json`, so they are catalogued but not qualified.

## 5. Lifetime / ownership notes
- **Process/DLL lifetime (effectively per Lua state, because exu.dll is unpinned and Lua 5.1 FreeLibrarys it at `lua_close`):** the `Scanner` globals in PlayOption, SoundOptions and GraphicsOptions resolve their pointer chains and call `VirtualProtect(PAGE_EXECUTE_READWRITE)` on the target page during CRT static init, which runs inside `DllMain` under the loader lock (contrary to dllmain.cpp's "keep loader-lock work minimal"). That page is a **heap** page for the profile chain, and it stays RWX until unload. `playOption` and `difficulty` write their captured values back from static destructors (F-3). Also at this lifetime: the OS.cpp caches (`ResolveNativeSaveGame`, `ResolveNativeSaveShellGame`, `ResolveMissionSaveFlag` fn and flag pointers, which are stable exe addresses, fine), `g_nativeSaveLogMutex`, and `IO::keyMap`.
- **Temporary-patch lifetime:** the missionSave byte is saved, zeroed and restored around a direct native save, SEH-guarded, and restored on the SEH path (OS.cpp:691-710). No Lua call happens inside that window, so a Lua error cannot skip the restore. One exception, unverified: if native SaveGame re-enters the mission's Lua `Save()` callback unprotected and it raises, the resulting longjmp passes through the `__try` without running `__except`, and missionSave stays 0.
- **Lua-state / mission:** StorageApi and ContinuityApi are stateless per call. They keep no registry references and no cached handles, so nothing needs resetting at `HandleLuaStateClosing`. `Install` runs on every attach (PublicAPI.cpp:114-115).
- **Disk:** storage files persist across processes, per Wine prefix under Linux (F-10).

## 6. Performance notes
- `exu.storage.Save`/`Load` are synchronous on the game thread: write-through + `FlushFileBuffers` + `CopyFileA` + `MoveFileExA`, a bitwise CRC (8 iterations per byte, about 100M+ ops at the 16 MiB cap, likely ~100 ms or more; reasoned, not measured), and a double buffer copy (`payload` then `fileBytes`). This is fine for KB-sized careers. Use a table-driven CRC and build the header in place.
- `CaptureWorld`/`CaptureObjects` make roughly 14 `lua_pcall`s per object into globals. `IsPlayerHandle` runs twice per object (in `ShouldCaptureHandle` and again in `PushObjectDescriptor`), which means 4 `GetPlayerHandle` calls, and `GetTeamNum` is called twice. At 4096-10000 objects that is 60k-140k pcalls in one frame, a visible hitch. Resolve the player handles once per capture.
- OS.cpp resolvers: the first SaveShellGame resolve walks every byte of every executable section for `E8` (several MB). Failures are never cached (F-6). `LogNativeSave` does `GetModuleFileNameA`, `CreateDirectoryA`, a mutex, a set lookup and an ofstream open/close per line, about 5 lines per save. Acceptable, since it is per save and not per frame.
- Option getters/setters call `IsReadableRange` (VirtualQuery) per `Read`/`Write`. They are not on hot paths.

## 7. Patterns worth keeping
- StorageApi's on-disk contract: magic + format version + explicit length + CRC, strict trailing-byte rejection, no silent reset to an empty table (a corrupt file returns nil), tmp → `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` (atomic on NTFS and a POSIX `rename` under Wine), and a one-generation backup. Namespaces are strict file stems (no separators and no leading `.`, so no traversal).
- The serializer rejects functions, userdata, threads and lightuserdata, non-finite numbers and cycles (identity set on `lua_topointer`). It reads numeric keys with `lua_tonumber`, never `lua_tolstring`, so `lua_next` is not corrupted. Strings are length-prefixed and binary-safe. The decoder bounds every read against `size - offset` (no overflow), caps strings, entries and depth, and validates every tag, boolean and NaN.
- ContinuityApi calls **every** mission-visible global through `lua_pcall` and restores `lua_settop` on every path. It uses only documented BZR calls and keeps no engine pointers. It has an explicit MP host guard, and all of its limits (`maxObjects`, hard cap) are checked.
- NativeSaveFlag: the build-specific decode is derived from an already-qualified function, not from a second address table. It is pure, `noexcept`, fails closed on zero, and is host-tested against real bytes. The direct-save invoker restores the prior value under SEH.
- SoundOptions' OpenShim bridge fails closed per export and logs once per missing export.

## 8. Low-severity items
- OS.cpp:789: `saveType` is any `lua_tointeger` value passed to native SaveGame, which stores it as a field (0x004FD4FB). Stock callers always pass 0. Range-check it or drop it from the public API.
- OS.cpp:742-751: `GetScreenResolution` returns primary-monitor `GetSystemMetrics` values, which are DPI-virtualized if BZR is DPI-unaware and are not the game's monitor. The docs say "screen resolution for the local user", which is accurate but easily misread as the game resolution.
- GraphicsOptions.cpp:50-57: `GetUIScaling` returns `Radar::radarLeft / 5`. 0x008E77A8 is the radar panel's left pixel (exu.json notes the alias). It is a proxy, 0 before the first HUD layout, and depends on Radar.cpp:204 keeping the scale-1 left value.
- GraphicsOptions.cpp:36-42: `mainCam.Get()` is dereferenced with no null check (the Scanner returns nullptr when the readability check fails).
- VectorSpider.h:27-41: no readability or `begin <= end` validation, so it can loop forever or read out of bounds on a corrupt vector. The triple-pointer cast only works because callers pass `&constexpr_address`, which is obscure. Replace with an explicit `(begin, end)` read of a validated `std::vector` header.
- Vec3.h: no `#pragma once`/include guard, unlike every other header. It is included from bzr.h and from WeaponConvergenceMath.h, and is listed as `ClCompile` in ExtraUtilities.vcxproj:183, as is OS.h at :170. `Normalize()` divides by a zero length.
- IO.h:29-32: `#pragma warning(disable: 4244)` in a header silences the warning for every includer in Debug.
- SoundOptions.cpp:35-60: per-export "logged" flags use a strcmp chain. A `static bool` inside a per-export helper would be simpler.
- ContinuityApi.h:838-842: `truncated=true` is set when exactly `maxObjects` objects exist, even if the iterator would have ended there. `IsHandleValue` accepts full userdata, which `ToHandle` then turns into a block pointer.
- ContinuityApi.h:474-503 and 527-573: `lua_getfield` on a non-table `offset` (e.g. a number) raises an unhelpful "attempt to index" error instead of the argerror.
- Docs drift: PERSISTENCE_AND_CONTINUITY.md:138 vs :186-193. The captured `odf` is shown as `"avfact.odf"`, but the `odfMap` example uses the key `avfact` while also saying "use the exact ODF string". Storage docs say "excessive nesting" without the effective limit (F-12). `exu.SaveGame` docs (ExtraUtils.lua:2059-2070) mention no description length limit (F-1), the `-binarysave` difference (F-7), or that the slot path is resolved from the exe directory rather than the engine's save directory (F-8).
- Storage has no per-mod isolation: any mod can `Load`/`Delete` any namespace (e.g. `campaign_reimagined`). Document it or namespace by mod ID.
- `ReadWholeFile` uses `FILE_SHARE_READ` only. When another process holds the primary open for writing, Load silently falls back to `.bak` (`recovered=true`) rather than reporting "busy".
- Unverified: whether native SaveGame re-enters mission Lua (`Save()` callback) under `pcall`, and whether it is safe to call `exu.SaveGame` from inside EXU hook callbacks or during load. Neither is documented.
