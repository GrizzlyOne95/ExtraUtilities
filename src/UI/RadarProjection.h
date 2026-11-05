// Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace ExtraUtilities::RadarPresentation
{
    inline uint64_t Pack(int x, int y)
    {
        return static_cast<uint32_t>(x) | (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 32);
    }

    inline uint64_t Project(const float* point, float range, int radius,
        float distance, int centerX, int centerY)
    {
        const uint64_t center = Pack(centerX, centerY);
        if (!point || !std::isfinite(range) || range <= 0 || radius <= 0 ||
            !std::isfinite(distance) || distance < 3) return center;
        const float divisor = range * distance + point[2];
        if (!std::isfinite(divisor) || divisor <= 0) return center;
        const float factor = static_cast<float>(radius) * distance / divisor;
        const double x = centerX + std::floor(point[0] * factor + 0.5);
        const double y = centerY - std::floor(point[1] * factor + 0.5);
        if (!std::isfinite(x) || !std::isfinite(y) ||
            x < (std::numeric_limits<int>::min)() || x > (std::numeric_limits<int>::max)() ||
            y < (std::numeric_limits<int>::min)() || y > (std::numeric_limits<int>::max)()) return center;
        return Pack(static_cast<int>(x), static_cast<int>(y));
    }

    // Same projection without pixel snapping: textured triangles rotate every
    // frame, and whole-pixel vertices would make the texture shimmer.
    inline bool ProjectExact(const float* point, float range, int radius,
        float distance, int centerX, int centerY, float& x, float& y)
    {
        if (!point || !std::isfinite(range) || range <= 0 || radius <= 0 ||
            !std::isfinite(distance) || distance < 3) return false;
        const float divisor = range * distance + point[2];
        if (!std::isfinite(divisor) || divisor <= 0) return false;
        const float factor = static_cast<float>(radius) * distance / divisor;
        x = centerX + point[0] * factor;
        y = centerY - point[1] * factor;
        return std::isfinite(x) && std::isfinite(y);
    }
}
