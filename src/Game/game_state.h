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
#include <cstdint>

namespace ExtraUtilities
{
	namespace GameState
	{
		struct PauseMenuDebugState
		{
			bool cursorVisible = false;
			bool gameUiOpen = false;
			bool pauseMenuOpen = false;
			bool singleplayerPauseOpen = false;
			bool multiplayerPauseOpen = false;
			bool currentScreenMatchesPauseRoot = false;
			uintptr_t singleplayerPauseRoot = 0;
			uintptr_t multiplayerPauseRoot = 0;
			uintptr_t uiCurrentScreen = 0;
			uint32_t escapeUiWrapperActive = 0;
			uint32_t uiWrapperActive = 0;
			uint32_t uiCurrentScreenType = 0;
			uint32_t multiplayerPauseFlag = 0;
		};

		bool IsGameUiOpen() noexcept;
		bool IsPauseMenuOpen() noexcept;
		bool TryGetPauseMenuDebugState(PauseMenuDebugState& outState) noexcept;
		const char* DescribeScreenType(uint32_t screenType) noexcept;
	}
}
