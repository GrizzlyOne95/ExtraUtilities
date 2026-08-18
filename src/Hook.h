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

#include "BasicPatch.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace ExtraUtilities
{
	class Hook : public BasicPatch
	{
	private:
		const void* m_function;
		const void** p_function = &m_function; // address embedded into FF 15 hook instruction

		// x86 shellcode
		static constexpr uint8_t CALL[] = { 0xFF, 0x15 }; // call near absolute indirect

		bool ValidateHook() const
		{
			if (!CanPatch())
			{
				return false;
			}

			if (m_function == nullptr)
			{
				LogPatchIssue("refusing to install null hook target", m_address, m_length);
				return false;
			}

			if (m_length < 6)
			{
				LogPatchIssue("refusing to install undersized hook", m_address, m_length);
				return false;
			}
			return true;
		}

		void DoPatch() override
		{
			if (!ValidateHook() || !ValidatePreimage())
			{
				return;
			}

			uint8_t* p_address = reinterpret_cast<uint8_t*>(m_address);
			DWORD previousProtect{};

			if (!VirtualProtect(p_address, m_length, PAGE_EXECUTE_READWRITE, &previousProtect))
			{
				LogPatchIssue("failed to change hook protections", m_address, m_length);
				return;
			}

			std::memset(p_address, NOP, m_length);
			std::memcpy(p_address, CALL, sizeof(CALL));

			// FF 15 on x86 dereferences a static pointer-to-function address. Because
			// that pointer lives inside this Hook object, the object itself must never
			// move after installation.
			std::memcpy(p_address + sizeof(CALL), &p_function, sizeof(uintptr_t));
			FlushPatchedRange();

			if (!VirtualProtect(p_address, m_length, previousProtect, &dummyProtect))
			{
				LogPatchIssue("failed to restore hook memory protection", m_address, m_length);
			}

			m_status = Status::ACTIVE;
		}

	public:
		Hook(
			uintptr_t address,
			const void* function,
			size_t length,
			Status status,
			std::vector<uint8_t> expectedBytes = {})
			: BasicPatch(address, length, status, std::move(expectedBytes)), m_function(function)
		{
			if (!ValidateHook())
			{
				return;
			}

			if (m_status == Status::ACTIVE)
			{
				DoPatch();
			}
		}

		Hook(const Hook&) = delete;
		Hook& operator=(const Hook&) = delete;
		Hook(Hook&&) = delete;
		Hook& operator=(Hook&&) = delete;

		~Hook() = default;
	};
}
