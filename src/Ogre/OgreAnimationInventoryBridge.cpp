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

#include <cstring>
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
			using GetNumAnimationsFn = unsigned short(__thiscall*)(void* container);
			using GetAnimationByIndexFn = void*(__thiscall*)(void* container, unsigned short index);
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
				// Byte offset of the AnimationContainer base inside Skeleton.
				long containerOffset = -1;

				bool IsComplete() const noexcept
				{
					return getAllAnimationStates && getSkeleton && getNumAnimations &&
						getAnimation && getName && hasAnimationState && getAnimationState &&
						containerOffset > 0;
				}
			};

			// Follows incremental-link `jmp rel32` thunks to the real body.
			const unsigned char* FollowThunks(const void* code) noexcept
			{
				const auto* bytes = static_cast<const unsigned char*>(code);
				for (int hop = 0; bytes != nullptr && hop < 4 && bytes[0] == 0xE9; ++hop)
				{
					long rel = 0;
					std::memcpy(&rel, bytes + 1, sizeof(rel));
					bytes = bytes + 5 + rel;
				}
				return bytes;
			}

			// SkeletonInstance::getNumAnimations/getAnimation(ushort) override
			// AnimationContainer virtuals. AnimationContainer is a non-primary base
			// of Skeleton, so MSVC compiles them to expect `this` pointing at that
			// subobject, not at the SkeletonInstance (live fault 2026-10-03:
			// [this+0x68] read garbage, then faulted at null+0xD0). Both bodies
			// forward through mSkeleton with `mov ecx,[ecx+disp8]; add ecx,imm32`;
			// the imm32 is the base offset. Read it from the shipped code rather
			// than hard-coding it, and refuse if either body does not match.
			long DecodeForwardOffset(const unsigned char* body) noexcept
			{
				if (body == nullptr)
				{
					return -1;
				}
				// getAnimation(ushort) opens a frame first: push ebp; mov ebp,esp.
				if (body[0] == 0x55 && body[1] == 0x8B && body[2] == 0xEC)
				{
					body += 3;
				}
				if (body[0] != 0x8B || body[1] != 0x49 || body[3] != 0x81 || body[4] != 0xC1)
				{
					return -1;
				}
				long offset = 0;
				std::memcpy(&offset, body + 5, sizeof(offset));
				return offset;
			}

			long ResolveContainerOffset(const void* getNumAnimations, const void* getAnimation) noexcept
			{
				long numOffset = -1;
				long animOffset = -1;
				const bool read = Seh::Guard(
					"ResolveContainerOffset",
					[&]
					{
						numOffset = DecodeForwardOffset(FollowThunks(getNumAnimations));
						animOffset = DecodeForwardOffset(FollowThunks(getAnimation));
					});
				if (!read || numOffset <= 0 || numOffset != animOffset || numOffset > 0x1000)
				{
					Logging::LogMessage(
						"[EXU::Animation] inventory unavailable: SkeletonInstance forwarders not recognised (getNumAnimations=%ld getAnimation=%ld)",
						numOffset,
						animOffset);
					return -1;
				}
				return numOffset;
			}

			InventoryProcs ResolveInventoryProcs() noexcept
			{
				static const OgreDll::OgreProc<GetAllAnimationStatesFn> getAllAnimationStates(
					"?getAllAnimationStates@Entity@Ogre@@QBEPAVAnimationStateSet@2@XZ");
				static const OgreDll::OgreProc<GetSkeletonFn> getSkeleton(
					"?getSkeleton@Entity@Ogre@@QBEPAVSkeletonInstance@2@XZ");
				// Entity::getSkeleton always returns a SkeletonInstance, so calling
				// SkeletonInstance's own overrides directly is the virtual target;
				// they take the AnimationContainer subobject (containerOffset).
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
				if (procs.getNumAnimations != nullptr && procs.getAnimation != nullptr)
				{
					static const long containerOffset = ResolveContainerOffset(
						reinterpret_cast<const void*>(procs.getNumAnimations),
						reinterpret_cast<const void*>(procs.getAnimation));
					procs.containerOffset = containerOffset;
				}
				return procs;
			}

			// Skeleton::getBlendMode is a virtual declared on Skeleton itself, so
			// it lives in the primary (Resource) vtable and takes the plain
			// Skeleton/SkeletonInstance pointer; no base adjustment applies.
			// The shipped body is `mov eax,[ecx+0D4h]; ret` and 0xD4 is the
			// AnimationContainer subobject offset (0xD0, decoded above) + 4:
			// mBlendState, Skeleton's first data member, right after that
			// subobject's vptr. Both facts are checked against the loaded code
			// before the first call; anything else leaves the mode unknown.
			using GetBlendModeFn = int(__thiscall*)(void* skeleton);

			GetBlendModeFn QualifyGetBlendMode(GetBlendModeFn proc, long containerOffset) noexcept
			{
				if (proc == nullptr || containerOffset <= 0)
				{
					return nullptr;
				}
				long displacement = -1;
				const bool read = Seh::Guard(
					"QualifyGetBlendMode",
					[&]
					{
						const unsigned char* body = FollowThunks(reinterpret_cast<const void*>(proc));
						if (body != nullptr && body[0] == 0x8B && body[1] == 0x81 && body[6] == 0xC3)
						{
							std::memcpy(&displacement, body + 2, sizeof(displacement));
						}
					});
				if (!read || displacement != containerOffset + 4)
				{
					Logging::LogMessage(
						"[EXU::Animation] skeleton blend mode unavailable: Skeleton::getBlendMode not the verified field read (displacement=%ld containerOffset=%ld)",
						displacement,
						containerOffset);
					return nullptr;
				}
				return proc;
			}

			GetBlendModeFn ResolveGetBlendMode(long containerOffset) noexcept
			{
				static const OgreDll::OgreProc<GetBlendModeFn> getBlendMode(
					"?getBlendMode@Skeleton@Ogre@@UBE?AW4SkeletonAnimationBlendMode@2@XZ");
				static const GetBlendModeFn qualified =
					QualifyGetBlendMode(getBlendMode.Get(), containerOffset);
				return qualified;
			}
		}

		bool TryGetSkeletonBlendMode(void* entity, int& outMode) noexcept
		{
			outMode = -1;
			if (entity == nullptr)
			{
				return false;
			}
			const InventoryProcs procs = ResolveInventoryProcs();
			if (procs.getSkeleton == nullptr || procs.containerOffset <= 0)
			{
				return false;
			}
			const GetBlendModeFn getBlendMode = ResolveGetBlendMode(procs.containerOffset);
			if (getBlendMode == nullptr)
			{
				return false;
			}

			bool hasSkeleton = false;
			const bool completed = Seh::Guard(
				"TryGetSkeletonBlendMode",
				[&]
				{
					void* const skeleton = procs.getSkeleton(entity);
					if (skeleton == nullptr)
					{
						return;
					}
					hasSkeleton = true;
					outMode = getBlendMode(skeleton);
				},
				[&](unsigned long exceptionCode)
				{
					Logging::LogMessage(
						"[EXU::Animation] blend mode read fault entity=%p code=0x%08lX",
						entity,
						exceptionCode);
				});
			return completed && hasSkeleton;
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
			// Diagnostic breadcrumbs for the fault log: which Ogre call faulted.
			const char* stage = "getAllAnimationStates";
			void* faultSkeleton = nullptr;
			void* faultAnimation = nullptr;
			unsigned int faultIndex = 0;
			unsigned int faultCount = 0;
			const bool completed = Seh::Guard(
				"TryEnumerateAnimationStates",
				[&]
				{
					void* const stateSet = procs.getAllAnimationStates(entity);
					stage = "getSkeleton";
					void* const skeleton = procs.getSkeleton(entity);
					faultSkeleton = skeleton;
					if (stateSet == nullptr || skeleton == nullptr)
					{
						return;
					}
					hasSkeleton = true;

					void* const container = static_cast<char*>(skeleton) + procs.containerOffset;
					stage = "getNumAnimations";
					const unsigned short count = procs.getNumAnimations(container);
					faultCount = count;
					outStates.reserve(count);
					for (unsigned short index = 0; index < count; ++index)
					{
						faultIndex = index;
						faultAnimation = nullptr;
						stage = "getAnimation";
						void* const animation = procs.getAnimation(container, index);
						faultAnimation = animation;
						if (animation == nullptr)
						{
							continue;
						}

						// The animation name is the AnimationStateSet key, so it is
						// the name Has/GetInfo/Play must be given.
						AnimationStateRef ref;
						stage = "getName";
						ref.name = procs.getName(animation);
						stage = "hasAnimationState";
						if (!procs.hasAnimationState(stateSet, ref.name))
						{
							continue;
						}
						stage = "getAnimationState";
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
						"[EXU::Animation] enumeration fault entity=%p code=0x%08lX stage=%s skeleton=%p count=%u index=%u animation=%p",
						entity,
						exceptionCode,
						stage,
						faultSkeleton,
						faultCount,
						faultIndex,
						faultAnimation);
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
