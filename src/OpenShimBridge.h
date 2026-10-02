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

/* Optional OpenShim bridge resolution shared by ExtraUtilities modules. */
#pragma once

#include "Util/Logging.h"

#include <Windows.h>

#include <cstdint>

namespace ExtraUtilities::OpenShimBridge
{
	using GetApiFn = const void* (__cdecl*)(std::uint32_t requestedVersion);

	// winmm.dll exports every OpenShim* name whether or not the OpenShim plugin
	// (plugins\openshim.dll) installed its provider table. Without the provider
	// each export returns a fixed "unavailable" value that EXU would otherwise
	// read as success (0 == AppliedLive/Accepted) while standing its own
	// fallbacks down. OpenShimGetApi returns nullptr in that state, so it is the
	// liveness probe; builds that predate the export have no thunk layer and are
	// live whenever the module is loaded.
	inline bool IsProviderLive(HMODULE module) noexcept
	{
		if (module == nullptr)
		{
			return false;
		}

		const auto getApi = reinterpret_cast<GetApiFn>(GetProcAddress(module, "OpenShimGetApi"));
		return getApi == nullptr || getApi(0) != nullptr;
	}

	// Null unless OpenShim is loaded AND its provider is live, so every Resolve
	// caller fails closed on a bootstrap-only winmm.dll.
	inline HMODULE GetModule() noexcept
	{
		static HMODULE cachedModule = nullptr;
		static bool cachedLive = false;

		const HMODULE module = GetModuleHandleA("winmm.dll");
		if (module != cachedModule || (module != nullptr && !cachedLive))
		{
			cachedModule = module;
			cachedLive = IsProviderLive(module);
		}
		return cachedLive ? module : nullptr;
	}

	template <typename T>
	T Resolve(const char* exportName) noexcept
	{
		HMODULE module = GetModule();
		return module && exportName
			? reinterpret_cast<T>(GetProcAddress(module, exportName))
			: nullptr;
	}

	inline bool HasExport(const char* exportName) noexcept
	{
		return Resolve<FARPROC>(exportName) != nullptr;
	}

	// An optional export cached per winmm.dll module: resolved once while the
	// module GetModule() reports stays the same, and again if it changes or
	// appears late, so an early miss is never latched for the life of the
	// DLL. constexpr-constructible, so a function-local `static constinit`
	// needs no guard. When `missingLog` is set it goes to exu.log the first
	// time the export is found missing.
	template <typename Fn>
	class CachedExport
	{
	public:
		constexpr explicit CachedExport(const char* exportName, const char* missingLog = nullptr) noexcept
			: m_name(exportName), m_missingLog(missingLog)
		{
		}

		Fn Get() noexcept
		{
			const HMODULE module = GetModule();
			if (!m_resolved || module != m_module)
			{
				m_resolved = true;
				m_module = module;
				m_fn = module != nullptr && m_name != nullptr
					? reinterpret_cast<Fn>(GetProcAddress(module, m_name))
					: nullptr;
				if (m_fn == nullptr && m_missingLog != nullptr && !m_loggedMissing)
				{
					m_loggedMissing = true;
					Logging::LogMessage("%s", m_missingLog);
				}
			}
			return m_fn;
		}

	private:
		const char* m_name;
		const char* m_missingLog;
		HMODULE m_module = nullptr;
		Fn m_fn = nullptr;
		bool m_resolved = false;
		bool m_loggedMissing = false;
	};

	using ResolveLocalFirstPersonEntityFn = std::int32_t (__cdecl*)(
		void** outEntity, std::uint64_t* outGeneration);

	inline bool HasLocalFirstPersonEntityBridge() noexcept
	{
		return HasExport("OpenShimResolveLocalFirstPersonEntity");
	}

	// Returns a one-operation snapshot. Callers intentionally resolve again for
	// every public animation operation and never retain the Ogre pointer.
	inline bool ResolveLocalFirstPersonEntity(
		void*& outEntity, std::uint64_t& outGeneration) noexcept
	{
		outEntity = nullptr;
		outGeneration = 0;
		const auto resolve = Resolve<ResolveLocalFirstPersonEntityFn>(
			"OpenShimResolveLocalFirstPersonEntity");
		return resolve && resolve(&outEntity, &outGeneration) == 1 && outEntity;
	}

	// Mirrors OpenShim's stable storefront ABI without linking EXU against
	// OpenShim headers. Unknown is fail-closed and also covers older OpenShim
	// builds that do not expose the distribution query yet.
	enum class BzrDistribution : std::uint32_t
	{
		Unknown = 0,
		GOG = 1,
		Steam = 2,
	};

	using GetBzrDistributionFn = std::uint32_t (__cdecl*)();

	inline BzrDistribution GetBzrDistribution() noexcept
	{
		const GetBzrDistributionFn getter =
			Resolve<GetBzrDistributionFn>("OpenShimGetBzrDistribution");
		if (!getter)
		{
			return BzrDistribution::Unknown;
		}

		switch (getter())
		{
		case static_cast<std::uint32_t>(BzrDistribution::GOG):
			return BzrDistribution::GOG;
		case static_cast<std::uint32_t>(BzrDistribution::Steam):
			return BzrDistribution::Steam;
		default:
			return BzrDistribution::Unknown;
		}
	}

	// Mirrors OpenShim's stable status values without linking EXU against any
	// OpenShim headers. The extra sentinel is EXU-only and means winmm.dll does
	// not expose the optional high-level nickname bridge.
	enum class BzrNetNicknameResult : std::uint32_t
	{
		AppliedLive = 0,
		StoredForNextConnection = 1,
		InvalidNickname = 2,
		UnsupportedBuild = 3,
		NativeStateInvalid = 4,
		PersistenceFailed = 5,
		OpenShimUnavailable = 0xFFFFFFFFu,
	};

	using SetBzrNetNicknameFn = DWORD (WINAPI*)(LPCSTR nickname);

	inline BzrNetNicknameResult SetBzrNetNickname(const char* nickname) noexcept
	{
		const SetBzrNetNicknameFn setter =
			Resolve<SetBzrNetNicknameFn>("OpenShimSetBZRNetNickname");
		if (!setter)
		{
			return BzrNetNicknameResult::OpenShimUnavailable;
		}

		// OpenShim owns validation, persistence, local UI synchronization, and all
		// BZRNet/native ABI details. EXU deliberately passes only the text value.
		return static_cast<BzrNetNicknameResult>(setter(nickname));
	}
}
