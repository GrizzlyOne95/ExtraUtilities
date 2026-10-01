/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace ExtraUtilities::NativeHud
{
    enum class Meter : unsigned { Hull = 0, Ammo = 1 };
    enum class Result { Accepted, InvalidMeter, InvalidRect, Unavailable, Rejected };
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };
    constexpr int kCoordinateLimit = 65535;
    constexpr int kDimensionLimit = 16384;

    inline const char* Name(Meter meter) noexcept
    {
        switch (meter)
        {
        case Meter::Hull: return "hull";
        case Meter::Ammo: return "ammo";
        default: return nullptr;
        }
    }
    inline unsigned Bit(Meter meter) noexcept { return Name(meter) ? 1u << static_cast<unsigned>(meter) : 0; }
    inline bool ParseMeter(std::string_view name, Meter& meter) noexcept
    {
        if (name == "hull") { meter = Meter::Hull; return true; }
        if (name == "ammo") { meter = Meter::Ammo; return true; }
        return false;
    }
    inline bool ValidRect(Rect rect) noexcept
    {
        const auto coordinate = [](int64_t value) { return value >= -kCoordinateLimit && value <= kCoordinateLimit; };
        return rect.w > 0 && rect.h > 0 && rect.w <= kDimensionLimit && rect.h <= kDimensionLimit &&
            coordinate(rect.x) && coordinate(rect.y) &&
            coordinate(static_cast<int64_t>(rect.x) + rect.w) &&
            coordinate(static_cast<int64_t>(rect.y) + rect.h);
    }
    inline const char* Reason(Result result) noexcept
    {
        switch (result)
        {
        case Result::InvalidMeter: return "invalid_meter";
        case Result::InvalidRect: return "invalid_rect";
        case Result::Unavailable: return "unavailable";
        case Result::Rejected: return "native_state_unavailable";
        default: return nullptr;
        }
    }

    // Optional native transport. The control layer is independently testable
    // without Lua, Windows, engine addresses or native object pointers.
    struct Provider
    {
        virtual ~Provider() = default;
        virtual uint32_t Capabilities() = 0;
        virtual bool GetRect(const char* name, bool stock, Rect& rect) = 0;
        virtual bool SetRect(const char* name, Rect rect) = 0;
        virtual bool SetVisible(const char* name, bool visible) = 0;
        virtual bool Restore(const char* name) = 0;
    };

    class Controls
    {
    public:
        explicit Controls(Provider& provider) noexcept : m_provider(provider) {}
        uint32_t Capabilities() { return m_provider.Capabilities() & 3u; }
        bool Available(Meter meter) { return (Capabilities() & Bit(meter)) != 0; }
        bool GetRect(Meter meter, bool stock, Rect& rect)
        {
            if (!Available(meter)) return false;
            Rect candidate;
            if (!m_provider.GetRect(Name(meter), stock, candidate) || !ValidRect(candidate)) return false;
            rect = candidate;
            return true;
        }
        Result SetRect(Meter meter, Rect rect)
        {
            if (!Name(meter)) return Result::InvalidMeter;
            if (!ValidRect(rect)) return Result::InvalidRect;
            if (!Available(meter)) return Result::Unavailable;
            if (!m_provider.SetRect(Name(meter), rect)) return Result::Rejected;
            m_owned |= Bit(meter);
            return Result::Accepted;
        }
        Result SetVisible(Meter meter, bool visible)
        {
            if (!Name(meter)) return Result::InvalidMeter;
            if (!Available(meter)) return Result::Unavailable;
            if (!m_provider.SetVisible(Name(meter), visible)) return Result::Rejected;
            m_owned |= Bit(meter);
            return Result::Accepted;
        }
        Result Restore(Meter meter)
        {
            if (!Name(meter)) return Result::InvalidMeter;
            if (!Available(meter)) return Result::Unavailable;
            if (!m_provider.Restore(Name(meter))) return Result::Rejected;
            m_owned &= ~Bit(meter);
            return Result::Accepted;
        }
        // Restore only slots changed through this EXU owner. Native HUD, radar
        // and other companions' unrelated state are never globally reset here.
        Result RestoreOwned()
        {
            Result result = Result::Accepted;
            for (Meter meter : {Meter::Hull, Meter::Ammo})
                if ((m_owned & Bit(meter)) != 0)
                {
                    const Result restored = Restore(meter);
                    if (restored != Result::Accepted) result = restored;
                }
            return result;
        }
        void ResetMission()
        {
            RestoreOwned();
            // If a provider disappeared, do not carry old ownership into a
            // later mission and restore another consumer's newly authored HUD.
            m_owned = 0;
        }

    private:
        Provider& m_provider;
        unsigned m_owned = 0;
    };
}
