// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace ExtraUtilities::PlayerControlPolicy
{
    enum Mask : std::uint32_t
    {
        Throttle = 1u << 0, Strafe = 1u << 1, Look = 1u << 2,
        Fire = 1u << 3, Jump = 1u << 4, Deploy = 1u << 5,
        Eject = 1u << 6, Abandon = 1u << 7, Turbo = 1u << 8,
        Weapons = 1u << 9, Nav = 1u << 10, Jetpack = 1u << 11,
        All = (1u << 12) - 1,
        Movement = Throttle | Strafe | Jump | Turbo | Jetpack
    };

    // Redux's polled USER_CTRL (56 bytes) and the GAS_CTRL smoothing cache.
    // Offsets are qualified against UserProcess::Execute; see the RE note.
    using Inputs = std::array<std::uint8_t, 56>;
    using Gas = std::array<float, 11>;
    inline void Suppress(Inputs& input, Gas& gas, std::uint32_t mask) noexcept
    {
        auto zero = [&](std::size_t start, std::size_t count) {
            for (std::size_t i = start; i < start + count; ++i) input[i] = 0;
        };
        if (mask & Throttle) { zero(4, 6); gas[3] = 0; }
        if (mask & Strafe) { zero(12, 6); gas[2] = 0; }
        if (mask & Look) { zero(20, 7); zero(28, 8); gas[0] = gas[1] = 0; }
        if (mask & Fire) { zero(36, 2); input[40] = 0; }
        if (mask & Jump) { input[47] = 0; gas[5] = 0; }
        if (mask & Deploy) { input[48] = 0; gas[7] = 0; }
        if (mask & Eject) { input[49] = 0; gas[6] = 0; }
        if (mask & Abandon) { input[50] = 0; gas[8] = 0; }
        if (mask & Turbo) { input[46] = 0; gas[4] = 0; }
        if (mask & Weapons) { zero(38, 2); zero(41, 5); }
        if (mask & Nav) input[53] = 0;
        if (mask & Jetpack) { input[54] = 0; gas[10] = 0; }
        // Preserve the key/UI prefix, unknown padding and unrelated gas slot 9.
    }

    class Owners
    {
        struct Entry { std::array<char, 64> name{}; std::uint32_t mask = 0; };
        std::array<Entry, 16> entries{};
    public:
        static bool ValidOwner(std::string_view owner) noexcept
        {
            return !owner.empty() && owner.size() < 64 && owner.find('\0') == std::string_view::npos;
        }
        bool Set(std::string_view owner, std::uint32_t mask) noexcept
        {
            if (!ValidOwner(owner) || (mask & ~All)) return false;
            Entry* empty = nullptr;
            for (auto& entry : entries)
            {
                if (entry.mask && std::string_view(entry.name.data()) == owner)
                { entry.mask = mask; return true; }
                if (!entry.mask && !empty) empty = &entry;
            }
            if (!mask) return true; // idempotent release
            if (!empty) return false;
            empty->name.fill(0);
            std::memcpy(empty->name.data(), owner.data(), owner.size());
            empty->mask = mask;
            return true;
        }
        std::uint32_t Combined() const noexcept
        { std::uint32_t mask = 0; for (const auto& e : entries) mask |= e.mask; return mask; }
        unsigned Count() const noexcept
        { unsigned n = 0; for (const auto& e : entries) if (e.mask) ++n; return n; }
        void Reset() noexcept { entries = {}; }
    };
}
