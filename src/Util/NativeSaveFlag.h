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

#include <cstddef>
#include <cstdint>
#include <cstring>

// Byte-level decoding of Redux's missionSave global out of the already-qualified
// SaveGame prolog. Kept free of Windows and Ogre so tests/host can exercise the
// drift-rejection cases against synthetic buffers.
namespace ExtraUtilities::NativeSave
{
	// The qualified SaveGame signature pins `movzx eax, byte ptr [missionSave]`
	// at entry +37, with the four-byte absolute operand immediately after the
	// 0F B6 05 opcode. The offset is not a guess: bytes 37..39 are literals in
	// the signature itself, so a match already proves the opcode is there.
	inline constexpr std::size_t MISSION_SAVE_OPCODE_OFFSET = 37;
	inline constexpr std::size_t MISSION_SAVE_OPERAND_OFFSET = MISSION_SAVE_OPCODE_OFFSET + 3;
	inline constexpr std::size_t MISSION_SAVE_PROBE_SIZE = MISSION_SAVE_OPERAND_OFFSET + sizeof(uint32_t);

	// Returns the absolute address of the missionSave byte, or 0 when the prolog
	// does not match. Zero always means "fail closed", never a usable address.
	inline uint32_t ReadMissionSaveFlagAddress(const uint8_t* entry) noexcept
	{
		if (entry == nullptr)
		{
			return 0;
		}

		if (entry[MISSION_SAVE_OPCODE_OFFSET] != 0x0F ||
			entry[MISSION_SAVE_OPCODE_OFFSET + 1] != 0xB6 ||
			entry[MISSION_SAVE_OPCODE_OFFSET + 2] != 0x05)
		{
			return 0;
		}

		uint32_t address = 0;
		std::memcpy(&address, entry + MISSION_SAVE_OPERAND_OFFSET, sizeof(address));
		return address;
	}

	// missionSave is a bool byte; the engine only ever stores 0 or 1. Anything
	// else means the operand did not land on the flag.
	inline bool IsPlausibleMissionSaveValue(uint8_t value) noexcept
	{
		return value <= 1;
	}
}
