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

	// Raw render-bridge read for a Person the caller has already qualified
	// (e.g. PilotState::CaptureIfCurrent inside Person::Simulate): the WORLD and
	// first-person Ogre::Entity the stock animation apply helpers drive. Either
	// may be null. No qualification, Ogre call, or allocation; false only on a
	// null person/bridge or a faulting read. One-operation snapshot.
	bool ReadPersonRenderEntities(const void* person, void*& outWorldEntity,
		void*& outFirstPersonEntity) noexcept;

	// The same render-bridge read for ANY GameObject (the bridge layout lives
	// on GameObject, not Person). For a craft, +0xC0 is the cockpit Entity the
	// first-person view draws: a second Entity of the main mesh whose '2'
	// (cockpit) bones are live and '1' bones are hidden (0x67E5A0), or the
	// separate <name>_cockpit.mesh Entity. Null outside cockpit view or before
	// the renderer creates it. Same contract: one-operation snapshot, false
	// only on a null object/bridge, a closed runtime gate or a faulting read.
	bool ReadRenderBridgeEntities(const void* object, void*& outWorldEntity,
		void*& outFirstPersonEntity) noexcept;
}
