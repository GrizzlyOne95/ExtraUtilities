/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#pragma once

#include <cstdint>

namespace ExtraUtilities::Clock
{
    // No Win32 dependency: OS.cpp feeds QueryPerformanceCounter ticks in.
    // Whole seconds and the remainder are converted separately, so a large
    // tick count times a large frequency never overflows or loses the
    // sub-microsecond part to double rounding.
    inline double TicksToSeconds(std::int64_t ticks, std::int64_t origin, std::int64_t frequency) noexcept
    {
        if (frequency <= 0)
        {
            return 0.0;
        }

        const std::int64_t elapsed = ticks - origin;
        const std::int64_t whole = elapsed / frequency;
        const std::int64_t rest = elapsed % frequency;
        return static_cast<double>(whole) + static_cast<double>(rest) / static_cast<double>(frequency);
    }
}
