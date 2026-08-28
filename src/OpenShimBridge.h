/* Optional OpenShim bridge resolution shared by ExtraUtilities modules. */
#pragma once

#include <Windows.h>

#include <cstdint>

namespace ExtraUtilities::OpenShimBridge
{
	inline HMODULE GetModule() noexcept
	{
		return GetModuleHandleA("winmm.dll");
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

	inline bool HasBzrNetNicknameBridge() noexcept
	{
		return HasExport("OpenShimSetBZRNetNickname");
	}

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
