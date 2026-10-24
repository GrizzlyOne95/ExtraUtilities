// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Game/SteamIdentity.h"
#include <cassert>
#include <string>
int main()
{
    using ExtraUtilities::SteamIdentity::Decode;
    std::uint64_t value = 0;
    const std::uint64_t steam = 76561198012345678ull;
    assert(Decode(1,steam,value) && value == steam);
    assert(std::to_string(value) == "76561198012345678");
    assert(!Decode(0,steam,value) && value == 0);
    assert(!Decode(2,steam,value) && value == 0);
    assert(!Decode(1,0,value));
    assert(!Decode(1,steam & 0xffffffff00000000ull,value));
    assert(!Decode(1,steam & 0x00ffffffffffffffull,value));
    assert(!Decode(1,steam ^ (1ull << 52),value));
}
