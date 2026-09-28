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
		bool TryReadRawPilotTargets(RawPilotTargets& outTargets) noexcept
		{
			outTargets = {};

			__try
			{
				auto* const userObjectSlot =
					reinterpret_cast<void* const*>(BZR::GameObject::p_userObject);
				if (userObjectSlot == nullptr)
				{
					return false;
				}

				void* const person = *userObjectSlot;
				if (person == nullptr)
				{
					return false;
				}

				auto* const personBytes = reinterpret_cast<const uint8_t*>(person);
				void* const renderBridge =
					*reinterpret_cast<void* const*>(personBytes + kPersonRenderBridgeOffset);
				if (renderBridge == nullptr)
				{
					return false;
				}

				auto* const bridgeBytes = reinterpret_cast<const uint8_t*>(renderBridge);
				void* const worldEntity =
					*reinterpret_cast<void* const*>(bridgeBytes + kRenderBridgeWorldEntityOffset);
				void* const firstPersonEntity =
					*reinterpret_cast<void* const*>(bridgeBytes + kRenderBridgeFirstPersonEntityOffset);

				outTargets.person = person;
				outTargets.renderBridge = renderBridge;
				outTargets.worldEntity = worldEntity;
				outTargets.firstPersonEntity = firstPersonEntity;
				return firstPersonEntity != nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outTargets = {};
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
			__except (EXCEPTION_EXECUTE_HANDLER)
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

	bool ResolveNativeLocalFirstPersonEntity(void*& outEntity) noexcept
	{
		outEntity = nullptr;
		if (!IsNativeResolverAvailable())
		{
			return false;
		}

		RawPilotTargets targets{};
		if (!TryReadRawPilotTargets(targets) ||
			!IsPersonObject(targets.person) ||
			targets.firstPersonEntity == targets.worldEntity)
		{
			return false;
		}

		// Keep the production qualification intentionally narrow for chunk 1:
		// this is the stock pilot vocabulary already proven on the dedicated
		// *_fp entity. Custom mesh registration/relaxed qualification belongs
		// to the later asset-qualification work order.
		const std::string idle("idle");
		const std::string standToKneel("stand2Kneel");
		if (!GameObject::HasAnimation(targets.firstPersonEntity, idle) ||
			!GameObject::HasAnimation(targets.firstPersonEntity, standToKneel))
		{
			return false;
		}

		outEntity = targets.firstPersonEntity;
		return true;
	}
}
