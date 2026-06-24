#include "game_state.h"

#include <Windows.h>
#include <cstdint>

namespace ExtraUtilities
{
	namespace GameState
	{
		namespace
		{
			constexpr uintptr_t kMultiplayerPauseFlagAddr = 0x00945549;
			constexpr uintptr_t kMultiplayerPauseRootAddr = 0x0094557C;
			constexpr uintptr_t kSingleplayerPauseRootAddr = 0x009454EC;

			constexpr uintptr_t kUiCurrentScreenAddr = 0x00918320;
			constexpr uintptr_t kUiWrapperActiveAddr = 0x00918324;
			constexpr uintptr_t kUiCurrentScreenTypeAddr = 0x00918328;

			constexpr uint32_t kPauseScreenType = 0x0B;
			constexpr uint32_t kOptionsScreenType = 0x03;
			constexpr uint32_t kSaveGameScreenType = 0x11;
			constexpr uint32_t kLoadGameScreenType = 0x12;
			constexpr uint32_t kRestartScreenType = 0x17;

			bool IsCursorVisible() noexcept
			{
				CURSORINFO info{};
				info.cbSize = sizeof(info);
				return GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING) != 0;
			}
		}

		const char* DescribeScreenType(uint32_t screenType) noexcept
		{
			switch (screenType)
			{
			case kOptionsScreenType:
				return "options";
			case kPauseScreenType:
				return "pause";
			case kSaveGameScreenType:
				return "save";
			case kLoadGameScreenType:
				return "load";
			case kRestartScreenType:
				return "restart";
			case 0:
				return "none";
			default:
				return "other";
			}
		}

		bool TryGetPauseMenuDebugState(PauseMenuDebugState& outState) noexcept
		{
			outState = {};

			__try
			{
				const auto* multiplayerPauseRoot = reinterpret_cast<void* const*>(kMultiplayerPauseRootAddr);
				const auto* multiplayerPauseFlag = reinterpret_cast<const uint8_t*>(kMultiplayerPauseFlagAddr);
				const auto* singleplayerPauseRoot = reinterpret_cast<void* const*>(kSingleplayerPauseRootAddr);
				const auto* uiCurrentScreen = reinterpret_cast<void* const*>(kUiCurrentScreenAddr);
				const auto* uiWrapperActive = reinterpret_cast<const uint32_t*>(kUiWrapperActiveAddr);
				const auto* uiCurrentScreenType = reinterpret_cast<const uint32_t*>(kUiCurrentScreenTypeAddr);

				outState.cursorVisible = IsCursorVisible();
				outState.singleplayerPauseRoot = reinterpret_cast<uintptr_t>(*singleplayerPauseRoot);
				outState.multiplayerPauseRoot = reinterpret_cast<uintptr_t>(*multiplayerPauseRoot);
				outState.uiCurrentScreen = reinterpret_cast<uintptr_t>(*uiCurrentScreen);
				outState.uiWrapperActive = *uiWrapperActive;
				outState.uiCurrentScreenType = *uiCurrentScreenType;
				outState.multiplayerPauseFlag = static_cast<uint32_t>(*multiplayerPauseFlag);
				outState.currentScreenMatchesPauseRoot = (*uiCurrentScreen != nullptr) && (*uiCurrentScreen == *singleplayerPauseRoot);

				const bool multiplayerOpen = ((*multiplayerPauseRoot != nullptr) || (*multiplayerPauseFlag != 0)) && outState.cursorVisible;
				bool singleplayerOpen = false;
				if (*singleplayerPauseRoot != nullptr && *uiWrapperActive != 0)
				{
					if (*uiCurrentScreen == *singleplayerPauseRoot)
					{
						singleplayerOpen = true;
					}
					else
					{
						switch (*uiCurrentScreenType)
						{
						case kPauseScreenType:
						case kOptionsScreenType:
						case kSaveGameScreenType:
						case kLoadGameScreenType:
						case kRestartScreenType:
							singleplayerOpen = true;
							break;
						default:
							break;
						}
					}
				}

				outState.multiplayerPauseOpen = multiplayerOpen;
				outState.singleplayerPauseOpen = singleplayerOpen;
				outState.pauseMenuOpen = multiplayerOpen || singleplayerOpen;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outState = {};
				return false;
			}
		}

		bool IsMultiplayerPauseMenuOpen() noexcept
		{
			PauseMenuDebugState state{};
			if (!TryGetPauseMenuDebugState(state))
			{
				return false;
			}
			return state.multiplayerPauseOpen;
		}

		bool IsSingleplayerPauseMenuOpen() noexcept
		{
			PauseMenuDebugState state{};
			if (!TryGetPauseMenuDebugState(state))
			{
				return false;
			}
			return state.singleplayerPauseOpen;
		}

		bool IsPauseMenuOpen() noexcept
		{
			PauseMenuDebugState state{};
			if (!TryGetPauseMenuDebugState(state))
			{
				return false;
			}
			return state.pauseMenuOpen;
		}
	}
}
