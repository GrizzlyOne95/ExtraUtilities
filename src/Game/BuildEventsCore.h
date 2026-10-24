// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace ExtraUtilities::BuildEventsCore
{
    enum class Type { Queued, Cancelled, Completed };
    struct Event
    {
        Type type = Type::Queued;
        std::uint32_t producer = 0, built = 0, kind = 0;
        int team = 0;
        char odf[9]{};
        std::uint64_t sequence = 0;
    };
    template<std::size_t Capacity> struct Queue
    {
        static_assert(Capacity > 0);
        std::array<Event, Capacity> entries{};
        std::size_t head = 0, size = 0;
        std::uint64_t sequence = 0, dropped = 0;
        void Clear() noexcept { head = size = 0; sequence = dropped = 0; }
        void Push(Event event) noexcept
        {
            event.sequence = ++sequence;
            if (size == Capacity) { head = (head + 1) % Capacity; --size; ++dropped; }
            entries[(head + size++) % Capacity] = event;
        }
        bool Pop(Event& event) noexcept
        {
            if (!size) return false;
            event = entries[head]; head = (head + 1) % Capacity; --size; return true;
        }
    };
}
