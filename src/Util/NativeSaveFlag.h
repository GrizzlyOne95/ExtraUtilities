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

	// The stock slot-save wrapper (SaveShellGame, FUN_004FDC80) selects binary
	// output right before it calls SaveGame:
	//
	//   C6 05 <missionSave> 00        mov byte ptr [missionSave], 0
	//   83 3D <binarySwitch> 00       cmp dword ptr [binarySwitch], 0
	//   74 09                         je  +9
	//   C7 45 xx 01 00 00 00          mov dword ptr [ebp+xx], 1
	//   EB 07                         jmp +7
	//   C7 45 xx 00 00 00 00          mov dword ptr [ebp+xx], 0
	//   8A xx xx                      mov r8, byte ptr [ebp+xx]
	//   88 xx <binarySave>            mov byte ptr [binarySave], r8
	//
	// binarySwitch is the -binarysave command-line option and binarySave is the
	// byte SaveGame reads to pick the writer. Direct callers of SaveGame must set
	// it the same way or every save is text regardless of the option.
	inline constexpr std::size_t BINARY_SAVE_SEQUENCE_SIZE = 41;

	struct BinarySaveAddresses
	{
		uint32_t commandLineSwitch = 0;
		uint32_t saveFlag = 0;
	};

	// Searches `size` bytes of the wrapper for the sequence above, anchored on
	// the already-decoded missionSave address. Returns false, leaving `out`
	// zeroed, when the sequence is absent or its operands are implausible.
	inline bool FindBinarySaveAddresses(
		const uint8_t* wrapper,
		std::size_t size,
		uint32_t missionSave,
		BinarySaveAddresses& out) noexcept
	{
		out = {};
		if (wrapper == nullptr || missionSave == 0 || size < BINARY_SAVE_SEQUENCE_SIZE)
		{
			return false;
		}

		const auto readU32 = [](const uint8_t* at) noexcept
		{
			uint32_t value = 0;
			std::memcpy(&value, at, sizeof(value));
			return value;
		};

		for (std::size_t offset = 0; offset + BINARY_SAVE_SEQUENCE_SIZE <= size; ++offset)
		{
			const uint8_t* at = wrapper + offset;
			if (at[0] != 0xC6 || at[1] != 0x05 || readU32(at + 2) != missionSave || at[6] != 0x00)
			{
				continue;
			}

			const bool shape =
				at[7] == 0x83 && at[8] == 0x3D && at[13] == 0x00 &&
				at[14] == 0x74 && at[15] == 0x09 &&
				at[16] == 0xC7 && at[17] == 0x45 && readU32(at + 19) == 1 &&
				at[23] == 0xEB && at[24] == 0x07 &&
				at[25] == 0xC7 && at[26] == 0x45 && readU32(at + 28) == 0 &&
				// mov r8, byte ptr [ebp+disp8]: mod=01, r/m=101.
				at[32] == 0x8A && (at[33] & 0xC7) == 0x45 &&
				// mov byte ptr [disp32], r8: mod=00, r/m=101.
				at[35] == 0x88 && (at[36] & 0xC7) == 0x05 &&
				// The register stored must be the one just loaded.
				((at[36] >> 3) & 7) == ((at[33] >> 3) & 7);
			if (!shape)
			{
				return false;
			}

			const uint32_t commandLineSwitch = readU32(at + 9);
			const uint32_t saveFlag = readU32(at + 37);
			// binarySave is the bool immediately before missionSave in the
			// same save-state block. Anything else means the decode drifted.
			if (commandLineSwitch == 0 || saveFlag != missionSave - 1)
			{
				return false;
			}

			out.commandLineSwitch = commandLineSwitch;
			out.saveFlag = saveFlag;
			return true;
		}

		return false;
	}
}
