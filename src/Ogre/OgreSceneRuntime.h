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

#include "bzr.h"
#include "Ogre/OgreProc.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>

// Ogre scene runtime for the environment features: OgreMain proc resolution,
// Root/RenderSystem/SceneManager viewport discovery, the raw
// Viewport::mMaterialSchemeName layout, viewport overlay toggles and the
// SceneManager sky entry points, all resolved by mangled name.

namespace ExtraUtilities::Lua::Environment
{
	namespace Detail
	{
		struct ActiveViewportSet
		{
			std::array<void*, 2> viewports{};
			size_t count = 0;
		};
		bool TryGetViewportMaterialScheme(void* viewport, std::string& outScheme);
		bool TrySetViewportMaterialScheme(void* viewport, const std::string& scheme);
		ActiveViewportSet GetActiveViewports();
		using ExtraUtilities::OgreDll::OgreModule;
		using ExtraUtilities::OgreDll::OgreProc;
		using ExtraUtilities::OgreDll::ResolveOgreProc;
	}

	using namespace Detail;

		struct OgreQuaternionValue
		{
			float w = 1.0f;
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
		};

		struct OgrePlaneValue
		{
			BZR::VECTOR_3D normal{ 0.0f, -1.0f, 0.0f };
			float d = 1000.0f;
		};

		using IsSkyEnabledFn = bool(__thiscall*)(void*);
		using SetSkyEnabledFn = void(__thiscall*)(void*, bool);
		using SetSkyBoxFn = void(__thiscall*)(void*, bool, const std::string&, float, bool, const OgreQuaternionValue&, const std::string&);
		using SetSkyDomeFn = void(__thiscall*)(void*, bool, const std::string&, float, float, float, bool, const OgreQuaternionValue&, int, int, int, const std::string&);
		using SetSkyPlaneFn = void(__thiscall*)(void*, bool, const OgrePlaneValue&, const std::string&, float, float, bool, float, int, int, const std::string&);
		IsSkyEnabledFn ResolveIsSkyBoxEnabled();
		IsSkyEnabledFn ResolveIsSkyDomeEnabled();
		IsSkyEnabledFn ResolveIsSkyPlaneEnabled();
		SetSkyEnabledFn ResolveSetSkyBoxEnabled();
		SetSkyEnabledFn ResolveSetSkyDomeEnabled();
		SetSkyEnabledFn ResolveSetSkyPlaneEnabled();
		SetSkyBoxFn ResolveSetSkyBox();
		SetSkyDomeFn ResolveSetSkyDome();
		SetSkyPlaneFn ResolveSetSkyPlane();
	bool TryGetViewportOverlaysEnabled(void* viewport, bool& outEnabled);
	bool TrySetViewportOverlaysEnabled(void* viewport, bool enabled);
	bool TryRefreshViewport(void* viewport);
}
