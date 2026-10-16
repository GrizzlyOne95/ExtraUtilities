// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::VehicleControl
{
    // Exact native types: turbo/jump are integer booleans, not floats.
    struct Inputs
    {
        float steer = 0, pitch = 0, strafe = 0, throttle = 0;
        std::int32_t turbo = 0, jump = 0;
    };
    static_assert(sizeof(Inputs) == 24);
    struct Command
    {
        Inputs inputs{};
        bool fire = false, special = false, selectWeapons = false;
        std::uint32_t weaponMask = 0;
        float timeout = 0.5f;
    };
    inline bool Valid(const Command& c) noexcept
    {
        const auto axis = [](float x) { return std::isfinite(x) && x >= -1 && x <= 1; };
        return axis(c.inputs.steer) && axis(c.inputs.pitch) && axis(c.inputs.strafe) && axis(c.inputs.throttle) &&
            (c.inputs.turbo == 0 || c.inputs.turbo == 1) && (c.inputs.jump == 0 || c.inputs.jump == 1) &&
            std::isfinite(c.timeout) && c.timeout >= 0.01f && c.timeout <= 5 && c.weaponMask <= 31;
    }
    struct Entry
    {
        std::uint32_t handle = 0;
        // Identity tokens only. Native code resolves fresh pointers before use.
        std::uintptr_t process = 0, processVtable = 0;
        Command command{};
        Inputs applied{};
        float issued = 0, deadline = 0;
        std::uint32_t applications = 0;
        std::uint64_t revision = 0;
        bool Expired(float now) const noexcept
        {
            return !std::isfinite(now) || now < issued || now >= deadline;
        }
    };
    // Fixed capacity, no allocation or exceptions on the native update path.
    template<std::size_t Capacity = 128> class Policy
    {
        std::array<Entry, Capacity> entries{};
        std::uint64_t revision = 0;
    public:
        Entry* Find(std::uint32_t handle) noexcept
        {
            if (handle) for (auto& e : entries) if (e.handle == handle) return &e;
            return nullptr;
        }
        bool Claim(std::uint32_t handle, std::uintptr_t process, std::uintptr_t vtable, float now) noexcept
        {
            if (!handle || !process || !vtable || !std::isfinite(now)) return false;
            if (const auto* e = Find(handle)) return e->process == process && e->processVtable == vtable;
            for (auto& e : entries) if (!e.handle)
            {
                e = {}; e.handle = handle; e.process = process; e.processVtable = vtable;
                e.issued = now; e.deadline = now + e.command.timeout;
                e.revision = ++revision;
                return true;
            }
            return false;
        }
        bool Set(std::uint32_t handle, const Command& c, float now) noexcept
        {
            if (!Valid(c) || !std::isfinite(now)) return false;
            auto* e = Find(handle);
            if (!e || e->Expired(now)) return false;
            e->command = c; e->issued = now; e->deadline = now + c.timeout;
            e->revision = ++revision;
            return true;
        }
        void Release(std::uint32_t handle) noexcept { if (auto* e = Find(handle)) *e = {}; }
        void Reset() noexcept { entries = {}; }
        auto& Entries() noexcept { return entries; }
    };
    // The same dispatch decision/order runs against the engine adapter and host
    // fixtures. True means the caller must skip the stock process update.
    template<std::size_t N, class Sink>
    bool Tick(Policy<N>& policy, std::uint32_t handle, std::uintptr_t process,
        std::uintptr_t vtable, float now, Sink& sink)
    {
        auto* e = policy.Find(handle);
        if (!e) return false;
        if (e->process != process || e->processVtable != vtable)
        { policy.Release(handle); return false; }
        if (e->Expired(now))
        { sink.Clear(); policy.Release(handle); return false; }
        const auto c = e->command;
        const auto revision = e->revision;
        const auto current = [&]() { const auto* live = policy.Find(handle); return live == e && live->revision == revision; };
        sink.SetInputs(c.inputs);
        if (c.selectWeapons) sink.Select(c.weaponMask);
        if (current() && c.fire) sink.Fire();
        if (current() && c.special) sink.Special();
        // Native weapon calls can invoke mission callbacks. A callback may
        // release/reset/re-claim or replace this command; never resurrect it.
        if (current()) { e->applied = c.inputs; ++e->applications; }
        return true;
    }
    inline const char* ProcessStateName(int state) noexcept
    {
        constexpr const char* names[] = {"NO_STATE","START","DONE","WAIT","GOTO","PATROL","STAGE","ATTACK",
            "FOLLOW","DEFEND","REPAIR","RELOAD","RESCUE","RECYCLE","PACKUP","SUBATTACK","USTATE1","USTATE2"};
        return state >= 0 && state < 18 ? names[state] : "UNKNOWN";
    }
    inline const char* TaskStateName(int state) noexcept
    {
        constexpr const char* names[] = {"NO_STATE","START","GOTO","STUCK","FOLLOW","ATTACK","WAIT","SLIDE",
            "STAND","FLEE","BLAST","LEAD","CATCH","DONE"};
        return state >= 0 && state < 14 ? names[state] : "UNKNOWN";
    }
}
