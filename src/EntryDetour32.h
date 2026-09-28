/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

#include "BasicPatch.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace ExtraUtilities
{
	// x86 function-entry detour with a reloc-free stolen-byte trampoline.
	// Entry and trampoline return use absolute "push imm32; ret" transfers so
	// large-address-aware 32-bit layouts do not depend on rel32 reachability.
	//
	// The caller is responsible for choosing a detour length that ends on an
	// instruction boundary and contains no relative/control-transfer instruction
	// whose displacement would need relocation in the trampoline. Construction
	// requires a verified stock preimage; the detour otherwise uses BasicPatch's
	// build gate, foreign-overwrite-safe restore, and Lua-state teardown.
	class EntryDetour32 final : public BasicPatch
	{
	private:
		const void* m_hook = nullptr;
		void* m_trampoline = nullptr;

		bool EnsureTrampoline() noexcept
		{
#if !defined(_M_IX86)
			LogPatchIssue("x86 entry detour requested on a non-x86 build", m_address, m_length);
			return false;
#else
			if (m_trampoline != nullptr)
			{
				return true;
			}
			if (!CanPatch() || m_length < 6 || m_originalBytes.size() != m_length)
			{
				return false;
			}

			const std::size_t trampolineSize = m_length + 6;
			auto* trampoline = static_cast<std::uint8_t*>(
				VirtualAlloc(nullptr, trampolineSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (trampoline == nullptr)
			{
				LogPatchIssue("failed to allocate entry-detour trampoline", m_address, m_length);
				return false;
			}

			std::memcpy(trampoline, m_originalBytes.data(), m_length);

			// Absolute transfer: push resumeAddress; ret. Net stack delta is zero,
			// registers/EFLAGS are preserved, and unlike E9 this works even when a
			// large-address-aware process places the trampoline over 2 GiB away.
			const std::uint32_t resumeAddress =
				static_cast<std::uint32_t>(m_address + m_length);
			trampoline[m_length] = 0x68;
			std::memcpy(trampoline + m_length + 1, &resumeAddress, sizeof(resumeAddress));
			trampoline[m_length + 5] = 0xC3;

			DWORD previousProtect = 0;
			if (!VirtualProtect(trampoline, trampolineSize, PAGE_EXECUTE_READ, &previousProtect))
			{
				VirtualFree(trampoline, 0, MEM_RELEASE);
				LogPatchIssue("failed to make entry-detour trampoline executable", m_address, m_length);
				return false;
			}
			FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineSize);

			m_trampoline = trampoline;
			return true;
#endif
		}

		void DoPatch() override
		{
			if (!CanPatch() || !ValidatePreimage() || m_hook == nullptr || m_length < 6)
			{
				return;
			}
			if (!EnsureTrampoline())
			{
				return;
			}

			auto* target = reinterpret_cast<std::uint8_t*>(m_address);
			DWORD previousProtect = 0;
			if (!VirtualProtect(target, m_length, PAGE_EXECUTE_READWRITE, &previousProtect))
			{
				LogPatchIssue("failed to change entry-detour protections", m_address, m_length);
				return;
			}

			std::memset(target, NOP, m_length);
			const std::uint32_t hookAddress =
				static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(m_hook));
			target[0] = 0x68;
			std::memcpy(target + 1, &hookAddress, sizeof(hookAddress));
			target[5] = 0xC3;
			FlushPatchedRange();

			if (!VirtualProtect(target, m_length, previousProtect, &dummyProtect))
			{
				LogPatchIssue("failed to restore entry-detour memory protection", m_address, m_length);
			}

			MarkPatched();
		}

	public:
		EntryDetour32(
			std::uintptr_t address,
			const void* hook,
			std::size_t length,
			Status status,
			std::vector<std::uint8_t> expectedBytes)
			: BasicPatch(address, length, status, expectedBytes),
			  m_hook(hook)
		{
			if (expectedBytes.empty() || expectedBytes.size() != m_length)
			{
				LogPatchIssue("entry detour requires a complete expected-byte preimage", m_address, m_length);
				m_status = Status::INACTIVE;
				m_requestedStatus = Status::INACTIVE;
				return;
			}
			if (m_hook == nullptr)
			{
				LogPatchIssue("refusing to install null entry-detour hook", m_address, m_length);
				m_status = Status::INACTIVE;
				m_requestedStatus = Status::INACTIVE;
				return;
			}
			if (m_length < 6)
			{
				LogPatchIssue("refusing to install entry detour shorter than push/ret transfer", m_address, m_length);
				m_status = Status::INACTIVE;
				m_requestedStatus = Status::INACTIVE;
				return;
			}

			if (m_status == Status::ACTIVE)
			{
				DoPatch();
			}
		}

		EntryDetour32(const EntryDetour32&) = delete;
		EntryDetour32& operator=(const EntryDetour32&) = delete;
		EntryDetour32(EntryDetour32&&) = delete;
		EntryDetour32& operator=(EntryDetour32&&) = delete;

		~EntryDetour32() override
		{
			// Restore the entry before freeing the trampoline. The base
			// destructor will see the now-inactive patch and only unregister it.
			Unload();
			if (m_trampoline != nullptr)
			{
				VirtualFree(m_trampoline, 0, MEM_RELEASE);
				m_trampoline = nullptr;
			}
		}

		bool PrepareTrampoline() noexcept
		{
			return EnsureTrampoline();
		}

		void* GetTrampoline() const noexcept
		{
			return m_trampoline;
		}

		template <typename FunctionPointer>
		FunctionPointer GetTrampolineAs() const noexcept
		{
			static_assert(std::is_pointer_v<FunctionPointer>);
			static_assert(std::is_function_v<std::remove_pointer_t<FunctionPointer>>);
			return reinterpret_cast<FunctionPointer>(m_trampoline);
		}
	};
}
