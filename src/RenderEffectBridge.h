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

/* Optional OpenShim render-effect bridge.
 *
 * EXU expresses mission-scoped intent. OpenShim decides whether a renderer
 * feature is supported and effective, and owns every D3D11/Ogre resource
 * needed to implement it. Nothing but integers and a POD status struct crosses
 * this boundary - no SRV, no D3D11 object, no Ogre handle, no shader name.
 *
 * Mirrors OpenShim's stable winmm ABI (BZROpenShim include/render_effect_intent.h)
 * without linking against any OpenShim header, exactly like RenderProfileBridge.
 * Every call fails closed, so an absent, older, or unsupported OpenShim can
 * never break a mission script.
 *
 * NOTE ON WHAT THIS DOES TODAY: no renderer effect is implemented yet on the
 * OpenShim side - scene depth is still being qualified - so every status query
 * currently answers supported = false. That is the designed steady state for
 * an optional feature, not a fault: a mission asks, is told no, and carries on.
 */
#pragma once

#include "Game/RenderEffectNames.h"
#include "OpenShimBridge.h"

#include <Windows.h>

#include <cstdint>

namespace ExtraUtilities::RenderEffectBridge
{
	// Layout-compatible with OpenShim's Abi::StatusV1. `size` is set by the
	// caller so a future, larger status struct stays distinguishable.
	struct Status
	{
		std::uint32_t size;
		std::uint32_t version;
		BOOL requested;
		BOOL supported;
		BOOL effective;
		std::uint32_t reasonCode;
	};

	static_assert(sizeof(Status) == 24, "Status must match OpenShim's StatusV1 layout");

	using GetApiVersionFn = UINT(WINAPI*)();
	using SetEnabledFn = DWORD(WINAPI*)(DWORD, BOOL);
	using SetFloatFn = DWORD(WINAPI*)(DWORD, DWORD, float);
	using GetStatusFn = BOOL(WINAPI*)(DWORD, Status*, DWORD);
	using ResetFn = BOOL(WINAPI*)();

	namespace Detail
	{
		struct Table
		{
			HMODULE module = nullptr;
			bool resolved = false;
			GetApiVersionFn getApiVersion = nullptr;
			SetEnabledFn setEnabled = nullptr;
			SetFloatFn setFloat = nullptr;
			GetStatusFn getStatus = nullptr;
			ResetFn reset = nullptr;
		};

		// Resolving five exports on every call would be wasteful, but latching
		// a failure forever is worse: EXU can run before winmm is where we
		// expect it, and a permanently latched "absent" would survive OpenShim
		// actually being there. Keying the cache on the module handle gets
		// both - one resolve per module, and an automatic re-resolve if the
		// handle ever changes or appears late.
		inline Table& Resolve() noexcept
		{
			static Table table;
			const HMODULE current = OpenShimBridge::GetModule();

			if (table.resolved && table.module == current)
			{
				return table;
			}

			table = Table{};
			table.module = current;
			table.resolved = true;

			if (current == nullptr)
			{
				return table;
			}

			table.getApiVersion =
				OpenShimBridge::Resolve<GetApiVersionFn>("OpenShimGetRenderEffectApiVersion");
			table.setEnabled =
				OpenShimBridge::Resolve<SetEnabledFn>("OpenShimSetRenderEffectEnabled");
			table.setFloat =
				OpenShimBridge::Resolve<SetFloatFn>("OpenShimSetRenderEffectFloat");
			table.getStatus =
				OpenShimBridge::Resolve<GetStatusFn>("OpenShimGetRenderEffectStatus");
			table.reset =
				OpenShimBridge::Resolve<ResetFn>("OpenShimResetRenderEffects");

			return table;
		}
	}

	// A winmm.dll that happens to be loaded is not necessarily OpenShim, and
	// an OpenShim older than this ABI will not have these exports. Successful
	// resolution of the exports is therefore the capability test, not the
	// presence of the module. A live provider older than the winmm.dll thunks
	// still resolves them but answers 0 (== kResultAccepted), so the API
	// version, which shipped with the same exports and is never 0 when
	// implemented, must also be non-zero.
	inline bool IsAvailable() noexcept
	{
		const Detail::Table& table = Detail::Resolve();
		return table.setEnabled != nullptr
			&& table.setFloat != nullptr
			&& table.getStatus != nullptr
			&& table.getApiVersion != nullptr
			&& table.getApiVersion() != 0u;
	}

	inline std::uint32_t ApiVersion() noexcept
	{
		const Detail::Table& table = Detail::Resolve();
		return table.getApiVersion ? static_cast<std::uint32_t>(table.getApiVersion()) : 0u;
	}

	// Returns the OpenShim result code, or kResultRejectedEffect when the
	// bridge is unavailable - from a mission's point of view "OpenShim is not
	// here" and "OpenShim refused" are both simply "this did not happen", and
	// the status query is where the difference is explained.
	inline std::uint32_t SetEnabled(std::uint32_t effectId, bool enabled) noexcept
	{
		const Detail::Table& table = Detail::Resolve();
		if (!IsAvailable())
		{
			return RenderEffects::Abi::kResultRejectedEffect;
		}
		return static_cast<std::uint32_t>(
			table.setEnabled(static_cast<DWORD>(effectId), enabled ? TRUE : FALSE));
	}

	inline std::uint32_t SetFloat(std::uint32_t effectId, std::uint32_t paramId, float value) noexcept
	{
		const Detail::Table& table = Detail::Resolve();
		if (!IsAvailable())
		{
			return RenderEffects::Abi::kResultRejectedEffect;
		}
		return static_cast<std::uint32_t>(
			table.setFloat(static_cast<DWORD>(effectId), static_cast<DWORD>(paramId), value));
	}

	// Fails closed to "nothing requested, nothing supported, OpenShim absent"
	// rather than to a zeroed struct that would read as a plain refusal.
	inline bool GetStatus(std::uint32_t effectId, Status& outStatus) noexcept
	{
		outStatus = Status{};
		outStatus.size = sizeof(Status);

		const Detail::Table& table = Detail::Resolve();
		if (!table.getStatus)
		{
			outStatus.version = RenderEffects::Abi::kStatusVersion;
			outStatus.reasonCode = RenderEffects::Abi::kReasonNotImplemented;
			return false;
		}

		return table.getStatus(
			static_cast<DWORD>(effectId), &outStatus, static_cast<DWORD>(sizeof(Status))) != FALSE;
	}

	// Mission-scoped teardown. OpenShim also clears this from its own mission
	// lifecycle seam, so this is belt and braces rather than the only defence.
	inline bool Reset() noexcept
	{
		const Detail::Table& table = Detail::Resolve();
		return table.reset ? table.reset() != FALSE : false;
	}
}
