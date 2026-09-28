/* Copyright (C) 2023-2026 VTrider
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

#include "bzr.h"

#include <cstdint>

// Validated handle <-> object conversion (GameObjectHandle.cpp), shared by the
// GameObject feature files and the patches that need a handle for an engine
// object. Both fail closed, under SEH, with zeroed outputs.

namespace ExtraUtilities::Lua::GameObject
{
	namespace Detail
	{
		// True only when rawValue is a non-zero handle whose object maps back
		// to the same handle.
		bool TryResolveHandleValue(uint32_t rawValue, BZR::handle& outHandle, BZR::GameObject*& outObject);

		// The object's handle, or false for a null object, a zero handle or a
		// fault.
		bool TryGetHandleFromObject(BZR::GameObject* obj, BZR::handle& outHandle);
	}
}
