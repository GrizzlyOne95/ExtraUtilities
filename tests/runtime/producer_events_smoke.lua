-- Observer for an isolated mission fixture, after enabling an empty event queue.
-- Supply live handles for one avcnst, avmuf and avslf on the same team. Queue an
-- abspow on the rig, Stop it before completion, then queue another abspow plus
-- an avtank on the deployed factory and an aprepa on the deployed armory.
-- Feed every drained batch to Record, then call Verify after all builds finish.
return function(rig, factory, armory)
    local records = {}
    local function record(events, dropped, pending)
        assert(dropped == 0 and pending == 0, "fixture lost or left pending events")
        for _, event in ipairs(events) do
            assert(event.sequence == #records + 1, "event sequence gap or duplicate")
            records[#records + 1] = event
        end
    end
    local function verify()
        assert(#records == 8, "expected exactly eight production observations")
        local expected = {
            [rig] = {kind="constructor", odf="abspow", types={"queued","cancelled","queued","completed"}},
            [factory] = {kind="producer", odf="avtank", types={"queued","completed"}},
            [armory] = {kind="armory", odf="aprepa", types={"queued","completed"}},
        }
        for _, event in ipairs(records) do
            local item = assert(expected[event.producer], "unexpected producer")
            local nextType = table.remove(item.types, 1)
            assert(event.type == nextType and event.odf == item.odf and event.producerType == item.kind,
                "production lifecycle mismatch")
            if event.type == "completed" and event.producer ~= armory then
                assert(event.built and IsValid(event.built), "missing completed object")
            else
                assert(event.built == nil, "unexpected completed object handle")
            end
        end
        for _, item in pairs(expected) do assert(#item.types == 0, "missing production event") end
        print("[CAPABILITIES] production lifecycle assertions PASS;")
    end
    return {Record=record, Verify=verify}
end
