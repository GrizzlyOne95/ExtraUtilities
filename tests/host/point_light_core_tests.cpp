/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "Game/PointLightCore.h"
#include "HostTest.h"
#include <cmath>
#include <limits>
#include <string>

using namespace ExtraUtilities::PointLightCore;
using namespace HostTest;

namespace
{
    bool Near(float a, float b, float tolerance)
    {
        return std::fabs(a - b) <= tolerance;
    }
}

int main()
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();

    // Cap
    Expect(HasCapacity(0), "an empty table has room");
    Expect(HasCapacity(kMaxPointLights - 1), "one below the cap has room");
    Expect(!HasCapacity(kMaxPointLights), "the cap itself is full");
    Expect(!HasCapacity(kMaxPointLights + 3), "past the cap is full");
    Expect(kMaxPointLights == 8, "the cap is eight lights");

    // Names
    Expect(IsValidName("vent1"), "plain name");
    Expect(IsValidName("core vent #2"), "spaces and punctuation are fine");
    Expect(!IsValidName(""), "empty name");
    Expect(IsValidName(std::string(kMaxNameLength, 'a')), "name at the length limit");
    Expect(!IsValidName(std::string(kMaxNameLength + 1, 'a')), "name past the length limit");
    Expect(!IsValidName(std::string("a\0b", 3)), "embedded NUL");
    Expect(!IsValidName("tab\there"), "control character");
    Expect(OgreLightName("vent1") == "__exu_pl_vent1", "Ogre light name is prefixed");
    Expect(OgreNodeName("vent1") == "__exu_pl_node_vent1", "Ogre node name is prefixed");
    Expect(OgreLightName("x") != OgreNodeName("x"), "light and node names differ");

    // Range
    Expect(IsValidRange(1.0f), "small range");
    Expect(IsValidRange(kMaxRange), "range at the limit");
    Expect(!IsValidRange(kMaxRange * 1.01f), "range past the limit");
    Expect(!IsValidRange(0.0f), "zero range");
    Expect(!IsValidRange(-5.0f), "negative range");
    Expect(!IsValidRange(inf), "infinite range");
    Expect(!IsValidRange(nan), "NaN range");

    // Engine attenuation curve: 1 / (1 + 15 (d/range)^2) is 1/16 at the edge.
    const Attenuation a = AttenuationForRange(40.0f);
    Expect(a.range == 40.0f && a.constant == 1.0f && a.linear == 0.0f, "range, constant and linear terms");
    Expect(Near(a.quadratic, 15.0f / 1600.0f, 1e-9f), "quadratic term is 15 / range^2");
    const float edge = 1.0f / (a.constant + a.linear * 40.0f + a.quadratic * 1600.0f);
    Expect(Near(edge, 1.0f / 16.0f, 1e-6f), "intensity is 1/16 at the range edge");
    Expect(IsValidAttenuation(a), "the engine curve validates");
    Expect(IsValidAttenuation(AttenuationForRange(kMaxRange)), "the curve at the max range validates");

    Attenuation zero = a;
    zero.constant = zero.linear = zero.quadratic = 0.0f;
    Expect(!IsValidAttenuation(zero), "all-zero terms would divide by zero");
    Attenuation negative = a;
    negative.linear = -0.1f;
    Expect(!IsValidAttenuation(negative), "negative term");
    Attenuation nonFinite = a;
    nonFinite.quadratic = nan;
    Expect(!IsValidAttenuation(nonFinite), "NaN term");
    Attenuation badRange = a;
    badRange.range = 0.0f;
    Expect(!IsValidAttenuation(badRange), "attenuation range must itself be valid");

    // Colour and power
    Expect(IsValidColour(Colour{ 1.0f, 0.4f, 0.1f }), "ordinary colour");
    Expect(IsValidColour(Colour{ 0.0f, 0.0f, 0.0f }), "black is allowed");
    Expect(IsValidColour(Colour{ 4.0f, 2.0f, 1.0f }), "over-range (HDR) colour is allowed");
    Expect(!IsValidColour(Colour{ -0.1f, 0.0f, 0.0f }), "negative component");
    Expect(!IsValidColour(Colour{ 0.0f, nan, 0.0f }), "NaN component");
    Expect(!IsValidColour(Colour{ 0.0f, 0.0f, kMaxColourComponent * 2.0f }), "absurd component");
    Expect(IsValidPowerScale(0.0f), "zero power is allowed (dark)");
    Expect(IsValidPowerScale(2.5f), "ordinary power");
    Expect(!IsValidPowerScale(-1.0f), "negative power");
    Expect(!IsValidPowerScale(inf), "infinite power");

    const Colour scaled = ScaledColour(Colour{ 1.0f, 0.5f, 0.25f }, 2.0f);
    Expect(scaled.r == 2.0f && scaled.g == 1.0f && scaled.b == 0.5f, "power scale multiplies each channel");
    const Colour dark = ScaledColour(Colour{ 1.0f, 0.5f, 0.25f }, 0.0f);
    Expect(dark.r == 0.0f && dark.g == 0.0f && dark.b == 0.0f, "zero power is black");

    return Finish("point_light_core");
}
