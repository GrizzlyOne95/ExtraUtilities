# Pause Menu Probe

This repo exposes `exu.IsPauseMenuOpen()` through the helper in
[`src/game_state.cpp`](/c:/Users/istuart/Documents/GIT/ExtraUtilities-G1/src/game_state.cpp).

## Addresses

- `0x009454EC`: single-player Escape screen root
- `0x00918320`: current UI screen pointer
- `0x00918324`: UI wrapper active flag
- `0x00918328`: current UI screen type
- `0x0094557C`: multiplayer pause root
- `0x00945549`: multiplayer pause flag

## Single-Player Logic

Treat the game as paused when:

1. the Escape root is non-null
2. the UI wrapper is active
3. and either:
   - current screen equals the Escape root
   - or current screen type is one of:
     - `0x0B` pause
     - `0x03` options
     - `0x11` save
     - `0x12` load
     - `0x17` restart

## Multiplayer Logic

Treat the game as paused when the multiplayer pause root or pause flag is set
and the OS cursor is visible.

## Source

The reverse-engineering rationale is recorded in the shim repo at
[`pause_menu_notes.md`](/c:/Users/istuart/Documents/GIT/Battlezone98Redux_Shim/reverse_engineering/pause_menu_notes.md).
