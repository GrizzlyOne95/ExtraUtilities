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

#include <cstring>
#include <utility>
#include <vector>

namespace ExtraUtilities
{
	class InlinePatch : public BasicPatch
	{
	private:
		std::vector<uint8_t> m_payload;

		void DoPatch() override
		{
			if (!CanPatch() || m_payload.size() != m_length || !ValidatePreimage())
			{
				return;
			}

			uint8_t* p_address = reinterpret_cast<uint8_t*>(m_address);
			DWORD previousProtect{};

			if (!VirtualProtect(p_address, m_length, PAGE_EXECUTE_READWRITE, &previousProtect))
			{
				LogPatchIssue("failed to change inline patch protections", m_address, m_length);
				return;
			}

			std::memcpy(p_address, m_payload.data(), m_length);
			FlushPatchedRange();

			if (!VirtualProtect(p_address, m_length, previousProtect, &dummyProtect))
			{
				LogPatchIssue("failed to restore inline patch memory protection", m_address, m_length);
			}

			m_status = Status::ACTIVE;
		}

	public:
		// Inline patch from buffer
		InlinePatch(
			uintptr_t address,
			const void* payload,
			size_t length,
			Status status,
			std::vector<uint8_t> expectedBytes = {})
			: BasicPatch(address, length, status, std::move(expectedBytes)), m_payload(length)
		{
			if (payload == nullptr && length != 0)
			{
				LogPatchIssue("refusing to install null inline patch payload", m_address, m_length);
				m_status = Status::INACTIVE;
				m_requestedStatus = Status::INACTIVE;
				return;
			}

			if (length != 0)
			{
				std::memcpy(m_payload.data(), payload, length);
			}

			if (m_status == Status::ACTIVE)
			{
				DoPatch();
			}
		}

		// Shellcode inline patch
		InlinePatch(
			uintptr_t address,
			std::vector<uint8_t> payload,
			Status status,
			std::vector<uint8_t> expectedBytes = {})
			: BasicPatch(address, payload.size(), status, std::move(expectedBytes)), m_payload(std::move(payload))
		{
			if (m_status == Status::ACTIVE)
			{
				DoPatch();
			}
		}

		// Single byte inline patch
		InlinePatch(
			uintptr_t address,
			uint8_t value,
			size_t length,
			Status status,
			std::vector<uint8_t> expectedBytes = {})
			: BasicPatch(address, length, status, std::move(expectedBytes)), m_payload(length, value)
		{
			if (m_status == Status::ACTIVE)
			{
				DoPatch();
			}
		}

		// Multi byte/pointer inline patch
		template <typename T>
		InlinePatch(
			uintptr_t address,
			T value,
			Status status,
			std::vector<uint8_t> expectedBytes = {})
			: BasicPatch(address, sizeof(T), status, std::move(expectedBytes)),
			  m_payload(reinterpret_cast<uint8_t*>(&value), reinterpret_cast<uint8_t*>(&value) + sizeof(T))
		{
			if (m_status == Status::ACTIVE)
			{
				DoPatch();
			}
		}

		InlinePatch(const InlinePatch&) = delete;
		InlinePatch& operator=(const InlinePatch&) = delete;

		InlinePatch(InlinePatch&& p) noexcept
			: BasicPatch(std::move(p)), m_payload(std::move(p.m_payload))
		{
		}

		InlinePatch& operator=(InlinePatch&&) = delete;

		~InlinePatch() = default;
	};
}
