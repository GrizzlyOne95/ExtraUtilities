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

#include <Windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ExtraUtilities
{
	class BasicPatch
	{
	public:
		enum class Status : uint8_t
		{
			ACTIVE, // patched code is running
			INACTIVE // game code is running
		};

		// x86 shellcode
		static constexpr uint8_t NOP = 0x90; // no operation
		static constexpr uint8_t RET = 0xC3; // return

	protected:
		Status m_status;
		Status m_requestedStatus;
		bool m_initialized = false;

		uintptr_t m_address;
		size_t m_length;

		DWORD m_oldProtect{};
		static inline DWORD dummyProtect{};
		static inline bool patchActivationEnabled = false;
		static inline std::vector<BasicPatch*> deferredPatches{};
		std::vector<uint8_t> m_originalBytes;

		static void RegisterDeferredPatch(BasicPatch* patch)
		{
			if (patch == nullptr)
			{
				return;
			}

			deferredPatches.push_back(patch);
		}

		static void ReplaceDeferredPatch(BasicPatch* oldPatch, BasicPatch* newPatch)
		{
			if (oldPatch == nullptr || newPatch == nullptr)
			{
				return;
			}

			for (BasicPatch*& patch : deferredPatches)
			{
				if (patch == oldPatch)
				{
					patch = newPatch;
					return;
				}
			}

			RegisterDeferredPatch(newPatch);
		}

		static void UnregisterDeferredPatch(BasicPatch* patch) noexcept
		{
			if (patch == nullptr)
			{
				return;
			}

			for (size_t i = 0; i < deferredPatches.size();)
			{
				if (deferredPatches[i] == patch)
				{
					deferredPatches.erase(deferredPatches.begin() + i);
					continue;
				}

				++i;
			}
		}

		static void LogPatchIssue(const char* message, uintptr_t address, size_t length) noexcept
		{
			char buffer[160]{};
			std::snprintf(
				buffer,
				sizeof(buffer),
				"ExtraUtilities: %s at %p (len=%zu)\n",
				message,
				reinterpret_cast<void*>(address),
				length
			);
			OutputDebugStringA(buffer);
		}

		static bool IsAccessibleRange(uintptr_t address, size_t length) noexcept
		{
			if (address == 0 || length == 0)
			{
				return false;
			}

			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0)
			{
				return false;
			}

			if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0 || mbi.Protect == PAGE_NOACCESS)
			{
				return false;
			}

			auto regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
			if (address < regionBase)
			{
				return false;
			}

			auto offset = address - regionBase;
			return offset <= mbi.RegionSize && length <= (mbi.RegionSize - offset);
		}

		virtual void DoPatch() = 0;

		void RestorePatch()
		{
			if (!m_initialized || m_originalBytes.size() != m_length)
			{
				return;
			}

			uint8_t* p_address = reinterpret_cast<uint8_t*>(m_address);

			if (!VirtualProtect(p_address, m_length, PAGE_EXECUTE_READWRITE, &m_oldProtect))
			{
				LogPatchIssue("failed to restore patch protections", m_address, m_length);
				return;
			}

			std::memcpy(p_address, m_originalBytes.data(), m_length);

			VirtualProtect(p_address, m_length, m_oldProtect, &dummyProtect);

			m_status = Status::INACTIVE;
		}

		bool CanPatch() const noexcept
		{
			return m_initialized;
		}

	public:
		static void EnableDeferredPatchActivation()
		{
			patchActivationEnabled = true;
			for (BasicPatch* patch : deferredPatches)
			{
				if (patch && patch->m_requestedStatus == Status::ACTIVE)
				{
					patch->Reload();
				}
			}
		}

		static void UnloadAllPatches() noexcept
		{
			patchActivationEnabled = false;
			for (auto it = deferredPatches.rbegin(); it != deferredPatches.rend(); ++it)
			{
				BasicPatch* patch = *it;
				if (patch != nullptr)
				{
					patch->Unload();
				}
			}
		}

		BasicPatch(uintptr_t address, size_t length, Status status)
			: m_status(patchActivationEnabled ? status : Status::INACTIVE)
			, m_requestedStatus(status)
			, m_address(address)
			, m_length(length)
		{
			if (!IsAccessibleRange(m_address, m_length))
			{
				LogPatchIssue("refusing to patch invalid memory", m_address, m_length);
				m_status = Status::INACTIVE;
				return;
			}

			uint8_t* p_address = reinterpret_cast<uint8_t*>(m_address);

			if (!VirtualProtect(p_address, m_length, PAGE_EXECUTE_READWRITE, &m_oldProtect))
			{
				LogPatchIssue("failed to change patch protections", m_address, m_length);
				m_status = Status::INACTIVE;
				return;
			}

			m_originalBytes.insert(m_originalBytes.end(), p_address, p_address + m_length);

			VirtualProtect(p_address, m_length, m_oldProtect, &dummyProtect);
			m_initialized = true;
			RegisterDeferredPatch(this);
		}

		BasicPatch(BasicPatch& p) = delete; // Patch should not be initialized twice

		BasicPatch(BasicPatch&& p) noexcept
		{
			this->m_status = p.m_status;
			this->m_requestedStatus = p.m_requestedStatus;
			this->m_initialized = p.m_initialized;
			this->m_address = p.m_address;
			this->m_length = p.m_length;
			this->m_oldProtect = p.m_oldProtect;
			this->dummyProtect = p.dummyProtect;
			this->m_originalBytes = std::move(p.m_originalBytes);
			ReplaceDeferredPatch(&p, this);

			p.m_status = Status::INACTIVE;
			p.m_requestedStatus = Status::INACTIVE;
			p.m_initialized = false;
			p.m_address = 0;
			p.m_length = 0;
			p.m_oldProtect = 0;
		}

		virtual ~BasicPatch()
		{
			if (m_initialized && m_status == Status::ACTIVE)
			{
				RestorePatch();
			}

			UnregisterDeferredPatch(this);
		}

		bool IsActive()
		{
			return m_status == Status::ACTIVE ? true : false;
		}

		void Reload()
		{
			if (m_initialized && m_status == Status::INACTIVE)
			{
				DoPatch();
			}
		}

		void Unload()
		{
			if (m_initialized && m_status == Status::ACTIVE)
			{
				RestorePatch();
			}
		}

		void SetStatus(Status s)
		{
			switch (s)
			{
			case Status::ACTIVE:
				Reload();
				break;

			case Status::INACTIVE:
				Unload();
				break;
			}
		}

		void SetStatus(bool status)
		{
			SetStatus(static_cast<Status>(!status));
		}
	};
}
