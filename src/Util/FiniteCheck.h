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

#include <cmath>

// NaN/infinity guards for values handed to the engine or Ogre. Templates over
// any x/y/z vector or r/g/b/a colour, so the same rule covers BZR::VECTOR_3D,
// the Ogre value mirrors and tests/host.
namespace ExtraUtilities::FiniteCheck
{
	inline bool IsFiniteScalar(float value) noexcept
	{
		return std::isfinite(value);
	}

	template <typename Vector>
	inline bool IsFiniteVector(const Vector& vector) noexcept
	{
		return std::isfinite(vector.x)
			&& std::isfinite(vector.y)
			&& std::isfinite(vector.z);
	}

	template <typename Color>
	inline bool IsFiniteColor(const Color& color) noexcept
	{
		return std::isfinite(color.r)
			&& std::isfinite(color.g)
			&& std::isfinite(color.b)
			&& std::isfinite(color.a);
	}
}
