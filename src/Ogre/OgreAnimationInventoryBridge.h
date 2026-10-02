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

#include <string>
#include <vector>

namespace ExtraUtilities
{
	namespace OgreAnimationInventory
	{
		struct AnimationStateRef
		{
			std::string name;
			void* state = nullptr;
		};

		// Snapshot the current AnimationStateSet without retaining Ogre pointers
		// after the caller finishes the operation. The bridge is compiled in
		// C++14 against the vendored Ogre headers so the STL-backed iterator ABI
		// stays isolated from the rest of EXU.
		bool TryEnumerateAnimationStates(
			void* entity,
			std::vector<AnimationStateRef>& outStates) noexcept;
	}
}
