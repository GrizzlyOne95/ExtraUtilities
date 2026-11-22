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

#include "Game/MissionPaths.h"
#include "Game/MissionPathsCore.h"

#include "LuaHelpers.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"

#include <lua.hpp>
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Redux GOG 2.2.301 facts (Ghidra /battlezone98redux.exe-007c64):
// - AiPath is 0x18 bytes. Ctor 0x00460390 writes label @+0x00 (SetLabel
//   0x00460DE0 copies it with operator new), count @+0x04, points @+0x08
//   (operator new(count * 8) via 0x0083D92C, zero-filled), bDontSave byte
//   @+0x0C = 0, pathType @+0x10 = 0, list iterator @+0x14 after pushing the
//   path on the front of the list at 0x0094DE1C.
// - ~AiPath 0x00460590 erases the list node and delete[]s label and points
//   (MSVCR120 ??_V@YAXPAX@Z, IAT 0x00869478). Cleanup 0x004610C0 deletes
//   every path at mission end; the engine never deletes a labeled path
//   mid-mission (Release 0x00460600 deletes only label == NULL plan paths).
// - SaveAll 0x00460790 saves every path whose bDontSave is 0, so a built
//   path is written to saves (Save 0x00460C20; restored by LoadAll 0x00460670
//   -> Load 0x00460910: UNVERIFIED live).
// - Runtime precedent: FUN_006A5000 does operator new(0x18) then ctor
//   0x00460390 with a "bad_%d_%d" label mid-mission.
// - AiPath objects are 0x18 bytes: every runtime builder pushes 0x18 before
//   calling operator new (0x006A539E, 0x00466518, 0x005A500C: 6A 18 FF 15
//   28 94 86 00), and AI_Cleanup 0x0045C030 ends with AiPath::Cleanup.
// Why RemovePath never frees (1.5 PDB source, functions/0040..0056): Goto
// 0x0045FD25 stores AiPath::Find() in the unit command, GotoTask 0x00413BD7
// keeps `path` (it copies only label == NULL plan paths) and reads
// points[pointCount - 1], and fsm_camera_trans_path 0x0056E080 keeps
// Camera_Record.Last_Path. None of them is registered anywhere a delete could
// clear, so freeing a path is a use-after-free. RemovePath therefore writes a
// NUL over the first byte of the label in place (the buffer stays owned by
// the path, so ~AiPath 0x00460590 still frees it at Cleanup). Find 0x00460FC0
// compares bytes and never matches "" against a non-empty name, AllPaths
// skips empty labels, and Release 0x00460600 still leaves it alone (it
// deletes only label == NULL). bDontSave is left 0 on purpose: tasks save the
// AiPath pointer and in_ptr/Addr::Cleanup (1.5 0x00549C4C/0x00549CD1) turn
// an unsaved one into NULL on load. Saved, the path keeps its "old_ptr"
// (Save 0x00460C20) and loads back with label NULL (Load 0x00460910 leaves
// the label NULL for a 0-length name), so it stays unnamed and followers'
// pointers still resolve. Units already on the path keep its points.
// Removing a point is safe because the block is never shrunk: a follower's
// stale index still reads allocated memory, and every holder re-checks
// pointCount (ChoosePathPoint 0x00413EB1, DoState 0x004140A1, camera
// `pointCount <= segment + 2`) before advancing. Growing a full block swaps
// `points` for a bigger copy, as the stock editor does (1.5 AiPath::SetPoints
// 0x00403347 reallocates and frees the old block; PathDisplay::SetEditPath
// 0x004d5326 calls it mid-mission); holders read path->points afresh
// (GotoTask above), they do not cache it.

namespace ExtraUtilities::MissionPaths
{
	namespace
	{
		namespace Addr = EngineAddresses::MissionPaths;
		namespace Core = MissionPathsCore;

		struct MissionPathLayout
		{
			const char* label;     // +0x00, NULL for AI plan paths
			std::int32_t count;    // +0x04
			float* points;         // +0x08, count {x, z} pairs
			std::uint8_t dontSave; // +0x0C, SaveAll skips nonzero
			std::uint8_t pad[3];
			std::int32_t pathType; // +0x10, stock PathType
			void* listIter;        // +0x14
		};
		static_assert(offsetof(MissionPathLayout, count) == 0x04);
		static_assert(offsetof(MissionPathLayout, points) == 0x08);
		static_assert(offsetof(MissionPathLayout, dontSave) == 0x0C);
		static_assert(offsetof(MissionPathLayout, pathType) == 0x10);
		static_assert(offsetof(MissionPathLayout, listIter) == 0x14);
		static_assert(sizeof(MissionPathLayout) == 0x18);

		// MSVC 2013 std::list<AiPath*> node (begin 0x0042D8C0 reads
		// head->_Next, deref 0x004242F0 reads node + 8).
		struct ListNode
		{
			ListNode* next;
			ListNode* prev;
			MissionPathLayout* value;
		};
		static_assert(offsetof(ListNode, value) == 0x08);

		using CtorFn = void*(__thiscall*)(void* path, const char* label, int count);
		using NewFn = void*(__cdecl*)(std::size_t);
		using DeleteFn = void(__cdecl*)(void*);

		// The engine's allocator (MSVCR120): exu.dll uses the UCRT, so engine
		// blocks must be made and freed through the same exports the engine
		// imports (??2 at IAT 0x00869428, ??_V at 0x00869478).
		struct EngineAllocator
		{
			NewFn alloc = nullptr;
			DeleteFn freeArray = nullptr;
		};

		bool ResolveAllocator(EngineAllocator& a) noexcept
		{
			const HMODULE crt = GetModuleHandleW(L"msvcr120.dll");
			if (crt == nullptr)
			{
				return false;
			}
			a.alloc = reinterpret_cast<NewFn>(GetProcAddress(crt, "??2@YAPAXI@Z"));
			a.freeArray = reinterpret_cast<DeleteFn>(GetProcAddress(crt, "??_V@YAXPAX@Z"));
			return a.alloc != nullptr && a.freeArray != nullptr;
		}

		// 1 = found (out set), 0 = not found, -1 = fault or no list.
		int FindPathSeh(const char* name, MissionPathLayout*& out) noexcept
		{
			__try
			{
				const ListNode* head = *reinterpret_cast<ListNode* const*>(Addr::PathList);
				if (head == nullptr)
				{
					return -1;
				}
				const ListNode* node = head->next;
				for (std::size_t n = 0; node != nullptr && node != head && n < Core::kMaxListNodes; ++n)
				{
					MissionPathLayout* path = node->value;
					if (path != nullptr && path->label != nullptr && std::strcmp(path->label, name) == 0)
					{
						out = path;
						return 1;
					}
					node = node->next;
				}
				return 0;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return -1;
			}
		}

		// Copies up to cap label pointers of named paths, in list order.
		bool CollectLabelsSeh(const char** out, std::size_t cap, std::size_t& n) noexcept
		{
			n = 0;
			__try
			{
				const ListNode* head = *reinterpret_cast<ListNode* const*>(Addr::PathList);
				if (head == nullptr)
				{
					return false;
				}
				const ListNode* node = head->next;
				for (std::size_t walked = 0; node != nullptr && node != head && walked < Core::kMaxListNodes && n < cap; ++walked)
				{
					const MissionPathLayout* path = node->value;
					if (path != nullptr && path->label != nullptr && path->label[0] != '\0')
					{
						out[n++] = path->label;
					}
					node = node->next;
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				n = 0;
				return false;
			}
		}

		// Copies a label into dst (cap bytes incl. NUL). False on fault or when
		// it does not fit.
		bool CopyLabelSeh(const char* src, char* dst, std::size_t cap) noexcept
		{
			__try
			{
				for (std::size_t i = 0; i < cap; ++i)
				{
					dst[i] = src[i];
					if (src[i] == '\0')
					{
						return true;
					}
				}
				return false;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool ReadPathSeh(const MissionPathLayout* path, std::int32_t& count, float*& points) noexcept
		{
			__try
			{
				count = path->count;
				points = path->points;
				return count > 0 && count <= Core::kSaneCount && points != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Constructs a path in a fresh 0x18 block and fills its points.
		bool ConstructSeh(const EngineAllocator& a, const char* name, const float* xz, std::int32_t count) noexcept
		{
			void* block = nullptr;
			__try
			{
				block = a.alloc(sizeof(MissionPathLayout));
				if (block == nullptr)
				{
					return false;
				}
				auto* path = static_cast<MissionPathLayout*>(
					reinterpret_cast<CtorFn>(Addr::Ctor)(block, name, count));
				if (path == nullptr || path->count != count || path->points == nullptr)
				{
					// Linked into the list already; leave it (Cleanup frees it).
					return false;
				}
				std::memcpy(path->points, xz, static_cast<std::size_t>(count) * 2 * sizeof(float));
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool InsertSeh(const EngineAllocator& a, MissionPathLayout* path, std::int32_t count, float* points,
			std::int32_t slot, float x, float z) noexcept
		{
			__try
			{
				// Always a fresh block one pair larger (block sizes are never
				// queried, so no assumption about the engine heap beyond
				// operator new/delete[]); never smaller than the old block, so
				// a follower's index stays inside the block it reads.
				const std::size_t bytes = static_cast<std::size_t>(count + 1) * 2 * sizeof(float);
				auto* grown = static_cast<float*>(a.alloc(bytes));
				if (grown == nullptr)
				{
					return false;
				}
				std::memcpy(grown, points, static_cast<std::size_t>(count) * 2 * sizeof(float));
				Core::InsertPair(grown, count, slot, x, z);
				path->points = grown;
				path->count = count + 1;
				a.freeArray(points);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool RemoveSeh(MissionPathLayout* path, std::int32_t count, float* points, std::int32_t slot) noexcept
		{
			__try
			{
				Core::RemovePair(points, count, slot);
				path->count = count - 1;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Takes the name off a path without freeing anything (see the top of
		// this file): label[0] = NUL in place.
		bool UnnameSeh(MissionPathLayout* path) noexcept
		{
			__try
			{
				char* label = const_cast<char*>(path->label);
				if (label == nullptr || label[0] == '\0')
				{
					return false;
				}
				label[0] = '\0';
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// A vector userdata/table with numeric x and z (y is ignored).
		void CheckPosition(lua_State* L, int idx, float& x, float& z)
		{
			if (!lua_isuserdata(L, idx) && !lua_istable(L, idx))
			{
				luaL_typerror(L, idx, "vector");
			}
			const int abs = Lua::AbsoluteStackIndex(L, idx);
			lua_getfield(L, abs, "x");
			lua_getfield(L, abs, "z");
			if (!lua_isnumber(L, -2) || !lua_isnumber(L, -1))
			{
				luaL_typerror(L, idx, "vector");
			}
			x = static_cast<float>(lua_tonumber(L, -2));
			z = static_cast<float>(lua_tonumber(L, -1));
			lua_pop(L, 2);
		}

		const char* CheckName(lua_State* L, int idx, bool& valid)
		{
			std::size_t len = 0;
			const char* name = luaL_checklstring(L, idx, &len);
			valid = Core::IsValidPathName(name, len);
			return name;
		}

		// Optional 1-based point index: absent/nil = none; a non-integer
		// number makes the call fail (valid = false).
		bool OptPoint(lua_State* L, int idx, long long& point, bool& valid)
		{
			valid = true;
			if (lua_isnoneornil(L, idx))
			{
				return false;
			}
			const lua_Number value = luaL_checknumber(L, idx);
			if (!(value >= -2147483648.0 && value <= 2147483647.0) || static_cast<lua_Number>(static_cast<long long>(value)) != value)
			{
				valid = false;
				return true;
			}
			point = static_cast<long long>(value);
			return true;
		}

		int PushBool(lua_State* L, bool value)
		{
			lua_pushboolean(L, value);
			return 1;
		}
	}

	int AllPaths(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return Lua::PushUnsupportedBuild(L);
		constexpr std::size_t kMaxNames = 8192;
		std::vector<const char*> labels(kMaxNames, nullptr);
		std::size_t n = 0;
		if (!CollectLabelsSeh(labels.data(), labels.size(), n))
		{
			lua_pushnil(L);
			return 1;
		}
		std::vector<std::string> names;
		names.reserve(n);
		char buffer[1024];
		for (std::size_t i = 0; i < n; ++i)
		{
			if (CopyLabelSeh(labels[i], buffer, sizeof(buffer)))
			{
				names.emplace_back(buffer);
			}
		}
		lua_createtable(L, static_cast<int>(names.size()), 0);
		for (std::size_t i = 0; i < names.size(); ++i)
		{
			lua_pushlstring(L, names[i].data(), names[i].size());
			lua_rawseti(L, -2, static_cast<int>(i + 1));
		}
		return 1;
	}

	int BuildPath(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return Lua::PushUnsupportedBuild(L);
		bool nameOk = false;
		const char* name = CheckName(L, 1, nameOk);
		const int top = lua_gettop(L);
		if (top < 2)
		{
			return luaL_argerror(L, 2, "vector expected");
		}
		const int positions = top - 1;
		// Type-check every position before any C++ object exists: a Lua
		// error longjmps past destructors.
		for (int i = 0; i < positions; ++i)
		{
			float x = 0.0f;
			float z = 0.0f;
			CheckPosition(L, 2 + i, x, z);
		}
		if (!nameOk || positions > Core::kMaxPoints)
		{
			return PushBool(L, false);
		}
		std::vector<float> xz;
		xz.reserve(static_cast<std::size_t>(positions) * 2);
		bool finite = true;
		for (int i = 0; i < positions; ++i)
		{
			float x = 0.0f;
			float z = 0.0f;
			CheckPosition(L, 2 + i, x, z);
			finite = finite && Core::IsFinitePoint(x, z);
			xz.push_back(x);
			xz.push_back(z);
		}
		if (!finite)
		{
			return PushBool(L, false);
		}

		MissionPathLayout* existing = nullptr;
		if (FindPathSeh(name, existing) != 0)
		{
			// Name taken, or no readable path list.
			return PushBool(L, false);
		}
		EngineAllocator alloc;
		if (!ResolveAllocator(alloc))
		{
			Logging::LogMessage("exu.BuildPath: msvcr120 allocator exports not found");
			return PushBool(L, false);
		}
		return PushBool(L, ConstructSeh(alloc, name, xz.data(), positions));
	}

	int AddPathPoint(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return Lua::PushUnsupportedBuild(L);
		bool nameOk = false;
		const char* name = CheckName(L, 1, nameOk);
		float x = 0.0f;
		float z = 0.0f;
		CheckPosition(L, 2, x, z);
		long long point = 0;
		bool pointOk = true;
		const bool hasPoint = OptPoint(L, 3, point, pointOk);
		if (!nameOk || !pointOk || !Core::IsFinitePoint(x, z))
		{
			return PushBool(L, false);
		}

		MissionPathLayout* path = nullptr;
		std::int32_t count = 0;
		float* points = nullptr;
		if (FindPathSeh(name, path) != 1 || !ReadPathSeh(path, count, points))
		{
			return PushBool(L, false);
		}
		std::int32_t slot = 0;
		if (!Core::InsertSlot(count, hasPoint, point, slot))
		{
			return PushBool(L, false);
		}
		EngineAllocator alloc;
		if (!ResolveAllocator(alloc))
		{
			return PushBool(L, false);
		}
		return PushBool(L, InsertSeh(alloc, path, count, points, slot, x, z));
	}

	int RemovePathPoint(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return Lua::PushUnsupportedBuild(L);
		bool nameOk = false;
		const char* name = CheckName(L, 1, nameOk);
		long long point = 0;
		bool pointOk = true;
		const bool hasPoint = OptPoint(L, 2, point, pointOk);
		if (!nameOk || !pointOk)
		{
			return PushBool(L, false);
		}

		MissionPathLayout* path = nullptr;
		std::int32_t count = 0;
		float* points = nullptr;
		if (FindPathSeh(name, path) != 1 || !ReadPathSeh(path, count, points))
		{
			return PushBool(L, false);
		}
		std::int32_t slot = 0;
		if (!Core::RemoveSlot(count, hasPoint, point, slot))
		{
			return PushBool(L, false);
		}
		return PushBool(L, RemoveSeh(path, count, points, slot));
	}

	int RemovePath(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return Lua::PushUnsupportedBuild(L);
		bool nameOk = false;
		const char* name = CheckName(L, 1, nameOk);
		if (!nameOk)
		{
			return PushBool(L, false);
		}
		MissionPathLayout* path = nullptr;
		if (FindPathSeh(name, path) != 1)
		{
			return PushBool(L, false);
		}
		return PushBool(L, UnnameSeh(path));
	}
}
