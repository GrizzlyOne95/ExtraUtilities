#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace ExtraUtilities::ExplosionEventsCore
{
    struct Event
    {
        char odf[9]{};
        double x = 0, y = 0, z = 0;
        float radius = 0;
        std::uint64_t sequence = 0;
    };

    template<std::size_t Capacity> struct Queue
    {
        std::array<Event, Capacity> events{};
        std::size_t head = 0, size = 0;
        std::uint64_t dropped = 0, sequence = 0;
        void Clear() noexcept { head = size = 0; dropped = sequence = 0; }
        bool Push(Event event) noexcept
        {
            if (!std::isfinite(event.x) || !std::isfinite(event.y) || !std::isfinite(event.z) ||
                !std::isfinite(event.radius) || event.radius < 0 || !event.odf[0]) return false;
            event.sequence = ++sequence;
            if (size == Capacity) { head = (head + 1) % Capacity; --size; ++dropped; }
            events[(head + size++) % Capacity] = event;
            return true;
        }
        bool Pop(Event& event) noexcept
        {
            if (!size) return false;
            event = events[head]; head = (head + 1) % Capacity; --size; return true;
        }
    };
}
