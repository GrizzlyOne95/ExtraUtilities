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
- `UpdateCommandReplacements()` polls registered replacements once per game tick
  from campaign-side update loops and currently auto-intercepts selected-unit
  `Hunt` transitions.
- If the callback returns true or leaves the unit on stock `Hunt`,
  `UpdateCommandReplacements()` clears the stock command back to `NONE` so the
  stock Hunt behavior is consumed by the replacement.
- The Hunt label override is now a runtime lookup of the command label pointer
  table in the live executable image, rather than a hardcoded one-build address.
- The callback receives:
  - `handle`
  - stock command display name
  - replacement label
  - dispatch origin string

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
- The first full pass still uses Lua polling of `GetCurrentCommand`, not a
  direct native `SetActiveMode` or menu-click hook.
- Automatic interception is intentionally limited to selected units to better
  approximate local player command-menu use.
- The current prototype takes a Lua function, not a Lua filename.
- The command label override is best-effort and depends on successfully finding
  the stock command label pointer table at runtime.

## Next Native Steps

The RE still points at the same likely native path:

- `Wingman::UpdateModeList`
- `Wingman::SetActiveMode`
- `Craft::SetActiveMode`
- `ActionMode::GetCommand`
- `PathDisplay::DrawCommandMenu`

The next native step is still:

1. move from polling to a real native stock-command activation hook
2. dispatch to `DispatchRegisteredReplacement(...)` directly from that hook
3. suppress the stock action before it becomes `CMD_HUNT`
4. expand beyond `Hunt` once other stock slot paths are validated
