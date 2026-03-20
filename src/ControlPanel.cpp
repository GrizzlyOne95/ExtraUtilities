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

#include "ControlPanel.h"

#include "Hook.h"
#include "Logging.h"
#include "LuaHelpers.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace ExtraUtilities::Lua::ControlPanel
{
	namespace
	{
		constexpr uintptr_t kScrapPilotHudDrawHookAddress = 0x005C6FF0;
		constexpr size_t kScrapPilotHudDrawHookLength = 10;
		constexpr uintptr_t kScrapLabelColorHookAddress = 0x005C712B;
		constexpr uintptr_t kScrapValueColorHookAddress = 0x005C719B;
		constexpr uintptr_t kPilotLabelColorHookAddress = 0x005C72F1;
		constexpr uintptr_t kPilotValueColorHookAddress = 0x005C7361;
		constexpr size_t kScrapHudTextColorHookLength = 6;
		constexpr size_t kPilotHudTextColorHookLength = 7;
		constexpr size_t kScrapGroupStartIndex = 0;
		constexpr size_t kScrapGroupEndIndex = 2;
		constexpr size_t kPilotGroupStartIndex = 2;
		constexpr size_t kPilotGroupEndIndex = 4;
		constexpr uint32_t kDefaultHudTextColor = 0xFFFFFFFFu;

		using HudPaletteSelectorFn = char(__thiscall*)(void*);
		using ApplyScrapPilotHudOffsetFn = void(__cdecl*)();
		using OpenShimGetHudSpriteRectFn = BOOL(WINAPI*)(LPCSTR, int*, int*, int*, int*);
		using OpenShimSetHudSpriteRectFn = BOOL(WINAPI*)(LPCSTR, int, int, int, int);
		using OpenShimSetHudSpriteVisibleFn = BOOL(WINAPI*)(LPCSTR, BOOL);
		using OpenShimRestoreHudSpriteFn = BOOL(WINAPI*)(LPCSTR);
		using OpenShimRestoreAllHudSpritesFn = BOOL(WINAPI*)();

		struct HudTextPoint
		{
			int* x = nullptr;
			int* y = nullptr;
		};

		enum class HudTextGroup : size_t
		{
			Scrap = 0,
			Pilot = 1
		};

		inline HudPaletteSelectorFn g_hudPaletteSelector = reinterpret_cast<HudPaletteSelectorFn>(0x0047C070);
		inline void* g_hudPaletteSelectorThis = reinterpret_cast<void*>(0x0094F4B0);
		inline std::array<HudTextPoint, 4> g_scrapPilotHudTextPoints{ {
			{ reinterpret_cast<int*>(0x0091829C), reinterpret_cast<int*>(0x009182A0) },
			{ reinterpret_cast<int*>(0x0091826C), reinterpret_cast<int*>(0x00918270) },
			{ reinterpret_cast<int*>(0x00918280), reinterpret_cast<int*>(0x00918284) },
			{ reinterpret_cast<int*>(0x00918278), reinterpret_cast<int*>(0x0091827C) }
		} };
		inline std::array<int, 8> g_scrapPilotHudBaseline{};
		inline std::array<int, 2> g_scrapPilotHudOffsetX{};
		inline std::array<int, 2> g_scrapPilotHudOffsetY{};
		inline std::array<int, 2> g_scrapPilotHudAppliedX{};
		inline std::array<int, 2> g_scrapPilotHudAppliedY{};
		inline uint32_t g_scrapHudColor = kDefaultHudTextColor;
		inline uint32_t g_pilotHudColor = kDefaultHudTextColor;
		inline bool g_scrapPilotHudBaselineValid = false;

		uint32_t CheckHudColor(lua_State* L, int index, const char* label)
		{
			const lua_Number rawColor = luaL_checknumber(L, index);
			if (!std::isfinite(rawColor) ||
				rawColor < 0.0 ||
				rawColor > 4294967295.0 ||
				std::floor(rawColor) != rawColor)
			{
				luaL_error(L, "%s must be an unsigned 32-bit color value", label);
			}

			return static_cast<uint32_t>(rawColor);
		}

		constexpr size_t ToIndex(HudTextGroup group) noexcept
		{
			return static_cast<size_t>(group);
		}

		constexpr HudTextGroup GroupForPointIndex(size_t pointIndex) noexcept
		{
			return (pointIndex < kPilotGroupStartIndex) ? HudTextGroup::Scrap : HudTextGroup::Pilot;
		}

		constexpr size_t GetGroupStartIndex(HudTextGroup group) noexcept
		{
			return (group == HudTextGroup::Scrap) ? kScrapGroupStartIndex : kPilotGroupStartIndex;
		}

		constexpr size_t GetGroupEndIndex(HudTextGroup group) noexcept
		{
			return (group == HudTextGroup::Scrap) ? kScrapGroupEndIndex : kPilotGroupEndIndex;
		}

		void CaptureScrapPilotHudBaseline() noexcept
		{
			size_t baselineIndex = 0;
			for (const HudTextPoint& point : g_scrapPilotHudTextPoints)
			{
				if (point.x == nullptr || point.y == nullptr)
				{
					g_scrapPilotHudBaselineValid = false;
					return;
				}

				g_scrapPilotHudBaseline[baselineIndex++] = *point.x;
				g_scrapPilotHudBaseline[baselineIndex++] = *point.y;
			}

			g_scrapPilotHudAppliedX.fill(0);
			g_scrapPilotHudAppliedY.fill(0);
			g_scrapPilotHudBaselineValid = true;
		}

		bool ScrapPilotHudMatchesExpectedLayout() noexcept
		{
			if (!g_scrapPilotHudBaselineValid)
			{
				return false;
			}

			size_t baselineIndex = 0;
			for (size_t pointIndex = 0; pointIndex < g_scrapPilotHudTextPoints.size(); ++pointIndex)
			{
				const HudTextPoint& point = g_scrapPilotHudTextPoints[pointIndex];
				if (point.x == nullptr || point.y == nullptr)
				{
					return false;
				}

				const size_t groupIndex = ToIndex(GroupForPointIndex(pointIndex));
				const int expectedX = g_scrapPilotHudBaseline[baselineIndex++] + g_scrapPilotHudAppliedX[groupIndex];
				const int expectedY = g_scrapPilotHudBaseline[baselineIndex++] + g_scrapPilotHudAppliedY[groupIndex];
				if (*point.x != expectedX || *point.y != expectedY)
				{
					return false;
				}
			}

			return true;
		}

		void ApplyScrapPilotHudOffset() noexcept
		{
			if (!ScrapPilotHudMatchesExpectedLayout())
			{
				CaptureScrapPilotHudBaseline();
			}

			if (!g_scrapPilotHudBaselineValid)
			{
				return;
			}

			size_t baselineIndex = 0;
			for (size_t pointIndex = 0; pointIndex < g_scrapPilotHudTextPoints.size(); ++pointIndex)
			{
				const HudTextPoint& point = g_scrapPilotHudTextPoints[pointIndex];
				const size_t groupIndex = ToIndex(GroupForPointIndex(pointIndex));

				*point.x = g_scrapPilotHudBaseline[baselineIndex++] + g_scrapPilotHudOffsetX[groupIndex];
				*point.y = g_scrapPilotHudBaseline[baselineIndex++] + g_scrapPilotHudOffsetY[groupIndex];
			}

			g_scrapPilotHudAppliedX = g_scrapPilotHudOffsetX;
			g_scrapPilotHudAppliedY = g_scrapPilotHudOffsetY;
		}

		bool GetScrapPilotHudBaselineTopLeft(int& outLeft, int& outTop) noexcept
		{
			if (!g_scrapPilotHudBaselineValid)
			{
				CaptureScrapPilotHudBaseline();
			}

			if (!g_scrapPilotHudBaselineValid)
			{
				return false;
			}

			outLeft = g_scrapPilotHudBaseline[0];
			outTop = g_scrapPilotHudBaseline[1];
			for (size_t index = 2; index + 1 < g_scrapPilotHudBaseline.size(); index += 2)
			{
				outLeft = (outLeft < g_scrapPilotHudBaseline[index]) ? outLeft : g_scrapPilotHudBaseline[index];
				outTop = (outTop < g_scrapPilotHudBaseline[index + 1]) ? outTop : g_scrapPilotHudBaseline[index + 1];
			}

			return true;
		}

		bool GetHudBaselineTopLeft(HudTextGroup group, int& outLeft, int& outTop) noexcept
		{
			if (!g_scrapPilotHudBaselineValid)
			{
				CaptureScrapPilotHudBaseline();
			}

			if (!g_scrapPilotHudBaselineValid)
			{
				return false;
			}

			size_t startIndex = GetGroupStartIndex(group) * 2;
			const size_t endIndex = GetGroupEndIndex(group) * 2;
			outLeft = g_scrapPilotHudBaseline[startIndex];
			outTop = g_scrapPilotHudBaseline[startIndex + 1];
			for (size_t index = startIndex + 2; index + 1 < endIndex; index += 2)
			{
				outLeft = (outLeft < g_scrapPilotHudBaseline[index]) ? outLeft : g_scrapPilotHudBaseline[index];
				outTop = (outTop < g_scrapPilotHudBaseline[index + 1]) ? outTop : g_scrapPilotHudBaseline[index + 1];
			}

			return true;
		}

		bool GetHudCurrentTopLeft(HudTextGroup group, int& outLeft, int& outTop) noexcept
		{
			if (!GetHudBaselineTopLeft(group, outLeft, outTop))
			{
				return false;
			}

			const size_t groupIndex = ToIndex(group);
			outLeft += g_scrapPilotHudAppliedX[groupIndex];
			outTop += g_scrapPilotHudAppliedY[groupIndex];
			return true;
		}

		void SetHudGroupOffset(HudTextGroup group, int x, int y) noexcept
		{
			const size_t groupIndex = ToIndex(group);
			g_scrapPilotHudOffsetX[groupIndex] = x;
			g_scrapPilotHudOffsetY[groupIndex] = y;
		}

		inline ApplyScrapPilotHudOffsetFn g_applyScrapPilotHudOffsetFn = &ApplyScrapPilotHudOffset;

		OpenShimGetHudSpriteRectFn ResolveHudSpriteGetRectBridge()
		{
			static OpenShimGetHudSpriteRectFn fn = nullptr;
			static bool attempted = false;
			static bool loggedMissing = false;
			if (attempted)
			{
				return fn;
			}

			attempted = true;
			if (HMODULE module = GetModuleHandleA("winmm.dll"))
			{
				fn = reinterpret_cast<OpenShimGetHudSpriteRectFn>(
					GetProcAddress(module, "OpenShimGetHudSpriteRect"));
			}

			if (!fn && !loggedMissing)
			{
				loggedMissing = true;
				Logging::LogMessage("[EXU::ControlPanel] OpenShim HUD sprite get-rect bridge unavailable");
			}

			return fn;
		}

		OpenShimSetHudSpriteRectFn ResolveHudSpriteRectBridge()
		{
			static OpenShimSetHudSpriteRectFn fn = nullptr;
			static bool attempted = false;
			static bool loggedMissing = false;
			if (attempted)
			{
				return fn;
			}

			attempted = true;
			if (HMODULE module = GetModuleHandleA("winmm.dll"))
			{
				fn = reinterpret_cast<OpenShimSetHudSpriteRectFn>(
					GetProcAddress(module, "OpenShimSetHudSpriteRect"));
			}

			if (!fn && !loggedMissing)
			{
				loggedMissing = true;
				Logging::LogMessage("[EXU::ControlPanel] OpenShim HUD sprite rect bridge unavailable");
			}

			return fn;
		}

		OpenShimSetHudSpriteVisibleFn ResolveHudSpriteVisibleBridge()
		{
			static OpenShimSetHudSpriteVisibleFn fn = nullptr;
			static bool attempted = false;
			static bool loggedMissing = false;
			if (attempted)
			{
				return fn;
			}

			attempted = true;
			if (HMODULE module = GetModuleHandleA("winmm.dll"))
			{
				fn = reinterpret_cast<OpenShimSetHudSpriteVisibleFn>(
					GetProcAddress(module, "OpenShimSetHudSpriteVisible"));
			}

			if (!fn && !loggedMissing)
			{
				loggedMissing = true;
				Logging::LogMessage("[EXU::ControlPanel] OpenShim HUD sprite visibility bridge unavailable");
			}

			return fn;
		}

		OpenShimRestoreHudSpriteFn ResolveRestoreHudSpriteBridge()
		{
			static OpenShimRestoreHudSpriteFn fn = nullptr;
			static bool attempted = false;
			static bool loggedMissing = false;
			if (attempted)
			{
				return fn;
			}

			attempted = true;
			if (HMODULE module = GetModuleHandleA("winmm.dll"))
			{
				fn = reinterpret_cast<OpenShimRestoreHudSpriteFn>(
					GetProcAddress(module, "OpenShimRestoreHudSprite"));
			}

			if (!fn && !loggedMissing)
			{
				loggedMissing = true;
				Logging::LogMessage("[EXU::ControlPanel] OpenShim HUD sprite restore bridge unavailable");
			}

			return fn;
		}

		OpenShimRestoreAllHudSpritesFn ResolveRestoreAllHudSpritesBridge()
		{
			static OpenShimRestoreAllHudSpritesFn fn = nullptr;
			static bool attempted = false;
			static bool loggedMissing = false;
			if (attempted)
			{
				return fn;
			}

			attempted = true;
			if (HMODULE module = GetModuleHandleA("winmm.dll"))
			{
				fn = reinterpret_cast<OpenShimRestoreAllHudSpritesFn>(
					GetProcAddress(module, "OpenShimRestoreAllHudSprites"));
			}

			if (!fn && !loggedMissing)
			{
				loggedMissing = true;
				Logging::LogMessage("[EXU::ControlPanel] OpenShim HUD sprite restore-all bridge unavailable");
			}

			return fn;
		}

		static void __declspec(naked) ScrapPilotHudDrawHook()
		{
			__asm
			{
				mov ecx, dword ptr [g_hudPaletteSelectorThis]
				call dword ptr [g_hudPaletteSelector]
				mov byte ptr [ebp-0x19], al

				pushfd
				pushad
				call dword ptr [g_applyScrapPilotHudOffsetFn]
				popad
				popfd

				ret
			}
		}

		static void __declspec(naked) ScrapLabelColorHook()
		{
			__asm
			{
				pop ecx
				mov eax, dword ptr [g_scrapHudColor]
				push eax
				push ecx
				ret
			}
		}

		static void __declspec(naked) ScrapValueColorHook()
		{
			__asm
			{
				pop ecx
				mov eax, dword ptr [g_scrapHudColor]
				push eax
				push ecx
				ret
			}
		}

		static void __declspec(naked) PilotLabelColorHook()
		{
			__asm
			{
				pop ecx
				mov edx, dword ptr [g_pilotHudColor]
				push edx
				push ecx
				ret
			}
		}

		static void __declspec(naked) PilotValueColorHook()
		{
			__asm
			{
				pop ecx
				mov edx, dword ptr [g_pilotHudColor]
				push edx
				push ecx
				ret
			}
		}

		inline Hook g_scrapPilotHudDrawHook(
			kScrapPilotHudDrawHookAddress,
			&ScrapPilotHudDrawHook,
			kScrapPilotHudDrawHookLength,
			BasicPatch::Status::ACTIVE);
		inline Hook g_scrapLabelColorHook(
			kScrapLabelColorHookAddress,
			&ScrapLabelColorHook,
			kScrapHudTextColorHookLength,
			BasicPatch::Status::ACTIVE);
		inline Hook g_scrapValueColorHook(
			kScrapValueColorHookAddress,
			&ScrapValueColorHook,
			kPilotHudTextColorHookLength,
			BasicPatch::Status::ACTIVE);
		inline Hook g_pilotLabelColorHook(
			kPilotLabelColorHookAddress,
			&PilotLabelColorHook,
			kPilotHudTextColorHookLength,
			BasicPatch::Status::ACTIVE);
		inline Hook g_pilotValueColorHook(
			kPilotValueColorHookAddress,
			&PilotValueColorHook,
			kPilotHudTextColorHookLength,
			BasicPatch::Status::ACTIVE);
	}

	bool TryGetScrapPilotHudTopLefts(int& scrapLeft, int& scrapTop, int& pilotLeft, int& pilotTop) noexcept
	{
		return GetHudCurrentTopLeft(HudTextGroup::Scrap, scrapLeft, scrapTop) &&
			GetHudCurrentTopLeft(HudTextGroup::Pilot, pilotLeft, pilotTop);
	}

	bool RestoreScrapPilotHudTopLefts(
		int scrapLeft,
		int scrapTop,
		int pilotLeft,
		int pilotTop) noexcept
	{
		CaptureScrapPilotHudBaseline();

		int scrapBaselineLeft = 0;
		int scrapBaselineTop = 0;
		if (!GetHudBaselineTopLeft(HudTextGroup::Scrap, scrapBaselineLeft, scrapBaselineTop))
		{
			return false;
		}

		int pilotBaselineLeft = 0;
		int pilotBaselineTop = 0;
		if (!GetHudBaselineTopLeft(HudTextGroup::Pilot, pilotBaselineLeft, pilotBaselineTop))
		{
			return false;
		}

		SetHudGroupOffset(
			HudTextGroup::Scrap,
			scrapLeft - scrapBaselineLeft,
			scrapTop - scrapBaselineTop);
		SetHudGroupOffset(
			HudTextGroup::Pilot,
			pilotLeft - pilotBaselineLeft,
			pilotTop - pilotBaselineTop);
		ApplyScrapPilotHudOffset();
		return true;
	}

	int GetScrapPilotHudOffset(lua_State* L)
	{
		int baselineLeft = 0;
		int baselineTop = 0;
		int currentLeft = 0;
		int currentTop = 0;
		if (!GetScrapPilotHudBaselineTopLeft(baselineLeft, baselineTop) ||
			!GetHudCurrentTopLeft(HudTextGroup::Scrap, currentLeft, currentTop))
		{
			lua_pushnil(L);
			lua_pushnil(L);
			return 2;
		}

		int pilotLeft = 0;
		int pilotTop = 0;
		if (GetHudCurrentTopLeft(HudTextGroup::Pilot, pilotLeft, pilotTop))
		{
			currentLeft = (currentLeft < pilotLeft) ? currentLeft : pilotLeft;
			currentTop = (currentTop < pilotTop) ? currentTop : pilotTop;
		}

		lua_pushinteger(L, currentLeft - baselineLeft);
		lua_pushinteger(L, currentTop - baselineTop);
		return 2;
	}

	int SetScrapPilotHudOffset(lua_State* L)
	{
		const int offsetX = static_cast<int>(luaL_checkinteger(L, 1));
		const int offsetY = static_cast<int>(luaL_checkinteger(L, 2));
		SetHudGroupOffset(HudTextGroup::Scrap, offsetX, offsetY);
		SetHudGroupOffset(HudTextGroup::Pilot, offsetX, offsetY);

		ApplyScrapPilotHudOffset();
		Logging::LogMessage(
			"exu: scrap/pilot HUD text offset set to x=%d y=%d",
			offsetX,
			offsetY);
		return 0;
	}

	int GetScrapPilotHudTopLeft(lua_State* L)
	{
		int scrapLeft = 0;
		int scrapTop = 0;
		int pilotLeft = 0;
		int pilotTop = 0;
		if (!GetHudCurrentTopLeft(HudTextGroup::Scrap, scrapLeft, scrapTop) ||
			!GetHudCurrentTopLeft(HudTextGroup::Pilot, pilotLeft, pilotTop))
		{
			lua_pushnil(L);
			lua_pushnil(L);
			return 2;
		}

		lua_pushinteger(L, (scrapLeft < pilotLeft) ? scrapLeft : pilotLeft);
		lua_pushinteger(L, (scrapTop < pilotTop) ? scrapTop : pilotTop);
		return 2;
	}

	int SetScrapPilotHudTopLeft(lua_State* L)
	{
		const int targetLeft = static_cast<int>(luaL_checkinteger(L, 1));
		const int targetTop = static_cast<int>(luaL_checkinteger(L, 2));

		int baselineLeft = 0;
		int baselineTop = 0;
		if (!GetScrapPilotHudBaselineTopLeft(baselineLeft, baselineTop))
		{
			return luaL_error(L, "scrap/pilot HUD baseline is not available");
		}

		const int offsetX = targetLeft - baselineLeft;
		const int offsetY = targetTop - baselineTop;
		SetHudGroupOffset(HudTextGroup::Scrap, offsetX, offsetY);
		SetHudGroupOffset(HudTextGroup::Pilot, offsetX, offsetY);
		ApplyScrapPilotHudOffset();

		Logging::LogMessage(
			"exu: scrap/pilot HUD top-left set to x=%d y=%d (offset x=%d y=%d)",
			targetLeft,
			targetTop,
			offsetX,
			offsetY);
		return 0;
	}

	int GetScrapHudTopLeft(lua_State* L)
	{
		int left = 0;
		int top = 0;
		if (!GetHudCurrentTopLeft(HudTextGroup::Scrap, left, top))
		{
			lua_pushnil(L);
			lua_pushnil(L);
			return 2;
		}

		lua_pushinteger(L, left);
		lua_pushinteger(L, top);
		return 2;
	}

	int SetScrapHudTopLeft(lua_State* L)
	{
		const int targetLeft = static_cast<int>(luaL_checkinteger(L, 1));
		const int targetTop = static_cast<int>(luaL_checkinteger(L, 2));

		int baselineLeft = 0;
		int baselineTop = 0;
		if (!GetHudBaselineTopLeft(HudTextGroup::Scrap, baselineLeft, baselineTop))
		{
			return luaL_error(L, "scrap HUD baseline is not available");
		}

		SetHudGroupOffset(
			HudTextGroup::Scrap,
			targetLeft - baselineLeft,
			targetTop - baselineTop);
		ApplyScrapPilotHudOffset();

		Logging::LogMessage(
			"exu: scrap HUD top-left set to x=%d y=%d",
			targetLeft,
			targetTop);
		return 0;
	}

	int GetPilotHudTopLeft(lua_State* L)
	{
		int left = 0;
		int top = 0;
		if (!GetHudCurrentTopLeft(HudTextGroup::Pilot, left, top))
		{
			lua_pushnil(L);
			lua_pushnil(L);
			return 2;
		}

		lua_pushinteger(L, left);
		lua_pushinteger(L, top);
		return 2;
	}

	int SetPilotHudTopLeft(lua_State* L)
	{
		const int targetLeft = static_cast<int>(luaL_checkinteger(L, 1));
		const int targetTop = static_cast<int>(luaL_checkinteger(L, 2));

		int baselineLeft = 0;
		int baselineTop = 0;
		if (!GetHudBaselineTopLeft(HudTextGroup::Pilot, baselineLeft, baselineTop))
		{
			return luaL_error(L, "pilot HUD baseline is not available");
		}

		SetHudGroupOffset(
			HudTextGroup::Pilot,
			targetLeft - baselineLeft,
			targetTop - baselineTop);
		ApplyScrapPilotHudOffset();

		Logging::LogMessage(
			"exu: pilot HUD top-left set to x=%d y=%d",
			targetLeft,
			targetTop);
		return 0;
	}

	int GetScrapHudColor(lua_State* L)
	{
		lua_pushnumber(L, static_cast<lua_Number>(g_scrapHudColor));
		return 1;
	}

	int SetScrapHudColor(lua_State* L)
	{
		g_scrapHudColor = CheckHudColor(L, 1, "scrap HUD color");
		Logging::LogMessage(
			"exu: scrap HUD color set to 0x%08X",
			g_scrapHudColor);
		return 0;
	}

	int GetPilotHudColor(lua_State* L)
	{
		lua_pushnumber(L, static_cast<lua_Number>(g_pilotHudColor));
		return 1;
	}

	int SetPilotHudColor(lua_State* L)
	{
		g_pilotHudColor = CheckHudColor(L, 1, "pilot HUD color");
		Logging::LogMessage(
			"exu: pilot HUD color set to 0x%08X",
			g_pilotHudColor);
		return 0;
	}

	int GetHudSpriteRect(lua_State* L)
	{
		const char* spriteName = luaL_checkstring(L, 1);
		if (spriteName == nullptr || spriteName[0] == '\0')
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: sprite name must not be empty");
		}

		int x = 0;
		int y = 0;
		int w = 0;
		int h = 0;
		if (OpenShimGetHudSpriteRectFn fn = ResolveHudSpriteGetRectBridge())
		{
			if (fn(spriteName, &x, &y, &w, &h))
			{
				lua_pushinteger(L, x);
				lua_pushinteger(L, y);
				lua_pushinteger(L, w);
				lua_pushinteger(L, h);
				return 4;
			}
		}

		lua_pushnil(L);
		lua_pushnil(L);
		lua_pushnil(L);
		lua_pushnil(L);
		return 4;
	}

	int SetHudSpriteRect(lua_State* L)
	{
		const char* spriteName = luaL_checkstring(L, 1);
		const int x = static_cast<int>(luaL_checkinteger(L, 2));
		const int y = static_cast<int>(luaL_checkinteger(L, 3));
		const int w = static_cast<int>(luaL_checkinteger(L, 4));
		const int h = static_cast<int>(luaL_checkinteger(L, 5));
		if (spriteName == nullptr || spriteName[0] == '\0')
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: sprite name must not be empty");
		}

		if (OpenShimSetHudSpriteRectFn fn = ResolveHudSpriteRectBridge())
		{
			lua_pushboolean(L, fn(spriteName, x, y, w, h) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int SetHudSpriteVisible(lua_State* L)
	{
		const char* spriteName = luaL_checkstring(L, 1);
		const bool visible = lua_toboolean(L, 2) != 0;
		if (spriteName == nullptr || spriteName[0] == '\0')
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: sprite name must not be empty");
		}

		if (OpenShimSetHudSpriteVisibleFn fn = ResolveHudSpriteVisibleBridge())
		{
			lua_pushboolean(L, fn(spriteName, visible ? TRUE : FALSE) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int RestoreHudSprite(lua_State* L)
	{
		const char* spriteName = luaL_checkstring(L, 1);
		if (spriteName == nullptr || spriteName[0] == '\0')
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: sprite name must not be empty");
		}

		if (OpenShimRestoreHudSpriteFn fn = ResolveRestoreHudSpriteBridge())
		{
			lua_pushboolean(L, fn(spriteName) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int RestoreAllHudSprites(lua_State* L)
	{
		if (OpenShimRestoreAllHudSpritesFn fn = ResolveRestoreAllHudSpritesBridge())
		{
			lua_pushboolean(L, fn() ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}

	int SelectAdd(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		BZR::ControlPanel::SelectAdd(controlPanel, obj);
		return 0;
	}

	int SelectNone(lua_State*)
	{
		BZR::ControlPanel::SelectNone(controlPanel);
		return 0;
	}

	int SelectOne(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		BZR::ControlPanel::SelectOne(controlPanel, obj);
		return 0;
	}
}
