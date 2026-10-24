// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include "Util/BuildValidation.h"
#include "Util/RuntimeGate.h"
#include <cstring>

namespace ExtraUtilities
{
    // Optional anchors qualify their own feature without disabling unrelated
    // EXU APIs. Call before taking locks or installing a detour.
    inline bool QualifyFeature(const char* prefix, unsigned expected) noexcept
    {
        if (!RuntimeGate::IsSupported()) return false;
        unsigned matched = 0;
        for (const auto& anchor : BzrBuildProfile::kRuntimeAnchors)
        {
            if (std::strncmp(anchor.name, prefix, std::strlen(prefix)) != 0) continue;
            if (!BuildValidation::Detail::MatchAnchor(GetModuleHandleA(nullptr), anchor)) return false;
            ++matched;
        }
        return matched == expected;
    }
}
