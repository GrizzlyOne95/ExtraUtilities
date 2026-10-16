// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/PlayerControls.h"
#include "Game/PlayerControlPolicy.h"
#include "EntryDetour32.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "Util/SignatureResolver.h"
#include "bzr.h"
#include <lua.hpp>
#include <atomic>
#include <cmath>
#include <new>

namespace ExtraUtilities::Lua::PlayerControls
{
    namespace
    {
        using namespace PlayerControlPolicy;
        using ExecuteFn = void(__thiscall*)(void*);
        Owners owners;
        std::atomic<std::uint32_t> requested{0}, lastApplied{0};
        std::atomic<unsigned> applications{0};
        std::atomic<bool> faulted{false};
        EntryDetour32* detour = nullptr;
        ExecuteFn original = nullptr;
        constexpr std::array<std::uint8_t, 9> entryBytes = {0x55,0x8B,0xEC,0x81,0xEC,0x1C,0x02,0x00,0x00};
        constexpr std::array<std::uint8_t, 22> bufferRead = {
            0x66,0xA1,0x9C,0x98,0x91,0x00,0x66,0x89,0x85,0xBC,0xFE,0xFF,0xFF,
            0x6A,0x38,0x6A,0x00,0x68,0x9C,0x98,0x91,0x00};

        bool NetworkSession() noexcept
        {
            __try { return *BZR::Multiplayer::isNetGame; }
            __except(Seh::Filter(GetExceptionCode())) { return true; }
        }
        bool Qualified() noexcept
        {
            return RuntimeGate::IsSupported() && !faulted.load() &&
                SignatureResolver::MatchBytes(EngineAddresses::PlayerInput::UserControlClearRead, bufferRead) &&
                (detour && detour->IsActive() ? true :
                 SignatureResolver::MatchBytes(EngineAddresses::PlayerInput::UserProcessExecute, entryBytes));
        }

        // POD-only SEH boundary. Never cache a player/process/GAS_CTRL pointer:
        // hopping between a pilot and a craft replaces these native objects.
        bool Apply(void* process, std::uint32_t mask, bool& local) noexcept
        {
            local = false;
            __try
            {
                auto* object = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(process)+0x0C);
                auto* user = *reinterpret_cast<void**>(BZR::GameObject::p_userObject);
                if (!object || object != user) return true;
                local = true;
                auto* gas = *reinterpret_cast<std::uint8_t**>(object+0x230);
                if (!gas) return false;
                auto* input = reinterpret_cast<void*>(EngineAddresses::PlayerInput::UserControls);
                Inputs snapshot{};
                Gas smoothing{};
                std::memcpy(snapshot.data(), input, sizeof(snapshot));
                std::memcpy(smoothing.data(), gas+0xC4, sizeof(smoothing));
                Suppress(snapshot, smoothing, mask);
                std::memcpy(input, snapshot.data(), sizeof(snapshot));
                std::memcpy(gas+0xC4, smoothing.data(), sizeof(smoothing));
                return true;
            }
            __except(Seh::Filter(GetExceptionCode())) { return false; }
        }

        // No stack arguments: the __fastcall wrapper receives this in ECX,
        // consumes the unused EDX, and preserves the original __thiscall ABI.
        void __fastcall Execute(void* process, void*) noexcept
        {
            const auto call = original;
            if (!call) return; // published before activation, removed at teardown
            const auto mask = requested.load(std::memory_order_relaxed);
            bool applied = false;
            if (mask && !faulted.load() && !NetworkSession())
            {
                bool local = false;
                if (!Apply(process, mask, local))
                {
                    faulted.store(true);
                    lastApplied.store(0);
                }
                else if (local)
                {
                    applied = true;
                }
            }
            call(process);
            // Stock also synthesizes turbo from full analogue throttle. Clear
            // the masked output cache after it runs, before craft simulation.
            if (applied && !faulted.load())
            {
                bool local = false;
                const auto current = requested.load(std::memory_order_relaxed);
                if (!Apply(process,current,local)) { faulted.store(true); lastApplied.store(0); }
                else if (local) { lastApplied.store(current); applications.fetch_add(1,std::memory_order_relaxed); }
            }
        }

        bool Install() noexcept
        {
            if (faulted.load()) return false;
            if (detour && detour->IsActive()) return Qualified();
            if (!Qualified()) return false;
            try
            {
                if (!detour)
                {
                    detour = new(std::nothrow) EntryDetour32(
                        EngineAddresses::PlayerInput::UserProcessExecute,
                        reinterpret_cast<const void*>(&Execute), entryBytes.size(), BasicPatch::Status::INACTIVE,
                        std::vector<std::uint8_t>(entryBytes.begin(),entryBytes.end()));
                    if (!detour || !detour->PrepareTrampoline()) { Shutdown(); return false; }
                    original = reinterpret_cast<ExecuteFn>(detour->GetTrampoline());
                }
                if (!original) { Shutdown(); return false; }
                detour->SetStatus(true);
                if (!detour->IsActive()) { Shutdown(); return false; }
                Logging::LogMessage("exu: opt-in local player control mask active (single player)");
                return true;
            }
            catch (...) { Shutdown(); return false; }
        }

        std::string_view OwnerArgument(lua_State* L)
        {
            std::size_t size = 0;
            const char* name = luaL_checklstring(L,1,&size);
            std::string_view owner(name,size);
            if (!Owners::ValidOwner(owner)) luaL_argerror(L,1,"owner must contain 1..63 bytes, without NUL");
            return owner;
        }
    }

    void ResetMissionState() noexcept
    {
        owners.Reset(); requested.store(0); lastApplied.store(0);
        applications.store(0); faulted.store(false);
    }
    void Shutdown() noexcept
    {
        requested.store(0);
        delete detour; detour = nullptr; original = nullptr;
        ResetMissionState();
    }
    int SetMask(lua_State* L)
    {
        auto owner = OwnerArgument(L);
        const double value = luaL_checknumber(L,2);
        if (!std::isfinite(value) || value<0 || value>static_cast<double>(All) || std::floor(value)!=value)
            return luaL_argerror(L,2,"mask must be an integer composed of exu.CONTROL flags");
        const auto mask = static_cast<std::uint32_t>(value);
        // Clearing remains possible even after a runtime fault or in a network session.
        if (mask && (!RuntimeGate::IsSupported() || NetworkSession() || !Install()))
        { lua_pushboolean(L,0); return 1; }
        const bool accepted = owners.Set(owner,mask);
        requested.store(owners.Combined());
        if (!requested.load()) lastApplied.store(0);
        lua_pushboolean(L,accepted); return 1;
    }
    int ClearMask(lua_State* L)
    {
        const bool cleared = owners.Set(OwnerArgument(L),0);
        requested.store(owners.Combined());
        if (!requested.load()) lastApplied.store(0);
        lua_pushboolean(L,cleared); return 1;
    }
    int GetState(lua_State* L)
    {
        const bool qualified = Qualified();
        const bool network = qualified && NetworkSession();
        lua_newtable(L);
        lua_pushboolean(L,qualified && !network); lua_setfield(L,-2,"available");
        lua_pushboolean(L,detour && detour->IsActive()); lua_setfield(L,-2,"hookActive");
        lua_pushinteger(L,requested.load()); lua_setfield(L,-2,"requestedMask");
        lua_pushinteger(L,lastApplied.load()); lua_setfield(L,-2,"lastAppliedMask");
        lua_pushinteger(L,owners.Count()); lua_setfield(L,-2,"ownerCount");
        lua_pushinteger(L,applications.load()); lua_setfield(L,-2,"applications");
        const char* reason = !RuntimeGate::IsSupported() ? "unsupported-build" : faulted.load() ? "runtime-fault" :
            !qualified ? "qualification-failed" : network ? "multiplayer-unsupported" : "available";
        lua_pushstring(L,reason); lua_setfield(L,-2,"reason");
        return 1;
    }
    void RegisterConstants(lua_State* L,int exuIndex)
    {
        lua_newtable(L);
        const struct { const char* name; std::uint32_t mask; } values[] = {
            {"THROTTLE",Throttle},{"STRAFE",Strafe},{"LOOK",Look},{"FIRE",Fire},
            {"JUMP",Jump},{"DEPLOY",Deploy},{"EJECT",Eject},{"ABANDON",Abandon},
            {"TURBO",Turbo},{"WEAPONS",Weapons},{"NAV",Nav},{"JETPACK",Jetpack},
            {"MOVEMENT",Movement},{"ALL",All}};
        for (auto& value : values) { lua_pushinteger(L,value.mask); lua_setfield(L,-2,value.name); }
        lua_setfield(L,exuIndex,"CONTROL");
    }
}
