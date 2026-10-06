/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

// Pure math for exu.SetMaterialTextureWindow: the TextureUnitState scale and
// scroll that make a texture unit sample the window [u0, u0 + du] x
// [v0, v0 + dv] across UV 0..1, the way an overlay panel's uv_coords do.
//
// Ogre 1.10 TextureUnitState::recalcTextureMatrix (no rotation) gives
//   tex = (uv - 0.5) / scale + 0.5 + scroll
// so scale = 1 / d and scroll = origin + 0.5 * d - 0.5 per axis. A negative
// extent mirrors that axis (negative scale). Engine-free; host-tested.

#include <cmath>

namespace ExtraUtilities::TextureWindowMath
{
	struct Transform
	{
		float scrollU = 0.0f;
		float scrollV = 0.0f;
		float scaleU = 1.0f;
		float scaleV = 1.0f;
	};

	// Smallest |extent| accepted: a smaller window would need a scale large
	// enough to lose every texel to float precision.
	constexpr float kMinExtent = 1.0e-4f;
	// Largest |extent|: beyond this the window repeats a texture so often that
	// the request is almost certainly a mistake (and scale underflows).
	constexpr float kMaxExtent = 1.0e4f;

	inline bool IsUsableExtent(float d) noexcept
	{
		return std::isfinite(d) && std::fabs(d) >= kMinExtent && std::fabs(d) <= kMaxExtent;
	}

	// False (and `out` untouched) for a zero, non-finite or absurd window.
	inline bool FromWindow(float u0, float v0, float du, float dv, Transform& out) noexcept
	{
		if (!std::isfinite(u0) || !std::isfinite(v0) || !IsUsableExtent(du) || !IsUsableExtent(dv))
		{
			return false;
		}
		Transform t;
		t.scaleU = 1.0f / du;
		t.scaleV = 1.0f / dv;
		t.scrollU = u0 + 0.5f * du - 0.5f;
		t.scrollV = v0 + 0.5f * dv - 0.5f;
		out = t;
		return true;
	}

	// Where Ogre samples for mesh UV (u, v) under `t` (rotation 0).
	inline void Apply(const Transform& t, float u, float v, float& outU, float& outV) noexcept
	{
		outU = (u - 0.5f) / t.scaleU + 0.5f + t.scrollU;
		outV = (v - 0.5f) / t.scaleV + 0.5f + t.scrollV;
	}
}
