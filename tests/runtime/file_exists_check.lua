-- Invoke from a live mission with the newly built EXU module:
-- dofile(...)(exu, knownStem). knownStem is a mission stem that ships
-- <stem>.des (default "misn2bcr", which ships pages 1-2 in CR).
-- Prints one [FILEEXISTS] line per check. After it runs, BZLogger.txt must
-- hold no new 'Couldn't find file' line for the absent names below.
return function(exu, stem)
    stem = stem or "misn2bcr"
    local function say(fmt, ...) print(string.format("[FILEEXISTS] " .. fmt, ...)) end
    local function agrees(name)
        local exists = exu.FileExists(name)
        assert(exists ~= nil, "unsupported build or lookup fault: " .. name)
        if exists then
            local data = UseItem(name)
            assert(type(data) == "string" and #data > 0, "FileExists true but UseItem failed: " .. name)
        end
        say("%s exists=%s", name, tostring(exists))
        return exists
    end

    assert(agrees(stem .. ".des") == true)
    assert(agrees("ivtank.odf") == true) -- stock archive asset
    assert(agrees(stem .. "_9.des") == false)
    assert(agrees("exu_no_such_file.des") == false)
    assert(exu.FileExists("") == false and exu.FileExists("null.des") == false)
    assert(not pcall(exu.FileExists))
    say("PASS (LOOK: no new Couldn't find file lines in BZLogger.txt)")
end
