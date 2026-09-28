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

#include "OgreAnimationInventoryBridge.h"

#include "Util/Logging.h"
#include "Util/SehGuard.h"

#ifndef register
#define EXU_OGRE_RESTORE_REGISTER
#define register
#endif

#ifndef _STLP_MSVC
#define _STLP_MSVC 1
#endif

#include <Windows.h>

#include <OgreAnimationState.h>
#include <OgreEntity.h>

#ifdef EXU_OGRE_RESTORE_REGISTER
#undef register
#undef EXU_OGRE_RESTORE_REGISTER
#endif

#include <exception>

namespace ExtraUtilities
{
	namespace OgreAnimationInventory
	{
		bool TryEnumerateAnimationStates(
			void* entity,
			std::vector<AnimationStateRef>& outStates) noexcept
		{
			outStates.clear();
			if (entity == nullptr)
			{
				return false;
			}

			const bool completed = Seh::Guard(
				"TryEnumerateAnimationStates",
				[&]
				{
					Ogre::Entity* const ogreEntity = static_cast<Ogre::Entity*>(entity);
					Ogre::AnimationStateSet* const stateSet = ogreEntity->getAllAnimationStates();
					if (stateSet == nullptr)
					{
						return;
					}

					Ogre::AnimationStateIterator it = stateSet->getAnimationStateIterator();
					while (it.hasMoreElements())
					{
						Ogre::AnimationState* const state = it.getNext();
						if (state == nullptr)
						{
							continue;
						}

						AnimationStateRef ref;
						ref.name = state->getAnimationName();
						ref.state = state;
						outStates.push_back(ref);
					}
				},
				[&](unsigned long exceptionCode)
				{
					Logging::LogMessage(
						"[EXU::Animation] enumeration fault entity=%p code=0x%08lX",
						entity,
						exceptionCode);
				});

			if (!completed)
			{
				outStates.clear();
			}
			return completed;
		}
	}
}
