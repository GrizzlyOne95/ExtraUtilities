// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/VehicleControl.h"
#include "Game/VehicleControlPolicy.h"
#include "EntryDetour32.h"
#include "LuaHelpers.h"
#include "Util/MsvcRtti.h"
#include "Util/SehGuard.h"
#include <array>
#include <cstring>
#include <limits>
#include <new>

namespace ExtraUtilities::Lua::VehicleControl
{
    namespace
    {
        namespace Policy = ExtraUtilities::VehicleControl;
        namespace Addresses = EngineAddresses::VehicleControl;
        Policy::Policy<> controls;
        EntryDetour32* detour = nullptr;
        using ExecuteFn = void(__thiscall*)(void*);
        ExecuteFn original = nullptr;
        void __fastcall Execute(void* process, void*);
        constexpr std::array<std::uint8_t, 9> entryBytes = {0x55,0x8B,0xEC,0x83,0xEC,0x08,0x89,0x4D,0xFC};
        struct View
        {
            BZR::GameObject* object = nullptr;
            void* process = nullptr;
            std::uintptr_t vtable = 0;
            std::uint8_t* sim = nullptr;
            void* carrier = nullptr;
        };
        template<class T> T& Field(void* p, std::size_t offset) noexcept
        {
            return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
        }
        // Only call inside a POD-only SEH shell. RTTI bounds are conservative;
        // neither a class name substring nor an arbitrary task pointer is enough.
        bool HasBase(void* p, const char* name)
        {
            if (!p) return false;
            auto** vt = *reinterpret_cast<void***>(p);
            const auto* col = reinterpret_cast<const MsvcRtti::CompleteObjectLocator*>(vt[-1]);
            if (col->signature != 0 || !col->pClassDescriptor) return false;
            const auto* hierarchy = col->pClassDescriptor;
            if (hierarchy->numBaseClasses > 64) return false;
            for (std::uint32_t n = 0; n < hierarchy->numBaseClasses; ++n)
                if (std::strcmp(hierarchy->pBaseClassArray[n]->pTypeDescriptor->name, name) == 0) return true;
            return false;
        }
        void CopyType(void* p, char (&name)[80])
        {
            name[0] = 0;
            if (!p) return;
            auto** vt = *reinterpret_cast<void***>(p);
            const auto* col = reinterpret_cast<const MsvcRtti::CompleteObjectLocator*>(vt[-1]);
            const char* raw = col->pTypeDescriptor->name;
            if (std::strncmp(raw, ".?AV", 4) == 0 || std::strncmp(raw, ".?AU", 4) == 0) raw += 4;
            std::size_t n = 0;
            while (n < sizeof(name)-1 && raw[n] && raw[n] != '@') { name[n] = raw[n]; ++n; }
            name[n] = 0;
        }
        float Now() noexcept
        {
            __try { return *reinterpret_cast<const float*>(EngineAddresses::Time::SimTimeSeconds); }
            __except(Seh::Filter(GetExceptionCode())) { return std::numeric_limits<float>::quiet_NaN(); }
        }
        bool OwnsHook() noexcept
        {
            if (!detour || !detour->IsActive()) return false;
            std::array<std::uint8_t,9> installed{0x68,0,0,0,0,0xC3,0x90,0x90,0x90};
            const auto address = reinterpret_cast<std::uint32_t>(&Execute);
            std::memcpy(installed.data()+1,&address,sizeof(address));
            return SignatureResolver::MatchBytes(Addresses::UnitProcessExecute,installed);
        }
        bool Qualified()
        {
            if (!RuntimeGate::IsSupported()) return false;
            unsigned count = 0;
            for (const auto& anchor : BzrBuildProfile::kRuntimeAnchors)
                if (std::strncmp(anchor.name, "VehicleControl.", 15) == 0)
                {
                    ++count;
                    if (std::strcmp(anchor.name, "VehicleControl.UnitProcessExecute") == 0 && OwnsHook()) continue;
                    if (!BuildValidation::Detail::MatchAnchor(GetModuleHandleA(nullptr), anchor)) return false;
                }
            return count == 5;
        }
        bool ReadView(BZR::handle h, View& v, const char*& reason) noexcept
        {
            v = {};
            reason = "invalid or destroyed handle";
            __try
            {
                if (!RuntimeGate::IsSupported()) { reason = "unsupported executable"; return false; }
                if (*BZR::Multiplayer::isNetGame) { reason = "vehicle control v1 is single-player only"; return false; }
                v.object = BZR::GameObject::GetObj(h);
                if (!v.object) return false;
                if (v.object == *reinterpret_cast<BZR::GameObject**>(BZR::GameObject::p_userObject))
                { reason = "the local player cannot be claimed"; return false; }
                if (Field<std::uintptr_t>(v.object, 0) != Addresses::HoverCraftVtable)
                { reason = "vehicle control v1 requires an ordinary HoverCraft"; return false; }
                v.process = Field<void*>(v.object, 0xFC);
                if (!v.process || !HasBase(v.process, ".?AVUnitProcess@@"))
                { reason = "vehicle has no supported UnitProcess"; return false; }
                v.vtable = Field<std::uintptr_t>(v.process, 0);
                if (Field<std::uintptr_t>(reinterpret_cast<void*>(v.vtable), 0x1C) != Addresses::UnitProcessExecute ||
                    Field<void*>(v.process, 0x0C) != v.object || Field<void*>(v.process, 0x34) != v.object ||
                    Field<std::uint32_t>(v.process, 0x14) != 0)
                { reason = "AI process identity or Execute is unsupported"; return false; }
                v.sim = Field<std::uint8_t*>(v.object, 0x230);
                v.carrier = Field<void*>(v.object, 0x1A0);
                if (!v.sim) { reason = "vehicle has no simulation control block"; return false; }
                reason = nullptr;
                return true;
            }
            __except(Seh::Filter(GetExceptionCode())) { v = {}; reason = "native object inspection failed"; return false; }
        }
        bool Same(const Policy::Entry& e, const View& v) noexcept
        {
            return e.process == reinterpret_cast<std::uintptr_t>(v.process) && e.processVtable == v.vtable;
        }
        void ClearEntry(const Policy::Entry& e) noexcept
        {
            View v{}; const char* reason = nullptr;
            if (!ReadView(e.handle, v, reason) || !Same(e, v)) return;
            __try { Field<Policy::Inputs>(v.sim, 0xC4) = {}; }
            __except(Seh::Filter(GetExceptionCode())) {}
        }
        struct EngineSink
        {
            const View& view;
            BZR::handle handle;
            void SetInputs(const Policy::Inputs& i) { Field<Policy::Inputs>(view.sim,0xC4) = i; }
            void Clear() { SetInputs({}); }
            void* Carrier()
            {
                View current{}; const char* reason = nullptr;
                return ReadView(handle,current,reason) && current.process == view.process && current.vtable == view.vtable
                    ? current.carrier : nullptr;
            }
            void Select(std::uint32_t mask)
            {
                if (auto* carrier = Carrier()) reinterpret_cast<void(__thiscall*)(void*,std::uint32_t)>(Addresses::SetSelected)(carrier,mask);
            }
            void Fire() { if (auto* carrier = Carrier()) reinterpret_cast<void(__thiscall*)(void*)>(Addresses::FireSelected)(carrier); }
            void Special() { if (auto* carrier = Carrier()) reinterpret_cast<void(__thiscall*)(void*)>(Addresses::FireSpecial)(carrier); }
        };
        bool Apply(void* process) noexcept
        {
            // Resolve through the live handle arena before touching a saved entry.
            // No Lua calls, allocations or retained task pointers on this path.
            __try
            {
                auto* object = Field<BZR::GameObject*>(process, 0x0C);
                if (!BZR::GameObject::IsLiveArenaObject(object)) return false;
                const auto h = BZR::GameObject::GetHandle(object);
                auto* e = controls.Find(h);
                if (!e) return false;
                View v{}; const char* reason = nullptr;
                if (!ReadView(h, v, reason) || !Same(*e, v) || process != v.process)
                { controls.Release(h); return false; }
                EngineSink sink{v,h};
                return Policy::Tick(controls,h,reinterpret_cast<std::uintptr_t>(process),v.vtable,Now(),sink);
            }
            __except(Seh::Filter(GetExceptionCode())) { ResetMissionState(); return false; }
        }
        void __fastcall Execute(void* process, void*)
        {
            bool applied = false;
            try { applied = Apply(process); }
            catch (...) { ResetMissionState(); }
            if (!applied && original) original(process);
        }
        bool Install()
        {
            if (!Qualified()) return false;
            if (OwnsHook()) return true;
            if (!detour)
                detour = new EntryDetour32(Addresses::UnitProcessExecute, reinterpret_cast<const void*>(&Execute),
                    entryBytes.size(), BasicPatch::Status::INACTIVE, {entryBytes.begin(),entryBytes.end()});
            if (!detour->PrepareTrampoline()) return false;
            original = reinterpret_cast<ExecuteFn>(detour->GetTrampoline());
            detour->SetStatus(true);
            if (!detour->IsActive()) return false;
            Logging::LogMessage("exu: mission vehicle control enabled (single-player hovercraft)");
            return true;
        }
        int Result(lua_State* L, bool ok, const char* reason = nullptr)
        {
            lua_pushboolean(L, ok);
            if (ok) return 1;
            lua_pushstring(L, reason ? reason : "vehicle controller unavailable");
            return 2;
        }
        void RawField(lua_State* L, const char* name) { lua_pushstring(L,name); lua_rawget(L,2); }
        float Axis(lua_State* L, const char* name)
        {
            RawField(L,name);
            float value = 0;
            if (!lua_isnil(L,-1))
            {
                const double x = lua_tonumber(L,-1);
                if (lua_type(L,-1) != LUA_TNUMBER || !std::isfinite(x) || x < -1 || x > 1)
                    luaL_error(L,"SetVehicleControls.%s must be a finite number in [-1,1]",name);
                value = static_cast<float>(x);
            }
            lua_pop(L,1); return value;
        }
        bool Boolean(lua_State* L, const char* name)
        {
            RawField(L,name);
            if (!lua_isnil(L,-1) && lua_type(L,-1) != LUA_TBOOLEAN)
                luaL_error(L,"SetVehicleControls.%s must be boolean",name);
            const bool value = lua_toboolean(L,-1) != 0;
            lua_pop(L,1); return value;
        }
        Policy::Command Parse(lua_State* L)
        {
            luaL_checktype(L,2,LUA_TTABLE);
            lua_pushnil(L);
            while (lua_next(L,2))
            {
                if (lua_type(L,-2) != LUA_TSTRING) luaL_error(L,"SetVehicleControls keys must be strings");
                std::size_t length = 0;
                const char* key = lua_tolstring(L,-2,&length);
                if (std::strlen(key) != length) luaL_error(L,"SetVehicleControls keys cannot contain NUL");
                constexpr const char* keys[] = {"steer","pitch","strafe","throttle","turbo","jump","fire","special","weaponMask","timeout"};
                bool known = false;
                for (const auto* k : keys) if (std::strcmp(k,key) == 0) known = true;
                if (!known) luaL_error(L,"unsupported SetVehicleControls field '%s'",key);
                lua_pop(L,1);
            }
            Policy::Command c{};
            c.inputs.steer = Axis(L,"steer"); c.inputs.pitch = Axis(L,"pitch");
            c.inputs.strafe = Axis(L,"strafe"); c.inputs.throttle = Axis(L,"throttle");
            c.inputs.turbo = Boolean(L,"turbo"); c.inputs.jump = Boolean(L,"jump");
            c.fire = Boolean(L,"fire"); c.special = Boolean(L,"special");
            RawField(L,"weaponMask");
            if (!lua_isnil(L,-1))
            {
                const auto n = lua_tonumber(L,-1);
                if (lua_type(L,-1) != LUA_TNUMBER || !std::isfinite(n) || n < 0 || n > 31 || std::floor(n) != n)
                    luaL_error(L,"SetVehicleControls.weaponMask must be an integer in [0,31]");
                c.selectWeapons = true; c.weaponMask = static_cast<std::uint32_t>(n);
            }
            lua_pop(L,1);
            RawField(L,"timeout");
            if (!lua_isnil(L,-1))
            {
                const auto n = lua_tonumber(L,-1);
                if (lua_type(L,-1) != LUA_TNUMBER || !std::isfinite(n) || n < 0.01 || n > 5)
                    luaL_error(L,"SetVehicleControls.timeout must be finite seconds in [0.01,5]");
                c.timeout = static_cast<float>(n);
            }
            lua_pop(L,1); return c;
        }
        struct Snapshot
        {
            Policy::Inputs inputs{};
            int current = 0, pending = 0, saved = 0, taskState = 0, taskNext = -1;
            bool unitTask = false;
            std::uint32_t selected = 0, enabled = 0;
            char processType[80]{}, taskType[80]{};
        };
        bool ReadSnapshot(const View& v, Snapshot& s) noexcept
        {
            __try
            {
                s.inputs = Field<Policy::Inputs>(v.sim,0xC4);
                s.current = Field<int>(v.process,0x1C); s.pending = Field<int>(v.process,0x20); s.saved = Field<int>(v.process,0x24);
                CopyType(v.process,s.processType);
                auto* task = Field<void*>(v.process,0x38);
                CopyType(task,s.taskType);
                s.unitTask = HasBase(task,".?AVUnitTask@@");
                if (s.unitTask) { s.taskState = Field<int>(task,0x08); s.taskNext = Field<int>(task,0x0C); }
                if (v.carrier) { s.selected = Field<std::uint32_t>(v.carrier,0x30); s.enabled = Field<std::uint32_t>(v.carrier,0x34); }
                return true;
            }
            __except(Seh::Filter(GetExceptionCode())) { return false; }
        }
        void Number(lua_State* L, const char* name, double x) { lua_pushnumber(L,x); lua_setfield(L,-2,name); }
        void Bool(lua_State* L, const char* name, bool x) { lua_pushboolean(L,x); lua_setfield(L,-2,name); }
        void String(lua_State* L, const char* name, const char* x) { lua_pushstring(L,x); lua_setfield(L,-2,name); }
        void PushInputs(lua_State* L, const Policy::Inputs& i)
        {
            lua_createtable(L,0,6);
            Number(L,"steer",i.steer); Number(L,"pitch",i.pitch); Number(L,"strafe",i.strafe); Number(L,"throttle",i.throttle);
            Bool(L,"turbo",i.turbo != 0); Bool(L,"jump",i.jump != 0);
        }
    }
    int HasVehicleControl(lua_State* L)
    {
        bool available = Qualified();
        if (available)
        {
            __try { available = !*BZR::Multiplayer::isNetGame; }
            __except(Seh::Filter(GetExceptionCode())) { available = false; }
        }
        lua_pushboolean(L,available); return 1;
    }
    int ClaimVehicleControl(lua_State* L)
    {
        const auto h = CheckHandle(L,1);
        View v{}; const char* reason = nullptr;
        if (!ReadView(h,v,reason)) return Result(L,false,reason);
        if (!Install()) return Result(L,false,"vehicle control hook failed qualification or installation");
        for (auto& e : controls.Entries()) if (e.handle && (e.Expired(Now()) || !BZR::GameObject::GetObj(e.handle)))
        { ClearEntry(e); e = {}; }
        return Result(L,controls.Claim(h,reinterpret_cast<std::uintptr_t>(v.process),v.vtable,Now()),"vehicle control capacity or identity check failed");
    }
    int SetVehicleControls(lua_State* L)
    {
        const auto h = CheckHandle(L,1);
        const auto command = Parse(L); // All errors precede any native/cache write.
        if (!OwnsHook()) return Result(L,false,"vehicle controller hook is inactive; claim first");
        View v{}; const char* reason = nullptr;
        if (!ReadView(h,v,reason)) return Result(L,false,reason);
        auto* e = controls.Find(h);
        if (!e || !Same(*e,v)) return Result(L,false,"vehicle is not claimed by this mission");
        return Result(L,controls.Set(h,command,Now()),"vehicle command lease expired; claim again");
    }
    int GetVehicleControlState(lua_State* L)
    {
        const auto h = CheckHandle(L,1);
        View v{}; const char* reason = nullptr;
        Snapshot s{};
        if (!RuntimeGate::IsSupported() || !ReadView(h,v,reason) || !ReadSnapshot(v,s))
        { lua_pushnil(L); lua_pushstring(L,reason ? reason : "native vehicle snapshot unavailable"); return 2; }
        const auto* e = controls.Find(h);
        const bool claimed = e && Same(*e,v) && !e->Expired(Now());
        lua_createtable(L,0,18);
        Bool(L,"claimed",claimed); Bool(L,"hookActive",OwnsHook());
        String(L,"processType",s.processType);
        Number(L,"processState",s.current); String(L,"processStateName",Policy::ProcessStateName(s.current));
        Number(L,"pendingProcessState",s.pending); Number(L,"savedProcessState",s.saved);
        String(L,"taskType",s.taskType);
        if (s.unitTask)
        {
            Number(L,"taskState",s.taskState); String(L,"taskStateName",Policy::TaskStateName(s.taskState));
            Number(L,"pendingTaskState",s.taskNext);
        }
        Number(L,"selectedWeaponMask",s.selected); Number(L,"enabledWeaponMask",s.enabled);
        PushInputs(L,s.inputs); lua_setfield(L,-2,"inputs");
        if (claimed)
        {
            Number(L,"applications",e->applications); Number(L,"leaseRemaining",e->deadline-Now());
            Bool(L,"fire",e->command.fire); Bool(L,"special",e->command.special);
            PushInputs(L,e->command.inputs); lua_setfield(L,-2,"requested");
            PushInputs(L,e->applied); lua_setfield(L,-2,"lastApplied");
        }
        return 1;
    }
    int ReleaseVehicleControl(lua_State* L)
    {
        const auto h = CheckHandle(L,1);
        if (const auto* e = controls.Find(h)) ClearEntry(*e);
        controls.Release(h); return Result(L,true);
    }
    int ResetVehicleControls(lua_State* L) { ResetMissionState(); return Result(L,true); }
    void ResetMissionState() noexcept
    {
        for (const auto& e : controls.Entries()) if (e.handle) ClearEntry(e);
        controls.Reset();
    }
    void Shutdown() noexcept
    {
        ResetMissionState();
        delete detour; detour = nullptr; original = nullptr;
    }
}
