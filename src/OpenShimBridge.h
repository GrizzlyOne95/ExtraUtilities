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
