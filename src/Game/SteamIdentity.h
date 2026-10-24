// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <cstdint>
namespace ExtraUtilities::SteamIdentity
{
    // Released UserId discriminator: 1 is Steam. Preserve the integer until
    // conversion to a decimal string at the Lua boundary (Lua 5.1 uses double).
    inline bool Decode(std::uint32_t provider, std::uint64_t value, std::uint64_t& out) noexcept
    {
        out = 0;
        const auto universe = value >> 56;
        if (provider != 1 || !static_cast<std::uint32_t>(value) ||
            ((value >> 52) & 15) != 1 || universe < 1 || universe > 4) return false;
        out = value; return true;
    }
}
