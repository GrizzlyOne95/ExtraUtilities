-- Invoke from a live qualified mission with the newly built EXU module.
-- No native hooks or object changes here: gameplay event validation is separate.
return function(exu)
    local function fails(fn) local ok = pcall(fn); assert(not ok, "expected Lua argument error") end
    local player = GetPlayerHandle()
    local pos = GetPosition(player)
    local matrix = GetTransform(player)
    local a,b,c = exu.WorldToScreen(player)
    local x,y,z = exu.WorldToScreen(pos)
    local mx,my,mz = exu.WorldToScreen(matrix)
    print(string.format("[CAPABILITIES] projection equality handle=%s,%s,%s vector=%s,%s,%s matrix=%s,%s,%s pos=%s;",tostring(a),tostring(b),tostring(c),tostring(x),tostring(y),tostring(z),tostring(mx),tostring(my),tostring(mz),tostring(pos)))
    assert(a and x and mx and math.abs(a-x)<0.01 and math.abs(b-y)<0.01 and math.abs(c-z)<0.01)
    assert(math.abs(mx-x)<0.01 and math.abs(my-y)<0.01 and math.abs(mz-z)<0.01)
    assert(type(exu.IsPointInView(pos)) == "boolean")
    local invalid = exu.WorldToScreen(GetHandle("absent_capabilities_object"))
    assert(invalid == nil)
    assert(exu.WorldToScreen({x=0/0,y=0,z=0}) == nil)
    print("[CAPABILITIES] projection input PASS;")
    assert(exu.GetPlayerSteam64(65535) == nil)
    fails(function() exu.GetPlayerSteam64(-1) end)
    print("[CAPABILITIES] absent Steam identity PASS;")
end
