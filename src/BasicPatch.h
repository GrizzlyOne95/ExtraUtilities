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

#include "Util/SignatureResolver.h"

#include <Windows.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <utility>
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
			char buffer[192]{};
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

		bool ValidatePreimage() const noexcept
		{
			if (!m_initialized || m_originalBytes.size() != m_length)
			{
				return false;
			}

			if (!SignatureResolver::MatchBytes(m_address, m_originalBytes))
			{
				LogPatchIssue("refusing to patch because the original bytes no longer match", m_address, m_length);
				return false;
			}

			return true;
		}

		void FlushPatchedRange() const noexcept
		{
			if (m_address == 0 || m_length == 0)
			{
				return;
			}

			if (!FlushInstructionCache(
				GetCurrentProcess(),
				reinterpret_cast<const void*>(m_address),
				m_length))
			{
				LogPatchIssue("failed to flush instruction cache", m_address, m_length);
			}
		}

		virtual void DoPatch() = 0;

		void RestorePatch()
		{
			if (!m_initialized || m_originalBytes.size() != m_length)
			{
				return;
			}

			uint8_t* p_address = reinterpret_cast<uint8_t*>(m_address);
			DWORD previousProtect{};

			if (!VirtualProtect(p_address, m_length, PAGE_EXECUTE_READWRITE, &previousProtect))
			{
				LogPatchIssue("failed to restore patch protections", m_address, m_length);
				return;
			}

			std::memcpy(p_address, m_originalBytes.data(), m_length);
			FlushPatchedRange();

			if (!VirtualProtect(p_address, m_length, previousProtect, &dummyProtect))
			{
				LogPatchIssue("failed to restore original memory protection", m_address, m_length);
			}

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

		BasicPatch(
			uintptr_t address,
			size_t length,
			Status status,
			std::vector<uint8_t> expectedBytes = {})
			: m_status(patchActivationEnabled ? status : Status::INACTIVE)
			, m_requestedStatus(status)
			, m_address(address)
			, m_length(length)
		{
			if (!SignatureResolver::IsReadableRange(reinterpret_cast<const void*>(m_address), m_length))
			{
				LogPatchIssue("refusing to patch invalid memory", m_address, m_length);
				m_status = Status::INACTIVE;
				return;
			}

			if (!expectedBytes.empty())
			{
				if (expectedBytes.size() != m_length)
				{
					LogPatchIssue("expected-byte preimage length does not match patch length", m_address, m_length);
					m_status = Status::INACTIVE;
					return;
				}

				if (!SignatureResolver::MatchBytes(m_address, expectedBytes))
				{
					LogPatchIssue("refusing to patch because expected bytes do not match", m_address, m_length);
					m_status = Status::INACTIVE;
					return;
				}
			}

			const auto* p_address = reinterpret_cast<const uint8_t*>(m_address);
			m_originalBytes.assign(p_address, p_address + m_length);
			m_initialized = true;
			RegisterDeferredPatch(this);
		}

		BasicPatch(const BasicPatch&) = delete; // Patch should not be initialized twice
		BasicPatch& operator=(const BasicPatch&) = delete;

		BasicPatch(BasicPatch&& p) noexcept
		{
			this->m_status = p.m_status;
			this->m_requestedStatus = p.m_requestedStatus;
			this->m_initialized = p.m_initialized;
			this->m_address = p.m_address;
			this->m_length = p.m_length;
			this->m_oldProtect = p.m_oldProtect;
			this->m_originalBytes = std::move(p.m_originalBytes);
			ReplaceDeferredPatch(&p, this);

			p.m_status = Status::INACTIVE;
			p.m_requestedStatus = Status::INACTIVE;
			p.m_initialized = false;
			p.m_address = 0;
			p.m_length = 0;
			p.m_oldProtect = 0;
		}

		BasicPatch& operator=(BasicPatch&&) = delete;

		virtual ~BasicPatch()
		{
			if (m_initialized && m_status == Status::ACTIVE)
			{
				RestorePatch();
			}

			UnregisterDeferredPatch(this);
		}

		bool IsActive() const noexcept
		{
			return m_status == Status::ACTIVE;
		}

		void Reload()
		{
			if (!patchActivationEnabled || !m_initialized || m_status != Status::INACTIVE)
			{
				return;
			}

			if (!ValidatePreimage())
			{
				return;
			}

			DoPatch();
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
			m_requestedStatus = s;
			if (s == Status::ACTIVE)
			{
				if (patchActivationEnabled)
				{
					Reload();
				}
				return;
			}

			Unload();
		}

		void SetStatus(bool status)
		{
			SetStatus(status ? Status::ACTIVE : Status::INACTIVE);
		}
	};

	// Temporarily disables a patch and restores its prior active state on normal
	// C++ scope exit. Do not span a Lua API call that can longjmp; use lua_pcall
	// and restore the patch before propagating the Lua error instead.
	class ScopedPatchDisable
	{
	private:
		BasicPatch* m_patch = nullptr;
		bool m_restore = false;

	public:
		explicit ScopedPatchDisable(BasicPatch& patch)
			: m_patch(&patch), m_restore(patch.IsActive())
		{
			if (m_restore)
			{
				m_patch->Unload();
			}
		}

		ScopedPatchDisable(const ScopedPatchDisable&) = delete;
		ScopedPatchDisable& operator=(const ScopedPatchDisable&) = delete;

		~ScopedPatchDisable()
		{
			if (m_restore && m_patch != nullptr)
			{
				m_patch->Reload();
			}
		}
	};
}
