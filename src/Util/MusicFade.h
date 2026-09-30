/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#pragma once

#include <algorithm>
#include <cmath>

namespace ExtraUtilities::Soundtrack
{
    struct State
    {
        int track = -1;
        int slot = -1;
        bool started = false;
        bool paused = false;
        int userVolume = 0;
    };

    // No Win32 or Lua dependency: the controller runs on the mission thread.
    // The backend owns native qualification, file checks and hardware access.
    class Backend
    {
    public:
        virtual ~Backend() = default;
        virtual bool Read(State& state) = 0;
        virtual bool CanPlay(int track) = 0;
        virtual bool Play(int track) = 0;
        virtual bool SetGain(float gain) = 0;
    };

    inline bool ValidDuration(double seconds) noexcept
    {
        return std::isfinite(seconds) && seconds >= 0;
    }

    // Match Redux's volume curve, but clamp silence before taking log2.
    // The stock integer setter takes log2(0); calling the DirectSound buffer
    // with -10000 explicitly also avoids its pause/resume side effects.
    inline long BufferVolume(int userVolume, float gain) noexcept
    {
        if (userVolume <= 0 || !(gain > 0))
            return -10000;
        const double level = static_cast<double>(userVolume) * gain / 10.0;
        return static_cast<long>(std::clamp(800.0 * std::log2(level), -10000.0, 0.0));
    }

    class FadeController
    {
    public:
        float Gain() const noexcept { return gain_; }
        bool Active() const noexcept { return phase_ != Phase::Idle; }

        bool Fade(Backend& backend, float target, double seconds)
        {
            if (!std::isfinite(target) || target < 0 || target > 1 || !ValidDuration(seconds))
                return false;
            State state;
            if (!backend.Read(state) || state.slot < 0)
                return false;
            if (!backend.SetGain(seconds == 0 ? target : gain_))
                return false;
            expectedTrack_ = state.track;
            expectedSlot_ = state.slot;
            Begin(Phase::Fade, target, seconds);
            return true;
        }

        bool Change(Backend& backend, int track, double fadeOut, double fadeIn)
        {
            if (track < 0 || track > 255 || !ValidDuration(fadeOut) || !ValidDuration(fadeIn) ||
                !backend.CanPlay(track))
                return false;
            State state;
            if (!backend.Read(state))
                return false;
            // Keep an existing fade untouched until the new request is valid.
            if (state.slot >= 0 && !backend.SetGain(gain_))
                return false;
            recoveryGain_ = gain_;
            nextTrack_ = track;
            fadeIn_ = fadeIn;
            expectedTrack_ = state.track;
            expectedSlot_ = state.slot;
            if (state.slot < 0 || !state.started || fadeOut == 0)
                return Switch(backend);
            Begin(Phase::Out, 0, fadeOut);
            return true;
        }

        bool Update(Backend& backend, double seconds)
        {
            if (!ValidDuration(seconds))
                return false;
            if (!Active() && gain_ == 1)
                return true;
            State state;
            if (!backend.Read(state))
                return false;
            if (state.track != expectedTrack_ || state.slot != expectedSlot_)
            {
                // Stock playlist/shell/another caller took ownership.
                Reset(backend);
                return true;
            }
            if (state.paused || !state.started)
                return backend.SetGain(gain_);
            if (!Active())
                return backend.SetGain(gain_); // respect a changed options volume

            // Consume overshoot across both stages, so a long mission update
            // does not lengthen the transition or skip its exact endpoints.
            for (int stage = 0; stage < 2 && Active(); ++stage)
            {
                const double used = (std::min)(seconds, duration_ - elapsed_);
                elapsed_ += used;
                seconds -= used;
                const float nextGain = static_cast<float>(from_ + (target_ - from_) * (elapsed_ / duration_));
                if (!backend.SetGain(nextGain))
                {
                    Reset(backend);
                    return false;
                }
                gain_ = nextGain;
                if (elapsed_ < duration_)
                    break;
                const Phase completed = phase_;
                phase_ = Phase::Idle;
                if (completed == Phase::Out && !Switch(backend))
                    return false;
            }
            return true;
        }

        bool Reset(Backend& backend)
        {
            phase_ = Phase::Idle;
            gain_ = 1;
            nextTrack_ = -1;
            return backend.SetGain(1);
        }

    private:
        enum class Phase { Idle, Fade, Out, In };
        Phase phase_ = Phase::Idle;
        float gain_ = 1;
        float from_ = 1;
        float target_ = 1;
        float recoveryGain_ = 1;
        double duration_ = 0;
        double elapsed_ = 0;
        double fadeIn_ = 0;
        int expectedTrack_ = -1;
        int expectedSlot_ = -1;
        int nextTrack_ = -1;

        void Begin(Phase phase, float target, double seconds) noexcept
        {
            from_ = gain_;
            target_ = target;
            duration_ = seconds;
            elapsed_ = 0;
            phase_ = seconds > 0 ? phase : Phase::Idle;
            if (seconds == 0)
                gain_ = target;
        }

        bool Switch(Backend& backend)
        {
            phase_ = Phase::Idle;
            // Recheck: mod content could have disappeared during fade-out.
            if (!backend.CanPlay(nextTrack_) || !backend.Play(nextTrack_))
            {
                gain_ = recoveryGain_;
                backend.SetGain(gain_);
                return false;
            }
            State state;
            if (!backend.Read(state))
            {
                Reset(backend);
                return false;
            }
            expectedTrack_ = state.track;
            expectedSlot_ = state.slot;
            gain_ = fadeIn_ > 0 ? 0.0f : 1.0f;
            if (!backend.SetGain(gain_))
            {
                Reset(backend);
                return false;
            }
            Begin(Phase::In, 1, fadeIn_);
            return true;
        }
    };
}
