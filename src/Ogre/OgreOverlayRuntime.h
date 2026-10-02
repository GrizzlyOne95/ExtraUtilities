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

#include "Ogre/OgreOverlayShim.h"
#include "Ogre/OgreProc.h"

#include <Windows.h>

#include <string>
#include <unordered_set>

// OgreOverlay runtime for the overlay feature: the OverlaySystem lifecycle
// (construction, scene-manager attachment, teardown) and the OgreMain and
// OgreOverlay proc resolvers behind it. Lua bindings live in UI/Overlay.cpp.

namespace ExtraUtilities::Lua::Overlay
{
	namespace Detail
	{
		extern std::unordered_set<void*> attachedOverlaySceneManagers;
		extern void* overlaySystemInstance;
		using ExtraUtilities::OgreDll::GetOgreOverlayModule;
		using ExtraUtilities::OgreDll::OgreModule;
		using ExtraUtilities::OgreDll::OgreProc;
		using ExtraUtilities::OgreDll::ResolveOgreProc;
		::Ogre::OverlayManager* GetOverlayManagerRaw();
		::Ogre::Overlay* FindExistingOverlay(const std::string& name);
		bool TryGetRootSingleton(void*& outRoot);
		void EnsureOverlaySupport();
		void DetachAndDestroyOverlaySystem();
		bool TryGetRootRenderSystem(void* root, void*& outRenderSystem);
		bool TryGetRenderSystemSharedListener(void*& outSharedListener);
		bool TryGetRenderSystemViewport(void* renderSystem, void*& outViewport);
		void* GetCurrentViewportForOverlay();
		bool TryGetViewportOverlaysEnabled(void* viewport, bool& outEnabled);
	}

	using namespace Detail;
}
