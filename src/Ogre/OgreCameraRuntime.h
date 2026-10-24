// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include "bzr.h"
#include "Ogre/CameraProjection.h"

namespace ExtraUtilities::OgreCameraRuntime
{
    // Engine thread only. No retained Ogre pointers or mission state.
    bool Project(const BZR::VECTOR_3D& position, CameraProjection::Result& out) noexcept;
    bool IsPointInView(const BZR::VECTOR_3D& position, bool& out) noexcept;
}
