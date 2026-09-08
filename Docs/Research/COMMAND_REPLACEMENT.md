# Command Replacement Prototype

Date: 2026-03-16

## Scope

This is the first EXU-side prototype for replacing a stock command slot with a
Lua callback.

What exists now:

- `exu.ReplaceStockCmd(handle, stockCommandName, replacementLabel, callbackFn)`
- `exu.RemoveStockCmdReplacement(handle, stockCommandName)`
- `exu.HasStockCmdReplacement(handle, stockCommandName)`
- `exu.GetStockCmdReplacement(handle, stockCommandName)`
- `exu.TriggerStockCmdReplacement(handle, stockCommandName)`
- `exu.UpdateCommandReplacements()`

## Current Behavior

- Replacements are stored per-unit-handle and per stock command name.
- The callback is stored as a Lua registry reference.
- `TriggerStockCmdReplacement` manually dispatches the callback for direct
  testing.
- A native EXU hook now scans the live executable for the Redux
  `Wingman::SetActiveMode` `mode 0x0D -> CMD_HUNT` block and intercepts that
  path before stock `CMD_HUNT` is issued.
- If the selected unit has a registered `Hunt` replacement and the callback
  returns true or nil, the stock Hunt action is suppressed and the Lua callback
  becomes the command behavior.
- `UpdateCommandReplacements()` is still called from campaign-side update loops,
  but it now mainly maintains the contextual label override and only falls back
  to polling-based Hunt interception if the native hook signature is not found.
- The Hunt label override is now a runtime lookup of the command label pointer
  table in the live executable image, rather than a hardcoded one-build address.
- The callback receives:
  - `handle`
  - stock command display name
  - replacement label
  - dispatch origin string

## Legacy Verification

Cross-checking against the exact legacy BZ1 decompile in
`Battlezone98Redux_Shim\reverse_engineering\workshop\global_decompile\legacy_bz1_exact_full`
strengthened the native hook model:

- `Wingman::UpdateModeList` explicitly installs stock mode `0x0D` in the
  wingman mode list
- `Wingman::SetActiveMode` maps `0x0D` directly to `GameObject::SetCommand(CMD_HUNT)`
- `Craft::SetActiveMode` does not own `Hunt`; it handles other stock craft
  modes

That is not the Redux binary itself, but it matches the Redux PDB naming and
supports the current EXU first-pass design choice: treat `Hunt` as a reusable
stock wingman slot and intercept around that path first.

Example:

```lua
exu.ReplaceStockCmd(h, "Hunt", "Hold", function(unit, stockName, replacementName, origin)
    SetCommand(unit, AiCommand.NONE, 0)
    return true
end)

exu.TriggerStockCmdReplacement(h, "Hunt")
```

## Deliberate Limitations

- Automatic interception is currently implemented only for the `Hunt` stock
  command.
- Automatic interception is intentionally limited to selected units to better
  approximate local player command-menu use.
- The current prototype takes a Lua function, not a Lua filename.
- The command label override is best-effort and depends on successfully finding
  the stock command label pointer table at runtime rather than a dedicated
  command-menu render hook.
- There is still no extra-button support; this only repurposes an existing
  stock slot.

## Next Native Steps

The RE still points at the same likely native path:

- `Wingman::UpdateModeList`
- `Wingman::SetActiveMode`
- `PathDisplay::DrawCommandMenu`
- `Craft::SetActiveMode`
- `ActionMode::GetCommand`

The next native steps are now:

1. replace the global Hunt-text swap with a dedicated contextual menu-text hook
2. expand beyond `Hunt` once other stock slot paths are validated
3. decide whether broader unit classes should hook their own `SetActiveMode`
   wrappers or a lower shared command path
