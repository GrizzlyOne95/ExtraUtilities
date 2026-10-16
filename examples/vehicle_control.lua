-- Opt-in mission helper. Call Start(enemy), Update(dt), Stop() from your mission.
-- Choose a known, ordinary HoverCraft (for example a stock tank/scout).
-- Requires this EXU build. Single player only. No automatic unit spawning.
local exu = require("exu")
local M = {}
local enemy, started

function M.Start(h)
    if not exu.HasVehicleControl or not exu.HasVehicleControl() then
        return false, "vehicle control unavailable"
    end
    local ok, reason = exu.ClaimVehicleControl(h)
    if not ok then return false, reason end
    enemy, started = h, GetTime()
    return true
end

function M.Update(dt)
    if not enemy then return end
    if not IsValid(enemy) then M.Stop(); return end
    local elapsed = GetTime() - started
    -- Demonstrate real driving and a short held jump. After 3 seconds hand
    -- back to the original AI task. Aim/firing remain off in this smoke demo.
    if elapsed >= 3 then M.Stop(); return end
    local ok, reason = exu.SetVehicleControls(enemy, {
        throttle = 0.7,
        strafe = elapsed < 1.5 and -0.5 or 0.5,
        steer = 0.15,
        pitch = 0,
        turbo = false,
        jump = elapsed > 1 and elapsed < 1.3,
        fire = false,
        timeout = 0.5,
    })
    if not ok then M.Stop(); return false, reason end
    return true
end

function M.Stop()
    if enemy then exu.ReleaseVehicleControl(enemy) end
    enemy, started = nil, nil
end

-- Call Stop before Save; after Load, explicitly choose and claim the restored
-- unit again. Never persist native pointers or assume a lease survived a load.
return M
