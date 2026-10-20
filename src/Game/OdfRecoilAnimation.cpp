/* Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later; see COPYING. */
#include "Game/OdfRecoilAnimation.h"
#include "Game/GameObjectInternal.h"
#include "Game/FirstPersonTarget.h"
#include "Ogre/OgreEntityRuntime.h"
#include "Util/RuntimeGate.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <tuple>

// Native companion for OpenShim's opt-in ODF recoil trial. Reuses the exact
// target resolver and guarded AnimationState operations used by exu.animation
// and the FPS layers. It never calls Lua or advances the pilot FSM clock.
namespace ExtraUtilities::OdfRecoilAnimation
{
    namespace
    {
        namespace Detail = Lua::GameObject::Detail;
        using Key = std::tuple<uint32_t, bool, std::string>;
        struct Saved
        {
            void* entity = nullptr;
            void* state = nullptr;
            float time = 0, weight = 1;
            bool loop = false;
        };
        std::map<Key, Saved> saved;
        std::atomic<DWORD> ownerThread{0};

        void* Resolve(uint32_t handle, bool cockpit)
        {
            if (!RuntimeGate::IsSupported()) return nullptr;
            return Detail::GetRenderableEntity(Detail::EntityTarget{static_cast<BZR::handle>(handle), cockpit});
        }

        bool Restore(const Key& key, const Saved& old)
        {
            void* entity = Resolve(std::get<0>(key), std::get<1>(key));
            if (!entity || entity != old.entity) return true; // old entity gone: never dereference it
            void* state = Detail::GetNamedAnimationState(entity, std::get<2>(key));
            if (!state || state != old.state) return true;
            return Detail::TrySetAnimationEnabled(state, false) &&
                Detail::TrySetAnimationLoop(state, old.loop) &&
                Detail::TrySetAnimationWeight(state, old.weight) &&
                Detail::TrySetAnimationTimePosition(state, old.time);
        }
    }

    void Reset() noexcept
    {
        try { for (const auto& item : saved) Restore(item.first, item.second); } catch (...) {}
        saved.clear(); ownerThread.store(0);
    }

    // mode 0 queries an idle clip, 1 seeks/enables our clip, 2 restores it.
    int Apply(uint32_t handle, const char* name, int cockpit, int mode, float time, float* length)
    {
        const DWORD thread = GetCurrentThreadId();
        DWORD expected = 0;
        ownerThread.compare_exchange_strong(expected, thread);
        if (ownerThread.load() != thread) return 0;
        if (!handle || !name || (cockpit != 0 && cockpit != 1) || mode < 0 || mode > 2 ||
            !std::isfinite(time) || time < 0) return 0;
        const size_t count = strnlen(name, 64);
        if (!count || count > 63) return 0;
        Key key{handle, cockpit != 0, std::string(name, count)};
        auto previous = saved.find(key);
        if (mode == 2)
        {
            if (previous == saved.end()) return 1;
            if (!Restore(key, previous->second)) return 0;
            saved.erase(previous); return 1;
        }
        void* entity = Resolve(handle, cockpit != 0);
        if (!entity) return 0;
        void* state = Detail::GetNamedAnimationState(entity, std::get<2>(key));
        float duration = 0;
        if (!state || !Detail::TryGetAnimationLength(state, duration) ||
            !std::isfinite(duration) || duration <= 0 || duration > 60) return 0;
        if (length) *length = duration;
        if (previous != saved.end() && (previous->second.entity != entity || previous->second.state != state))
        { saved.erase(previous); previous = saved.end(); }
        bool enabled = false;
        if (previous == saved.end())
        {
            // Don't take ownership from a mission controller or the pilot FSM.
            if (!Detail::TryGetAnimationEnabled(state, enabled) || enabled) return 0;
            if (mode == 0) return 1;
            if (saved.size() >= 256) return 0;
            Saved old; old.entity = entity; old.state = state;
            if (!Detail::TryGetAnimationTimePosition(state, old.time) ||
                !Detail::TryGetAnimationWeight(state, old.weight) ||
                !Detail::TryGetAnimationLoop(state, old.loop) || !std::isfinite(old.time) ||
                !std::isfinite(old.weight) || old.time < 0 || old.weight < 0 || old.weight > 1) return 0;
            saved.emplace(key, old);
        }
        if (mode == 0) return 1;
        return Detail::TrySetAnimationLoop(state, false) &&
            Detail::TrySetAnimationWeight(state, 1) &&
            Detail::TrySetAnimationTimePosition(state, (std::min)(time, duration)) &&
            Detail::TrySetAnimationEnabled(state, true);
    }
}

extern "C" __declspec(dllexport) int __cdecl EXU_OdfRecoilAnimation(
    uint32_t handle, const char* clip, int cockpit, int mode, float time, float* length) noexcept
{
    try { return ExtraUtilities::OdfRecoilAnimation::Apply(handle, clip, cockpit, mode, time, length); }
    catch (...) { return 0; }
}
