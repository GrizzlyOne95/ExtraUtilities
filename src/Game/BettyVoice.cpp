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

#include "Game/BettyVoice.h"

#include "EntryDetour32.h"
#include "LuaHelpers.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <lua.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

namespace ExtraUtilities::BettyVoice
{
	namespace
	{
		namespace Addr = EngineAddresses::BettyVoice;

		// BettyVoice::AmmoWarning(this = BettyVoice*, GameObject*), ret 4.
		using AmmoWarningFn = void(__thiscall*)(void* self, void* obj);

		// push ebp; mov ebp,esp; push ecx; push esi; mov [ebp-4],ecx. All
		// whole, position-independent instructions. The call at +8 (E8 rel32)
		// is not stolen but is part of the identity check.
		constexpr std::array<std::uint8_t, 8> kStolenEntry = { 0x55, 0x8B, 0xEC, 0x51, 0x56, 0x89, 0x4D, 0xFC };
		constexpr std::uint8_t kCallOpcode = 0xE8;

		bool g_warningEnabled = true; // false: the user's line is dropped
		bool g_entryChecked = false;
		bool g_entryQualified = false;
		EntryDetour32* g_detour = nullptr;
		AmmoWarningFn g_original = nullptr;

		bool ReadUserObjectSeh(void*& out) noexcept
		{
			out = nullptr;
			__try
			{
				out = *reinterpret_cast<void* const*>(BZR::GameObject::p_userObject);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out = nullptr;
				return false;
			}
		}

		bool EntryMatchesSeh() noexcept
		{
			__try
			{
				const auto* code = reinterpret_cast<const std::uint8_t*>(Addr::AmmoWarning);
				for (std::size_t i = 0; i < kStolenEntry.size(); ++i)
				{
					if (code[i] != kStolenEntry[i])
						return false;
				}
				return code[kStolenEntry.size()] == kCallOpcode;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Checks the entry bytes once per session; logs once on failure.
		bool QualifyEntry() noexcept
		{
			if (!g_entryChecked)
			{
				g_entryChecked = true;
				g_entryQualified = RuntimeGate::IsSupported() && EntryMatchesSeh();
				if (!g_entryQualified)
					Logging::LogMessage("exu: betty ammo warning unavailable; BettyVoice::AmmoWarning did not qualify");
			}
			return g_entryQualified;
		}

		// __thiscall with one stack argument (ret 4): an x86 __fastcall wrapper
		// takes ECX/EDX as the first two parameters and leaves obj in the same
		// slot with the same callee pop.
		void __fastcall AmmoWarningHook(void* self, void* /*edx*/, void* obj)
		{
			const AmmoWarningFn original = g_original;
			if (original == nullptr)
				return;
			void* user = nullptr;
			if (!g_warningEnabled && obj != nullptr && ReadUserObjectSeh(user) && obj == user)
				return;
			original(self, obj);
		}

		void RemoveDetour() noexcept
		{
			// Restores the engine entry before the trampoline is freed.
			delete g_detour;
			g_detour = nullptr;
			g_original = nullptr;
		}

		bool EnsureDetour() noexcept
		{
			try
			{
				if (g_detour != nullptr && g_detour->IsActive())
					return true;
				RemoveDetour();
				if (!QualifyEntry())
					return false;

				g_detour = new (std::nothrow) EntryDetour32(
					Addr::AmmoWarning, reinterpret_cast<const void*>(&AmmoWarningHook), kStolenEntry.size(),
					BasicPatch::Status::INACTIVE,
					std::vector<std::uint8_t>(kStolenEntry.begin(), kStolenEntry.end()));
				if (g_detour == nullptr || !g_detour->PrepareTrampoline())
				{
					Logging::LogMessage("exu: betty ammo warning unavailable; detour could not be prepared");
					RemoveDetour();
					return false;
				}
				g_original = reinterpret_cast<AmmoWarningFn>(g_detour->GetTrampoline());
				g_detour->SetStatus(true);
				if (g_original == nullptr || !g_detour->IsActive())
				{
					Logging::LogMessage("exu: betty ammo warning detour could not activate");
					RemoveDetour();
					return false;
				}
				Logging::LogMessage("exu: betty ammo warning detour active");
				return true;
			}
			catch (...)
			{
				RemoveDetour();
				return false;
			}
		}

		// Calls the stock function (the trampoline when hooked) for the user object.
		bool PlayStockSeh() noexcept
		{
			__try
			{
				void* user = *reinterpret_cast<void* const*>(BZR::GameObject::p_userObject);
				if (user == nullptr)
					return false;
				const AmmoWarningFn fn = g_original != nullptr
					? g_original
					: reinterpret_cast<AmmoWarningFn>(Addr::AmmoWarning);
				fn(reinterpret_cast<void*>(Addr::bettyVoice), user);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}
	}

	void Shutdown() noexcept
	{
		g_warningEnabled = true;
		RemoveDetour();
		g_entryChecked = false;
		g_entryQualified = false;
	}

	// exu.SetUserAmmoWarning(enabled) -> boolean ok
	int SetUserAmmoWarning(lua_State* L)
	{
		const bool enabled = Lua::CheckBool(L, 1);
		bool ok = true;
		if (!enabled)
		{
			ok = EnsureDetour();
			if (ok)
				g_warningEnabled = false;
		}
		else
		{
			g_warningEnabled = true;
		}
		lua_pushboolean(L, ok ? 1 : 0);
		return 1;
	}

	// exu.GetUserAmmoWarning() -> boolean
	int GetUserAmmoWarning(lua_State* L)
	{
		lua_pushboolean(L, g_warningEnabled ? 1 : 0);
		return 1;
	}

	// exu.PlayUserAmmoWarning() -> boolean played
	int PlayUserAmmoWarning(lua_State* L)
	{
		const bool played = QualifyEntry() && PlayStockSeh();
		lua_pushboolean(L, played ? 1 : 0);
		return 1;
	}
}
