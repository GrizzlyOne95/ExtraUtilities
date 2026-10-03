/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Game/PlayerTrigger.h"

#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "Util/SignatureResolver.h"
#include "bzr.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::Lua::PlayerTrigger
{
	namespace
	{
		// UserProcess::Execute, 0x0060A93D (exu.json PlayerInput.WeaponFireHeldRead):
		//   movzx edx,al; test edx,edx; jne +0x2B6;
		//   movsx eax, byte ptr [WeaponFireHeld]; test eax,eax; je +0x0C
		// The four operand bytes at +0x0E are read, not matched.
		constexpr std::size_t kFireOperandOffset = 0x0E;
		constexpr std::array<std::uint8_t, kFireOperandOffset> kFireReadPrefix = {
			0x0F, 0xB6, 0xD0, 0x85, 0xD2, 0x0F, 0x85, 0xB6, 0x02, 0x00, 0x00, 0x0F, 0xBE, 0x05
		};
		constexpr std::array<std::uint8_t, 4> kFireReadSuffix = { 0x85, 0xC0, 0x74, 0x0C };

		// 0x0060A984 (PlayerInput.WeaponFireAutoHeldRead):
		//   movsx eax, byte ptr [WeaponFireAutoHeld]; test eax,eax; je rel32;
		//   mov byte ptr [ebp-0x118],1
		constexpr std::size_t kAutoOperandOffset = 0x03;
		constexpr std::array<std::uint8_t, kAutoOperandOffset> kAutoReadPrefix = { 0x0F, 0xBE, 0x05 };
		constexpr std::array<std::uint8_t, 15> kAutoReadSuffix = {
			0x85, 0xC0, 0x0F, 0x84, 0x3F, 0x02, 0x00, 0x00, 0xC6, 0x85, 0xE8, 0xFE, 0xFF, 0xFF, 0x01
		};

		std::atomic<bool> g_qualified{ false };
		std::atomic<bool> g_attempted{ false };
		volatile const std::int8_t* g_fire = nullptr;
		volatile const std::int8_t* g_fireAuto = nullptr;

		bool ReadOperandSeh(std::uintptr_t address, std::uintptr_t& out) noexcept
		{
			out = 0;
			__try
			{
				out = *reinterpret_cast<const std::uint32_t*>(address);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Matches prefix, operand, suffix at site and checks the operand names
		// the catalogued byte.
		template <std::size_t P, std::size_t S>
		bool QualifySite(
			std::uintptr_t site,
			const std::array<std::uint8_t, P>& prefix,
			const std::array<std::uint8_t, S>& suffix,
			std::uintptr_t expectedTarget,
			std::uintptr_t& outTarget) noexcept
		{
			outTarget = 0;
			if (!SignatureResolver::MatchBytes(site, prefix) ||
				!SignatureResolver::MatchBytes(site + P + 4, suffix))
			{
				return false;
			}
			std::uintptr_t operand = 0;
			if (!ReadOperandSeh(site + P, operand) || operand != expectedTarget)
			{
				return false;
			}
			outTarget = operand;
			return true;
		}

		bool ReadByteSeh(volatile const std::int8_t* source, bool& outHeld) noexcept
		{
			outHeld = false;
			__try
			{
				outHeld = *source != 0;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}
	}

	bool Qualify() noexcept
	{
		if (!RuntimeGate::IsSupported())
		{
			return false;
		}
		if (g_attempted.exchange(true, std::memory_order_acq_rel))
		{
			return g_qualified.load(std::memory_order_acquire);
		}

		std::uintptr_t fire = 0;
		std::uintptr_t fireAuto = 0;
		const bool ok =
			QualifySite(
				BZR::PlayerInput::WeaponFireHeldRead,
				kFireReadPrefix,
				kFireReadSuffix,
				reinterpret_cast<std::uintptr_t>(BZR::PlayerInput::weaponFireHeld),
				fire) &&
			QualifySite(
				BZR::PlayerInput::WeaponFireAutoHeldRead,
				kAutoReadPrefix,
				kAutoReadSuffix,
				reinterpret_cast<std::uintptr_t>(BZR::PlayerInput::weaponFireAutoHeld),
				fireAuto);
		if (!ok)
		{
			try
			{
				Logging::LogMessage(
					"exu: first-person trigger signal unavailable; UserProcess fire-byte read sites do not match the catalog");
			}
			catch (...)
			{
				OutputDebugStringA("ExtraUtilities: first-person trigger qualification log line dropped\n");
			}
			return false;
		}

		g_fire = reinterpret_cast<volatile const std::int8_t*>(fire);
		g_fireAuto = reinterpret_cast<volatile const std::int8_t*>(fireAuto);
		g_qualified.store(true, std::memory_order_release);
		return true;
	}

	bool IsAvailable() noexcept
	{
		return g_qualified.load(std::memory_order_acquire);
	}

	bool IsHeld() noexcept
	{
		if (!g_qualified.load(std::memory_order_acquire))
		{
			return false;
		}
		bool fire = false;
		bool fireAuto = false;
		if (!ReadByteSeh(g_fire, fire) || !ReadByteSeh(g_fireAuto, fireAuto))
		{
			return false;
		}
		return fire || fireAuto;
	}
}
