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

#include "OverlayInternal.h"

// Overlay visibility and suppression: each overlay's requested show state, and
// the hooks on the stock pause and game-shell wrappers that hide EXU overlays
// while stock UI is up.

namespace ExtraUtilities::Lua::Overlay
{
	namespace
	{
		std::unique_ptr<Hook> overlayPauseEnterHook;
		std::unique_ptr<Hook> overlayPauseExitHook;
		std::unique_ptr<Hook> overlayGameShellEnterHook;
		std::unique_ptr<Hook> overlayGameShellExitHook;
		bool overlayPauseHooksAttempted = false;
		bool overlayPauseHooksReady = false;
		bool overlayGameShellHooksAttempted = false;
		bool overlayGameShellHooksReady = false;
		volatile long overlayPauseWrapperDepth = 0;
		volatile long overlayGameShellWrapperDepth = 0;
		constexpr uintptr_t kPauseWrapperFunctionAddr = 0x005D4690;
		constexpr uintptr_t kPauseWrapperEntryHookOffset = 0x26;
		constexpr uintptr_t kPauseWrapperExitHookOffset = 0x1CA;
		constexpr std::array<uint8_t, 33> kPauseWrapperFunctionPattern = {
			0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x2C, 0x83, 0x91, 0x00, 0x00, 0x74, 0x05,
			0xE9, 0x05, 0x02, 0x00, 0x00, 0x0F, 0xB6, 0x05, 0x2B, 0x81, 0x91, 0x00, 0x85,
			0xC0, 0x0F, 0x85, 0xF6, 0x01, 0x00, 0x00
		};
		constexpr std::array<uint8_t, 33> kPauseWrapperFunctionMask = {
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 0, 0, 0, 0
		};
		constexpr std::array<uint8_t, 9> kPauseWrapperEntryHookBytes = {
			0x8B, 0x4D, 0x08, 0x51, 0x68, 0x64, 0x7A, 0x88, 0x00
		};
		constexpr std::array<uint8_t, 7> kPauseWrapperExitHookBytes = {
			0xC6, 0x05, 0x2B, 0x81, 0x91, 0x00, 0x00
		};
		constexpr uintptr_t kGameShellWrapperFunctionAddr = 0x005D42E0;
		constexpr uintptr_t kGameShellWrapperEntryHookOffset = 0x32;
		constexpr uintptr_t kGameShellWrapperExitHookOffset = 0x332;
		constexpr std::array<uint8_t, 21> kGameShellWrapperFunctionPattern = {
			0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34, 0xA1, 0x00, 0x70, 0x8E, 0x00,
			0x33, 0xC5, 0x89, 0x45, 0xFC, 0x68, 0x28, 0x79, 0x88, 0x00
		};
		constexpr std::array<uint8_t, 21> kGameShellWrapperFunctionMask = {
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
			1, 1, 1, 1, 1, 1, 1, 1, 1, 1
		};
		constexpr std::array<uint8_t, 10> kGameShellWrapperEntryHookBytes = {
			0xC7, 0x05, 0x24, 0x83, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00
		};
		constexpr std::array<uint8_t, 10> kGameShellWrapperExitHookBytes = {
			0xC7, 0x05, 0x24, 0x83, 0x91, 0x00, 0x00, 0x00, 0x00, 0x00
		};

		using SignatureResolver::FindMaskedPattern;
		using SignatureResolver::MatchBytes;

		bool TryGetMainModuleTextSection(const uint8_t*& outData, size_t& outSize, uintptr_t& outAddress)
		{
			return SignatureResolver::TryGetModuleTextSection(
				GetModuleHandleA(nullptr), outData, outSize, outAddress);
		}

		bool IsOverlaySuppressedByGameUi() noexcept
		{
			return overlayPauseWrapperDepth > 0
				|| overlayGameShellWrapperDepth > 0
				|| overlayMissionSimulationState == 0
				|| ExtraUtilities::GameState::IsGameUiOpen();
		}

		void OnOverlayPauseWrapperEnter(int dialogId) noexcept
		{
			const long depth = InterlockedIncrement(&overlayPauseWrapperDepth);
			try
			{
				Logging::LogMessage("[EXU::Overlay] pause wrapper enter dialog=%d depth=%ld", dialogId, depth);
				RefreshOverlaySuppressionState("pause-wrapper-enter");
			}
			catch (...)
			{
			}
		}

		void OnOverlayPauseWrapperExit() noexcept
		{
			long depth = InterlockedDecrement(&overlayPauseWrapperDepth);
			if (depth < 0)
			{
				overlayPauseWrapperDepth = 0;
				depth = 0;
			}

			try
			{
				Logging::LogMessage("[EXU::Overlay] pause wrapper exit depth=%ld", depth);
				RefreshOverlaySuppressionState("pause-wrapper-exit");
			}
			catch (...)
			{
			}
		}

		void OnOverlayGameShellWrapperEnter() noexcept
		{
			const long depth = InterlockedIncrement(&overlayGameShellWrapperDepth);
			try
			{
				Logging::LogMessage("[EXU::Overlay] game shell wrapper enter depth=%ld", depth);
				// The shell means the mission is over.
				ForgetMissionShowRequests();
				RefreshOverlaySuppressionState("game-shell-wrapper-enter");
			}
			catch (...)
			{
			}
		}

		void OnOverlayGameShellWrapperExit() noexcept
		{
			long depth = InterlockedDecrement(&overlayGameShellWrapperDepth);
			if (depth < 0)
			{
				overlayGameShellWrapperDepth = 0;
				depth = 0;
			}

			try
			{
				Logging::LogMessage("[EXU::Overlay] game shell wrapper exit depth=%ld", depth);
				RefreshOverlaySuppressionState("game-shell-wrapper-exit", false);
			}
			catch (...)
			{
			}
		}

		static void __declspec(naked) OverlayPauseWrapperEnterHook()
		{
			__asm
			{
				pushad
				pushfd

				mov eax, [ebp+0x08]
				push eax
				call OnOverlayPauseWrapperEnter
				add esp, 0x04

				popfd
				popad

				pop eax
				mov ecx, [ebp+0x08]
				push ecx
				push 0x00887A64
				jmp eax
			}
		}

		static void __declspec(naked) OverlayPauseWrapperExitHook()
		{
			__asm
			{
				mov byte ptr ds:[0x0091812B], 0

				pushad
				pushfd

				call OnOverlayPauseWrapperExit

				popfd
				popad
				ret
			}
		}

		static void __declspec(naked) OverlayGameShellWrapperEnterHook()
		{
			__asm
			{
				mov dword ptr ds:[0x00918324], 1

				pushad
				pushfd

				call OnOverlayGameShellWrapperEnter

				popfd
				popad
				ret
			}
		}

		static void __declspec(naked) OverlayGameShellWrapperExitHook()
		{
			__asm
			{
				mov dword ptr ds:[0x00918324], 0

				pushad
				pushfd

				call OnOverlayGameShellWrapperExit

				popfd
				popad
				ret
			}
		}

		uintptr_t ResolvePauseWrapperFunctionAddress()
		{
			if (MatchBytes(kPauseWrapperFunctionAddr, kPauseWrapperFunctionPattern))
			{
				return kPauseWrapperFunctionAddr;
			}

			const uint8_t* textData = nullptr;
			size_t textSize = 0;
			uintptr_t textAddress = 0;
			if (!TryGetMainModuleTextSection(textData, textSize, textAddress))
			{
				return 0;
			}

			return FindMaskedPattern(
				textData,
				textSize,
				textAddress,
				kPauseWrapperFunctionPattern.data(),
				kPauseWrapperFunctionMask.data(),
				kPauseWrapperFunctionPattern.size());
		}

		uintptr_t ResolveGameShellWrapperFunctionAddress()
		{
			if (MatchBytes(kGameShellWrapperFunctionAddr, kGameShellWrapperFunctionPattern))
			{
				return kGameShellWrapperFunctionAddr;
			}

			const uint8_t* textData = nullptr;
			size_t textSize = 0;
			uintptr_t textAddress = 0;
			if (!TryGetMainModuleTextSection(textData, textSize, textAddress))
			{
				return 0;
			}

			return FindMaskedPattern(
				textData,
				textSize,
				textAddress,
				kGameShellWrapperFunctionPattern.data(),
				kGameShellWrapperFunctionMask.data(),
				kGameShellWrapperFunctionPattern.size());
		}

		void EnsureOverlayGameShellHooksInstalled()
		{
			if (overlayGameShellHooksAttempted)
			{
				return;
			}

			overlayGameShellHooksAttempted = true;
			const uintptr_t functionAddress = ResolveGameShellWrapperFunctionAddress();
			if (functionAddress == 0)
			{
				Logging::LogMessage("[EXU::Overlay] game shell wrapper hook install failed reason=function-not-found");
				return;
			}

			const uintptr_t entryHookAddress = functionAddress + kGameShellWrapperEntryHookOffset;
			const uintptr_t exitHookAddress = functionAddress + kGameShellWrapperExitHookOffset;
			if (!MatchBytes(entryHookAddress, kGameShellWrapperEntryHookBytes)
				|| !MatchBytes(exitHookAddress, kGameShellWrapperExitHookBytes))
			{
				Logging::LogMessage(
					"[EXU::Overlay] game shell wrapper hook install failed reason=byte-mismatch function=%p entry=%p exit=%p",
					reinterpret_cast<void*>(functionAddress),
					reinterpret_cast<void*>(entryHookAddress),
					reinterpret_cast<void*>(exitHookAddress));
				return;
			}

			overlayGameShellEnterHook = std::make_unique<Hook>(
				entryHookAddress,
				&OverlayGameShellWrapperEnterHook,
				kGameShellWrapperEntryHookBytes.size(),
				BasicPatch::Status::ACTIVE);
			overlayGameShellExitHook = std::make_unique<Hook>(
				exitHookAddress,
				&OverlayGameShellWrapperExitHook,
				kGameShellWrapperExitHookBytes.size(),
				BasicPatch::Status::ACTIVE);

			overlayGameShellHooksReady = overlayGameShellEnterHook != nullptr
				&& overlayGameShellEnterHook->IsActive()
				&& overlayGameShellExitHook != nullptr
				&& overlayGameShellExitHook->IsActive();
			Logging::LogMessage(
				"[EXU::Overlay] game shell wrapper hooks %s function=%p entry=%p exit=%p",
				overlayGameShellHooksReady ? "ready" : "inactive",
				reinterpret_cast<void*>(functionAddress),
				reinterpret_cast<void*>(entryHookAddress),
				reinterpret_cast<void*>(exitHookAddress));
		}
	}

	namespace Detail
	{
		std::unordered_map<std::string, OverlayVisibilityState> overlayVisibilityStates;
		bool overlaySuppressionActive = false;
		// -1 means an older/missing OpenShim has not supplied a mission state.
		// Once known, overlays are allowed only while Redux is RUN_STARTED.
		volatile long overlayMissionSimulationState = -1;

		void SyncOverlayVisibilityState(const std::string& name, OverlayVisibilityState& visibilityState, const char* reason)
		{
			const bool shouldBeVisible = visibilityState.requestedVisible && !overlaySuppressionActive;
			if (visibilityState.effectiveVisible == shouldBeVisible)
			{
				return;
			}

			::Ogre::Overlay* overlay = FindExistingOverlay(name);
			if (overlay == nullptr)
			{
				visibilityState.effectiveVisible = false;
				return;
			}

			unsigned int exceptionCode = 0;
			const bool success = shouldBeVisible
				? TryShowOverlay(overlay, exceptionCode)
				: TryHideOverlay(overlay, exceptionCode);

			if (!success)
			{
				Logging::LogMessage(
					"[EXU::Overlay] SyncOverlayVisibility failed name=%s reason=%s targetVisible=%d code=0x%08X",
					name.c_str(),
					reason != nullptr ? reason : "unknown",
					shouldBeVisible ? 1 : 0,
					exceptionCode);
				return;
			}

			visibilityState.effectiveVisible = shouldBeVisible;
			Logging::LogMessage(
				"[EXU::Overlay] SyncOverlayVisibility name=%s reason=%s requested=%d effective=%d suppressed=%d",
				name.c_str(),
				reason != nullptr ? reason : "unknown",
				visibilityState.requestedVisible ? 1 : 0,
				visibilityState.effectiveVisible ? 1 : 0,
				overlaySuppressionActive ? 1 : 0);
		}

		void RefreshOverlaySuppressionState(const char* reason, bool synchronizeVisibility)
		{
			const bool shouldSuppress = IsOverlaySuppressedByGameUi();
			if (overlaySuppressionActive != shouldSuppress)
			{
				Logging::LogMessage(
					"[EXU::Overlay] Suppression state changed reason=%s suppressed=%d pauseDepth=%ld shellDepth=%ld gameUiProbe=%d tracked=%u",
					reason != nullptr ? reason : "unknown",
					shouldSuppress ? 1 : 0,
					overlayPauseWrapperDepth,
					overlayGameShellWrapperDepth,
					ExtraUtilities::GameState::IsGameUiOpen() ? 1 : 0,
					static_cast<unsigned>(overlayVisibilityStates.size()));
			}

			overlaySuppressionActive = shouldSuppress;
			if (!synchronizeVisibility || overlayVisibilityStates.empty())
			{
				return;
			}

			for (auto& [name, visibilityState] : overlayVisibilityStates)
			{
				SyncOverlayVisibilityState(name, visibilityState, reason);
			}
		}

		// Overlays a mission asked to show belong to that mission. Without this,
		// the next refresh that synchronizes visibility (the next mission's
		// first CreateOverlay/ShowOverlay, or a pause) re-showed them.
		void ForgetMissionShowRequests()
		{
			for (auto& [name, visibilityState] : overlayVisibilityStates)
			{
				visibilityState.requestedVisible = false;
			}
		}

		void DestroyOverlayPauseHooks()
		{
			overlayGameShellExitHook.reset();
			overlayGameShellEnterHook.reset();
			overlayPauseExitHook.reset();
			overlayPauseEnterHook.reset();
			overlayGameShellHooksReady = false;
			overlayGameShellHooksAttempted = false;
			overlayPauseHooksReady = false;
			overlayPauseHooksAttempted = false;
			overlayGameShellWrapperDepth = 0;
			overlayPauseWrapperDepth = 0;
			overlaySuppressionActive = false;
		}

		void EnsureOverlayPauseHooksInstalled()
		{
			EnsureOverlayGameShellHooksInstalled();
			if (overlayPauseHooksAttempted)
			{
				return;
			}

			overlayPauseHooksAttempted = true;
			const uintptr_t functionAddress = ResolvePauseWrapperFunctionAddress();
			if (functionAddress == 0)
			{
				Logging::LogMessage("[EXU::Overlay] pause wrapper hook install failed reason=function-not-found");
				return;
			}

			const uintptr_t entryHookAddress = functionAddress + kPauseWrapperEntryHookOffset;
			const uintptr_t exitHookAddress = functionAddress + kPauseWrapperExitHookOffset;
			if (!MatchBytes(entryHookAddress, kPauseWrapperEntryHookBytes)
				|| !MatchBytes(exitHookAddress, kPauseWrapperExitHookBytes))
			{
				Logging::LogMessage(
					"[EXU::Overlay] pause wrapper hook install failed reason=byte-mismatch function=%p entry=%p exit=%p",
					reinterpret_cast<void*>(functionAddress),
					reinterpret_cast<void*>(entryHookAddress),
					reinterpret_cast<void*>(exitHookAddress));
				return;
			}

			overlayPauseEnterHook = std::make_unique<Hook>(
				entryHookAddress,
				&OverlayPauseWrapperEnterHook,
				kPauseWrapperEntryHookBytes.size(),
				BasicPatch::Status::ACTIVE);
			overlayPauseExitHook = std::make_unique<Hook>(
				exitHookAddress,
				&OverlayPauseWrapperExitHook,
				kPauseWrapperExitHookBytes.size(),
				BasicPatch::Status::ACTIVE);

			overlayPauseHooksReady = overlayPauseEnterHook != nullptr
				&& overlayPauseEnterHook->IsActive()
				&& overlayPauseExitHook != nullptr
				&& overlayPauseExitHook->IsActive();
			Logging::LogMessage(
				"[EXU::Overlay] pause wrapper hooks %s function=%p entry=%p exit=%p",
				overlayPauseHooksReady ? "ready" : "inactive",
				reinterpret_cast<void*>(functionAddress),
				reinterpret_cast<void*>(entryHookAddress),
				reinterpret_cast<void*>(exitHookAddress));
		}
	}
}

// Optional process-local bridge used by OpenShim. Keeping the C ABI tiny lets
// either DLL run without the other and avoids coupling their C++ interfaces.
extern "C" __declspec(dllexport) void __cdecl ExuNotifyMissionSimulationState(int active)
{
	ExtraUtilities::Lua::Overlay::NotifyMissionSimulationState(active != 0);
}
