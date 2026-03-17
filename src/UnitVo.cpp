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

#include "UnitVo.h"

#include "Hook.h"
#include "Logging.h"

#include <Windows.h>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ExtraUtilities::Patch
{
	std::mutex g_unitVoMutex;
	DWORD g_lastUnitVoAttemptTick = 0;

	std::string NormalizeFilename(const char* filename)
	{
		std::string normalized;
		if (filename == nullptr)
		{
			return normalized;
		}

		normalized.reserve(std::strlen(filename));
		for (const unsigned char c : std::string_view(filename))
		{
			normalized.push_back(static_cast<char>(std::tolower(c)));
		}

		return normalized;
	}

	namespace
	{
		struct ExecutableSection
		{
			const uint8_t* address = nullptr;
			size_t size = 0;
			std::string name;
		};

		struct UnitVoQueueItem
		{
			char name[16];
			void* owner;
			void* sound;
			int priority;
			DWORD time;
			UnitVoQueueItem* next;
		};

		struct UnitVoQueueInspection
		{
			bool containsNonUnitVo = false;
			bool hasDuplicate = false;
			bool hasStale = false;
			size_t unitVoCount = 0;
			std::vector<std::string> queuedNames;
		};

		struct UnitVoQueueDecision
		{
			std::string filename;
			bool drop = false;
			bool flushQueue = false;
		};

		using UnitVoKillQueueFn = void(__cdecl*)(int);

		constexpr std::array<int, 52> UNIT_VO_SAY_QUEUE_CALL_SIGNATURE = {
			0x6A, 0x00, 0x8B, 0x4D, 0xFC, 0x83, 0xC1, 0x18, 0x8B, 0x55,
			0xFC, 0x8B, 0x42, 0x18, 0x8B, 0x50, 0x30, 0xFF, 0xD2, 0x50,
			0x8B, 0x45, 0xF8, 0x50, 0xE8, -1, -1, -1, -1, 0x83, 0xC4, 0x0C,
			0x8B, 0x4D, 0x0C, 0x89, 0x0D, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1,
			0xD9, 0x1D, -1, -1, -1, -1
		};

		constexpr std::array<int, 35> UNIT_VO_RECYCLE_TASK_QUEUE_CALL_SIGNATURE = {
			0x6A, 0x03, 0x8B, 0x4D, 0xFC, 0x83, 0xC1, 0x18, 0x8B, 0x45,
			0xFC, 0x8B, 0x50, 0x18, 0x8B, 0x42, 0x30, 0xFF, 0xD0, 0x50,
			0x8B, 0x4D, 0x08, 0x51, 0xE8, -1, -1, -1, -1, 0x83, 0xC4, 0x0C,
			0x5E, 0x8B, 0xE5
		};

		uintptr_t g_unitVoSayQueueCallSite = 0;
		uintptr_t g_unitVoRecycleTaskQueueCallSite = 0;
		UnitVoQueueFn g_unitVoQueue = nullptr;
		UnitVoKillQueueFn g_unitVoKillQueue = nullptr;
		uintptr_t g_unitVoQueueListStorageAddress = 0;

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

				const size_t size = section->Misc.VirtualSize > section->SizeOfRawData
					? static_cast<size_t>(section->Misc.VirtualSize)
					: static_cast<size_t>(section->SizeOfRawData);
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

		uintptr_t ResolveRelativeCallTarget(uintptr_t callSite, const char* label)
		{
			if (callSite == 0)
			{
				return 0;
			}

			const auto* callInstruction = reinterpret_cast<const uint8_t*>(callSite);
			if (callInstruction[0] != 0xE8)
			{
				Logging::LogMessage("[EXU::UnitVo] %s at %p is not a relative call", label, reinterpret_cast<void*>(callSite));
				return 0;
			}

			int32_t displacement = 0;
			std::memcpy(&displacement, callInstruction + 1, sizeof(displacement));
			const uintptr_t target = callSite + 5 + static_cast<intptr_t>(displacement);
			Logging::LogMessage(
				"[EXU::UnitVo] resolved %s target %p from call site %p",
				label,
				reinterpret_cast<void*>(target),
				reinterpret_cast<void*>(callSite)
			);
			return target;
		}

		uintptr_t ResolveCallSite(const auto& pattern, size_t callOffset, const char* label)
		{
			const auto sections = GetExecutableSections();
			if (sections.empty())
			{
				Logging::LogMessage("[EXU::UnitVo] failed to enumerate executable sections for %s", label);
				return 0;
			}

			for (const auto& section : sections)
			{
				const auto* match = FindPattern(section.address, section.size, pattern);
				if (match == nullptr)
				{
					continue;
				}

				const uintptr_t callSite = reinterpret_cast<uintptr_t>(match + callOffset);
				Logging::LogMessage(
					"[EXU::UnitVo] resolved %s call site at %p in section %s",
					label,
					reinterpret_cast<void*>(callSite),
					section.name.c_str()
				);
				return callSite;
			}

			Logging::LogMessage("[EXU::UnitVo] failed to resolve %s call site", label);
			return 0;
		}

		uintptr_t ResolveQueueListStorageAddress(uintptr_t queueFnAddress)
		{
			if (queueFnAddress == 0)
			{
				return 0;
			}

			const auto* queueFn = reinterpret_cast<const uint8_t*>(queueFnAddress);
			if (queueFn[14] != 0x83 || queueFn[15] != 0x3D || queueFn[20] != 0x00)
			{
				Logging::LogMessage("[EXU::UnitVo] QueueCB prologue did not match expected layout");
				return 0;
			}

			uint32_t noMoreCbAddress = 0;
			std::memcpy(&noMoreCbAddress, queueFn + 16, sizeof(noMoreCbAddress));
			const uintptr_t queueListAddress = static_cast<uintptr_t>(noMoreCbAddress) - sizeof(uint32_t);
			Logging::LogMessage(
				"[EXU::UnitVo] resolved QueueCB queue-list storage at %p",
				reinterpret_cast<void*>(queueListAddress)
			);
			return queueListAddress;
		}

		UnitVoKillQueueFn ResolveKillQueueFunction(uintptr_t queueFnAddress)
		{
			if (queueFnAddress == 0)
			{
				return nullptr;
			}

			const uintptr_t killQueueCallSite = queueFnAddress + 0xCA;
			const auto* bytes = reinterpret_cast<const uint8_t*>(queueFnAddress + 0xC8);
			if (bytes[0] != 0x6A || bytes[1] != 0x00 || bytes[2] != 0xE8)
			{
				Logging::LogMessage("[EXU::UnitVo] QueueCB kill-queue call layout did not match expected bytes");
				return nullptr;
			}

			return reinterpret_cast<UnitVoKillQueueFn>(ResolveRelativeCallTarget(killQueueCallSite, "KillCBQueue"));
		}

		bool EndsWith(std::string_view value, std::string_view suffix)
		{
			return value.size() >= suffix.size() &&
				value.substr(value.size() - suffix.size()) == suffix;
		}

		bool IsLikelyUnitVoFilename(std::string_view filename)
		{
			if (!EndsWith(filename, ".wav"))
			{
				return false;
			}

			const std::string_view stem = filename.substr(0, filename.size() - 4);
			if (stem.size() < 5 || stem.size() > 12)
			{
				return false;
			}

			for (const unsigned char c : stem)
			{
				if (!std::isalnum(c))
				{
					return false;
				}
			}

			const size_t voiceMarker = stem.rfind('v');
			if (voiceMarker == std::string_view::npos || voiceMarker < 2)
			{
				return false;
			}

			const size_t suffixLen = stem.size() - voiceMarker - 1;
			return suffixLen >= 1 && suffixLen <= 2;
		}

		UnitVoQueueItem* GetUnitVoQueueHead()
		{
			if (g_unitVoQueueListStorageAddress == 0)
			{
				return nullptr;
			}

			auto** const storage = reinterpret_cast<UnitVoQueueItem**>(g_unitVoQueueListStorageAddress);
			return *storage;
		}

		UnitVoQueueInspection InspectUnitVoQueueLocked(DWORD now, std::string_view duplicateCandidate)
		{
			UnitVoQueueInspection inspection;

			for (UnitVoQueueItem* item = GetUnitVoQueueHead(); item != nullptr; item = item->next)
			{
				const std::string queuedName = NormalizeFilename(item->name);
				if (!IsLikelyUnitVoFilename(queuedName))
				{
					inspection.containsNonUnitVo = true;
					continue;
				}

				inspection.unitVoCount++;
				inspection.queuedNames.push_back(queuedName);
				if (queuedName == duplicateCandidate)
				{
					inspection.hasDuplicate = true;
				}

				if (unitVoQueueStaleMs > 0 && now - item->time >= unitVoQueueStaleMs)
				{
					inspection.hasStale = true;
				}
			}

			return inspection;
		}

		bool IsQueuedName(const UnitVoQueueInspection& inspection, const std::string& filename)
		{
			for (const std::string& queuedName : inspection.queuedNames)
			{
				if (queuedName == filename)
				{
					return true;
				}
			}

			return false;
		}

		std::string SelectUnitVoFilenameLocked(
			std::string_view normalizedFilename,
			const char* fallbackFilename,
			const UnitVoQueueInspection& inspection)
		{
			const auto alternateIt = unitVoAlternates.find(std::string(normalizedFilename));
			if (alternateIt == unitVoAlternates.end() || alternateIt->second.empty())
			{
				return fallbackFilename;
			}

			std::vector<const std::string*> availableChoices;
			availableChoices.reserve(alternateIt->second.size());
			for (const std::string& alternate : alternateIt->second)
			{
				if (!IsQueuedName(inspection, alternate))
				{
					availableChoices.push_back(&alternate);
				}
			}

			const bool useFilteredChoices = !availableChoices.empty();
			const size_t choiceCount = useFilteredChoices ? availableChoices.size() : alternateIt->second.size();
			const size_t index = choiceCount == 1 ? 0 : static_cast<size_t>(std::rand()) % choiceCount;
			return useFilteredChoices ? *availableChoices[index] : alternateIt->second[index];
		}

		UnitVoQueueDecision PrepareUnitVoQueueDecision(const char* filename)
		{
			if (filename == nullptr)
			{
				return {};
			}

			const std::string normalized = NormalizeFilename(filename);
			if (!IsLikelyUnitVoFilename(normalized))
			{
				return { filename, false, false };
			}

			const DWORD now = GetTickCount();
			std::lock_guard<std::mutex> lock(g_unitVoMutex);

			if (unitVoMuted)
			{
				return { {}, true, false };
			}

			if (unitVoThrottleMs > 0 && g_lastUnitVoAttemptTick != 0)
			{
				const DWORD elapsed = now - g_lastUnitVoAttemptTick;
				if (elapsed < unitVoThrottleMs)
				{
					g_lastUnitVoAttemptTick = now;
					return { {}, true, false };
				}
			}

			g_lastUnitVoAttemptTick = now;

			UnitVoQueueDecision decision;
			UnitVoQueueInspection inspection = InspectUnitVoQueueLocked(now, normalized);
			decision.filename = SelectUnitVoFilenameLocked(normalized, filename, inspection);

			const std::string selectedNormalized = NormalizeFilename(decision.filename.c_str());
			if (selectedNormalized != normalized)
			{
				inspection = InspectUnitVoQueueLocked(now, selectedNormalized);
			}

			if (inspection.hasDuplicate)
			{
				decision.drop = true;
				return decision;
			}

			const bool depthExceeded = unitVoQueueDepthLimit > 0 && inspection.unitVoCount >= unitVoQueueDepthLimit;
			if (inspection.hasStale || depthExceeded)
			{
				if (!inspection.containsNonUnitVo && g_unitVoKillQueue != nullptr)
				{
					decision.flushQueue = true;
				}
				else
				{
					decision.drop = true;
				}
			}

			return decision;
		}

		int __cdecl QueueUnitVo(const char* filename, void* owner, int priority)
		{
			if (g_unitVoQueue == nullptr)
			{
				return 0;
			}

			const UnitVoQueueDecision decision = PrepareUnitVoQueueDecision(filename);
			if (decision.drop || decision.filename.empty())
			{
				return 0;
			}

			if (decision.flushQueue && g_unitVoKillQueue != nullptr)
			{
				g_unitVoKillQueue(0);
			}

			return g_unitVoQueue(decision.filename.c_str(), owner, priority);
		}

		static void __declspec(naked) UnitVoSayQueueHook()
		{
			__asm
			{
				mov eax, [esp+12]
				push eax
				mov eax, [esp+12]
				push eax
				mov eax, [esp+12]
				push eax
				call QueueUnitVo
				add esp, 0x0C
				ret 0x0C
			}
		}

		static void __declspec(naked) UnitVoRecycleTaskQueueHook()
		{
			__asm
			{
				mov eax, [esp+12]
				push eax
				mov eax, [esp+12]
				push eax
				mov eax, [esp+12]
				push eax
				call QueueUnitVo
				add esp, 0x0C
				ret 0x0C
			}
		}

		uintptr_t InitializeUnitVoQueueHooks()
		{
			g_unitVoSayQueueCallSite = ResolveCallSite(UNIT_VO_SAY_QUEUE_CALL_SIGNATURE, 24, "Say->QueueCB");
			g_unitVoRecycleTaskQueueCallSite = ResolveCallSite(
				UNIT_VO_RECYCLE_TASK_QUEUE_CALL_SIGNATURE,
				24,
				"RecycleTask::Say->QueueCB");

			const uintptr_t queueTarget = g_unitVoSayQueueCallSite != 0
				? ResolveRelativeCallTarget(g_unitVoSayQueueCallSite, "QueueCB")
				: ResolveRelativeCallTarget(g_unitVoRecycleTaskQueueCallSite, "QueueCB");
			g_unitVoQueue = reinterpret_cast<UnitVoQueueFn>(queueTarget);
			g_unitVoQueueListStorageAddress = ResolveQueueListStorageAddress(queueTarget);
			g_unitVoKillQueue = ResolveKillQueueFunction(queueTarget);
			return g_unitVoSayQueueCallSite;
		}

		inline uintptr_t g_unitVoQueueHooksInitialized = InitializeUnitVoQueueHooks();
	}

	Hook unitVoSayQueueHook(g_unitVoSayQueueCallSite, &UnitVoSayQueueHook, 8, BasicPatch::Status::ACTIVE);
	Hook unitVoRecycleTaskQueueHook(g_unitVoRecycleTaskQueueCallSite, &UnitVoRecycleTaskQueueHook, 8, BasicPatch::Status::ACTIVE);
}

namespace ExtraUtilities::Lua::Patches
{
	namespace
	{
		using OpenShimSetUnderAttackAlertModeFn = BOOL(WINAPI*)(int);

		OpenShimSetUnderAttackAlertModeFn ResolveUnderAttackAlertBridge()
		{
			static OpenShimSetUnderAttackAlertModeFn fn = nullptr;
			static bool attempted = false;
			static bool loggedMissing = false;
			if (attempted)
			{
				return fn;
			}

			attempted = true;
			if (HMODULE module = GetModuleHandleA("winmm.dll"))
			{
				fn = reinterpret_cast<OpenShimSetUnderAttackAlertModeFn>(
					GetProcAddress(module, "OpenShimSetUnderAttackAlertMode"));
			}

			if (!fn && !loggedMissing)
			{
				loggedMissing = true;
				Logging::LogMessage("[EXU::UnitVo] OpenShim under-attack alert bridge unavailable");
			}

			return fn;
		}

		constexpr uint32_t kMaxUnitVoThrottleMs = 60000;
		constexpr uint32_t kMaxUnitVoQueueDepth = 8;
		constexpr uint32_t kMaxUnitVoQueueStaleMs = 60000;
		constexpr size_t kMaxUnitVoAlternateCount = 16;
		constexpr size_t kMaxUnitVoFilenameLength = 15;

		std::string CheckUnitVoFilename(lua_State* L, int index)
		{
			size_t length{};
			std::string filename = luaL_checklstring(L, index, &length);
			if (length == 0 || length > kMaxUnitVoFilenameLength)
			{
				luaL_argerror(L, index, "Extra Utilities Error: Unit VO filename must be 1-15 characters");
			}

			return filename;
		}
	}

	int GetUnitVoThrottle(lua_State* L)
	{
		lua_pushinteger(L, static_cast<lua_Integer>(Patch::unitVoThrottleMs));
		return 1;
	}

	int SetUnitVoThrottle(lua_State* L)
	{
		lua_Integer requested = luaL_checkinteger(L, 1);
		if (requested < 0 || requested > kMaxUnitVoThrottleMs)
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: Unit VO throttle must be between 0 and 60000 milliseconds");
		}

		std::lock_guard<std::mutex> lock(Patch::g_unitVoMutex);
		Patch::unitVoThrottleMs = static_cast<uint32_t>(requested);
		Patch::g_lastUnitVoAttemptTick = 0;
		return 0;
	}

	int GetUnitVoQueueDepthLimit(lua_State* L)
	{
		lua_pushinteger(L, static_cast<lua_Integer>(Patch::unitVoQueueDepthLimit));
		return 1;
	}

	int SetUnitVoQueueDepthLimit(lua_State* L)
	{
		lua_Integer requested = luaL_checkinteger(L, 1);
		if (requested < 0 || requested > kMaxUnitVoQueueDepth)
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: Unit VO queue depth must be between 0 and 8");
		}

		std::lock_guard<std::mutex> lock(Patch::g_unitVoMutex);
		Patch::unitVoQueueDepthLimit = static_cast<uint32_t>(requested);
		return 0;
	}

	int GetUnitVoQueueStaleMs(lua_State* L)
	{
		lua_pushinteger(L, static_cast<lua_Integer>(Patch::unitVoQueueStaleMs));
		return 1;
	}

	int SetUnitVoQueueStaleMs(lua_State* L)
	{
		lua_Integer requested = luaL_checkinteger(L, 1);
		if (requested < 0 || requested > kMaxUnitVoQueueStaleMs)
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: Unit VO queue stale threshold must be between 0 and 60000 milliseconds");
		}

		std::lock_guard<std::mutex> lock(Patch::g_unitVoMutex);
		Patch::unitVoQueueStaleMs = static_cast<uint32_t>(requested);
		return 0;
	}

	int GetUnitVoMuted(lua_State* L)
	{
		std::lock_guard<std::mutex> lock(Patch::g_unitVoMutex);
		lua_pushboolean(L, Patch::unitVoMuted ? 1 : 0);
		return 1;
	}

	int SetUnitVoMuted(lua_State* L)
	{
		const bool requested = lua_toboolean(L, 1) != 0;

		std::lock_guard<std::mutex> lock(Patch::g_unitVoMutex);
		Patch::unitVoMuted = requested;
		Patch::g_lastUnitVoAttemptTick = 0;
		return 0;
	}

	int GetUnitVoAlternates(lua_State* L)
	{
		const std::string normalized = Patch::NormalizeFilename(CheckUnitVoFilename(L, 1).c_str());

		std::lock_guard<std::mutex> lock(Patch::g_unitVoMutex);
		const auto alternateIt = Patch::unitVoAlternates.find(normalized);
		if (alternateIt == Patch::unitVoAlternates.end())
		{
			lua_pushnil(L);
			return 1;
		}

		lua_newtable(L);
		lua_Integer index = 1;
		for (const std::string& alternate : alternateIt->second)
		{
			lua_pushlstring(L, alternate.c_str(), alternate.size());
			lua_rawseti(L, -2, index++);
		}

		return 1;
	}

	int SetUnitVoAlternates(lua_State* L)
	{
		const std::string normalized = Patch::NormalizeFilename(CheckUnitVoFilename(L, 1).c_str());

		if (lua_isnil(L, 2))
		{
			std::lock_guard<std::mutex> lock(Patch::g_unitVoMutex);
			Patch::unitVoAlternates.erase(normalized);
			Logging::LogMessage("[EXU::UnitVo] cleared alternates filename=%s", normalized.c_str());
			return 0;
		}

		luaL_checktype(L, 2, LUA_TTABLE);

		const int count = static_cast<int>(lua_objlen(L, 2));
		if (count <= 0 || count > static_cast<int>(kMaxUnitVoAlternateCount))
		{
			return luaL_argerror(L, 2, "Extra Utilities Error: Unit VO alternate table must contain 1-16 filenames");
		}

		std::vector<std::string> alternates;
		alternates.reserve(static_cast<size_t>(count));

		for (int i = 1; i <= count; ++i)
		{
			lua_rawgeti(L, 2, i);
			if (!lua_isstring(L, -1))
			{
				lua_pop(L, 1);
				return luaL_argerror(L, 2, "Extra Utilities Error: Unit VO alternate entries must be strings");
			}

			size_t length{};
			const char* value = lua_tolstring(L, -1, &length);
			if (value == nullptr || length == 0 || length > kMaxUnitVoFilenameLength)
			{
				lua_pop(L, 1);
				return luaL_argerror(L, 2, "Extra Utilities Error: Unit VO alternate filenames must be 1-15 characters");
			}

			alternates.emplace_back(Patch::NormalizeFilename(value));
			lua_pop(L, 1);
		}

		std::lock_guard<std::mutex> lock(Patch::g_unitVoMutex);
		Patch::unitVoAlternates[normalized] = std::move(alternates);
		Logging::LogMessage(
			"[EXU::UnitVo] set alternates filename=%s count=%d first=%s",
			normalized.c_str(),
			count,
			Patch::unitVoAlternates[normalized].empty() ? "" : Patch::unitVoAlternates[normalized].front().c_str());
		return 0;
	}

	int SetUnderAttackAlertMode(lua_State* L)
	{
		lua_Integer requested = luaL_checkinteger(L, 1);
		if (requested < 1 || requested > 3)
		{
			return luaL_argerror(L, 1, "Extra Utilities Error: under-attack alert mode must be 1-3");
		}

		if (OpenShimSetUnderAttackAlertModeFn fn = ResolveUnderAttackAlertBridge())
		{
			lua_pushboolean(L, fn(static_cast<int>(requested)) ? 1 : 0);
			return 1;
		}

		lua_pushboolean(L, 0);
		return 1;
	}
}
