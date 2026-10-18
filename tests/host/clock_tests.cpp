/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "Util/Clock.h"
#include "HostTest.h"
#include <cmath>
#include <cstdint>

using namespace ExtraUtilities::Clock;
using namespace HostTest;

namespace
{
    bool Near(double a, double b, double tolerance)
    {
        return std::fabs(a - b) <= tolerance;
    }
}

int main()
{
    // The usual QPC rate on Windows 10 and later.
    const std::int64_t f = 10'000'000;

    Expect(TicksToSeconds(500, 500, f) == 0.0, "origin reads as zero");
    Expect(TicksToSeconds(500 + f, 500, f) == 1.0, "one frequency of ticks is one second");
    Expect(Near(TicksToSeconds(1, 0, f), 1e-7, 1e-15), "one tick keeps 100 ns resolution");
    Expect(Near(TicksToSeconds(f + f / 2, 0, f), 1.5, 1e-12), "fractional seconds");

    // A month of uptime: ticks * frequency would overflow int64, the split
    // conversion must still resolve a single tick.
    const std::int64_t month = 30LL * 24 * 3600 * f;
    Expect(Near(TicksToSeconds(month + 1, 0, f) - TicksToSeconds(month, 0, f), 1e-7, 1e-9),
        "one tick still resolves after a month");

    // Older hardware reports the TSC rate (GHz) rather than 10 MHz.
    const std::int64_t tsc = 3'000'000'000LL;
    Expect(Near(TicksToSeconds(tsc * 7 + tsc / 4, 0, tsc), 7.25, 1e-12), "GHz frequency");

    // Monotonic: later ticks never read earlier.
    double previous = -1.0;
    bool monotonic = true;
    for (std::int64_t t = 0; t < 3 * f; t += 999'983)
    {
        const double now = TicksToSeconds(t, 0, f);
        monotonic = monotonic && now >= previous;
        previous = now;
    }
    Expect(monotonic, "monotonic across second boundaries");

    Expect(TicksToSeconds(123, 0, 0) == 0.0, "zero frequency fails closed");
    Expect(TicksToSeconds(123, 0, -5) == 0.0, "negative frequency fails closed");

    return Finish("clock");
}
