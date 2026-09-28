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

#include "Ogre/OgreProc.h"
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

#ifdef EXU_OGRE_RESTORE_REGISTER
#undef register
#undef EXU_OGRE_RESTORE_REGISTER
#endif

#include <exception>

namespace ExtraUtilities
{
	namespace OgreAnimationInventory
	{
		namespace
		{
			// Ogre entry points are resolved from the loaded OgreMain.dll by
			// mangled name (Ogre/OgreProc.h), never linked through the hand-made
			// lib/OgreMain.lib import subset. A load-time import of a name the
			// shipped OgreMain.dll does not export would stop exu.dll loading at
			// all; a missing export here only makes the inventory unavailable.
			//
			// Entity::getAllAnimationStates is the same export the GameObject
			// animation path already resolves in Ogre/Ogre.h.
			using GetAllAnimationStatesFn = void*(__thiscall*)(void* entity);

			// AnimationStateSet::getAnimationStateIterator() returns the header-only
			// MapIterator by value, so the call goes through the real return type
			// and the compiler supplies the hidden return slot.
			using GetAnimationStateIteratorFn =
				Ogre::AnimationStateIterator(__thiscall*)(void* stateSet);

			GetAllAnimationStatesFn ResolveGetAllAnimationStates() noexcept
			{
				static const OgreDll::OgreProc<GetAllAnimationStatesFn> proc(
					"?getAllAnimationStates@Entity@Ogre@@QBEPAVAnimationStateSet@2@XZ");
				return proc.Get();
			}

			GetAnimationStateIteratorFn ResolveGetAnimationStateIterator() noexcept
			{
				static const OgreDll::OgreProc<GetAnimationStateIteratorFn> proc(
					"?getAnimationStateIterator@AnimationStateSet@Ogre@@QAE?AV?$MapIterator@V?$map@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PAVAnimationState@Ogre@@U?$less@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@2@V?$allocator@U?$pair@$$CBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PAVAnimationState@Ogre@@@std@@@2@@std@@@2@XZ");
				return proc.Get();
			}
		}

		bool TryEnumerateAnimationStates(
			void* entity,
			std::vector<AnimationStateRef>& outStates) noexcept
		{
			outStates.clear();
			if (entity == nullptr)
			{
				return false;
			}

			const GetAllAnimationStatesFn getAllAnimationStates = ResolveGetAllAnimationStates();
			const GetAnimationStateIteratorFn getIterator = ResolveGetAnimationStateIterator();
			if (getAllAnimationStates == nullptr || getIterator == nullptr)
			{
				return false;
			}

			const bool completed = Seh::Guard(
				"TryEnumerateAnimationStates",
				[&]
				{
					void* const stateSet = getAllAnimationStates(entity);
					if (stateSet == nullptr)
					{
						return;
					}

					Ogre::AnimationStateIterator it = getIterator(stateSet);
					while (it.hasMoreElements())
					{
						// The map key is the animation name and is exactly what
						// AnimationStateSet::getAnimationState(name) accepts, so it
						// is the name Has/GetInfo/Play must be given.
						AnimationStateRef ref;
						ref.name = it.peekNextKey();
						Ogre::AnimationState* const state = it.getNext();
						if (state == nullptr)
						{
							continue;
						}

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
