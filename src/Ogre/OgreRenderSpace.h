/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Extra Utilities is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// Redux keeps simulation positions in the coordinates exposed to Lua, but
// recentres the Ogre render world around a per-map origin and mirrors Z. Any
// node created directly through Ogre must make the same conversion as the
// engine's own camera, terrain and object render paths.
//
// These helpers are templates so the conversion contract remains independent
// of Windows, Lua and Ogre headers and can be exercised by the host test lane.

namespace ExtraUtilities::OgreRenderSpace
{
	template <typename Vector>
	Vector SimPositionToRender(const Vector& position, const Vector& origin)
	{
		return Vector{
			position.x - origin.x,
			position.y - origin.y,
			-position.z - origin.z,
		};
	}

	template <typename Vector>
	Vector SimDirectionToRender(const Vector& direction)
	{
		return Vector{
			direction.x,
			direction.y,
			-direction.z,
		};
	}

	// Mirroring Z is a reflection, not a rotation, so a rotation carried
	// through it becomes its conjugate M R M with M = diag(1, 1, -1). That
	// mirrors the axis AND negates the angle, which for a quaternion
	// (w, x, y, z) leaves w alone and comes out as (w, -x, -y, z).
	//
	// A yaw-only quaternion therefore arrives in render space with its yaw
	// reversed, which is what the engine's own object render path does.
	template <typename Quaternion>
	Quaternion SimOrientationToRender(const Quaternion& orientation)
	{
		return Quaternion{
			orientation.w,
			-orientation.x,
			-orientation.y,
			orientation.z,
		};
	}
}
