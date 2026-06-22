-- OpenShim/EXU co-op mission sync helper.
-- Drop this file into a mission script folder and require it from host/client Lua.
-- The mission owns the packet type and must forward Receive(from, type, ...) into
-- sync:receive(from, type, ...).

local M = {}
M.__index = M

local function call_if_present(name, ...)
    local fn = _G[name]
    if type(fn) == "function" then
        return fn(...)
    end
    return nil
end

local function is_host()
    return type(IsHosting) == "function" and IsHosting()
end

local function send_all(packet_type, opcode, ...)
    if type(Send) == "function" then
        Send(0, packet_type, opcode, ...)
    end
end

function M.new(packet_type)
    assert(type(packet_type) == "number", "packet_type must be a number")
    return setmetatable({
        packet_type = packet_type,
        state = nil,
        ready = {},
        ready_count = 0,
    }, M)
end

function M:host_clear_objectives()
    if not is_host() then return false end
    call_if_present("ClearObjectives")
    send_all(self.packet_type, "clear_objectives")
    return true
end

function M:host_add_objective(name, color, duration, text)
    if not is_host() then return false end
    call_if_present("AddObjective", name, color, duration, text)
    send_all(self.packet_type, "add_objective", name, color, duration, text)
    return true
end

function M:host_update_objective(name, color, duration, text)
    if not is_host() then return false end
    call_if_present("UpdateObjective", name, color, duration, text)
    send_all(self.packet_type, "update_objective", name, color, duration, text)
    return true
end

function M:host_remove_objective(name)
    if not is_host() then return false end
    call_if_present("RemoveObjective", name)
    send_all(self.packet_type, "remove_objective", name)
    return true
end

function M:host_objective_marker(handle, enabled, name)
    if not is_host() then return false end
    if name ~= nil then
        call_if_present("SetObjectiveName", handle, name)
    end
    call_if_present(enabled and "SetObjectiveOn" or "SetObjectiveOff", handle)
    send_all(self.packet_type, "objective_marker", handle, enabled and 1 or 0, name)
    return true
end

function M:host_set_state(state_name)
    if not is_host() then return false end
    self.state = state_name
    send_all(self.packet_type, "state", state_name)
    return true
end

function M:host_succeed(delay, filename)
    if not is_host() then return false end
    call_if_present("SucceedMission", delay, filename)
    send_all(self.packet_type, "succeed", delay, filename)
    return true
end

function M:host_fail(delay, filename)
    if not is_host() then return false end
    call_if_present("FailMission", delay, filename)
    send_all(self.packet_type, "fail", delay, filename)
    return true
end

function M:client_ready()
    if is_host() then return false end
    if type(Send) == "function" then
        Send(0, self.packet_type, "ready")
        return true
    end
    return false
end

function M:host_ready_count()
    return self.ready_count
end

function M:receive(from, packet_type, opcode, ...)
    if packet_type ~= self.packet_type then
        return false
    end

    if opcode == "ready" then
        if is_host() and not self.ready[from] then
            self.ready[from] = true
            self.ready_count = self.ready_count + 1
        end
        return true
    end

    if is_host() then
        return true
    end

    if opcode == "clear_objectives" then
        call_if_present("ClearObjectives")
    elseif opcode == "add_objective" then
        call_if_present("AddObjective", ...)
    elseif opcode == "update_objective" then
        call_if_present("UpdateObjective", ...)
    elseif opcode == "remove_objective" then
        call_if_present("RemoveObjective", ...)
    elseif opcode == "objective_marker" then
        local handle, enabled, name = ...
        if name ~= nil then
            call_if_present("SetObjectiveName", handle, name)
        end
        call_if_present(enabled ~= 0 and "SetObjectiveOn" or "SetObjectiveOff", handle)
    elseif opcode == "state" then
        local state_name = ...
        self.state = state_name
    elseif opcode == "succeed" then
        call_if_present("SucceedMission", ...)
    elseif opcode == "fail" then
        call_if_present("FailMission", ...)
    else
        return false
    end

    return true
end

return M
