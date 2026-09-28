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

#include "Ogre/OgreOverlayRuntime.h"
#include "Ogre/OgreOverlayElementOps.h"

#include "Overlay.h"

#include "Hook.h"
#include "LuaHelpers.h"
#include "Util/Logging.h"
#include "Util/SignatureResolver.h"
#include "Ogre/OgreNativeFontBridge.h"
#include "Ogre/Ogre.h"
#include "Ogre/OgreOverlayShim.h"
#include "Game/game_state.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

// Internal to the overlay feature. State and helpers shared by Overlay.cpp,
// OverlayFontAssets.cpp and OverlaySuppression.cpp; the Ogre layer is in
// Ogre/OgreOverlayRuntime.h and Ogre/OgreOverlayElementOps.h.

namespace ExtraUtilities::Lua::Overlay
{
	namespace Detail
	{
		struct OverlayVisibilityState
		{
			bool requestedVisible = false;
			bool effectiveVisible = false;
		};
		extern std::unordered_map<std::string, OverlayVisibilityState> overlayVisibilityStates;
		extern bool overlayRuntimeResourcesReady;
		extern bool overlayRuntimeResourcesAttempted;
		extern bool overlayRuntimeFontReady;
		extern bool overlayRuntimeFontAttempted;
		extern std::string overlayRuntimeFontScriptPath;
		extern bool overlaySuppressionActive;
		extern volatile long overlayMissionSimulationState;
		void EnsureOverlayRuntimeFont();
		void SyncOverlayVisibilityState(const std::string& name, OverlayVisibilityState& visibilityState, const char* reason);
		void RefreshOverlaySuppressionState(const char* reason, bool synchronizeVisibility = true);
		void ForgetMissionShowRequests();
		void DestroyOverlayPauseHooks();
		void EnsureOverlayPauseHooksInstalled();
	}

	using namespace Detail;
}
