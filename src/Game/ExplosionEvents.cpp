#include "Game/ExplosionEvents.h"
#include "Game/ExplosionEventsCore.h"
#include "EntryDetour32.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "Util/Logging.h"
#include "bzr.h"
#include <Windows.h>
#include <algorithm>
#include <new>

namespace ExtraUtilities::Lua::ExplosionEvents
{
    namespace
    {
        using Event = ExplosionEventsCore::Event;
        using BuildFn = void*(__thiscall*)(void*, BZR::MAT_3D*, void*);
        EntryDetour32* detour = nullptr;
        BuildFn original = nullptr;
        SRWLOCK lock = SRWLOCK_INIT;
        bool enabled = false;
        ExplosionEventsCore::Queue<512> queue;

        // GOG 2.2.301 ExplosionClass constructor: packed 8-character name at
        // +0x10, damageRadius float at +0x50. The latter is the destination of
        // ParameterDB::GetFloat(ExplosionClass, damageRadius), including defaults.
        // These are class data, not a retained pointer to the transient explosion.
        bool Snapshot(void* type, const BZR::MAT_3D* matrix, Event& out) noexcept
        {
            if (!type || !matrix) return false;
            __try {
                const auto* bytes = static_cast<const unsigned char*>(type);
                for (int i = 0; i < 8; ++i) out.odf[i] = static_cast<char>(bytes[0x10 + i] & 0x7f);
                out.odf[8] = 0;
                out.radius = *reinterpret_cast<const float*>(bytes + 0x50);
                out.x = matrix->posit_x; out.y = matrix->posit_y; out.z = matrix->posit_z;
                return true;
            } __except (Seh::Filter(GetExceptionCode())) { return false; }
        }

        void* __fastcall BuildHook(void* self, void*, BZR::MAT_3D* matrix, void* owner)
        {
            Event event;
            AcquireSRWLockShared(&lock);
            const bool record = enabled;
            ReleaseSRWLockShared(&lock);
            const bool valid = record && Snapshot(self, matrix, event);
            // Preserve the original return value, calling convention and effects.
            void* result = original(self, matrix, owner);
            if (result && valid) {
                AcquireSRWLockExclusive(&lock);
                if (enabled) queue.Push(event);
                ReleaseSRWLockExclusive(&lock);
            }
            return result;
        }

        bool Install()
        {
            if (!RuntimeGate::IsSupported()) return false;
            if (detour) return detour->IsActive();
            // Whole reloc-free prologue instructions from the already-qualified
            // Plant.ExplosionBuild catalog target. Never install over another hook.
            detour = new (std::nothrow) EntryDetour32(EngineAddresses::Plant::ExplosionBuild,
                reinterpret_cast<const void*>(&BuildHook), 6, BasicPatch::Status::INACTIVE,
                {0x55,0x8b,0xec,0x83,0xec,0x10});
            if (!detour || !detour->PrepareTrampoline()) { delete detour; detour = nullptr; return false; }
            original = reinterpret_cast<BuildFn>(detour->GetTrampoline());
            detour->SetStatus(true);
            if (!detour->IsActive()) { delete detour; detour = nullptr; original = nullptr; return false; }
            Logging::LogMessage("exu: local explosion event queue active (capacity 512)");
            return true;
        }
    }

    int SetEnabled(lua_State* L)
    {
        luaL_checktype(L, 1, LUA_TBOOLEAN);
        const bool on = lua_toboolean(L, 1) != 0;
        bool available = false;
        try { available = !on || Install(); } catch (...) { available = false; }
        AcquireSRWLockExclusive(&lock);
        if (enabled != (on && available)) queue.Clear();
        enabled = on && available;
        ReleaseSRWLockExclusive(&lock);
        lua_pushboolean(L, available); return 1;
    }

    int Drain(lua_State* L)
    {
        const int limit = std::clamp(luaL_optint(L, 1, 64), 1, 64);
        Event events[64]; int count = 0;
        AcquireSRWLockExclusive(&lock);
        while (count < limit && queue.Pop(events[count])) ++count;
        const auto dropped = queue.dropped;
        const auto pending = queue.size;
        ReleaseSRWLockExclusive(&lock);
        lua_createtable(L, count, 0);
        for (int i = 0; i < count; ++i) {
            const auto& e = events[i];
            lua_createtable(L, 0, 6);
            lua_pushstring(L,e.odf); lua_setfield(L,-2,"odf");
            lua_pushnumber(L,e.x); lua_setfield(L,-2,"x");
            lua_pushnumber(L,e.y); lua_setfield(L,-2,"y");
            lua_pushnumber(L,e.z); lua_setfield(L,-2,"z");
            lua_pushnumber(L,e.radius); lua_setfield(L,-2,"radius");
            lua_pushnumber(L,static_cast<lua_Number>(e.sequence)); lua_setfield(L,-2,"sequence");
            lua_rawseti(L,-2,i+1);
        }
        lua_pushnumber(L,static_cast<lua_Number>(dropped));
        lua_pushinteger(L,static_cast<lua_Integer>(pending));
        return 3;
    }

    void Shutdown() noexcept
    {
        AcquireSRWLockExclusive(&lock); enabled = false; queue.Clear(); ReleaseSRWLockExclusive(&lock);
        delete detour; detour = nullptr; original = nullptr;
    }
}
