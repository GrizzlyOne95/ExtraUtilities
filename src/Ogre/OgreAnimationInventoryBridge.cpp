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

#include <Windows.h>

#include <string>

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
			// The shipped OgreMain.dll is built with MSVC 2013 (MSVCP120), whose
			// std::map node stores the value before _Color/_Isnil; EXU's MSVC 2015+
			// headers put it after. Walking AnimationStateSet's map through the
			// header-only MapIterator therefore reads keys from the wrong offset
			// (live fault on ISDFC's ispilo_cockpit, 2026-10-03). Enumerate by
			// index through exported calls instead: only std::string crosses the
			// boundary, and its layout is shared by both runtimes (the same
			// assumption GameObject::HasAnimation already relies on).
			using GetAllAnimationStatesFn = void*(__thiscall*)(void* entity);
			using GetSkeletonFn = void*(__thiscall*)(void* entity);
			using GetNumAnimationsFn = unsigned short(__thiscall*)(void* skeleton);
			using GetAnimationByIndexFn = void*(__thiscall*)(void* skeleton, unsigned short index);
			using GetAnimationNameFn = const std::string&(__thiscall*)(void* animation);
			using HasAnimationStateFn = bool(__thiscall*)(void* stateSet, const std::string& name);
			using GetAnimationStateFn = void*(__thiscall*)(void* stateSet, const std::string& name);

			struct InventoryProcs
			{
				GetAllAnimationStatesFn getAllAnimationStates = nullptr;
				GetSkeletonFn getSkeleton = nullptr;
				GetNumAnimationsFn getNumAnimations = nullptr;
				GetAnimationByIndexFn getAnimation = nullptr;
				GetAnimationNameFn getName = nullptr;
				HasAnimationStateFn hasAnimationState = nullptr;
				GetAnimationStateFn getAnimationState = nullptr;

				bool IsComplete() const noexcept
				{
					return getAllAnimationStates && getSkeleton && getNumAnimations &&
						getAnimation && getName && hasAnimationState && getAnimationState;
				}
			};

			InventoryProcs ResolveInventoryProcs() noexcept
			{
				static const OgreDll::OgreProc<GetAllAnimationStatesFn> getAllAnimationStates(
					"?getAllAnimationStates@Entity@Ogre@@QBEPAVAnimationStateSet@2@XZ");
				static const OgreDll::OgreProc<GetSkeletonFn> getSkeleton(
					"?getSkeleton@Entity@Ogre@@QBEPAVSkeletonInstance@2@XZ");
				// Entity::getSkeleton always returns a SkeletonInstance, so calling
				// SkeletonInstance's own overrides directly is the virtual target.
				static const OgreDll::OgreProc<GetNumAnimationsFn> getNumAnimations(
					"?getNumAnimations@SkeletonInstance@Ogre@@UBEGXZ");
				static const OgreDll::OgreProc<GetAnimationByIndexFn> getAnimation(
					"?getAnimation@SkeletonInstance@Ogre@@UBEPAVAnimation@2@G@Z");
				static const OgreDll::OgreProc<GetAnimationNameFn> getName(
					"?getName@Animation@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ");
				static const OgreDll::OgreProc<HasAnimationStateFn> hasAnimationState(
					"?hasAnimationState@AnimationStateSet@Ogre@@QBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
				static const OgreDll::OgreProc<GetAnimationStateFn> getAnimationState(
					"?getAnimationState@AnimationStateSet@Ogre@@QBEPAVAnimationState@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");

				InventoryProcs procs;
				procs.getAllAnimationStates = getAllAnimationStates.Get();
				procs.getSkeleton = getSkeleton.Get();
				procs.getNumAnimations = getNumAnimations.Get();
				procs.getAnimation = getAnimation.Get();
				procs.getName = getName.Get();
				procs.hasAnimationState = hasAnimationState.Get();
				procs.getAnimationState = getAnimationState.Get();
				return procs;
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

			const InventoryProcs procs = ResolveInventoryProcs();
			if (!procs.IsComplete())
			{
				return false;
			}

			// Skeletal animations only: vertex/pose-only entities have no
			// skeleton and report the inventory as unavailable rather than empty.
			bool hasSkeleton = false;
			const bool completed = Seh::Guard(
				"TryEnumerateAnimationStates",
				[&]
				{
					void* const stateSet = procs.getAllAnimationStates(entity);
					void* const skeleton = procs.getSkeleton(entity);
					if (stateSet == nullptr || skeleton == nullptr)
					{
						return;
					}
					hasSkeleton = true;

					const unsigned short count = procs.getNumAnimations(skeleton);
					outStates.reserve(count);
					for (unsigned short index = 0; index < count; ++index)
					{
						void* const animation = procs.getAnimation(skeleton, index);
						if (animation == nullptr)
						{
							continue;
						}

						// The animation name is the AnimationStateSet key, so it is
						// the name Has/GetInfo/Play must be given.
						AnimationStateRef ref;
						ref.name = procs.getName(animation);
						if (!procs.hasAnimationState(stateSet, ref.name))
						{
							continue;
						}
						ref.state = procs.getAnimationState(stateSet, ref.name);
						if (ref.state == nullptr)
						{
							continue;
						}
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

			if (!completed || !hasSkeleton)
			{
				outStates.clear();
				return false;
			}
			return true;
		}
	}
}
