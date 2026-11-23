-- Invoke from a live mission with the newly built EXU module, the player in a
-- craft and at least one friendly craft nearby: dofile(...)(exu, friendlyHandle).
-- Prints one [SMALLCALLS] line per call; items marked LOOK need a human eye or ear.
return function(exu, friendly)
    local function fails(fn) local ok = pcall(fn); assert(not ok, "expected Lua argument error") end
    local function say(fmt, ...) print(string.format("[SMALLCALLS] " .. fmt, ...)) end
    local player = GetPlayerHandle()
    friendly = friendly or player

    say("ClearMessages=%s (LOOK: message list empties)", tostring(exu.ClearMessages()))

    say("eject sound before=%s", tostring(exu.GetEjectSound()))
    assert(exu.GetEjectSound() == "bail.wav")
    assert(exu.SetEjectSound("jump.wav") == true and exu.GetEjectSound() == "jump.wav")
    assert(exu.SetEjectSound("bad/name.wav") == false and exu.GetEjectSound() == "jump.wav")
    assert(exu.SetEjectSound(nil) == true and exu.GetEjectSound() == "bail.wav")
    say("eject sound set/restore PASS")

    assert(exu.GetHopOutSound() == "jump.wav")
    assert(exu.SetHopOutSound("bail.wav") == true and exu.GetHopOutSound() == "bail.wav")
    say("hop-out sound now bail.wav (LOOK: hop out, hear bail.wav)")
    assert(exu.SetHopOutSound(nil) == true and exu.GetHopOutSound() == "jump.wav")
    say("hop-out sound set/restore PASS")

    local ratio = exu.GetPersonEjectRatio(player)
    say("player eject ratio=%s", tostring(ratio))
    assert(type(ratio) == "number")
    assert(exu.SetPersonEjectRatio(player, 0.25) == true)
    assert(math.abs(exu.GetPersonEjectRatio(player) - 0.25) < 1e-6)
    assert(exu.SetPersonEjectRatio(player, 1.5) == false)
    assert(exu.SetPersonEjectRatio(player, ratio) == true)
    fails(function() exu.GetPersonEjectRatio(GetHandle("absent_smallcalls_object")) end)
    say("eject ratio PASS")

    local pos = GetPosition(player)
    local scrapAt = SetVector(pos.x + 30, pos.y, pos.z)
    assert(exu.MakeScrapField(scrapAt, 6) == true)
    assert(exu.MakeScrapField(scrapAt, 0) == false)
    fails(function() exu.MakeScrapField("nope", 3) end)
    say("MakeScrapField PASS (LOOK: scrap 30 m east)")

    local has = exu.HasSatellite()
    say("HasSatellite=%s GetSatState=%s", tostring(has), tostring(exu.GetSatState()))
    if has then
        say("ActivateSatellite=%s GetSatState=%s", tostring(exu.ActivateSatellite()), tostring(exu.GetSatState()))
        say("DeactivateSatellite=%s GetSatState=%s", tostring(exu.DeactivateSatellite()), tostring(exu.GetSatState()))
    else
        assert(exu.ActivateSatellite() == false)
    end

    say("UnitMessage(attack)=%s (LOOK: voice plays if friendly is on your team)", tostring(exu.UnitMessage(friendly, "attack")))
    say("IsUnitMessagePlaying=%s", tostring(exu.IsUnitMessagePlaying()))
    assert(exu.UnitMessage(friendly, "no_such_msg") == false)
    assert(exu.UnitMessage(friendly, 27) == false)
    say("UnitMessageDirect(killed,1)=%s", tostring(exu.UnitMessageDirect(friendly, "killed", 1)))
    assert(exu.UnitMessageDirect(friendly, "go", 9) == false)
    say("StopUnitMessage=%s IsUnitMessagePlaying=%s", tostring(exu.StopUnitMessage()), tostring(exu.IsUnitMessagePlaying()))
    assert(exu.IsUnitMessagePlaying() == false)

    local section = "smallcalls_check"
    assert(exu.SaveSettingBool(section, "b", true))
    assert(exu.SaveSettingInt(section, "i", 42))
    assert(exu.SaveSettingFloat(section, "f", 1.5))
    assert(exu.SaveSettingString(section, "s", "red one"))
    assert(exu.SaveSettingInt("general_i", 7))
    local v, found = exu.GetSettingBool(section, "b", false); assert(v == true and found == true)
    v, found = exu.GetSettingInt(section, "i", 0); assert(v == 42 and found)
    v, found = exu.GetSettingFloat(section, "f"); assert(v == 1.5 and found)
    v, found = exu.GetSettingString(section, "s"); assert(v == "red one" and found)
    v, found = exu.GetSettingInt("general_i", 0); assert(v == 7 and found)
    v, found = exu.GetSettingInt(section, "f", -1); assert(v == -1 and found == false)
    v, found = exu.GetSettingBool(section, "missing"); assert(v == nil and found == false)
    fails(function() exu.SaveSettingInt(section, "i", 2.5) end)
    fails(function() exu.SaveSettingFloat(section, "f", 1/0) end)
    for _, label in ipairs({ "b", "i", "f", "s" }) do assert(exu.DeleteSetting(section, label)) end
    assert(exu.DeleteSetting("general_i"))
    v, found = exu.GetSettingInt(section, "i", 0); assert(v == 0 and found == false)
    say("settings PASS")

    -- Clock and constants. Values depend on this machine's clock and time zone.
    local stamp = exu.GetCurDate("%Y-%m-%d %H:%M:%S")
    say("GetCurDate=%s (LOOK: matches the system clock)", stamp)
    assert(type(stamp) == "string" and #stamp == 19)
    local utc = exu.GetCurDate("!*t", 86400)
    assert(utc.year == 1970 and utc.month == 1 and utc.day == 2 and utc.hour == 0 and utc.wday == 6 and utc.yday == 2)
    fails(function() exu.GetCurDate("%Q") end)
    fails(function() exu.GetCurDate("%#d") end)
    local now = exu.GetCurTime()
    local localT = exu.GetCurDate("*t", now)
    assert(exu.GetCurTime(localT) == now)
    fails(function() exu.GetCurTime({ year = 2026 }) end)
    assert(exu.GetTimeDiff(now + 90, now) == 90 and exu.GetTimeDiff(5) == 5)
    assert(exu.UNIT_MESSAGE.attack == nil and exu.UNIT_MESSAGE.ATTACK == 14 and exu.UNIT_MESSAGE.BLOCKED == 26)
    assert(exu.UnitMessage(friendly, exu.UNIT_MESSAGE.ATTACK) ~= nil)
    local clip = exu.GetUnitMessage(friendly, "attack")
    say("GetUnitMessage(attack)=%s", tostring(clip))
    assert(clip == nil or type(clip) == "string")
    assert(exu.GetUnitMessage(friendly, "no_such_msg") == nil)
    say("clock and constants PASS")

end
