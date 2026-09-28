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

#pragma once

#include <Windows.h>

// The one place EXU resolves Ogre entry points by mangled export name.
//
// Only procedure (and exported data) addresses are cached, never Ogre object
// pointers: those are looked up through these procs on every call, because
// the objects behind them belong to the runtime lifetime and can be replaced
// while the process stays alive.
//
// Kept to C++14 and out of ExtraUtilities::Ogre: OgreNativeFontBridge.cpp,
// the one file built against the real Ogre headers, includes it, compiles as
// C++14 and names the real ::Ogre from inside ExtraUtilities.
namespace ExtraUtilities
{
namespace OgreDll
{
	enum class OgreModule
	{
		Main,
		Overlay,
	};

	// Not cached: a handle looked up before the game loaded the module would
	// otherwise stay null for the life of the DLL.
	inline HMODULE GetOgreModule(OgreModule module) noexcept
	{
		return GetModuleHandleA(module == OgreModule::Overlay ? "OgreOverlay.dll" : "OgreMain.dll");
	}

	inline HMODULE GetOgreMainModule() noexcept
	{
		return GetOgreModule(OgreModule::Main);
	}

	inline HMODULE GetOgreOverlayModule() noexcept
	{
		return GetOgreModule(OgreModule::Overlay);
	}

	// One uncached lookup; null when the module is not loaded or does not
	// export the name.
	template <typename T>
	inline T ResolveOgreProc(OgreModule module, const char* mangledName) noexcept
	{
		const HMODULE handle = GetOgreModule(module);
		if (handle == nullptr || mangledName == nullptr)
		{
			return nullptr;
		}

		return reinterpret_cast<T>(GetProcAddress(handle, mangledName));
	}

	template <typename T>
	inline T ResolveOgreProc(const char* mangledName) noexcept
	{
		return ResolveOgreProc<T>(OgreModule::Main, mangledName);
	}

	// A lazily resolved export. It stays unresolved (and Get() returns null)
	// until the module is loaded; from then on the result, found or missing,
	// is latched for the life of the DLL. Constant-initialisable so it can be
	// a namespace-scope or function-local static without a guard.
	template <typename Fn>
	class OgreProc
	{
	public:
		constexpr explicit OgreProc(const char* mangledName) noexcept
			: m_name(mangledName)
		{
		}

		constexpr OgreProc(OgreModule module, const char* mangledName) noexcept
			: m_name(mangledName), m_module(module)
		{
		}

		Fn Get() const noexcept
		{
			if (!m_resolved)
			{
				if (const HMODULE handle = GetOgreModule(m_module))
				{
					m_fn = reinterpret_cast<Fn>(GetProcAddress(handle, m_name));
					m_resolved = true;
				}
			}
			return m_fn;
		}

		explicit operator bool() const noexcept
		{
			return Get() != nullptr;
		}

	private:
		const char* m_name;
		OgreModule m_module = OgreModule::Main;
		mutable Fn m_fn = nullptr;
		mutable bool m_resolved = false;
	};
}
}
