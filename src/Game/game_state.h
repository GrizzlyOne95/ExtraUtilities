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
			bool pauseMenuOpen = false;
			bool singleplayerPauseOpen = false;
			bool multiplayerPauseOpen = false;
			bool currentScreenMatchesPauseRoot = false;
			uintptr_t singleplayerPauseRoot = 0;
			uintptr_t multiplayerPauseRoot = 0;
			uintptr_t uiCurrentScreen = 0;
			uint32_t uiWrapperActive = 0;
			uint32_t uiCurrentScreenType = 0;
			uint32_t multiplayerPauseFlag = 0;
		};

		bool IsPauseMenuOpen() noexcept;
		bool IsSingleplayerPauseMenuOpen() noexcept;
		bool IsMultiplayerPauseMenuOpen() noexcept;
		bool TryGetPauseMenuDebugState(PauseMenuDebugState& outState) noexcept;
		const char* DescribeScreenType(uint32_t screenType) noexcept;
	}
}
