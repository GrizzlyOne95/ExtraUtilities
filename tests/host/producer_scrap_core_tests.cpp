// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Game/ProducerScrapCore.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
using namespace ExtraUtilities::ProducerScrapCore;
int main()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    assert(IsValidBuildSeconds(0.0) && IsValidBuildSeconds(2.5) && IsValidBuildSeconds(kMaxBuildSeconds));
    assert(!IsValidBuildSeconds(-0.1) && !IsValidBuildSeconds(nan) && !IsValidBuildSeconds(inf) && !IsValidBuildSeconds(2.0e6));

    std::int32_t v = -7;
    assert(TryScrapCapacity(1.0, v) && v == 1);
    assert(TryScrapCapacity(40.9, v) && v == 40);
    assert(!TryScrapCapacity(0.5, v) && !TryScrapCapacity(0.0, v) && !TryScrapCapacity(nan, v) && !TryScrapCapacity(inf, v));
    assert(!TryScrapCapacity(kMaxScrapCapacity + 1.0, v));
    assert(TryScrapCapacity(kMaxScrapCapacity, v) && v == static_cast<std::int32_t>(kMaxScrapCapacity));
    assert(IsValidBuildSeconds(-0.0));

    assert(TryClampScrap(5.7, 20, v) && v == 5);
    assert(TryClampScrap(-3.0, 20, v) && v == 0);
    assert(TryClampScrap(99.0, 20, v) && v == 20);
    assert(TryClampScrap(inf, 20, v) && v == 20);
    assert(TryClampScrap(-inf, 20, v) && v == 0);
    assert(TryClampScrap(0.5, 0, v) && v == 0);
    assert(TryClampScrap(19.9, 20, v) && v == 19);
    assert(!TryClampScrap(nan, 20, v) && !TryClampScrap(1.0, -1, v));

    char out[9];
    const unsigned char full[8] = {'a', 'v', 't', 'a', 'n', 'k', '0', '0'};
    assert(DecodeOdf8(full, out) && std::strcmp(out, "avtank00") == 0);
    const unsigned char shortName[8] = {'s', 'v', 0, 'x', 'x', 'x', 'x', 'x'};
    assert(DecodeOdf8(shortName, out) && std::strcmp(out, "sv") == 0);
    const unsigned char high[8] = {0xE1, 'b', 0, 0, 0, 0, 0, 0};
    assert(DecodeOdf8(high, out) && std::strcmp(out, "ab") == 0);
    const unsigned char empty[8] = {};
    assert(!DecodeOdf8(empty, out) && out[0] == 0);

    assert(PathDestinationIndex(1) == 0 && PathDestinationIndex(2) == 1 && PathDestinationIndex(kMaxPathPoints) == kMaxPathPoints - 1);
    assert(PathDestinationIndex(0) == -1 && PathDestinationIndex(-5) == -1 && PathDestinationIndex(kMaxPathPoints + 1) == -1);
    return 0;
}
