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

#include "game_state.h"
#include "Util/SehGuard.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/BuildValidation.h"
#include "Util/RuntimeGate.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

namespace ExtraUtilities
{
	namespace GameState
	{
		namespace
		{
			constexpr uintptr_t kMultiplayerPauseFlagAddr = EngineAddresses::GameUI::MultiplayerPauseFlag;
			constexpr uintptr_t kMultiplayerPauseRootAddr = EngineAddresses::GameUI::MultiplayerPauseRoot;
			constexpr uintptr_t kSingleplayerPauseRootAddr = EngineAddresses::GameUI::SingleplayerPauseRoot;

			constexpr uintptr_t kUiCurrentScreenAddr = EngineAddresses::GameUI::UiCurrentScreen;
			constexpr uintptr_t kEscapeUiWrapperActiveAddr = EngineAddresses::GameUI::EscapeWrapperActive;
			constexpr uintptr_t kUiWrapperActiveAddr = EngineAddresses::GameUI::MainShellWrapperActive;
			constexpr uintptr_t kUiCurrentScreenTypeAddr = EngineAddresses::GameUI::UiCurrentScreenType;

			constexpr uint32_t kPauseScreenType = 0x0B;
			constexpr uint32_t kOptionsScreenType = 0x03;
			constexpr uint32_t kSaveGameScreenType = 0x11;
			constexpr uint32_t kLoadGameScreenType = 0x12;
			constexpr uint32_t kMissionFailedScreenType = 0x13;
			constexpr uint32_t kMissionSuccessScreenType = 0x14;
			constexpr uint32_t kRestartScreenType = 0x17;

			constexpr uintptr_t kTextEntryNodeEntryOffset = 0x08;
			constexpr uintptr_t kTextEntryFlagsOffset = 0x120;
			constexpr uint32_t kTextEntryFocused = 0x100;
			constexpr size_t kMaxTextEntryNodes = 64;

			bool IsTextEntryProbeAvailable() noexcept
			{
				if (!RuntimeGate::IsSupported())
					return false;
				const HMODULE module = GetModuleHandleA(nullptr);
				if (reinterpret_cast<uintptr_t>(module) != BzrBuildProfile::kImageBase)
					return false;

				// Fixed-address checks are cheap enough for per-frame polling.
				// These optional anchors qualify only this read-only feature.
				size_t matched = 0;
				for (const auto& anchor : BzrBuildProfile::kRuntimeAnchors)
				{
					if (std::strncmp(anchor.name, "Text entry ", 11) != 0)
						continue;
					if (!BuildValidation::Detail::MatchAnchor(module, anchor))
						return false;
					++matched;
				}
				return matched == 3;
			}

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
			case kMissionFailedScreenType:
				return "mission-failed";
			case kMissionSuccessScreenType:
				return "mission-success";
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
				const auto* escapeUiWrapperActive = reinterpret_cast<const uint32_t*>(kEscapeUiWrapperActiveAddr);
				const auto* uiWrapperActive = reinterpret_cast<const uint32_t*>(kUiWrapperActiveAddr);
				const auto* uiCurrentScreenType = reinterpret_cast<const uint32_t*>(kUiCurrentScreenTypeAddr);

				outState.cursorVisible = IsCursorVisible();
				outState.singleplayerPauseRoot = reinterpret_cast<uintptr_t>(*singleplayerPauseRoot);
				outState.multiplayerPauseRoot = reinterpret_cast<uintptr_t>(*multiplayerPauseRoot);
				outState.uiCurrentScreen = reinterpret_cast<uintptr_t>(*uiCurrentScreen);
				outState.escapeUiWrapperActive = *escapeUiWrapperActive;
				outState.uiWrapperActive = *uiWrapperActive;
				outState.uiCurrentScreenType = *uiCurrentScreenType;
				outState.multiplayerPauseFlag = static_cast<uint32_t>(*multiplayerPauseFlag);
				outState.currentScreenMatchesPauseRoot = (*uiCurrentScreen != nullptr) && (*uiCurrentScreen == *singleplayerPauseRoot);

				const bool multiplayerOpen = ((*multiplayerPauseRoot != nullptr) || (*multiplayerPauseFlag != 0)) && outState.cursorVisible;
				// FUN_005D4690 owns this flag for its entire blocking Escape UI
				// loop, including every nested options/save/load/restart page.
				const bool singleplayerOpen = *escapeUiWrapperActive != 0;

				outState.multiplayerPauseOpen = multiplayerOpen;
				outState.singleplayerPauseOpen = singleplayerOpen;
				outState.pauseMenuOpen = multiplayerOpen || singleplayerOpen;
				const bool shellUiOpen = *uiWrapperActive != 0;
				outState.gameUiOpen = outState.pauseMenuOpen || shellUiOpen;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				outState = {};
				return false;
			}
		}

		bool IsGameUiOpen() noexcept
		{
			PauseMenuDebugState state{};
			if (!TryGetPauseMenuDebugState(state))
			{
				return false;
			}
			return state.gameUiOpen;
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

		bool TryGetTextEntryDebugState(TextEntryDebugState& outState) noexcept
		{
			outState = {};
			__try
			{
				if (!IsTextEntryProbeAvailable())
					return false;
				TextEntryDebugState state{};
				state.chatNode = *reinterpret_cast<const uintptr_t*>(EngineAddresses::GameUI::ChatEntryNode);
				state.allyNode = *reinterpret_cast<const uintptr_t*>(EngineAddresses::GameUI::AllyPromptNode);
				uintptr_t node = *reinterpret_cast<const uintptr_t*>(EngineAddresses::GameUI::TextEntryListHead);
				for (size_t count = 0; node != 0; ++count)
				{
					// Match the engine's first-focused-node lookup, with a bound
					// so corrupt/cyclic lists cannot hang the mission Lua update.
					if (count == kMaxTextEntryNodes || node < 0x10000 || (node & 3) != 0 ||
						node > UINTPTR_MAX - kTextEntryNodeEntryOffset)
						return false;
					const uintptr_t entry = *reinterpret_cast<const uintptr_t*>(node + kTextEntryNodeEntryOffset);
					if (entry < 0x10000 || (entry & 3) != 0 || entry > UINTPTR_MAX - kTextEntryFlagsOffset)
						return false;
					const uint32_t flags = *reinterpret_cast<const uint32_t*>(entry + kTextEntryFlagsOffset);
					if ((flags & kTextEntryFocused) != 0)
					{
						state.textEntryActive = true;
						state.chatOpen = node == state.chatNode;
						state.allyPromptOpen = node == state.allyNode;
						state.focusedNode = node;
						state.focusedEntry = entry;
						state.entryFlags = flags;
						break;
					}
					node = *reinterpret_cast<const uintptr_t*>(node);
				}
				outState = state;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool IsTextEntryActive() noexcept
		{
			TextEntryDebugState state{};
			return TryGetTextEntryDebugState(state) && state.textEntryActive;
		}

		bool IsAllyPromptOpen() noexcept
		{
			TextEntryDebugState state{};
			return TryGetTextEntryDebugState(state) && state.allyPromptOpen;
		}
	}
}
