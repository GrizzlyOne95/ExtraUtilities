// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Game/BuildEvents.h"
#include "Game/BuildEventsCore.h"
#include "EntryDetour32.h"
#include "LuaHelpers.h"
#include "Util/FeatureQualification.h"
#include "Util/SehGuard.h"
#include <algorithm>
#include <new>

namespace ExtraUtilities::Lua::BuildEvents
{
    namespace
    {
        using Event = BuildEventsCore::Event;
        using Type = BuildEventsCore::Type;
        using StartFn = bool(__thiscall*)(void*, void*);
        using CancelFn = bool(__thiscall*)(void*);
        using FinishFn = void*(__thiscall*)(void*);
        EntryDetour32* hooks[4]{};
        StartFn start = nullptr;
        CancelFn cancel = nullptr;
        FinishFn finish = nullptr, armoryFinish = nullptr;
        SRWLOCK lock = SRWLOCK_INIT;
        BuildEventsCore::Queue<512> queue;
        bool enabled = false;
        std::uint64_t generation = 0;

        bool Snapshot(void* self, void* cls, Type type, Event& event) noexcept
        {
            __try
            {
                if (!self || !cls) return false;
                const auto* object = static_cast<const unsigned char*>(self);
                if (*BZR::Multiplayer::isNetGame &&
                    object[BZR::Ownership::distributedObjectOffset + BZR::Ownership::stateOffset] != BZR::Ownership::stateLocal) return false;
                auto* producerClass = *reinterpret_cast<const unsigned char* const*>(object + 0xf8);
                if (!producerClass) return false;
                event.kind = *reinterpret_cast<const std::uint32_t*>(producerClass + 0x14);
                event.producer = BZR::GameObject::GetHandle(static_cast<BZR::GameObject*>(self));
                event.team = *reinterpret_cast<const int*>(object + 0x174);
                if (!event.producer || event.team < 0 || event.team > 15) return false;
                event.type = type;
                const auto* name = static_cast<const unsigned char*>(cls) + 0x30;
                for (int i = 0; i < 8; ++i) event.odf[i] = static_cast<char>(name[i] & 0x7f);
                event.odf[8] = 0;
                return event.odf[0] != 0;
            }
            __except (Seh::Filter(GetExceptionCode())) { return false; }
        }

        void* BuildClass(void* self) noexcept
        {
            __try { return *reinterpret_cast<void**>(static_cast<unsigned char*>(self) + 0x300); }
            __except (Seh::Filter(GetExceptionCode())) { return nullptr; }
        }

        bool Begin(std::uint64_t& epoch) noexcept
        {
            AcquireSRWLockShared(&lock); const bool on = enabled; epoch = generation; ReleaseSRWLockShared(&lock); return on;
        }
        void Record(const Event& event, std::uint64_t epoch) noexcept
        {
            AcquireSRWLockExclusive(&lock);
            if (enabled && epoch == generation) queue.Push(event);
            ReleaseSRWLockExclusive(&lock);
        }
        bool __fastcall StartHook(void* self, void*, void* cls)
        {
            Event event; std::uint64_t epoch;
            const bool capture = Begin(epoch) && Snapshot(self, cls, Type::Queued, event);
            const bool result = start(self, cls);
            if (capture && result && BuildClass(self) == cls) Record(event, epoch);
            return result;
        }
        bool __fastcall CancelHook(void* self, void*)
        {
            Event event; std::uint64_t epoch;
            const bool capture = Begin(epoch) && Snapshot(self, BuildClass(self), Type::Cancelled, event);
            const bool result = cancel(self);
            if (capture && result && !BuildClass(self)) Record(event, epoch);
            return result;
        }
        void* Finish(void* self, FinishFn original)
        {
            Event event; std::uint64_t epoch;
            const bool capture = Begin(epoch) && Snapshot(self, BuildClass(self), Type::Completed, event);
            void* result = original(self);
            if (capture && !BuildClass(self))
            {
                if (result && BZR::GameObject::IsLiveArenaObject(result))
                    event.built = BZR::GameObject::GetHandle(static_cast<BZR::GameObject*>(result));
                Record(event, epoch);
            }
            return result;
        }
        void* __fastcall FinishHook(void* self, void*) { return Finish(self, finish); }
        void* __fastcall ArmoryHook(void* self, void*) { return Finish(self, armoryFinish); }

        bool Install()
        {
            if (hooks[0]) return hooks[0]->IsActive();
            if (!QualifyFeature("Build events ", 4)) return false;
            using namespace EngineAddresses::ProducerEvents;
            hooks[0] = new (std::nothrow) EntryDetour32(StartBuild, reinterpret_cast<void*>(&StartHook), 6, BasicPatch::Status::INACTIVE, {0x55,0x8b,0xec,0x83,0xec,0x0c});
            hooks[1] = new (std::nothrow) EntryDetour32(CancelBuild, reinterpret_cast<void*>(&CancelHook), 6, BasicPatch::Status::INACTIVE, {0x55,0x8b,0xec,0x83,0xec,0x0c});
            hooks[2] = new (std::nothrow) EntryDetour32(FinishBuild, reinterpret_cast<void*>(&FinishHook), 9, BasicPatch::Status::INACTIVE, {0x55,0x8b,0xec,0x81,0xec,0x34,0x02,0,0});
            hooks[3] = new (std::nothrow) EntryDetour32(ArmoryFinishBuild, reinterpret_cast<void*>(&ArmoryHook), 6, BasicPatch::Status::INACTIVE, {0x55,0x8b,0xec,0x83,0xec,0x08});
            for (auto* hook : hooks) if (!hook || !hook->PrepareTrampoline()) { Shutdown(); return false; }
            start = reinterpret_cast<StartFn>(hooks[0]->GetTrampoline());
            cancel = reinterpret_cast<CancelFn>(hooks[1]->GetTrampoline());
            finish = reinterpret_cast<FinishFn>(hooks[2]->GetTrampoline());
            armoryFinish = reinterpret_cast<FinishFn>(hooks[3]->GetTrampoline());
            for (auto* hook : hooks) { hook->SetStatus(true); if (!hook->IsActive()) { Shutdown(); return false; } }
            return true;
        }
    }

    int SetEnabled(lua_State* L)
    {
        const bool on = CheckBool(L, 1);
        bool available = !on;
        try { if (on) available = Install(); } catch (...) { Shutdown(); available = false; }
        AcquireSRWLockExclusive(&lock);
        if (enabled != (on && available)) { queue.Clear(); ++generation; }
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
        const auto dropped = queue.dropped; const auto pending = queue.size;
        ReleaseSRWLockExclusive(&lock);
        lua_createtable(L, count, 0);
        for (int i = 0; i < count; ++i)
        {
            const auto& event = events[i];
            lua_createtable(L, 0, 7);
            lua_pushstring(L, event.type == Type::Queued ? "queued" : event.type == Type::Cancelled ? "cancelled" : "completed"); lua_setfield(L, -2, "type");
            lua_pushlightuserdata(L, reinterpret_cast<void*>(event.producer)); lua_setfield(L, -2, "producer");
            if (event.built) lua_pushlightuserdata(L, reinterpret_cast<void*>(event.built)); else lua_pushnil(L);
            lua_setfield(L, -2, "built");
            lua_pushinteger(L, event.team); lua_setfield(L, -2, "team");
            lua_pushstring(L, event.odf); lua_setfield(L, -2, "odf");
            lua_pushstring(L, event.kind == 0x41524d52 ? "armory" : event.kind == 0x434e5354 ? "constructor" : "producer"); lua_setfield(L, -2, "producerType");
            lua_pushnumber(L, static_cast<lua_Number>(event.sequence)); lua_setfield(L, -2, "sequence");
            lua_rawseti(L, -2, i + 1);
        }
        lua_pushnumber(L, static_cast<lua_Number>(dropped)); lua_pushinteger(L, static_cast<int>(pending)); return 3;
    }

    void Shutdown() noexcept
    {
        AcquireSRWLockExclusive(&lock); enabled = false; ++generation; queue.Clear(); ReleaseSRWLockExclusive(&lock);
        for (auto*& hook : hooks) { delete hook; hook = nullptr; }
        start = nullptr; cancel = nullptr; finish = armoryFinish = nullptr;
    }
}
