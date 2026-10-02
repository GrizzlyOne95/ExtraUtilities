-- Run from an active mission on a qualified Redux build after require("exu").
-- Does not write saves: every SaveGame call below has an invalid argument.
-- The caller supplies the loaded EXU module and records the printed results.
return function(exu)
    local cases = {
        { "SaveGame invalid slot", function() exu.SaveGame(0) end },
        { "SaveGame invalid description", function() exu.SaveGame(10, {}) end },
        { "SaveGame invalid third argument", function() exu.SaveGame(10, 0, {}) end },
        { "SetMaterialPassColors invalid table", function()
            exu.SetMaterialPassColors("audit_missing_material", false)
        end },
        { "SetUnitVoAlternates invalid entry", function()
            exu.SetUnitVoAlternates("audit.wav", { "first.wav", {} })
        end },
        { "SetOverlayCaption invalid text", function()
            exu.SetOverlayCaption("audit_missing_overlay", {})
        end },
        { "CreateParticleSystem invalid template argument", function()
            exu.CreateParticleSystem("audit_invalid_particle", {})
        end },
    }

    for _, case in ipairs(cases) do
        local ok, message = pcall(case[2])
        assert(not ok and type(message) == "string", case[1] .. " must raise a Lua error")
        assert(type(exu.GetScreenResolution()) == "number", "Lua state unusable after " .. case[1])
        print("[BOUNDARY] PASS " .. case[1])
    end

    -- Ogre throws for an unknown template. The SEH shell must pass that C++
    -- exception to its outer catch, preserve the false result, and leave Lua
    -- usable. exu.log must describe a C++ exception, not a 0xE06D7363 crash.
    local ok, result = pcall(exu.CreateParticleSystem,
        "audit_missing_particle", "EXU/AuditMissingTemplate", GetPosition(GetPlayerHandle()))
    assert(ok and result == false, "missing particle template must return false")
    assert(type(exu.GetScreenResolution()) == "number", "Lua state unusable after Ogre exception")
    print("[BOUNDARY] PASS missing particle template")
    print("[BOUNDARY] DONE passed=8")
end
