-- Live check for the net identity group. Paste into a mission's Start() (or
-- run from the console) in single player and again in a network game.
-- Prints results only.
local exu = exu or require("exu")

local function show(label, value)
    if type(value) == "table" then
        local parts = {}
        for k, v in pairs(value) do parts[#parts + 1] = tostring(k) .. "=" .. tostring(v) end
        value = "{" .. table.concat(parts, ", ") .. "}"
    end
    print("[netid] " .. label .. ": " .. tostring(value))
end

local me = GetPlayerHandle()
show("IsNetGame", IsNetGame and IsNetGame() or "n/a")
show("GetMyNetID", exu.GetMyNetID())

local id = me and exu.GetNetworkID(me)
show("GetNetworkID(player)", id)
show("GetOwnerID(player)", me and exu.GetOwnerID(me))
if id then
    local back = exu.GetHandleFromNetworkID(id)
    show("GetHandleFromNetworkID round trip", back == me)
end
show("GetHandleFromNetworkID(1)", exu.GetHandleFromNetworkID(1))

for i, p in ipairs(exu.AllPlayers()) do show("AllPlayers[" .. i .. "]", p) end
show("AllPlayers count", #exu.AllPlayers())

show("IsDeathmatch", exu.IsDeathmatch())
show("GetNetGameOptions", exu.GetNetGameOptions())

local start = exu.GetStartLocation()
show("GetStartLocation", start)
if start then
    show("SetStartLocation(same)", exu.SetStartLocation(start))
    show("GetStartLocation after", exu.GetStartLocation())
else
    show("SetStartLocation without session", exu.SetStartLocation(0, 0, 0))
end
