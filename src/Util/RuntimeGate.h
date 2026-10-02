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

// Whether this DLL load is running inside the qualified BZR build. Native
// patches are gated by BasicPatch; everything else that calls an engine
// function or writes engine memory at a fixed address checks this instead, so
// an unqualified executable gets nil/false from EXU rather than calls into
// arbitrary code.
//
// Deliberately dependency-free: headers as low as bzr.h consult it.
namespace ExtraUtilities::RuntimeGate
{
	namespace Detail
	{
		// False until Init records the result of
		// BuildValidation::IsSupportedBzr2301 (via
		// BasicPatch::EnableDeferredPatchActivation) for this Lua state.
		inline bool supported = false;
	}

	inline bool IsSupported() noexcept
	{
		return Detail::supported;
	}

	inline void SetSupported(bool supported) noexcept
	{
		Detail::supported = supported;
	}
}
