-- In-game check for the mission-path calls: exu.AllPaths, exu.BuildPath,
-- exu.AddPathPoint, exu.RemovePath and exu.RemovePathPoint.
--
-- This file is not shipped. Copy it next to a test mission script, then:
--
--     local check = require("bzx_paths_check")
--     function Update()
--         check.Update(exu)
--     end
--
-- It runs once on the first Update, near the player, and prints lines
-- prefixed "[BZXPATH] ". Every line should end in "ok". If a unit handle is
-- given (check.Update(exu, h)), the unit is sent down the built path so you
-- can watch it drive the three corners, and then the path is removed while
-- it drives: it must finish the route without a crash.
--
-- Stock calls used for cross-checks: GetPathPointCount, GetPosition,
-- GetPlayerHandle, SetVector, Goto.
--
-- Lua 5.1; uses no io/os/debug.

local M = {}
local done = false
local NAME = "exu_bzx_paths_check"

local function say(label, value, expected)
	local ok = value == expected
	print(string.format("[BZXPATH] %s = %s (want %s) %s", label, tostring(value), tostring(expected), ok and "ok" or "FAIL"))
	return ok
end

local function listed(exu, name)
	local names = exu.AllPaths()
	if names == nil then
		return nil
	end
	for _, n in ipairs(names) do
		if n == name then
			return true
		end
	end
	return false
end

function M.Update(exu, unit)
	if done then
		return
	end
	done = true

	local names = exu.AllPaths()
	say("AllPaths returns a table", type(names), "table")
	print("[BZXPATH] map paths: " .. table.concat(names or {}, ", "))

	local p = GetPosition(GetPlayerHandle())
	local a = SetVector(p.x + 60, p.y, p.z)
	local b = SetVector(p.x + 60, p.y, p.z + 60)
	local c = SetVector(p.x, p.y, p.z + 60)

	-- Leftover from an earlier run of this snippet in the same mission.
	exu.RemovePath(NAME)

	say("BuildPath new", exu.BuildPath(NAME, a, b), true)
	say("BuildPath taken name", exu.BuildPath(NAME, a), false)
	say("BuildPath empty name", exu.BuildPath("", a), false)
	say("BuildPath NaN", exu.BuildPath(NAME .. "_nan", SetVector(0 / 0, 0, 0)), false)
	say("AllPaths lists it", listed(exu, NAME), true)
	say("stock count after build", GetPathPointCount(NAME), 2)

	say("AddPathPoint append", exu.AddPathPoint(NAME, c), true)
	say("AddPathPoint at 1", exu.AddPathPoint(NAME, p, 1), true)
	say("AddPathPoint index 0", exu.AddPathPoint(NAME, p, 0), false)
	say("AddPathPoint index 99", exu.AddPathPoint(NAME, p, 99), false)
	say("AddPathPoint missing path", exu.AddPathPoint(NAME .. "_none", p), false)
	say("stock count after adds", GetPathPointCount(NAME), 4)

	say("RemovePathPoint first", exu.RemovePathPoint(NAME, 1), true)
	say("RemovePathPoint index 9", exu.RemovePathPoint(NAME, 9), false)
	say("stock count after remove", GetPathPointCount(NAME), 3)

	say("names are case-sensitive", exu.RemovePathPoint(string.upper(NAME)), false)

	-- Grow and shrink a scratch path many times (each insert swaps the block).
	local grow = NAME .. "_grow"
	exu.RemovePath(grow)
	exu.BuildPath(grow, a)
	local added = 0
	for i = 1, 50 do
		if exu.AddPathPoint(grow, SetVector(p.x + i, p.y, p.z), (i % 3 == 0) and 1 or nil) then
			added = added + 1
		end
	end
	say("50 inserts", added, 50)
	say("stock count after 50 inserts", GetPathPointCount(grow), 51)
	for i = 1, 25 do
		exu.RemovePathPoint(grow, (i % 2 == 0) and 1 or nil)
	end
	say("stock count after 25 removes", GetPathPointCount(grow), 26)
	exu.RemovePath(grow)

	-- A one-point scratch path keeps its last point.
	say("BuildPath one point", exu.BuildPath(NAME .. "_one", a), true)
	say("RemovePathPoint last of one", exu.RemovePathPoint(NAME .. "_one"), false)
	say("RemovePath scratch", exu.RemovePath(NAME .. "_one"), true)
	say("RemovePath twice", exu.RemovePath(NAME .. "_one"), false)
	say("scratch unlisted", listed(exu, NAME .. "_one"), false)
	say("name reusable", exu.BuildPath(NAME .. "_one", b), true)
	exu.RemovePath(NAME .. "_one")

	if unit ~= nil then
		Goto(unit, NAME)
		say("RemovePath while followed", exu.RemovePath(NAME), true)
		print("[BZXPATH] watch the unit: it should still drive a -> b -> c")
	else
		say("RemovePath main", exu.RemovePath(NAME), true)
	end
	say("main unlisted", listed(exu, NAME), false)
end

return M
