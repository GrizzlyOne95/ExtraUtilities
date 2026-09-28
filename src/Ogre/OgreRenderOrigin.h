/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

#include "bzr.h"

#include <Windows.h>

// The one reader of Redux's per-map render origin, the `origin` argument of
// OgreRenderSpace::SimPositionToRender. Kept out of OgreRenderSpace.h, which
// stays free of engine memory so tests/host can include it.
namespace ExtraUtilities::OgreRenderSpace
{
	// Reads BZR::Ogre::worldRenderOriginAddress under SEH in its own frame, so
	// callers that hold unwindable objects can use it. On a fault the origin
	// is zeroed and the SEH code goes to outExceptionCode when given. The
	// value is not validated: callers check it is finite before using it.
	inline bool TryReadWorldRenderOrigin(
		BZR::VECTOR_3D& outOrigin,
		unsigned long* outExceptionCode = nullptr) noexcept
	{
		__try
		{
			outOrigin = *reinterpret_cast<const BZR::VECTOR_3D*>(BZR::Ogre::worldRenderOriginAddress);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			if (outExceptionCode != nullptr)
			{
				*outExceptionCode = GetExceptionCode();
			}
			outOrigin = BZR::VECTOR_3D{};
			return false;
		}
	}
}
