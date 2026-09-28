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

#include "Game/Culling.h"
#include "GlobalTurbo.h"
#include "bzr.h"
#include "Hook.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"
#include "OpenShimBridge.h"
#include "TurboGate.h"
#include "TurboGateThunk.h"

#include <iterator>

namespace
{
	using OpenShimGetGlobalTurboFn = BOOL(WINAPI*)();
	using OpenShimSetGlobalTurboFn = BOOL(WINAPI*)(BOOL);
	using OpenShimGetUnitTurboFn = BOOL(WINAPI*)(DWORD);
	using OpenShimSetUnitTurboFn = BOOL(WINAPI*)(DWORD, BOOL);

	bool QueryOpenShimUnitTurboOwnership()
	{
		// Export presence is the ownership contract. Do not activate EXU's fallback
		// hooks merely because the shim is still waiting for Steam's runtime bytes
		// to settle; doing so would create a late-install race over the same sites.
		return ExtraUtilities::OpenShimBridge::HasExport("OpenShimHasUnitTurboHooks");
	}

	// OpenShim ships the global and per-unit turbo exports together, and when
	// it owns them it patches the same operand and gate sites. Ownership is
	// therefore all or nothing: either OpenShim does global and per-unit turbo,
	// or EXU's data gate below does both.
	const bool g_openShimOwnsUnitTurbo = QueryOpenShimUnitTurboOwnership();
}

namespace ExtraUtilities::Patch
{
	namespace
	{
		// The floats the two patched operands read. The engine only reads them
		// through the patched instructions, so they are volatile: every store
		// the callback makes must reach memory before the thunk returns. They
		// start at the stock values, so a data gate whose begin hook never
		// installed still compares exactly like stock code.
		volatile float g_turboToleranceOperand = TurboGate::kStockTolerance;
		volatile float g_turboGateLimitOperand = TurboGate::kStockGateLimit;
	}

	// The data gate: three patches written once, at activation, through the
	// patch engine (preimage checks, build gate, restore at unload). After
	// that nothing is rewritten per tick; the begin hook only stores two
	// floats. Registration order is the order below, so the operands are
	// redirected (to floats holding stock values) before the hook that
	// changes the floats is installed.
	static_assert(sizeof(uintptr_t) == 4, "the operands are x86 disp32 fields");
	InlinePatch turboToleranceOperandPatch(
		turboToleranceOperandAddr,
		reinterpret_cast<uintptr_t>(&g_turboToleranceOperand),
		g_openShimOwnsUnitTurbo ? InlinePatch::Status::INACTIVE : InlinePatch::Status::ACTIVE,
		{ 0x04, 0x26, 0x8A, 0x00 });
	InlinePatch turboGateOperandPatch(
		turboGateOperandAddr,
		reinterpret_cast<uintptr_t>(&g_turboGateLimitOperand),
		g_openShimOwnsUnitTurbo ? InlinePatch::Status::INACTIVE : InlinePatch::Status::ACTIVE,
		{ 0xC8, 0x25, 0x8A, 0x00 });

	namespace
	{
		bool OperandsRedirected() noexcept
		{
			return turboToleranceOperandPatch.IsActive() && turboGateOperandPatch.IsActive();
		}

		void PruneDeadTurboOverrides() noexcept
		{
			for (auto it = setTurboUnits.begin(); it != setTurboUnits.end();)
			{
				it = BZR::GameObject::GetObj(it->first) == nullptr ? setTurboUnits.erase(it) : std::next(it);
			}
		}
	}

	namespace TurboGate
	{
		// Runs for every unit, on the simulation thread, with an empty x87
		// stack (see TurboGateThunk.h). No Lua, no allocation on the turbo
		// path, no code writes.
		void __cdecl OnTurboDecisionBegin(void* gameObject) noexcept
		{
			auto* obj = static_cast<BZR::GameObject*>(gameObject);
			try
			{
				Culling::UpdateUnit(obj);
			}
			catch (...)
			{
			}

			bool forced = globalTurboEnabled;
			if (!setTurboUnits.empty())
			{
				const auto it = setTurboUnits.find(BZR::GameObject::GetHandle(obj));
				forced = IsForced(globalTurboEnabled, it != setTurboUnits.end(), it != setTurboUnits.end() && it->second);
			}

			// With one operand redirected and the other not, a forced unit would
			// get half of the turbo change. Fall back to stock instead.
			const Operands operands = SelectOperands(forced && OperandsRedirected());
			g_turboToleranceOperand = operands.tolerance;
			g_turboGateLimitOperand = operands.gateLimit;
		}
	}

	Hook turboPatchBegin(
		turboPatchBeginAddr,
		&TurboGate::TurboDecisionBeginThunk,
		6,
		g_openShimOwnsUnitTurbo ? InlinePatch::Status::INACTIVE : InlinePatch::Status::ACTIVE,
		{ 0x8B, 0x45, 0x90, 0xD9, 0x58, 0x08 });

	namespace
	{
		bool DataGateInstalled() noexcept
		{
			return turboPatchBegin.IsActive() && OperandsRedirected();
		}
	}

	bool SetUnitTurboOverride(BZR::handle h, bool status)
	{
		if (g_openShimOwnsUnitTurbo)
		{
			const auto fn = OpenShimBridge::Resolve<OpenShimSetUnitTurboFn>("OpenShimSetUnitTurbo");
			return fn && fn(static_cast<DWORD>(h), status ? TRUE : FALSE) != FALSE;
		}

		// Handles are only unique among live objects; drop overrides for dead
		// ones so the map stays bounded and a reused slot starts clean.
		PruneDeadTurboOverrides();
		if (BZR::GameObject::GetObj(h) == nullptr)
		{
			return false;
		}

		setTurboUnits[h] = status;
		return !status || DataGateInstalled();
	}

	bool GetUnitTurboOverride(BZR::handle h)
	{
		if (g_openShimOwnsUnitTurbo)
		{
			const auto fn = OpenShimBridge::Resolve<OpenShimGetUnitTurboFn>("OpenShimGetUnitTurbo");
			return fn && fn(static_cast<DWORD>(h)) != FALSE;
		}

		const auto it = setTurboUnits.find(h);
		return it != setTurboUnits.end() && it->second && BZR::GameObject::GetObj(h) != nullptr;
	}
}

namespace ExtraUtilities::Lua::Patches
{
	int GetGlobalTurbo(lua_State* L)
	{
		if (g_openShimOwnsUnitTurbo)
		{
			const auto fn = OpenShimBridge::Resolve<OpenShimGetGlobalTurboFn>("OpenShimGetGlobalTurbo");
			lua_pushboolean(L, fn && fn() != FALSE);
			return 1;
		}

		lua_pushboolean(L, Patch::globalTurboEnabled && Patch::DataGateInstalled());
		return 1;
	}

	int SetGlobalTurbo(lua_State* L)
	{
		bool status = CheckBool(L, 1);
		Patch::globalTurboEnabled = status;
		if (g_openShimOwnsUnitTurbo)
		{
			const auto fn = OpenShimBridge::Resolve<OpenShimSetGlobalTurboFn>("OpenShimSetGlobalTurbo");
			lua_pushboolean(L, fn && fn(status ? TRUE : FALSE) != FALSE);
			return 1;
		}

		// The begin hook reads the flag on the next decision; nothing to patch.
		lua_pushboolean(L, !status || Patch::DataGateInstalled());
		return 1;
	}

	int GetUnitTurbo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		lua_pushboolean(L, Patch::GetUnitTurboOverride(h));
		return 1;
	}

	int SetUnitTurbo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		bool status = CheckBool(L, 2);
		lua_pushboolean(L, Patch::SetUnitTurboOverride(h, status));
		return 1;
	}
}
