#pragma once
#include "Game/MuzzleFlashVisualState.h"

namespace ExtraUtilities { namespace OgreFlashParameters
{
    // Only call for a live, EXU-owned entity behind the existing runtime gate.
    // Optional for legacy materials; v2 materials consume custom parameter 1344.
    bool Available() noexcept;
    bool Set(void* entity, const MuzzleFlashVisualState::Sample& state) noexcept;
} }
