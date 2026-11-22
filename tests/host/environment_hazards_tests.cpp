// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/EnvironmentHazardsCore.h"
#include "HostTest.h"
#include <cstdint>
#include <limits>

int main()
{
    using namespace ExtraUtilities::EnvironmentHazards;
    using HostTest::Expect;

    Expect(PackArgb(0x12, 0x34, 0x56, 0xFF) == 0xFF123456u, "ARGB packs like the TRN palette path");
    const Rgba c = UnpackArgb(0x80A0B0C0u);
    Expect(c.a == 0x80 && c.r == 0xA0 && c.g == 0xB0 && c.b == 0xC0, "ARGB unpacks to r, g, b, a");
    Expect(UnpackArgb(PackArgb(1, 2, 3, 4)).b == 3, "pack/unpack round-trips");

    uint32_t ch = 0;
    Expect(ChannelFromNumber(0, ch) && ch == 0 && ChannelFromNumber(255, ch) && ch == 255, "channel range ends");
    Expect(!ChannelFromNumber(256, ch) && !ChannelFromNumber(-1, ch) && !ChannelFromNumber(1.5, ch), "bad channel rejected");
    Expect(!ChannelFromNumber(std::numeric_limits<double>::quiet_NaN(), ch), "NaN channel rejected");

    float s = 0.0f;
    Expect(MillisecondsToSeconds(1500, s) && s == 1.5f, "ms to seconds");
    Expect(MillisecondsToSeconds(0, s) && s == 0.0f, "zero ms allowed");
    Expect(!MillisecondsToSeconds(-1, s) && !MillisecondsToSeconds(std::numeric_limits<double>::infinity(), s) &&
        !MillisecondsToSeconds(2.0e9, s), "negative, infinite and huge ms rejected");
    Expect(SecondsToMilliseconds(0.25f) == 250.0, "seconds to ms");
    Expect(MillisecondsToSeconds(150, s) && SecondsToMilliseconds(s) == 150.0, "150 ms round-trips exactly");

    Expect(IsNonNegativeFinite(0) && IsNonNegativeFinite(40) && !IsNonNegativeFinite(-0.1) &&
        !IsNonNegativeFinite(std::numeric_limits<double>::quiet_NaN()), "non-negative finite check");

    Expect(IsValidSoundName("", 0) && IsValidSoundName("thunder.wav", 11), "short sound names fit");
    Expect(IsValidSoundName("123456789012345", 15) && !IsValidSoundName("1234567890123456", 16), "15 chars plus NUL is the limit");
    Expect(!IsValidSoundName("a\0b", 3) && !IsValidSoundName(nullptr, 0), "embedded NUL and null rejected");

    Expect(ResolveBoltCount(true, 4, 0) == 4 && ResolveBoltCount(true, 16, 2) == 16, "explicit count used");
    Expect(ResolveBoltCount(true, 0, 3) == -1 && ResolveBoltCount(true, 17, 3) == -1, "explicit count out of range");
    Expect(ResolveBoltCount(false, 0, 5) == 5 && ResolveBoltCount(false, 0, 0) == 1, "default is TRN count or 1");

    Expect(IsValidTerrainMaterial(0) && IsValidTerrainMaterial(15) && !IsValidTerrainMaterial(16) &&
        !IsValidTerrainMaterial(-1), "4-bit terrain material");
    return HostTest::Finish("environment hazards");
}
