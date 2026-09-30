/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Util/Soundtrack.h"
#include "Util/BuildValidation.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "OpenShimBridge.h"
#include <dsound.h>
#include <cstdio>
#include <cstring>

namespace ExtraUtilities::Soundtrack
{
    namespace
    {
        namespace Addresses = EngineAddresses::Soundtrack;
        using SelectFn = void(__cdecl*)(int, int, int, int);
        using VoidFn = void(__cdecl*)();
        using SizeFn = int(__cdecl*)(const char*);
        using BufferFn = IDirectSoundBuffer*(__cdecl*)(int);
        bool attempted = false;
        bool qualified = false;
        FadeController controller;

        bool NativeAvailable()
        {
            if (!RuntimeGate::IsSupported())
                return false;
            if (attempted)
                return qualified;
            attempted = true;
            const HMODULE module = GetModuleHandleA(nullptr);
            // This backend deliberately uses the recorded fixed layout. A
            // relocated/new image needs qualification, not address guessing.
            if (reinterpret_cast<uintptr_t>(module) != BzrBuildProfile::kImageBase)
                return false;
            size_t matched = 0;
            for (const auto& anchor : BzrBuildProfile::kRuntimeAnchors)
            {
                if (std::strncmp(anchor.name, "Soundtrack ", 11) != 0)
                    continue;
                if (!BuildValidation::Detail::MatchAnchor(module, anchor) ||
                    BuildValidation::Detail::CountTextMatches(module, anchor, 2) != 1)
                    return false;
                ++matched;
            }
            qualified = matched == 3;
            Logging::LogMessage("[EXU::Soundtrack] native backend %s", qualified ? "qualified" : "unavailable");
            return qualified;
        }

        template<typename T>
        T At(uintptr_t address) noexcept { return reinterpret_cast<T>(address); }

        bool ReadNative(State& state)
        {
            if (!NativeAvailable())
                return false;
            State snapshot;
            bool valid = false;
            const bool completed = Seh::Guard("Soundtrack.Read", [&]
            {
                snapshot.track = *At<const int*>(Addresses::SelectedTrack);
                snapshot.slot = *At<const int*>(Addresses::OggSlot);
                const int started = *At<const int*>(Addresses::Started);
                const int paused = *At<const int*>(Addresses::Paused);
                const uintptr_t profile = *At<const uintptr_t*>(EngineAddresses::SoundOptions::soundStruct1);
                if (!SignatureResolver::IsReadableRange(reinterpret_cast<const void*>(profile + 0x2A), 1))
                    return;
                snapshot.userVolume = *At<const unsigned char*>(profile + 0x2A);
                snapshot.started = started != 0;
                snapshot.paused = paused != 0;
                valid = snapshot.track >= -1 && snapshot.slot >= -1 && snapshot.slot <= 1 &&
                    (started == 0 || started == 1) && (paused == 0 || paused == 1) &&
                    snapshot.userVolume <= 10;
            });
            if (!completed || !valid)
                return false;
            state = snapshot;
            return true;
        }

        bool CanPlayNative(int track)
        {
            if (track < 0 || track > 255 || !NativeAvailable())
                return false;
            char name[16];
            std::snprintf(name, sizeof(name), "%02d.ogg", track);
            int size = 0;
            return Seh::Guard("Soundtrack.ResourceSize", [&]
            {
                size = At<SizeFn>(Addresses::ResourceSize)(name);
            }) && size > 0;
        }

        bool CallNative(uintptr_t address)
        {
            return NativeAvailable() && Seh::Guard("Soundtrack.Control", [&]
            {
                At<VoidFn>(address)();
            });
        }

        bool PlayNative(int track)
        {
            State before;
            if (!CanPlayNative(track) || !ReadNative(before))
                return false;
            if (!Seh::Guard("Soundtrack.Play", [&]
            {
                // Start allocates a stream even with options volume zero.
                // Avoid reopening/leaking that stream on repeated requests.
                if (before.track == track && before.slot >= 0 && !before.started)
                {
                    if (before.userVolume == 0)
                        return;
                    At<VoidFn>(Addresses::Stop)();
                }
                At<SelectFn>(Addresses::SelectTrack)(track, track, -1, track);
                At<VoidFn>(Addresses::Start)();
            }))
                return false;
            State after;
            return ReadNative(after) && after.track == track && after.slot >= 0;
        }

        class NativeBackend final : public Backend
        {
        public:
            bool Read(State& state) override { return ReadNative(state); }
            bool CanPlay(int track) override { return CanPlayNative(track); }
            bool Play(int track) override { return PlayNative(track); }
            bool SetGain(float gain) override
            {
                State state;
                if (!std::isfinite(gain) || gain < 0 || gain > 1 || !ReadNative(state))
                    return false;
                // No open/started stream means no volume to restore. Muted
                // startup stays muted; a fade never starts/resumes the stream.
                if (state.slot < 0 || !state.started)
                    return true;
                bool applied = false;
                return Seh::Guard("Soundtrack.SetGain", [&]
                {
                    auto* buffer = At<BufferFn>(Addresses::GetDSBuffer)(state.slot);
                    if (buffer)
                        applied = SUCCEEDED(buffer->SetVolume(BufferVolume(state.userVolume, gain)));
                }) && applied;
            }
        } backend;

        bool ShimControl(const char* name)
        {
            const auto fn = OpenShimBridge::Resolve<BOOL(WINAPI*)()>(name);
            return fn && fn() != FALSE;
        }
    }

    bool Play(int track)
    {
        if (track < 0 || track > 255)
            return false;
        if (NativeAvailable())
        {
            if (!CanPlayNative(track))
                return false; // invalid request leaves the current fade intact
            if (!controller.Reset(backend))
                return false;
            return PlayNative(track);
        }
        const auto fn = OpenShimBridge::Resolve<BOOL(WINAPI*)(int)>("OpenShimSetMusicTrack");
        return fn && fn(track) != FALSE;
    }

    bool Stop()
    {
        if (!NativeAvailable())
            return ShimControl("OpenShimStopMusic");
        if (!CallNative(Addresses::Stop))
            return false;
        return controller.Reset(backend);
    }

    bool Pause()
    {
        return NativeAvailable() ? CallNative(Addresses::Pause) : ShimControl("OpenShimPauseMusic");
    }

    bool Resume()
    {
        if (!NativeAvailable())
            return ShimControl("OpenShimResumeMusic");
        // Set the pending gain while still paused to avoid an audible burst.
        return backend.SetGain(controller.Gain()) && CallNative(Addresses::Resume);
    }

    bool GetTrack(int& track)
    {
        State state;
        if (NativeAvailable())
        {
            if (!ReadNative(state))
                return false;
            track = state.track;
            return true;
        }
        const auto fn = OpenShimBridge::Resolve<BOOL(WINAPI*)(int*)>("OpenShimGetMusicTrack");
        return fn && fn(&track) != FALSE;
    }

    bool GetState(State& state) { return ReadNative(state); }
    bool Fade(float gain, double seconds) { return controller.Fade(backend, gain, seconds); }
    bool Change(int track, double fadeOut, double fadeIn) { return controller.Change(backend, track, fadeOut, fadeIn); }
    bool Update(double seconds) { return controller.Update(backend, seconds); }
    bool Reset() { return controller.Reset(backend); }
    float GetGain() noexcept { return controller.Gain(); }
    bool IsFading() noexcept { return controller.Active(); }

    void ResetMissionState() noexcept
    {
        // Do not scan at teardown when this mission never used the backend.
        if (qualified && (controller.Active() || controller.Gain() != 1))
            Seh::CatchCpp("Soundtrack.ResetMissionState", [] { controller.Reset(backend); });
        controller = FadeController{};
        attempted = false;
        qualified = false;
    }
}
