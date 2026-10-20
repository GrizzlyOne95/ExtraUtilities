#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MuzzleFlashVisualState
{
    // Reserved for EXU-owned flash renderables, not a shared material uniform.
    constexpr unsigned kCustomParameter = 1344;
    struct Sample { float phase, seed, valid, duration; };
    inline Sample At(uint32_t id, float age, float duration)
    {
        uint32_t hash = id;
        hash ^= hash >> 16; hash *= 0x7feb352du;
        hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
        const bool valid = std::isfinite(age) && std::isfinite(duration) && duration > 0;
        const float phase = valid ? std::max(0.f, std::min(1.f, age / duration)) : 1.f;
        return {phase, float(hash & ((1u << 24) - 1u)) / 16777215.f, 1.f, valid ? duration : 0.f};
    }
}
