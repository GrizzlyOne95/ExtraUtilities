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

namespace ExtraUtilities::Lua::FirstPersonTarget
{
	// Why the native resolver declined. Diagnostic only: callers log changes
	// in this value; they must not branch on anything but the bool result.
	enum class NativeResolveFailure
	{
		None,
		RuntimeGateClosed,
		NoLocalUserObject,
		NoRenderBridge,
		NoFirstPersonEntity,
		ReadFaulted,
		NotPerson,
		SharedWithWorldEntity,
		MissingIdle,
		MissingStandToKneel,
	};

	const char* DescribeNativeResolveFailure(NativeResolveFailure failure) noexcept;

	// True when EXU's native BZR 2.2.301 runtime gate is open. This describes
	// resolver capability, not whether the player currently has an on-foot
	// first-person entity.
	bool IsNativeResolverAvailable() noexcept;

	// Resolves the current local pilot's dedicated first-person Ogre::Entity.
	// The result is a one-operation snapshot owned by Redux/Ogre. Callers must
	// not retain it between public API operations.
	bool ResolveNativeLocalFirstPersonEntity(void*& outEntity,
		NativeResolveFailure* outFailure = nullptr) noexcept;
}
