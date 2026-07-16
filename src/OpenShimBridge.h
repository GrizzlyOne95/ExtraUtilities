/* Optional OpenShim bridge resolution shared by ExtraUtilities modules. */
#pragma once

#include <Windows.h>

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
}
