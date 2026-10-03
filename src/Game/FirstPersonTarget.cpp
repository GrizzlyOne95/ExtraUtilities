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

#include "Game/FirstPersonTarget.h"

#include "Game/GameObject.h"
#include "Util/MsvcRtti.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ExtraUtilities::Lua::FirstPersonTarget
{
	namespace
	{
		// Released Redux 2.2.301 render bridge, runtime-qualified during the
		// 2026-09-05 pilot flashlight investigation:
		//   local Person + 0x0F0 -> render bridge
		//   bridge + 0x094       -> world pilot Ogre::Entity
		//   bridge + 0x0C0       -> first-person pilot Ogre::Entity
		//
		// The direct FP field is preferable to scene enumeration for EXU: it
		// follows the same mission-owned Person lifetime as p_userObject and
		// can be re-read for every Lua operation without caching Ogre pointers.
		constexpr size_t kPersonRenderBridgeOffset = 0x0F0;
		constexpr size_t kRenderBridgeWorldEntityOffset = 0x094;
		constexpr size_t kRenderBridgeFirstPersonEntityOffset = 0x0C0;

		struct RawPilotTargets
		{
			void* person = nullptr;
			void* renderBridge = nullptr;
			void* worldEntity = nullptr;
			void* firstPersonEntity = nullptr;
		};

		// POD-only SEH helper: no C++ objects that require unwinding may live in
		// a function containing __try under MSVC /EHsc.
		bool TryReadRawPilotTargets(RawPilotTargets& outTargets,
			NativeResolveFailure& outFailure) noexcept
		{
			outTargets = {};
			outFailure = NativeResolveFailure::ReadFaulted;

			__try
			{
				auto* const userObjectSlot =
					reinterpret_cast<void* const*>(BZR::GameObject::p_userObject);
				if (userObjectSlot == nullptr)
				{
					outFailure = NativeResolveFailure::NoLocalUserObject;
					return false;
				}

				void* const person = *userObjectSlot;
				if (person == nullptr)
				{
					outFailure = NativeResolveFailure::NoLocalUserObject;
					return false;
				}

				auto* const personBytes = reinterpret_cast<const uint8_t*>(person);
				void* const renderBridge =
					*reinterpret_cast<void* const*>(personBytes + kPersonRenderBridgeOffset);
				outTargets.person = person;
				if (renderBridge == nullptr)
				{
					outFailure = NativeResolveFailure::NoRenderBridge;
					return false;
				}

				auto* const bridgeBytes = reinterpret_cast<const uint8_t*>(renderBridge);
				void* const worldEntity =
					*reinterpret_cast<void* const*>(bridgeBytes + kRenderBridgeWorldEntityOffset);
				void* const firstPersonEntity =
					*reinterpret_cast<void* const*>(bridgeBytes + kRenderBridgeFirstPersonEntityOffset);

				outTargets.renderBridge = renderBridge;
				outTargets.worldEntity = worldEntity;
				outTargets.firstPersonEntity = firstPersonEntity;
				outFailure = firstPersonEntity != nullptr
					? NativeResolveFailure::None
					: NativeResolveFailure::NoFirstPersonEntity;
				return firstPersonEntity != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				outTargets = {};
				outFailure = NativeResolveFailure::ReadFaulted;
				return false;
			}
		}

		// The local user slot may hold a vehicle, building, shell-owned object,
		// or nothing at all. Qualify Person through the executable's MSVC RTTI
		// before interpreting render-bridge offsets as pilot fields.
		bool IsPersonObject(void* object) noexcept
		{
			if (object == nullptr)
			{
				return false;
			}

			bool isPerson = false;
			__try
			{
				void** const vftable = *reinterpret_cast<void***>(object);
				if (vftable == nullptr)
				{
					return false;
				}

				auto* const locator =
					*(reinterpret_cast<MsvcRtti::CompleteObjectLocator**>(vftable) - 1);
				if (locator == nullptr || locator->pTypeDescriptor == nullptr)
				{
					return false;
				}

				const char* const rawName = locator->pTypeDescriptor->name;
				isPerson = rawName != nullptr &&
					std::strstr(rawName, "Person@@") != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}

			return isPerson;
		}
	}

	bool IsNativeResolverAvailable() noexcept
	{
		return RuntimeGate::IsSupported();
	}

	const char* DescribeNativeResolveFailure(NativeResolveFailure failure) noexcept
	{
		switch (failure)
		{
		case NativeResolveFailure::None: return "none";
		case NativeResolveFailure::RuntimeGateClosed: return "runtime gate closed";
		case NativeResolveFailure::NoLocalUserObject: return "no local user object";
		case NativeResolveFailure::NoRenderBridge: return "user object has no render bridge";
		case NativeResolveFailure::NoFirstPersonEntity: return "render bridge has no first-person entity";
		case NativeResolveFailure::ReadFaulted: return "render bridge read faulted";
		case NativeResolveFailure::NotPerson: return "user object is not a Person";
		case NativeResolveFailure::SharedWithWorldEntity: return "first-person entity is the world entity";
		case NativeResolveFailure::MissingIdle: return "first-person entity lacks 'idle'";
		case NativeResolveFailure::MissingStandToKneel: return "first-person entity lacks 'stand2Kneel'";
		}
		return "unknown";
	}

	bool ResolveNativeLocalFirstPersonEntity(void*& outEntity,
		NativeResolveFailure* outFailure) noexcept
	{
		NativeResolveFailure ignored = NativeResolveFailure::None;
		NativeResolveFailure& failure = outFailure ? *outFailure : ignored;
		failure = NativeResolveFailure::None;
		outEntity = nullptr;
		if (!IsNativeResolverAvailable())
		{
			failure = NativeResolveFailure::RuntimeGateClosed;
			return false;
		}

		// Qualify Person before trusting the render-bridge offsets: a vehicle in
		// the user slot would otherwise be read with pilot layout assumptions.
		RawPilotTargets targets{};
		const bool readOk = TryReadRawPilotTargets(targets, failure);
		if (targets.person != nullptr && !IsPersonObject(targets.person))
		{
			failure = NativeResolveFailure::NotPerson;
			return false;
		}
		if (!readOk)
		{
			return false;
		}
		if (targets.firstPersonEntity == targets.worldEntity)
		{
			failure = NativeResolveFailure::SharedWithWorldEntity;
			return false;
		}

		// Keep the production qualification intentionally narrow for chunk 1:
		// this is the stock pilot vocabulary already proven on the dedicated
		// FP entity. Custom mesh registration/relaxed qualification belongs
		// to the later asset-qualification work order.
		const std::string idle("idle");
		const std::string standToKneel("stand2Kneel");
		if (!GameObject::HasAnimation(targets.firstPersonEntity, idle))
		{
			failure = NativeResolveFailure::MissingIdle;
			return false;
		}
		if (!GameObject::HasAnimation(targets.firstPersonEntity, standToKneel))
		{
			failure = NativeResolveFailure::MissingStandToKneel;
			return false;
		}

		outEntity = targets.firstPersonEntity;
		return true;
	}

	bool ReadPersonRenderEntities(const void* person, void*& outWorldEntity,
		void*& outFirstPersonEntity) noexcept
	{
		outWorldEntity = nullptr;
		outFirstPersonEntity = nullptr;
		if (person == nullptr || !IsNativeResolverAvailable())
		{
			return false;
		}

		__try
		{
			auto* const personBytes = reinterpret_cast<const uint8_t*>(person);
			void* const renderBridge =
				*reinterpret_cast<void* const*>(personBytes + kPersonRenderBridgeOffset);
			if (renderBridge == nullptr)
			{
				return false;
			}

			auto* const bridgeBytes = reinterpret_cast<const uint8_t*>(renderBridge);
			outWorldEntity =
				*reinterpret_cast<void* const*>(bridgeBytes + kRenderBridgeWorldEntityOffset);
			outFirstPersonEntity =
				*reinterpret_cast<void* const*>(bridgeBytes + kRenderBridgeFirstPersonEntityOffset);
			return true;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			outWorldEntity = nullptr;
			outFirstPersonEntity = nullptr;
			return false;
		}
	}
}
