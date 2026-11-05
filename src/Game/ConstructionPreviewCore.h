// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>

namespace ExtraUtilities::ConstructionPreviewCore
{
    enum class State { None, Ready, Warning, Blocked };
    inline State FromSprite(std::uintptr_t sprite, std::uintptr_t green,
        std::uintptr_t yellow, std::uintptr_t red) noexcept
    {
        if (!sprite) return State::None;
        if (sprite == green) return State::Ready;
        if (sprite == yellow) return State::Warning;
        if (sprite == red) return State::Blocked;
        return State::None;
    }
    // Native SetConstructionMatrix flattens the facing vector onto X/Z.
    // Keep the terrain height and yaw it actually chose, including double position.
    struct Vector { float x, y, z; };
    struct Quaternion { float w, x, y, z; };
    template<class Matrix>
    bool Pose(const Matrix& m, Vector& position, Quaternion& orientation) noexcept
    {
        if (!std::isfinite(m.posit_x) || !std::isfinite(m.posit_y) || !std::isfinite(m.posit_z) ||
            !std::isfinite(m.front_x) || !std::isfinite(m.front_z) ||
            !std::isfinite(m.front_y) || !std::isfinite(m.up_x) || !std::isfinite(m.up_y) || !std::isfinite(m.up_z) ||
            std::fabs(m.front_y) > .001f ||
            std::fabs(m.up_y - 1.f) > .001f || std::fabs(m.up_x) > .001f || std::fabs(m.up_z) > .001f)
            return false;
        const double length = std::hypot(double(m.front_x), double(m.front_z));
        if (!std::isfinite(length) || std::fabs(length - 1.) > .01) return false;
        position = {float(m.posit_x), float(m.posit_y + .01), float(m.posit_z)};
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
        const float half = float(std::atan2(double(m.front_x), double(m.front_z)) * .5);
        orientation = {std::cos(half), 0, std::sin(half), 0};
        return true;
    }
    inline bool SolidPhase(std::uint64_t milliseconds) noexcept { return ((milliseconds >> 9) & 1) != 0; }
}
