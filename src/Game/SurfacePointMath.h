#pragma once
#include <cmath>

namespace ExtraUtilities::SurfacePointMath
{
    // Invert the actual Ogre parent-node transform, including nonuniform scale.
    inline bool ToLocal(const float point[3], const float origin[3], const float q[4], const float scale[3], float out[3]) noexcept
    {
        float delta[3];
        for (int i = 0; i < 3; ++i)
        {
            if (!std::isfinite(point[i]) || !std::isfinite(origin[i]) || !std::isfinite(scale[i]) || std::fabs(scale[i]) < 1e-6f) return false;
            delta[i] = point[i] - origin[i];
        }
        float norm = 0;
        for (int i = 0; i < 4; ++i) { if (!std::isfinite(q[i])) return false; norm += q[i] * q[i]; }
        if (norm < 0.5f || norm > 1.5f) return false;
        const float x = -q[1], y = -q[2], z = -q[3], w = q[0];
        const float t[3] = {2 * (y * delta[2] - z * delta[1]), 2 * (z * delta[0] - x * delta[2]), 2 * (x * delta[1] - y * delta[0])};
        const float cross[3] = {y * t[2] - z * t[1], z * t[0] - x * t[2], x * t[1] - y * t[0]};
        for (int i = 0; i < 3; ++i) out[i] = (delta[i] + (w * t[i] + cross[i]) / norm) / scale[i];
        return true;
    }
}
