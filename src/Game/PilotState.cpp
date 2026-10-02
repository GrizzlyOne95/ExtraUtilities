/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Game/PilotState.h"

#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ExtraUtilities::Lua::PilotState
{
	namespace
	{
		constexpr std::size_t kPersonCarrierOffset = 0x1A0;
		constexpr std::size_t kPersonAnimStateOffset = 0x228;
		constexpr std::size_t kPersonGroundObjectOffset = 0x230;
		constexpr std::size_t kPersonCurAnimOffset = 0x2A8;
		constexpr std::size_t kPersonAnimHandleOffset = 0x2AC;

		constexpr std::size_t kGroundFlagsOffset = 0x114;
		constexpr std::uint32_t kGroundedBit = 0x80;

		constexpr std::size_t kCarrierWeaponsOffset = 0x18;
		constexpr std::size_t kCarrierSelectedMaskOffset = 0x30;
		constexpr std::size_t kWeaponClassOffset = 0x08;
		constexpr std::size_t kWeaponClassSignatureOffset = 0x0C;
		constexpr std::size_t kWeaponClassOdfOffset = 0x20;
		constexpr std::uint32_t kSniperSignature = 0x534E4950u; // "SNIP"

		struct TypeDescriptor
		{
			void* vftable;
			void* spare;
			char name[1];
		};

		struct CompleteObjectLocator
		{
			std::uint32_t signature;
			std::uint32_t offset;
			std::uint32_t cdOffset;
			TypeDescriptor* typeDescriptor;
			void* classDescriptor;
		};

		bool IsPersonObjectSeh(void* object) noexcept
		{
			if (object == nullptr)
			{
				return false;
			}

			__try
			{
				void** const vftable = *reinterpret_cast<void***>(object);
				if (vftable == nullptr)
				{
					return false;
				}

				auto* const locator =
					*(reinterpret_cast<CompleteObjectLocator**>(vftable) - 1);
				if (locator == nullptr || locator->typeDescriptor == nullptr)
				{
					return false;
				}

				const char* const name = locator->typeDescriptor->name;
				return name != nullptr && std::strcmp(name, ".?AVPerson@@") == 0;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool CaptureSeh(const void* expectedPerson, Snapshot& out) noexcept
		{
			out = {};

			__try
			{
				auto* const userObjectSlot =
					reinterpret_cast<void* const*>(BZR::GameObject::p_userObject);
				if (userObjectSlot == nullptr)
				{
					return false;
				}

				void* const person = *userObjectSlot;
				if (person == nullptr ||
					(expectedPerson != nullptr && person != expectedPerson) ||
					!IsPersonObjectSeh(person))
				{
					return false;
				}

				const auto* const personBytes =
					reinterpret_cast<const std::uint8_t*>(person);

				out.nativeState =
					*reinterpret_cast<const std::uint32_t*>(personBytes + kPersonAnimStateOffset);
				out.animationIndex =
					*reinterpret_cast<const std::int32_t*>(personBytes + kPersonCurAnimOffset);
				out.animationHandle =
					*reinterpret_cast<const std::int32_t*>(personBytes + kPersonAnimHandleOffset);

				void* const groundObject =
					*reinterpret_cast<void* const*>(personBytes + kPersonGroundObjectOffset);
				if (groundObject != nullptr)
				{
					const auto* const groundBytes =
						reinterpret_cast<const std::uint8_t*>(groundObject);
					const std::uint32_t flags =
						*reinterpret_cast<const std::uint32_t*>(groundBytes + kGroundFlagsOffset);
					out.grounded = (flags & kGroundedBit) != 0;
				}

				void* const carrier =
					*reinterpret_cast<void* const*>(personBytes + kPersonCarrierOffset);
				if (carrier != nullptr)
				{
					const auto* const carrierBytes =
						reinterpret_cast<const std::uint8_t*>(carrier);
					out.selectedWeaponMask =
						*reinterpret_cast<const std::uint32_t*>(
							carrierBytes + kCarrierSelectedMaskOffset);

					auto* const weapons =
						reinterpret_cast<void* const*>(
							carrierBytes + kCarrierWeaponsOffset);

					for (std::int32_t slot = 0; slot < 5; ++slot)
					{
						if ((out.selectedWeaponMask & (1u << slot)) == 0)
						{
							continue;
						}

						void* const weapon = weapons[slot];
						if (weapon == nullptr)
						{
							continue;
						}

						const auto* const weaponBytes =
							reinterpret_cast<const std::uint8_t*>(weapon);
						void* const weaponClass =
							*reinterpret_cast<void* const*>(
								weaponBytes + kWeaponClassOffset);
						if (weaponClass == nullptr)
						{
							continue;
						}

						const auto* const classBytes =
							reinterpret_cast<const std::uint8_t*>(weaponClass);
						const std::uint32_t signature =
							*reinterpret_cast<const std::uint32_t*>(
								classBytes + kWeaponClassSignatureOffset);

						if (out.selectedWeaponSlot < 0)
						{
							out.selectedWeaponSlot = slot;
							out.selectedWeaponSignature = signature;

							const char* const odf =
								reinterpret_cast<const char*>(
									classBytes + kWeaponClassOdfOffset);
							for (std::size_t i = 0; i + 1 < sizeof(out.selectedWeaponOdf); ++i)
							{
								const char ch = odf[i];
								out.selectedWeaponOdf[i] = ch;
								if (ch == '\0')
								{
									break;
								}
							}
							out.selectedWeaponOdf[sizeof(out.selectedWeaponOdf) - 1] = '\0';
						}

						if (signature == kSniperSignature)
						{
							out.sniperSelected = true;
						}
					}
				}

				out.available = true;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out = {};
				return false;
			}
		}
	}

	bool Capture(Snapshot& outSnapshot) noexcept
	{
		outSnapshot = {};
		if (!RuntimeGate::IsSupported())
		{
			return false;
		}

		return CaptureSeh(nullptr, outSnapshot);
	}

	bool CaptureIfCurrent(const void* candidate, Snapshot& outSnapshot) noexcept
	{
		outSnapshot = {};
		if (candidate == nullptr || !RuntimeGate::IsSupported())
		{
			return false;
		}

		return CaptureSeh(candidate, outSnapshot);
	}
}
