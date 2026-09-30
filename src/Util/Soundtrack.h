/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#pragma once
#include "Util/MusicFade.h"

namespace ExtraUtilities::Soundtrack
{
    bool Play(int track);
    bool Stop();
    bool Pause();
    bool Resume();
    bool GetTrack(int& track);
    bool GetState(State& state);
    bool Fade(float gain, double seconds);
    bool Change(int track, double fadeOut, double fadeIn);
    bool Update(double seconds);
    bool Reset();
    float GetGain() noexcept;
    bool IsFading() noexcept;
    void ResetMissionState() noexcept;
}
