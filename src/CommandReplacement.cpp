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

#include "CommandReplacement.h"

#include "Hook.h"
#include "Logging.h"
#include "LuaHelpers.h"
#include "LuaState.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ExtraUtilities::Lua::CommandReplacement
{
	namespace
	{
		constexpr int kCmdNone = 0;
		constexpr int kCmdHunt = 20;

		enum class StockCommandId : uint8_t
		{
			NO_ACTION,
			GO_TO_GEYSER,
			GO_TO_NAV,
			HUNT,
			SCAVENGE,
			BUILD,
			RECYCLE,
			ATTACK,
			DEFEND,
			FOLLOW_CLOSE,
			FOLLOW,
			SELECT,
			CANCEL,
			BACK,
			DECLOAK,
			CLOAK
		};

		struct StockCommandInfo
		{
			StockCommandId id;
			const char* normalized;
			const char* display;
		};

		struct ReplacementEntry
		{
			std::string replacementLabel;
			int callbackRef = LUA_NOREF;
			int lastObservedCommand = -1;
		};

		struct ModuleSection
		{
			uint8_t* address = nullptr;
			size_t size = 0;
		};

		struct ExecutableSection
		{
			const uint8_t* address = nullptr;
			size_t size = 0;
			std::string name;
		};

		using SetCommandIntFn = void(__thiscall*)(BZR::GameObject*, int);

		constexpr std::array<int, 44> WINGMAN_HUNT_ACTIVATION_SIGNATURE = {
			0x83, 0x7D, 0x08, 0x0D, 0x75, 0x10, 0x6A, 0x14, 0x8B, 0x4D,
			0xFC, 0xE8, -1, -1, -1, -1, 0xB0, 0x01, 0xEB, -1,
			0xEB, -1, 0x83, 0x7D, 0x08, 0x10, 0x75, -1, 0xA1, -1,
			-1, -1, -1, 0x50, 0x6A, 0x06, 0x8B, 0x4D, 0xFC, 0xE8,
			-1, -1, -1, -1
		};

		inline constexpr std::array STOCK_COMMANDS{
			StockCommandInfo{ StockCommandId::NO_ACTION, "noaction", "No Action" },
			StockCommandInfo{ StockCommandId::GO_TO_GEYSER, "gotogeyser", "Go To Geyser" },
			StockCommandInfo{ StockCommandId::GO_TO_NAV, "gotonav", "Go To Nav" },
			StockCommandInfo{ StockCommandId::HUNT, "hunt", "Hunt" },
			StockCommandInfo{ StockCommandId::SCAVENGE, "scavenge", "Scavenge" },
			StockCommandInfo{ StockCommandId::BUILD, "build", "Build" },
			StockCommandInfo{ StockCommandId::RECYCLE, "recycle", "Recycle" },
			StockCommandInfo{ StockCommandId::ATTACK, "attack", "Attack" },
			StockCommandInfo{ StockCommandId::DEFEND, "defend", "Defend" },
			StockCommandInfo{ StockCommandId::FOLLOW_CLOSE, "followclose", "Follow Close" },
			StockCommandInfo{ StockCommandId::FOLLOW, "follow", "Follow" },
			StockCommandInfo{ StockCommandId::SELECT, "select", "Select" },
			StockCommandInfo{ StockCommandId::CANCEL, "cancel", "Cancel" },
			StockCommandInfo{ StockCommandId::BACK, "back", "Back" },
			StockCommandInfo{ StockCommandId::DECLOAK, "decloak", "Decloak" },
			StockCommandInfo{ StockCommandId::CLOAK, "cloak", "Cloak" }
		};

		lua_State* g_ownerState = nullptr;
		std::unordered_map<uint64_t, ReplacementEntry> g_replacements;
		std::array<char, 32> g_huntLabelBuffer{};
		double g_lastUpdateAt = -1.0;
		const char** g_huntLabelPointer = nullptr;
		const char* g_stockHuntLabel = nullptr;
		bool g_huntLabelResolutionAttempted = false;
		uintptr_t g_wingmanHuntActivationHookAddress = 0;
		uintptr_t g_wingmanHuntActivationResumeAddress = 0;
		SetCommandIntFn g_wingmanHuntSetCommand = nullptr;
		bool g_lastWingmanHuntHandled = false;

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

			const auto* base = reinterpret_cast<const uint8_t*>(module);
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE)
			{
				return sections;
			}

			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
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

		uintptr_t ResolveRelativeCallTarget(uintptr_t callSite) noexcept
		{
			if (callSite == 0)
			{
				return 0;
			}

			const auto* callInstruction = reinterpret_cast<const uint8_t*>(callSite);
			if (callInstruction[0] != 0xE8)
			{
				return 0;
			}

			int32_t displacement = 0;
			std::memcpy(&displacement, callInstruction + 1, sizeof(displacement));
			return callSite + 5 + static_cast<intptr_t>(displacement);
		}

		std::string NormalizeStockCommandName(std::string_view input)
		{
			std::string normalized;
			normalized.reserve(input.size());

			for (char c : input)
			{
				const unsigned char uc = static_cast<unsigned char>(c);
				if (std::isalnum(uc) != 0)
				{
					normalized.push_back(static_cast<char>(std::tolower(uc)));
				}
			}

			return normalized;
		}

		std::optional<StockCommandInfo> FindStockCommand(std::string_view name)
		{
			const std::string normalized = NormalizeStockCommandName(name);
			for (const StockCommandInfo& info : STOCK_COMMANDS)
			{
				if (normalized == info.normalized)
				{
					return info;
				}
			}

			return std::nullopt;
		}

		uint64_t MakeReplacementKey(BZR::handle handle, StockCommandId stockCommand)
		{
			return (static_cast<uint64_t>(handle) << 32) |
				static_cast<uint64_t>(stockCommand);
		}

		void ReleaseEntry(lua_State* L, ReplacementEntry& entry)
		{
			if (L != nullptr && entry.callbackRef != LUA_NOREF)
			{
				luaL_unref(L, LUA_REGISTRYINDEX, entry.callbackRef);
			}

			entry.callbackRef = LUA_NOREF;
		}

		std::unordered_map<uint64_t, ReplacementEntry>::iterator FindReplacement(
			BZR::handle handle,
			StockCommandId stockCommand)
		{
			return g_replacements.find(MakeReplacementKey(handle, stockCommand));
		}

		std::optional<ModuleSection> FindMainModuleSection(std::string_view sectionName) noexcept
		{
			const HMODULE module = GetModuleHandleA(nullptr);
			if (module == nullptr)
			{
				return std::nullopt;
			}

			const auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
			if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
			{
				return std::nullopt;
			}

			const auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
				reinterpret_cast<const uint8_t*>(module) + dosHeader->e_lfanew);
			if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
			{
				return std::nullopt;
			}

			const auto* section = IMAGE_FIRST_SECTION(ntHeaders);
			for (uint16_t index = 0; index < ntHeaders->FileHeader.NumberOfSections; ++index, ++section)
			{
				const std::string_view currentName(
					reinterpret_cast<const char*>(section->Name),
					strnlen_s(reinterpret_cast<const char*>(section->Name), IMAGE_SIZEOF_SHORT_NAME));

				if (currentName != sectionName)
				{
					continue;
				}

				return ModuleSection{
					reinterpret_cast<uint8_t*>(module) + section->VirtualAddress,
					static_cast<size_t>(section->Misc.VirtualSize)
				};
			}

			return std::nullopt;
		}

		const char* FindCStringInSection(const ModuleSection& section, std::string_view value) noexcept
		{
			const size_t required = value.size() + 1;
			if (section.address == nullptr || section.size < required)
			{
				return nullptr;
			}

			for (size_t offset = 0; offset + required <= section.size; ++offset)
			{
				const auto* candidate = reinterpret_cast<const char*>(section.address + offset);
				if (std::memcmp(candidate, value.data(), value.size()) == 0 && candidate[value.size()] == '\0')
				{
					return candidate;
				}
			}

			return nullptr;
		}

		const char** FindHuntLabelPointer(const ModuleSection& dataSection) noexcept
		{
			const auto rdataSection = FindMainModuleSection(".rdata");
			if (!rdataSection.has_value())
			{
				return nullptr;
			}

			const char* geyserLabel = FindCStringInSection(*rdataSection, "Go To Geyser");
			const char* navLabel = FindCStringInSection(*rdataSection, "Go To Nav");
			const char* huntLabel = FindCStringInSection(*rdataSection, "Hunt");
			const char* scavengeLabel = FindCStringInSection(*rdataSection, "Scavenge");
			if (geyserLabel == nullptr || navLabel == nullptr || huntLabel == nullptr || scavengeLabel == nullptr)
			{
				return nullptr;
			}

			const uintptr_t sequence[]{
				reinterpret_cast<uintptr_t>(geyserLabel),
				reinterpret_cast<uintptr_t>(navLabel),
				reinterpret_cast<uintptr_t>(huntLabel),
				reinterpret_cast<uintptr_t>(scavengeLabel)
			};

			const size_t required = sizeof(sequence);
			if (dataSection.address == nullptr || dataSection.size < required)
			{
				return nullptr;
			}

			for (size_t offset = 0; offset + required <= dataSection.size; ++offset)
			{
				if (std::memcmp(dataSection.address + offset, sequence, required) == 0)
				{
					return reinterpret_cast<const char**>(dataSection.address + offset + (sizeof(uintptr_t) * 2));
				}
			}

			return nullptr;
		}

		bool EnsureHuntLabelPointerResolved() noexcept
		{
			if (g_huntLabelPointer != nullptr)
			{
				return true;
			}

			if (g_huntLabelResolutionAttempted)
			{
				return false;
			}

			g_huntLabelResolutionAttempted = true;

			const auto dataSection = FindMainModuleSection(".data");
			if (!dataSection.has_value())
			{
				Logging::LogMessage("exu: failed to resolve Hunt label pointer (.data not found)");
				return false;
			}

			g_huntLabelPointer = FindHuntLabelPointer(*dataSection);
			if (g_huntLabelPointer == nullptr)
			{
				Logging::LogMessage("exu: failed to resolve Hunt label pointer (command label table signature not found)");
				return false;
			}

			g_stockHuntLabel = *g_huntLabelPointer;
			Logging::LogMessage("exu: resolved Hunt label pointer at %p", g_huntLabelPointer);
			return g_stockHuntLabel != nullptr;
		}

		bool WriteHuntLabelPointer(const char* label) noexcept
		{
			if (!EnsureHuntLabelPointerResolved())
			{
				return false;
			}

			DWORD oldProtect = 0;
			if (!VirtualProtect(g_huntLabelPointer, sizeof(*g_huntLabelPointer), PAGE_EXECUTE_READWRITE, &oldProtect))
			{
				return false;
			}

			*g_huntLabelPointer = label;

			DWORD dummyProtect = 0;
			VirtualProtect(g_huntLabelPointer, sizeof(*g_huntLabelPointer), oldProtect, &dummyProtect);
			return true;
		}

		void RestoreStockHuntLabel() noexcept
		{
			if (g_stockHuntLabel != nullptr)
			{
				WriteHuntLabelPointer(g_stockHuntLabel);
			}
		}

		void ApplyHuntLabelOverride(std::string_view label) noexcept
		{
			const size_t copyLen = (std::min)(label.size(), g_huntLabelBuffer.size() - 1);
			std::memcpy(g_huntLabelBuffer.data(), label.data(), copyLen);
			g_huntLabelBuffer[copyLen] = '\0';
			WriteHuntLabelPointer(g_huntLabelBuffer.data());
		}

		bool CallGlobalHandleInt(lua_State* L, const char* fnName, const char* errorContext, BZR::handle handle, int& outValue)
		{
			StackGuard guard(L);
			lua_getglobal(L, fnName);
			if (!lua_isfunction(L, -1))
			{
				return false;
			}

			lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
			const int status = lua_pcall(L, 1, 1, 0);
			if (status != 0)
			{
				LuaCheckStatus(status, L, errorContext);
				return false;
			}

			if (!lua_isnumber(L, -1))
			{
				return false;
			}

			outValue = static_cast<int>(lua_tointeger(L, -1));
			return true;
		}

		bool CallGlobalHandleBool(lua_State* L, const char* fnName, const char* errorContext, BZR::handle handle, bool& outValue)
		{
			StackGuard guard(L);
			lua_getglobal(L, fnName);
			if (!lua_isfunction(L, -1))
			{
				return false;
			}

			lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
			const int status = lua_pcall(L, 1, 1, 0);
			if (status != 0)
			{
				LuaCheckStatus(status, L, errorContext);
				return false;
			}

			if (!lua_isboolean(L, -1))
			{
				return false;
			}

			outValue = lua_toboolean(L, -1) != 0;
			return true;
		}

		bool TryGetCurrentCommand(lua_State* L, BZR::handle handle, int& outCommand)
		{
			return CallGlobalHandleInt(
				L,
				"GetCurrentCommand",
				"Extra Utilities command replacement GetCurrentCommand error:\n%s",
				handle,
				outCommand
			);
		}

		bool TryIsSelected(lua_State* L, BZR::handle handle, bool& outSelected)
		{
			return CallGlobalHandleBool(
				L,
				"IsSelected",
				"Extra Utilities command replacement IsSelected error:\n%s",
				handle,
				outSelected
			);
		}

		void TrySetCommandNone(lua_State* L, BZR::handle handle)
		{
			StackGuard guard(L);
			lua_getglobal(L, "SetCommand");
			if (!lua_isfunction(L, -1))
			{
				return;
			}

			lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
			lua_pushinteger(L, kCmdNone);
			lua_pushinteger(L, 0);

			const int status = lua_pcall(L, 3, 0, 0);
			if (status != 0)
			{
				LuaCheckStatus(status, L, "Extra Utilities command replacement SetCommand error:\n%s");
			}
		}

		double TryGetNow(lua_State* L)
		{
			StackGuard guard(L);
			lua_getglobal(L, "GetTime");
			if (!lua_isfunction(L, -1))
			{
				return -1.0;
			}

			const int status = lua_pcall(L, 0, 1, 0);
			if (status != 0)
			{
				LuaCheckStatus(status, L, "Extra Utilities command replacement GetTime error:\n%s");
				return -1.0;
			}

			if (!lua_isnumber(L, -1))
			{
				return -1.0;
			}

			return lua_tonumber(L, -1);
		}

		bool TryHandleWingmanHuntActivation(BZR::GameObject* wingman)
		{
			if (wingman == nullptr)
			{
				return false;
			}

			const BZR::handle handle = BZR::GameObject::GetHandle(wingman);
			auto entryIt = FindReplacement(handle, StockCommandId::HUNT);
			if (entryIt == g_replacements.end())
			{
				return false;
			}

			lua_State* L = g_ownerState != nullptr ? g_ownerState : Lua::state;
			if (L == nullptr)
			{
				return false;
			}

			bool isSelected = false;
			if (!TryIsSelected(L, handle, isSelected) || !isSelected)
			{
				return false;
			}

			const bool handled = DispatchRegisteredReplacement(handle, "Hunt", "native_set_active_mode");
			entryIt = FindReplacement(handle, StockCommandId::HUNT);
			if (entryIt != g_replacements.end())
			{
				entryIt->second.lastObservedCommand = handled ? kCmdNone : kCmdHunt;
			}

			return handled;
		}

		void CallOriginalWingmanHuntActivation(BZR::GameObject* wingman)
		{
			if (wingman == nullptr || g_wingmanHuntSetCommand == nullptr)
			{
				return;
			}

			g_wingmanHuntSetCommand(wingman, kCmdHunt);

			const BZR::handle handle = BZR::GameObject::GetHandle(wingman);
			auto entryIt = FindReplacement(handle, StockCommandId::HUNT);
			if (entryIt != g_replacements.end())
			{
				entryIt->second.lastObservedCommand = kCmdHunt;
			}
		}

		static void __declspec(naked) WingmanHuntActivationHook()
		{
			__asm
			{
				pushad
				pushfd

				mov eax, [ebp-0x04]
				push eax
				call TryHandleWingmanHuntActivation
				add esp, 0x04
				mov byte ptr [g_lastWingmanHuntHandled], al

				popfd
				popad

				cmp byte ptr [g_lastWingmanHuntHandled], 0
				jne handled

				mov eax, [ebp-0x04]
				push eax
				call CallOriginalWingmanHuntActivation
				add esp, 0x04

			handled:
				mov eax, g_wingmanHuntActivationResumeAddress
				jmp eax
			}
		}

		uintptr_t InitializeWingmanHuntActivationHook()
		{
			const auto sections = GetExecutableSections();
			if (sections.empty())
			{
				Logging::LogMessage("exu: failed to enumerate executable sections for Wingman Hunt hook");
				return 0;
			}

			for (const auto& section : sections)
			{
				const auto* match = FindPattern(section.address, section.size, WINGMAN_HUNT_ACTIVATION_SIGNATURE);
				if (match == nullptr)
				{
					continue;
				}

				const uintptr_t matchAddress = reinterpret_cast<uintptr_t>(match);
				const uintptr_t callSite = matchAddress + 11;
				const uintptr_t setCommandTarget = ResolveRelativeCallTarget(callSite);
				if (setCommandTarget == 0)
				{
					Logging::LogMessage(
						"exu: matched Wingman Hunt signature in %s but failed to resolve SetCommand target",
						section.name.c_str()
					);
					continue;
				}

				g_wingmanHuntActivationHookAddress = matchAddress + 6;
				g_wingmanHuntActivationResumeAddress = g_wingmanHuntActivationHookAddress + 10;
				g_wingmanHuntSetCommand = reinterpret_cast<SetCommandIntFn>(setCommandTarget);

				Logging::LogMessage(
					"exu: resolved Wingman Hunt hook at %p resume=%p setCommand=%p in %s",
					reinterpret_cast<void*>(g_wingmanHuntActivationHookAddress),
					reinterpret_cast<void*>(g_wingmanHuntActivationResumeAddress),
					reinterpret_cast<void*>(setCommandTarget),
					section.name.c_str()
				);
				return g_wingmanHuntActivationHookAddress;
			}

			Logging::LogMessage("exu: failed to resolve Wingman Hunt activation hook signature");
			return 0;
		}

		inline uintptr_t g_wingmanHuntActivationHookInitialized = InitializeWingmanHuntActivationHook();
		inline std::unique_ptr<Hook> g_wingmanHuntActivationHook = g_wingmanHuntActivationHookAddress != 0
			? std::make_unique<Hook>(
				g_wingmanHuntActivationHookAddress,
				&WingmanHuntActivationHook,
				10,
				BasicPatch::Status::ACTIVE)
			: nullptr;

		void UpdateHuntLabelOverride(lua_State* L)
		{
			size_t selectedReplacementCount = 0;
			std::string selectedLabel;

			for (const auto& [key, entry] : g_replacements)
			{
				const auto stockCommand = static_cast<StockCommandId>(key & 0xFFFFFFFF);
				if (stockCommand != StockCommandId::HUNT)
				{
					continue;
				}

				const BZR::handle handle = static_cast<BZR::handle>(key >> 32);
				bool isSelected = false;
				if (!TryIsSelected(L, handle, isSelected) || !isSelected)
				{
					continue;
				}

				selectedReplacementCount += 1;
				selectedLabel = entry.replacementLabel;
				if (selectedReplacementCount > 1)
				{
					break;
				}
			}

			if (selectedReplacementCount == 1 && !selectedLabel.empty())
			{
				ApplyHuntLabelOverride(selectedLabel);
			}
			else
			{
				RestoreStockHuntLabel();
			}
		}
	}

	void ResetState(lua_State* L)
	{
		if (g_ownerState == L)
		{
			for (auto& [_, entry] : g_replacements)
			{
				ReleaseEntry(L, entry);
			}
		}

		g_replacements.clear();
		g_ownerState = L;
		g_lastUpdateAt = -1.0;
		RestoreStockHuntLabel();
		Logging::LogMessage("exu: command replacement registry reset");
	}

	bool DispatchRegisteredReplacement(BZR::handle handle, const char* stockCommandName, const char* origin)
	{
		lua_State* L = g_ownerState != nullptr ? g_ownerState : Lua::state;
		if (L == nullptr || stockCommandName == nullptr)
		{
			return false;
		}

		const auto stockCommand = FindStockCommand(stockCommandName);
		if (!stockCommand.has_value())
		{
			return false;
		}

		const auto entryIt = FindReplacement(handle, stockCommand->id);
		if (entryIt == g_replacements.end())
		{
			return false;
		}

		StackGuard guard(L);
		lua_rawgeti(L, LUA_REGISTRYINDEX, entryIt->second.callbackRef);

		if (!lua_isfunction(L, -1))
		{
			return false;
		}

		lua_pushlightuserdata(L, reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
		lua_pushstring(L, stockCommand->display);
		lua_pushstring(L, entryIt->second.replacementLabel.c_str());
		lua_pushstring(L, origin != nullptr ? origin : "unknown");

		const int status = lua_pcall(L, 4, 1, 0);
		if (status != 0)
		{
			LuaCheckStatus(status, L, "Extra Utilities ReplaceStockCmd error:\n%s");
			return false;
		}

		if (lua_isnoneornil(L, -1))
		{
			return true;
		}

		return lua_toboolean(L, -1) != 0;
	}

	int ReplaceStockCmd(lua_State* L)
	{
		const BZR::handle handle = CheckHandle(L, 1);
		const char* stockCommandName = luaL_checkstring(L, 2);
		const char* replacementLabel = luaL_checkstring(L, 3);

		if (!lua_isfunction(L, 4))
		{
			luaL_typerror(L, 4, "function");
		}

		const auto stockCommand = FindStockCommand(stockCommandName);
		if (!stockCommand.has_value())
		{
			luaL_argerror(L, 2, "unsupported stock command name");
		}

		if (g_ownerState == nullptr)
		{
			g_ownerState = L;
		}

		if (g_ownerState != L)
		{
			luaL_error(L, "ReplaceStockCmd registry is bound to a different lua state");
		}

		const uint64_t key = MakeReplacementKey(handle, stockCommand->id);
		auto& entry = g_replacements[key];
		ReleaseEntry(L, entry);

		entry.replacementLabel = replacementLabel;
		entry.lastObservedCommand = -1;
		lua_pushvalue(L, 4);
		entry.callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

		Logging::LogMessage(
			"exu: registered stock command replacement handle=%u stock=%s replacement=%s",
			handle,
			stockCommand->display,
			replacementLabel
		);

		return 0;
	}

	int RemoveStockCmdReplacement(lua_State* L)
	{
		const BZR::handle handle = CheckHandle(L, 1);
		const char* stockCommandName = luaL_checkstring(L, 2);

		const auto stockCommand = FindStockCommand(stockCommandName);
		if (!stockCommand.has_value())
		{
			luaL_argerror(L, 2, "unsupported stock command name");
		}

		const auto entryIt = FindReplacement(handle, stockCommand->id);
		if (entryIt == g_replacements.end())
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		ReleaseEntry(L, entryIt->second);
		g_replacements.erase(entryIt);
		if (stockCommand->id == StockCommandId::HUNT || g_replacements.empty())
		{
			RestoreStockHuntLabel();
		}

		lua_pushboolean(L, 1);
		return 1;
	}

	int HasStockCmdReplacement(lua_State* L)
	{
		const BZR::handle handle = CheckHandle(L, 1);
		const char* stockCommandName = luaL_checkstring(L, 2);

		const auto stockCommand = FindStockCommand(stockCommandName);
		if (!stockCommand.has_value())
		{
			luaL_argerror(L, 2, "unsupported stock command name");
		}

		lua_pushboolean(L, FindReplacement(handle, stockCommand->id) != g_replacements.end());
		return 1;
	}

	int GetStockCmdReplacement(lua_State* L)
	{
		const BZR::handle handle = CheckHandle(L, 1);
		const char* stockCommandName = luaL_checkstring(L, 2);

		const auto stockCommand = FindStockCommand(stockCommandName);
		if (!stockCommand.has_value())
		{
			luaL_argerror(L, 2, "unsupported stock command name");
		}

		const auto entryIt = FindReplacement(handle, stockCommand->id);
		if (entryIt == g_replacements.end())
		{
			lua_pushnil(L);
			return 1;
		}

		lua_createtable(L, 0, 2);
		lua_pushstring(L, stockCommand->display);
		lua_setfield(L, -2, "stockCommand");
		lua_pushstring(L, entryIt->second.replacementLabel.c_str());
		lua_setfield(L, -2, "replacementLabel");
		return 1;
	}

	int TriggerStockCmdReplacement(lua_State* L)
	{
		const BZR::handle handle = CheckHandle(L, 1);
		const char* stockCommandName = luaL_checkstring(L, 2);

		lua_pushboolean(L, DispatchRegisteredReplacement(handle, stockCommandName, "manual"));
		return 1;
	}

	int UpdateCommandReplacements(lua_State* L)
	{
		if (g_ownerState == nullptr)
		{
			g_ownerState = L;
		}
		else if (g_ownerState != L)
		{
			luaL_error(L, "UpdateCommandReplacements registry is bound to a different lua state");
		}

		const double now = TryGetNow(L);
		if (now >= 0.0 && now == g_lastUpdateAt)
		{
			return 0;
		}
		g_lastUpdateAt = now;

		UpdateHuntLabelOverride(L);

		const bool nativeHuntHookActive =
			g_wingmanHuntActivationHook != nullptr &&
			g_wingmanHuntActivationHook->IsActive();
		if (nativeHuntHookActive)
		{
			return 0;
		}

		for (auto& [key, entry] : g_replacements)
		{
			const auto stockCommand = static_cast<StockCommandId>(key & 0xFFFFFFFF);
			const BZR::handle handle = static_cast<BZR::handle>(key >> 32);

			int currentCommand = -1;
			if (!TryGetCurrentCommand(L, handle, currentCommand))
			{
				entry.lastObservedCommand = -1;
				continue;
			}

			if (currentCommand == entry.lastObservedCommand)
			{
				continue;
			}

			entry.lastObservedCommand = currentCommand;

			if (stockCommand != StockCommandId::HUNT || currentCommand != kCmdHunt)
			{
				continue;
			}

			bool isSelected = false;
			if (!TryIsSelected(L, handle, isSelected) || !isSelected)
			{
				continue;
			}

			const bool handled = DispatchRegisteredReplacement(handle, "Hunt", "stock_command_poll");
			if (!handled)
			{
				continue;
			}

			int commandAfterCallback = -1;
			if (TryGetCurrentCommand(L, handle, commandAfterCallback) && commandAfterCallback == kCmdHunt)
			{
				TrySetCommandNone(L, handle);
				commandAfterCallback = kCmdNone;
			}

			entry.lastObservedCommand = commandAfterCallback;
		}

		return 0;
	}
}
