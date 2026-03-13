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

#include "OS.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma push_macro("MessageBox")
#undef MessageBox

namespace ExtraUtilities::Lua::OS
{
	namespace
	{
		using NativeSaveGameFn = bool(__cdecl*)(char*, int);
		using NativeSaveShellGameFn = bool(__cdecl*)(int, const char*);

		struct ExecutableSection
		{
			const uint8_t* address = nullptr;
			size_t size = 0;
			std::string name;
		};

		constexpr std::array<int, 54> SAVE_GAME_SIGNATURE = {
			0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x94, 0x00, 0x00, 0x00, 0xC6, 0x45, 0xFF, 0x01, 0xE8, -1, -1,
			-1, -1, 0x89, 0x85, 0x78, 0xFF, 0xFF, 0xFF, 0x68, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1,
			0x83, 0xC4, 0x04, 0x0F, 0xB6, 0x05, -1, -1, -1, -1, 0x85, 0xC0, 0x0F, 0x84, -1, -1, -1, -1,
			0x6A, 0x2E
		};

		std::mutex g_nativeSaveLogMutex;

		template <typename... Args>
		void LogNativeSave(std::format_string<Args...> fmt, Args&&... args)
		{
			const auto message = std::format(fmt, std::forward<Args>(args)...);
			std::lock_guard<std::mutex> lock(g_nativeSaveLogMutex);

			OutputDebugStringA(message.c_str());
			OutputDebugStringA("\n");

			std::ofstream file("exu_native_save.log", std::ios::app);
			if (file.is_open())
			{
				file << message << '\n';
			}
		}

		std::string GetMainModuleDirectory()
		{
			char path[MAX_PATH]{};
			const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
			if (length == 0 || length >= MAX_PATH)
			{
				return {};
			}

			std::string result(path, length);
			const auto slash = result.find_last_of("\\/");
			if (slash == std::string::npos)
			{
				return {};
			}

			result.resize(slash);
			return result;
		}

		std::string BuildSlotSavePath(int slot)
		{
			const auto moduleDirectory = GetMainModuleDirectory();
			if (moduleDirectory.empty())
			{
				return std::format("Save\\game{}.sav", slot);
			}

			return std::format("{}\\Save\\game{}.sav", moduleDirectory, slot);
		}

		std::string NormalizeSavePath(std::string path)
		{
			std::filesystem::path normalized(path);
			if (normalized.is_relative())
			{
				const auto moduleDirectory = GetMainModuleDirectory();
				if (!moduleDirectory.empty())
				{
					normalized = std::filesystem::path(moduleDirectory) / normalized;
				}
			}

			return normalized.lexically_normal().string();
		}

		bool EnsureSaveParentDirectory(const std::string& filename)
		{
			const auto parent = std::filesystem::path(filename).parent_path();
			if (parent.empty())
			{
				return true;
			}

			std::error_code error;
			std::filesystem::create_directories(parent, error);
			return !error;
		}

		std::string TrimAsciiWhitespace(std::string_view value)
		{
			size_t start = 0;
			size_t end = value.size();

			while (start < end)
			{
				const char c = value[start];
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
				{
					break;
				}
				++start;
			}

			while (end > start)
			{
				const char c = value[end - 1];
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
				{
					break;
				}
				--end;
			}

			return std::string(value.substr(start, end - start));
		}

		std::vector<ExecutableSection> GetExecutableSections()
		{
			std::vector<ExecutableSection> sections;

			HMODULE module = GetModuleHandleA("Battlezone98Redux.exe");
			if (module == nullptr)
			{
				module = GetModuleHandleA(nullptr);
			}

			if (module == nullptr)
			{
				return sections;
			}

			auto* const base = reinterpret_cast<const uint8_t*>(module);
			auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			{
				return sections;
			}

			auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE)
			{
				return sections;
			}

			auto* section = IMAGE_FIRST_SECTION(nt);
			for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
			{
				if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
				{
					continue;
				}

				const size_t size = std::max<size_t>(section->Misc.VirtualSize, section->SizeOfRawData);
				if (size == 0)
				{
					continue;
				}

				char sectionName[9]{};
				std::memcpy(sectionName, section->Name, sizeof(section->Name));

				sections.push_back({
					base + section->VirtualAddress,
					size,
					sectionName
				});
			}

			return sections;
		}

		const uint8_t* FindPattern(const uint8_t* start, size_t size, const auto& pattern)
		{
			if (start == nullptr || size < pattern.size())
			{
				return nullptr;
			}

			const size_t lastOffset = size - pattern.size();
			for (size_t offset = 0; offset <= lastOffset; ++offset)
			{
				bool matched = true;
				for (size_t index = 0; index < pattern.size(); ++index)
				{
					const int expected = pattern[index];
					if (expected >= 0 && start[offset + index] != static_cast<uint8_t>(expected))
					{
						matched = false;
						break;
					}
				}

				if (matched)
				{
					return start + offset;
				}
			}

			return nullptr;
		}

		NativeSaveGameFn ResolveNativeSaveGame()
		{
			static NativeSaveGameFn cached = nullptr;
			if (cached != nullptr)
			{
				return cached;
			}

			const auto executableSections = GetExecutableSections();
			if (executableSections.empty())
			{
				LogNativeSave("[EXU::SaveGame] failed to enumerate executable sections");
				return nullptr;
			}

			for (const auto& section : executableSections)
			{
				if (const auto* address = FindPattern(section.address, section.size, SAVE_GAME_SIGNATURE))
				{
					LogNativeSave(
						"[EXU::SaveGame] resolved native SaveGame at {} in section {}",
						static_cast<const void*>(address),
						section.name
					);
					cached = reinterpret_cast<NativeSaveGameFn>(const_cast<uint8_t*>(address));
					return cached;
				}
			}

			LogNativeSave("[EXU::SaveGame] failed to resolve native SaveGame signature");
			return nullptr;
		}

		const uint8_t* BacktrackFunctionProlog(const ExecutableSection& section, const uint8_t* address, size_t maxBack = 0x400)
		{
			if (section.address == nullptr || address == nullptr || address < section.address || address >= section.address + section.size)
			{
				return nullptr;
			}

			const uint8_t* cursor = address;
			const uint8_t* lowerBound = (address > section.address + maxBack) ? address - maxBack : section.address;
			while (cursor >= lowerBound + 3)
			{
				if (cursor[0] == 0x55 && cursor[1] == 0x8B && cursor[2] == 0xEC)
				{
					return cursor;
				}
				--cursor;
			}

			return nullptr;
		}

		bool ContainsBytePattern(const uint8_t* start, size_t size, const auto& pattern)
		{
			return FindPattern(start, size, pattern) != nullptr;
		}

		bool IsSaveShellGameCandidate(const uint8_t* functionStart, const uint8_t* saveGameAddress)
		{
			if (functionStart == nullptr || saveGameAddress == nullptr)
			{
				return false;
			}

			constexpr size_t kWindowSize = 0x140;
			constexpr std::array<int, 20> SLOT_RANGE_PATTERN = {
				0x83, 0x7D, 0x08, 0x01, 0x0F, 0x8C, -1, -1, -1, -1,
				0x83, 0x7D, 0x08, 0x0A, 0x0F, 0x8F, -1, -1, -1, -1
			};

			constexpr std::array<int, 7> DESCRIPTION_BUFFER_PATTERN = {
				0xC7, 0x45, -1, 0xD8, 0x86, 0x8E, 0x00
			};

			if (!ContainsBytePattern(functionStart, kWindowSize, SLOT_RANGE_PATTERN))
			{
				return false;
			}

			if (!ContainsBytePattern(functionStart, 0x40, DESCRIPTION_BUFFER_PATTERN))
			{
				return false;
			}

			for (size_t offset = 0; offset + 5 <= kWindowSize; ++offset)
			{
				if (functionStart[offset] != 0xE8)
				{
					continue;
				}

				int32_t displacement = 0;
				std::memcpy(&displacement, functionStart + offset + 1, sizeof(displacement));
				const auto* target = functionStart + offset + 5 + displacement;
				if (target == saveGameAddress)
				{
					return true;
				}
			}

			return false;
		}

		NativeSaveShellGameFn ResolveNativeSaveShellGame()
		{
			static NativeSaveShellGameFn cached = nullptr;
			if (cached != nullptr)
			{
				return cached;
			}

			const auto executableSections = GetExecutableSections();
			if (executableSections.empty())
			{
				LogNativeSave("[EXU::SaveGame] failed to enumerate executable sections for SaveShellGame");
				return nullptr;
			}

			const auto saveGame = ResolveNativeSaveGame();
			if (saveGame == nullptr)
			{
				LogNativeSave("[EXU::SaveGame] cannot resolve SaveShellGame without SaveGame");
				return nullptr;
			}

			const auto* saveGameAddress = reinterpret_cast<const uint8_t*>(saveGame);
			for (const auto& section : executableSections)
			{
				if (section.address == nullptr || section.size < 5)
				{
					continue;
				}

				for (size_t offset = 0; offset + 5 <= section.size; ++offset)
				{
					if (section.address[offset] != 0xE8)
					{
						continue;
					}

					int32_t displacement = 0;
					std::memcpy(&displacement, section.address + offset + 1, sizeof(displacement));
					const auto* target = section.address + offset + 5 + displacement;
					if (target != saveGameAddress)
					{
						continue;
					}

					const auto* callSite = section.address + offset;
					const auto* functionStart = BacktrackFunctionProlog(section, callSite);
					if (!IsSaveShellGameCandidate(functionStart, saveGameAddress))
					{
						continue;
					}

					LogNativeSave(
						"[EXU::SaveGame] resolved native SaveShellGame at {} in section {} via call {}",
						static_cast<const void*>(functionStart),
						section.name,
						static_cast<const void*>(callSite)
					);
					cached = reinterpret_cast<NativeSaveShellGameFn>(const_cast<uint8_t*>(functionStart));
					return cached;
				}
			}

			LogNativeSave("[EXU::SaveGame] failed to resolve native SaveShellGame signature");
			return nullptr;
		}

		bool InvokeNativeSaveGame(NativeSaveGameFn saveGame, char* filename, int saveType, DWORD& exceptionCode) noexcept
		{
			exceptionCode = 0;

			__try
			{
				return saveGame(filename, saveType);
			}
			__except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool InvokeNativeSaveShellGame(
			NativeSaveShellGameFn saveShellGame,
			int slot,
			const char* description,
			DWORD& exceptionCode) noexcept
		{
			exceptionCode = 0;

			__try
			{
				return saveShellGame(slot, description);
			}
			__except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
	}

	int MessageBox(lua_State* L)
	{
		lua_getglobal(L, "tostring");
		lua_pushvalue(L, 1); // string parameter
		lua_call(L, 1, 1);
		const char* message = lua_tostring(L, -1);
		MessageBoxA(0, message, "Extra Utilities", MB_OK | MB_APPLMODAL);
		return 0;
	}

	int GetScreenResolution(lua_State* L)
	{
		int width = GetSystemMetrics(SM_CXSCREEN);
		int height = GetSystemMetrics(SM_CYSCREEN);

		lua_pushnumber(L, width);
		lua_pushnumber(L, height);

		return 2;
	}

	int SaveGame(lua_State* L)
	{
		std::string filename;
		int slot = 0;
		if (lua_type(L, 1) == LUA_TNUMBER)
		{
			slot = static_cast<int>(luaL_checkinteger(L, 1));
			if (slot < 1 || slot > 10)
			{
				return luaL_error(L, "SaveGame slot must be in range 1-10");
			}

			filename = BuildSlotSavePath(slot);
		}
		else
		{
			size_t length = 0;
			const char* rawPath = luaL_checklstring(L, 1, &length);
			filename = NormalizeSavePath(std::string(rawPath, length));
		}

		if (filename.empty())
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "save filename resolved to an empty path");
			return 2;
		}

		int saveType = 0;
		std::string description;
		const int top = lua_gettop(L);
		if (top >= 2)
		{
			const int arg2Type = lua_type(L, 2);
			if (arg2Type == LUA_TNUMBER)
			{
				saveType = static_cast<int>(lua_tointeger(L, 2));
			}
			else if (arg2Type == LUA_TSTRING)
			{
				size_t length = 0;
				const char* rawDescription = lua_tolstring(L, 2, &length);
				description.assign(rawDescription, length);
			}
			else if (arg2Type != LUA_TNIL && arg2Type != LUA_TNONE)
			{
				return luaL_error(L, "SaveGame second argument must be a saveType number or description string");
			}
		}

		if (top >= 3)
		{
			const int arg3Type = lua_type(L, 3);
			if (arg3Type == LUA_TSTRING)
			{
				size_t length = 0;
				const char* rawDescription = lua_tolstring(L, 3, &length);
				description.assign(rawDescription, length);
			}
			else if (arg3Type != LUA_TNIL && arg3Type != LUA_TNONE)
			{
				return luaL_error(L, "SaveGame third argument must be a description string");
			}
		}

		description = TrimAsciiWhitespace(description);

		if (slot != 0 && !description.empty())
		{
			const auto saveShellGame = ResolveNativeSaveShellGame();
			if (saveShellGame != nullptr)
			{
				if (!EnsureSaveParentDirectory(filename))
				{
					LogNativeSave("[EXU::SaveGame] failed to create parent directory for {}", filename);
					lua_pushboolean(L, 0);
					lua_pushfstring(L, "failed to create parent directory for %s", filename.c_str());
					return 2;
				}

				LogNativeSave("[EXU::SaveGame] calling native SaveShellGame slot={} description={}", slot, description);

				DWORD exceptionCode = 0;
				const bool saved = InvokeNativeSaveShellGame(saveShellGame, slot, description.c_str(), exceptionCode);
				if (exceptionCode != 0)
				{
					LogNativeSave(
						"[EXU::SaveGame] native SaveShellGame crashed slot={} code=0x{:08X}",
						slot,
						exceptionCode
					);
					lua_pushboolean(L, 0);
					lua_pushfstring(L, "native SaveShellGame crashed with exception 0x%08X", exceptionCode);
					return 2;
				}

				LogNativeSave(
					"[EXU::SaveGame] native SaveShellGame result={} slot={} path={}",
					saved ? 1 : 0,
					slot,
					filename
				);

				lua_pushboolean(L, saved ? 1 : 0);
				if (saved)
				{
					lua_pushlstring(L, filename.c_str(), filename.size());
				}
				else
				{
					lua_pushfstring(L, "native SaveShellGame returned false for slot %d", slot);
				}

				return 2;
			}

			LogNativeSave(
				"[EXU::SaveGame] native SaveShellGame unavailable for slot={} description={}, falling back to SaveGame",
				slot,
				description
			);
		}

		const auto saveGame = ResolveNativeSaveGame();
		if (saveGame == nullptr)
		{
			lua_pushboolean(L, 0);
			lua_pushstring(L, "native SaveGame routine could not be resolved");
			return 2;
		}

		if (!EnsureSaveParentDirectory(filename))
		{
			LogNativeSave("[EXU::SaveGame] failed to create parent directory for {}", filename);
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "failed to create parent directory for %s", filename.c_str());
			return 2;
		}

		LogNativeSave("[EXU::SaveGame] calling native save path={} type={}", filename, saveType);

		DWORD exceptionCode = 0;
		const bool saved = InvokeNativeSaveGame(saveGame, filename.data(), saveType, exceptionCode);
		if (exceptionCode != 0)
		{
			LogNativeSave(
				"[EXU::SaveGame] native save crashed path={} type={} code=0x{:08X}",
				filename,
				saveType,
				exceptionCode
			);
			lua_pushboolean(L, 0);
			lua_pushfstring(L, "native save crashed with exception 0x%08X", exceptionCode);
			return 2;
		}

		LogNativeSave("[EXU::SaveGame] native save result={} path={} type={}", saved ? 1 : 0, filename, saveType);

		lua_pushboolean(L, saved ? 1 : 0);
		if (saved)
		{
			lua_pushlstring(L, filename.c_str(), filename.size());
		}
		else
		{
			lua_pushfstring(L, "native SaveGame returned false for %s", filename.c_str());
		}

		return 2;
	}
}

#pragma pop_macro("MessageBox")
