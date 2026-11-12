// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/PlayerControls.h"
#include "Game/PlayerControlPolicy.h"
#include "EntryDetour32.h"
#include "Util/BuildValidation.h"
#include "Util/MsvcRtti.h"
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
        // Lua and UserProcess::Execute both run on the simulation thread.
        Injection injection;
        unsigned injections = 0;
        bool needZero = false;
        unsigned fires = 0;
        EntryDetour32* detour = nullptr;
        ExecuteFn original = nullptr;
        constexpr std::array<std::uint8_t, 9> entryBytes = {0x55,0x8B,0xEC,0x81,0xEC,0x1C,0x02,0x00,0x00};
        constexpr std::array<std::uint8_t, 22> bufferRead = {
            0x66,0xA1,0x9C,0x98,0x91,0x00,0x66,0x89,0x85,0xBC,0xFE,0xFF,0xFF,
            0x6A,0x38,0x6A,0x00,0x68,0x9C,0x98,0x91,0x00};

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

        float Now() noexcept
        {
            __try { return *reinterpret_cast<const float*>(EngineAddresses::Time::SimTimeSeconds); }
            __except(Seh::Filter(GetExceptionCode())) { return NAN; }
        }

        // Scripted fire reuses the AI path (Carrier::FireSelected) on ordinary hovercraft only.
        bool FireQualified() noexcept
        {
            static const bool cached = [] {
            unsigned matched = 0;
            for (const auto& anchor : BzrBuildProfile::kRuntimeAnchors)
                if (std::strcmp(anchor.name,"VehicleControl.FireSelected") == 0 ||
                    std::strcmp(anchor.name,"VehicleControl.HoverCraftVtable") == 0)
                {
                    if (!BuildValidation::Detail::MatchAnchor(GetModuleHandleA(nullptr),anchor)) return false;
                    ++matched;
                }
            return matched == 2;
            }();
            return cached;
        }

        // Ordinary player craft are Wingman (and other HoverCraft subclasses),
        // not bare HoverCraft. Accept a primary vtable whose RTTI lists
        // .?AVHoverCraft@@ as a non-virtual base at displacement 0, so the
        // carrier offset stays valid. Caller holds SEH; cached per vtable.
        bool IsHoverCraft(std::uintptr_t vtable) noexcept
        {
            if (vtable == EngineAddresses::VehicleControl::HoverCraftVtable) return true;
            static std::uintptr_t cachedVtable = 0;
            static bool cachedResult = false;
            if (vtable == cachedVtable) return cachedResult;
            bool result = false;
            const auto* col = *(reinterpret_cast<MsvcRtti::CompleteObjectLocator* const*>(vtable) - 1);
            if (col && col->signature == 0 && col->offset == 0 && col->pClassDescriptor && col->pClassDescriptor->pBaseClassArray)
            {
                const auto* hierarchy = col->pClassDescriptor;
                for (std::uint32_t i = 0; i < hierarchy->numBaseClasses && i < 64; ++i)
                {
                    const auto* base = hierarchy->pBaseClassArray[i];
                    if (base && base->pTypeDescriptor && base->where.mdisp == 0 && base->where.pdisp == -1
                        && std::strcmp(base->pTypeDescriptor->name, ".?AVHoverCraft@@") == 0) { result = true; break; }
                }
            }
            cachedVtable = vtable;
            cachedResult = result;
            return result;
        }

        // Writes GAS_CTRL slots 0..5 after stock UserProcess::Execute, the same
        // point the mask clears its output cache before craft simulation.
        bool WriteGas(void* process, const InjectedInputs& inputs, bool fire, bool& local) noexcept
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
                Gas values{};
                std::memcpy(values.data(), gas+0xC4, sizeof(values));
                PlayerControlPolicy::Inject(values, inputs);
                std::memcpy(gas+0xC4, values.data(), sizeof(values));
                if (fire && IsHoverCraft(*reinterpret_cast<std::uintptr_t*>(object)))
                    if (auto* carrier = *reinterpret_cast<void**>(object+0x1A0))
                    { reinterpret_cast<void(__thiscall*)(void*)>(EngineAddresses::VehicleControl::FireSelected)(carrier); ++fires; }
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
            const bool usable = !faulted.load();
            bool applied = false;
            // An expired lease zeroes the smoothing cache once, so held script
            // input does not bleed into the player's next frames.
            // Stays pending until the local player's own process takes the zero.
            if (injection.active) needZero = true;
            const bool inject = usable && injection.Live(Now());
            if (usable && needZero && !inject)
            {
                bool local = false;
                if (!WriteGas(process, {}, false, local)) { faulted.store(true); lastApplied.store(0); }
                else if (local) needZero = false;
            }
            if (mask && usable)
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
            // Scripted inputs win over the mask: the mask filters the user, not scripts.
            if (inject && !faulted.load() && injection.active)
            {
                bool local = false;
                const auto command = injection.command; // a fire callback may change the lease
                if (!WriteGas(process, command.inputs, command.fire, local)) { faulted.store(true); lastApplied.store(0); }
                else if (local) ++injections;
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
                Logging::LogMessage("exu: opt-in local player control mask active");
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
        injection.Clear(); injections = 0; needZero = false; fires = 0;
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
        // Clearing remains possible even after a runtime fault.
        if (mask && (!RuntimeGate::IsSupported() || !Install()))
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
    int SetInputs(lua_State* L)
    {
        luaL_checktype(L,1,LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L,1))
        {
            if (lua_type(L,-2) != LUA_TSTRING) luaL_error(L,"SetPlayerInputs keys must be strings");
            std::size_t length = 0;
            const char* key = lua_tolstring(L,-2,&length);
            if (std::strlen(key) != length) luaL_error(L,"SetPlayerInputs keys must not contain NUL");
            constexpr const char* keys[] = {"steer","pitch","strafe","throttle","turbo","jump","fire","timeout"};
            bool known = false;
            for (const auto* k : keys) if (std::strcmp(k,key) == 0) known = true;
            if (!known) luaL_error(L,"unsupported SetPlayerInputs field '%s'",key);
            lua_pop(L,1);
        }
        const auto number = [L](const char* name, double low, double high, double fallback) {
            lua_getfield(L,1,name);
            double value = fallback;
            if (!lua_isnil(L,-1))
            {
                value = lua_tonumber(L,-1);
                if (lua_type(L,-1) != LUA_TNUMBER || !std::isfinite(value) || value < low || value > high)
                    luaL_error(L,"SetPlayerInputs.%s must be a finite number in [%g,%g]",name,low,high);
            }
            lua_pop(L,1); return static_cast<float>(value);
        };
        const auto boolean = [L](const char* name) {
            lua_getfield(L,1,name);
            if (!lua_isnil(L,-1) && lua_type(L,-1) != LUA_TBOOLEAN) luaL_error(L,"SetPlayerInputs.%s must be boolean",name);
            const bool value = lua_toboolean(L,-1) != 0;
            lua_pop(L,1); return value;
        };
        InjectedCommand c{};
        c.inputs.steer = number("steer",-1,1,0); c.inputs.pitch = number("pitch",-1,1,0);
        c.inputs.strafe = number("strafe",-1,1,0); c.inputs.throttle = number("throttle",-1,1,0);
        c.inputs.turbo = boolean("turbo"); c.inputs.jump = boolean("jump");
        c.fire = boolean("fire");
        c.timeout = number("timeout",0.01,5,0.5);
        // All argument errors precede any state change.
        const char* reason = !RuntimeGate::IsSupported() ? "unsupported-build" :
            (c.fire && !FireQualified()) ? "fire-qualification-failed" : !Install() ? "qualification-failed" : nullptr;
        const float now = reason ? 0.f : Now();
        if (!reason && !std::isfinite(now)) reason = "clock-unavailable";
        if (!reason && !injection.Set(c,now)) reason = "invalid-command";
        lua_pushboolean(L,!reason);
        if (!reason) return 1;
        lua_pushstring(L,reason); return 2;
    }
    int ClearInputs(lua_State* L)
    {
        // Leave the lease to expire on the next native update so the smoothing cache is zeroed there.
        if (injection.active) injection.deadline = injection.issued;
        lua_pushboolean(L,1); return 1;
    }
    int GetState(lua_State* L)
    {
        const bool qualified = Qualified();
        lua_newtable(L);
        lua_pushboolean(L,qualified); lua_setfield(L,-2,"available");
        lua_pushboolean(L,detour && detour->IsActive()); lua_setfield(L,-2,"hookActive");
        lua_pushinteger(L,requested.load()); lua_setfield(L,-2,"requestedMask");
        lua_pushinteger(L,lastApplied.load()); lua_setfield(L,-2,"lastAppliedMask");
        lua_pushinteger(L,owners.Count()); lua_setfield(L,-2,"ownerCount");
        lua_pushinteger(L,applications.load()); lua_setfield(L,-2,"applications");
        const float now = Now();
        const bool injecting = injection.active && std::isfinite(now) && now >= injection.issued && now < injection.deadline;
        lua_pushboolean(L,injecting); lua_setfield(L,-2,"injecting");
        lua_pushinteger(L,injections); lua_setfield(L,-2,"injections");
        lua_pushinteger(L,fires); lua_setfield(L,-2,"injectedFires");
        lua_pushnumber(L,injecting ? injection.deadline-now : 0); lua_setfield(L,-2,"injectRemaining");
        const char* reason = !RuntimeGate::IsSupported() ? "unsupported-build" : faulted.load() ? "runtime-fault" :
            !qualified ? "qualification-failed" : "available";
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
