/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Extra Utilities is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 */

#pragma once

/*
 * ContinuityApi.h
 *
 * Portable mission-to-mission world snapshots assembled only from the public
 * Battlezone Lua object API documented by UltraKen for Battlezone 98 Redux.
 * This intentionally does not parse BZR's native .sav object layout or retain
 * engine pointers. A snapshot is an ordinary persistable Lua table and can
 * therefore be stored with exu.storage.
 *
 * Format 1 captures reconstruction-safe state:
 *   ODF, team, transform, health/ammo ratios, class metadata, and player/person
 *   classification. It deliberately does not claim to preserve AI task stacks,
 *   target pointers, native group internals, or other engine-owned transient
 *   state.
 *
 * Important BZR API details:
 *   - AllObjects() returns a Lua iterator, not a table.
 *   - SetMatrix() argument order is right, up, front, position.
 *   - There is no public IsPlayer(); player identity is derived by comparing
 *     against the documented GetPlayerHandle() variants.
 */

#include "LuaHelpers.h"

#include <lua.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace ExtraUtilities::Lua::ContinuityApi
{
	namespace Detail
	{
		constexpr int kSnapshotFormatVersion = 1;
		constexpr int kDefaultMaxObjects = 4096;
		constexpr int kHardMaxObjects = 10000;

		struct MatrixData
		{
			double right_x = 1.0;
			double right_y = 0.0;
			double right_z = 0.0;
			double up_x = 0.0;
			double up_y = 1.0;
			double up_z = 0.0;
			double front_x = 0.0;
			double front_y = 0.0;
			double front_z = 1.0;
			double posit_x = 0.0;
			double posit_y = 0.0;
			double posit_z = 0.0;
		};

		struct CaptureOptions
		{
			bool hasTeam = false;
			int team = 0;
			bool includePlayers = false;
			int maxObjects = kDefaultMaxObjects;
		};

		struct RestoreOptions
		{
			double offsetX = 0.0;
			double offsetY = 0.0;
			double offsetZ = 0.0;
			bool restoreHealth = true;
			bool restoreAmmo = true;
			bool skipPlayers = true;
			bool hasTeamOverride = false;
			int teamOverride = 0;
			int maxObjects = kDefaultMaxObjects;
			int optionsIndex = 0;
		};

		inline void PushHandle(lua_State* L, BZR::handle handle)
		{
			lua_pushlightuserdata(L, reinterpret_cast<void*>(handle));
		}

		inline bool IsHandleValue(lua_State* L, int index)
		{
			return lua_isuserdata(L, index) != 0;
		}

		inline BZR::handle ToHandle(lua_State* L, int index)
		{
			return reinterpret_cast<BZR::handle>(lua_touserdata(L, index));
		}

		inline bool GetGlobalFunction(lua_State* L, const char* name)
		{
			lua_getglobal(L, name);
			if (!lua_isfunction(L, -1))
			{
				lua_pop(L, 1);
				return false;
			}
			return true;
		}

		inline bool TryCallHandleString(lua_State* L, const char* name, BZR::handle handle, std::string& value)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, name))
			{
				return false;
			}
			PushHandle(L, handle);
			if (lua_pcall(L, 1, 1, 0) != 0 || !lua_isstring(L, -1))
			{
				lua_settop(L, top);
				return false;
			}
			size_t length = 0;
			const char* raw = lua_tolstring(L, -1, &length);
			value.assign(raw != nullptr ? raw : "", length);
			lua_settop(L, top);
			return true;
		}

		inline bool TryCallHandleNumber(lua_State* L, const char* name, BZR::handle handle, double& value)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, name))
			{
				return false;
			}
			PushHandle(L, handle);
			if (lua_pcall(L, 1, 1, 0) != 0 || !lua_isnumber(L, -1))
			{
				lua_settop(L, top);
				return false;
			}
			value = static_cast<double>(lua_tonumber(L, -1));
			lua_settop(L, top);
			return std::isfinite(value);
		}

		inline bool TryCallHandleInteger(lua_State* L, const char* name, BZR::handle handle, int& value)
		{
			double raw = 0.0;
			if (!TryCallHandleNumber(L, name, handle, raw) ||
				raw < static_cast<double>((std::numeric_limits<int>::min)()) ||
				raw > static_cast<double>((std::numeric_limits<int>::max)()))
			{
				return false;
			}
			value = static_cast<int>(raw);
			return true;
		}

		inline bool TryCallHandleBoolean(lua_State* L, const char* name, BZR::handle handle, bool& value)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, name))
			{
				return false;
			}
			PushHandle(L, handle);
			if (lua_pcall(L, 1, 1, 0) != 0)
			{
				lua_settop(L, top);
				return false;
			}
			value = lua_toboolean(L, -1) != 0;
			lua_settop(L, top);
			return true;
		}

		inline bool TryCallNoArgNumber(lua_State* L, const char* name, double& value)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, name))
			{
				return false;
			}
			if (lua_pcall(L, 0, 1, 0) != 0 || !lua_isnumber(L, -1))
			{
				lua_settop(L, top);
				return false;
			}
			value = static_cast<double>(lua_tonumber(L, -1));
			lua_settop(L, top);
			return std::isfinite(value);
		}

		inline bool TryCallNoArgBoolean(lua_State* L, const char* name, bool& value)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, name))
			{
				return false;
			}
			if (lua_pcall(L, 0, 1, 0) != 0)
			{
				lua_settop(L, top);
				return false;
			}
			value = lua_toboolean(L, -1) != 0;
			lua_settop(L, top);
			return true;
		}

		inline bool TryCallPlayerHandle(lua_State* L, bool useTeam, int team, BZR::handle& handle)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, "GetPlayerHandle"))
			{
				return false;
			}
			int args = 0;
			if (useTeam)
			{
				lua_pushinteger(L, team);
				args = 1;
			}
			if (lua_pcall(L, args, 1, 0) != 0)
			{
				lua_settop(L, top);
				return false;
			}
			if (lua_isnil(L, -1))
			{
				handle = BZR::handle{};
				lua_settop(L, top);
				return true;
			}
			if (!IsHandleValue(L, -1))
			{
				lua_settop(L, top);
				return false;
			}
			handle = ToHandle(L, -1);
			lua_settop(L, top);
			return true;
		}

		inline bool IsPlayerHandle(lua_State* L, BZR::handle handle, int team)
		{
			// The documented BZR API has no IsPlayer(handle). Compare against the
			// local player first, then the documented team-specific player slot.
			BZR::handle player{};
			if (TryCallPlayerHandle(L, false, 0, player) && player != 0 && player == handle)
			{
				return true;
			}
			if (TryCallPlayerHandle(L, true, team, player) && player != 0 && player == handle)
			{
				return true;
			}
			return false;
		}

		inline bool TryReadNumberField(lua_State* L, int tableIndex, const char* field, double& value)
		{
			const int absIndex = AbsoluteStackIndex(L, tableIndex);
			lua_getfield(L, absIndex, field);
			if (!lua_isnumber(L, -1))
			{
				lua_pop(L, 1);
				return false;
			}
			value = static_cast<double>(lua_tonumber(L, -1));
			lua_pop(L, 1);
			return std::isfinite(value);
		}

		inline bool TryReadMatrix(lua_State* L, int index, MatrixData& matrix)
		{
			return TryReadNumberField(L, index, "right_x", matrix.right_x) &&
				TryReadNumberField(L, index, "right_y", matrix.right_y) &&
				TryReadNumberField(L, index, "right_z", matrix.right_z) &&
				TryReadNumberField(L, index, "up_x", matrix.up_x) &&
				TryReadNumberField(L, index, "up_y", matrix.up_y) &&
				TryReadNumberField(L, index, "up_z", matrix.up_z) &&
				TryReadNumberField(L, index, "front_x", matrix.front_x) &&
				TryReadNumberField(L, index, "front_y", matrix.front_y) &&
				TryReadNumberField(L, index, "front_z", matrix.front_z) &&
				TryReadNumberField(L, index, "posit_x", matrix.posit_x) &&
				TryReadNumberField(L, index, "posit_y", matrix.posit_y) &&
				TryReadNumberField(L, index, "posit_z", matrix.posit_z);
		}

		inline bool TryCallHandleMatrix(lua_State* L, const char* name, BZR::handle handle, MatrixData& matrix)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, name))
			{
				return false;
			}
			PushHandle(L, handle);
			if (lua_pcall(L, 1, 1, 0) != 0 || !(lua_isuserdata(L, -1) || lua_istable(L, -1)))
			{
				lua_settop(L, top);
				return false;
			}
			const bool ok = TryReadMatrix(L, -1, matrix);
			lua_settop(L, top);
			return ok;
		}

		inline void PushPlainMatrix(lua_State* L, const MatrixData& matrix)
		{
			lua_createtable(L, 0, 12);
#define EXU_SET_MATRIX_FIELD(name) \
			lua_pushnumber(L, matrix.name); \
			lua_setfield(L, -2, #name)
			EXU_SET_MATRIX_FIELD(right_x);
			EXU_SET_MATRIX_FIELD(right_y);
			EXU_SET_MATRIX_FIELD(right_z);
			EXU_SET_MATRIX_FIELD(up_x);
			EXU_SET_MATRIX_FIELD(up_y);
			EXU_SET_MATRIX_FIELD(up_z);
			EXU_SET_MATRIX_FIELD(front_x);
			EXU_SET_MATRIX_FIELD(front_y);
			EXU_SET_MATRIX_FIELD(front_z);
			EXU_SET_MATRIX_FIELD(posit_x);
			EXU_SET_MATRIX_FIELD(posit_y);
			EXU_SET_MATRIX_FIELD(posit_z);
#undef EXU_SET_MATRIX_FIELD
		}

		inline bool PushNativeMatrix(lua_State* L, const MatrixData& matrix)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, "SetMatrix"))
			{
				return false;
			}

			// UltraKen's BZR reference defines SetMatrix as:
			// right(3), up(3), front(3), position(3).
			lua_pushnumber(L, matrix.right_x);
			lua_pushnumber(L, matrix.right_y);
			lua_pushnumber(L, matrix.right_z);
			lua_pushnumber(L, matrix.up_x);
			lua_pushnumber(L, matrix.up_y);
			lua_pushnumber(L, matrix.up_z);
			lua_pushnumber(L, matrix.front_x);
			lua_pushnumber(L, matrix.front_y);
			lua_pushnumber(L, matrix.front_z);
			lua_pushnumber(L, matrix.posit_x);
			lua_pushnumber(L, matrix.posit_y);
			lua_pushnumber(L, matrix.posit_z);
			if (lua_pcall(L, 12, 1, 0) != 0 || !lua_isuserdata(L, -1))
			{
				lua_settop(L, top);
				return false;
			}
			return true;
		}

		inline bool TryBuildObject(lua_State* L, const std::string& odf, int team, const MatrixData& matrix, BZR::handle& handle)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, "BuildObject"))
			{
				return false;
			}
			lua_pushlstring(L, odf.data(), odf.size());
			lua_pushinteger(L, team);
			if (!PushNativeMatrix(L, matrix))
			{
				lua_settop(L, top);
				return false;
			}
			if (lua_pcall(L, 3, 1, 0) != 0 || !IsHandleValue(L, -1))
			{
				lua_settop(L, top);
				return false;
			}
			handle = ToHandle(L, -1);
			lua_settop(L, top);
			return handle != 0;
		}

		inline bool TrySetHandleNumber(lua_State* L, const char* name, BZR::handle handle, double value)
		{
			const int top = lua_gettop(L);
			if (!GetGlobalFunction(L, name))
			{
				return false;
			}
			PushHandle(L, handle);
			lua_pushnumber(L, value);
			const bool ok = lua_pcall(L, 2, 0, 0) == 0;
			lua_settop(L, top);
			return ok;
		}

		inline bool PushObjectDescriptor(lua_State* L, BZR::handle handle)
		{
			std::string odf;
			int team = 0;
			MatrixData matrix{};
			if (!TryCallHandleString(L, "GetOdf", handle, odf) || odf.empty() ||
				!TryCallHandleInteger(L, "GetTeamNum", handle, team) ||
				!TryCallHandleMatrix(L, "GetTransform", handle, matrix))
			{
				return false;
			}

			lua_createtable(L, 0, 10);
			lua_pushlstring(L, odf.data(), odf.size());
			lua_setfield(L, -2, "odf");
			lua_pushinteger(L, team);
			lua_setfield(L, -2, "team");
			PushPlainMatrix(L, matrix);
			lua_setfield(L, -2, "transform");

			double number = 0.0;
			if (TryCallHandleNumber(L, "GetHealth", handle, number))
			{
				lua_pushnumber(L, number);
				lua_setfield(L, -2, "health");
			}
			if (TryCallHandleNumber(L, "GetAmmo", handle, number))
			{
				lua_pushnumber(L, number);
				lua_setfield(L, -2, "ammo");
			}

			std::string text;
			if (TryCallHandleString(L, "GetClassLabel", handle, text))
			{
				lua_pushlstring(L, text.data(), text.size());
				lua_setfield(L, -2, "classLabel");
			}
			if (TryCallHandleString(L, "GetClassSig", handle, text))
			{
				lua_pushlstring(L, text.data(), text.size());
				lua_setfield(L, -2, "classSig");
			}

			const bool isPlayer = IsPlayerHandle(L, handle, team);
			lua_pushboolean(L, isPlayer ? 1 : 0);
			lua_setfield(L, -2, "isPlayer");

			bool isPerson = false;
			if (TryCallHandleBoolean(L, "IsPerson", handle, isPerson))
			{
				lua_pushboolean(L, isPerson ? 1 : 0);
				lua_setfield(L, -2, "isPerson");
			}
			return true;
		}

		inline CaptureOptions ReadCaptureOptions(lua_State* L, int index)
		{
			CaptureOptions options{};
			if (index == 0 || lua_isnoneornil(L, index))
			{
				return options;
			}
			luaL_checktype(L, index, LUA_TTABLE);
			const int absIndex = AbsoluteStackIndex(L, index);

			lua_getfield(L, absIndex, "team");
			if (!lua_isnil(L, -1))
			{
				options.team = static_cast<int>(luaL_checkinteger(L, -1));
				options.hasTeam = true;
			}
			lua_pop(L, 1);

			lua_getfield(L, absIndex, "includePlayers");
			if (!lua_isnil(L, -1))
			{
				options.includePlayers = CheckBool(L, -1);
			}
			lua_pop(L, 1);

			lua_getfield(L, absIndex, "maxObjects");
			if (!lua_isnil(L, -1))
			{
				options.maxObjects = static_cast<int>(luaL_checkinteger(L, -1));
			}
			lua_pop(L, 1);
			if (options.maxObjects < 1 || options.maxObjects > kHardMaxObjects)
			{
				luaL_argerror(L, index, "maxObjects must be between 1 and 10000");
			}
			return options;
		}

		inline bool TryGetOffset(lua_State* L, int optionsIndex, double& x, double& y, double& z)
		{
			if (optionsIndex == 0 || lua_isnoneornil(L, optionsIndex))
			{
				return true;
			}
			const int absOptions = AbsoluteStackIndex(L, optionsIndex);
			lua_getfield(L, absOptions, "offset");
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				return true;
			}
			const int offsetIndex = AbsoluteStackIndex(L, -1);
			const bool ok = TryReadNumberField(L, offsetIndex, "x", x) &&
				TryReadNumberField(L, offsetIndex, "y", y) &&
				TryReadNumberField(L, offsetIndex, "z", z);
			lua_pop(L, 1);
			return ok;
		}

		inline RestoreOptions ReadRestoreOptions(lua_State* L, int index)
		{
			RestoreOptions options{};
			if (index == 0 || lua_isnoneornil(L, index))
			{
				return options;
			}
			luaL_checktype(L, index, LUA_TTABLE);
			options.optionsIndex = AbsoluteStackIndex(L, index);
			if (!TryGetOffset(L, options.optionsIndex, options.offsetX, options.offsetY, options.offsetZ))
			{
				luaL_argerror(L, index, "offset must contain finite x, y, and z numbers");
			}

			auto readBool = [L, &options](const char* field, bool& output)
			{
				lua_getfield(L, options.optionsIndex, field);
				if (!lua_isnil(L, -1))
				{
					output = CheckBool(L, -1);
				}
				lua_pop(L, 1);
			};
			readBool("restoreHealth", options.restoreHealth);
			readBool("restoreAmmo", options.restoreAmmo);
			readBool("skipPlayers", options.skipPlayers);

			lua_getfield(L, options.optionsIndex, "teamOverride");
			if (!lua_isnil(L, -1))
			{
				options.teamOverride = static_cast<int>(luaL_checkinteger(L, -1));
				options.hasTeamOverride = true;
			}
			lua_pop(L, 1);

			lua_getfield(L, options.optionsIndex, "maxObjects");
			if (!lua_isnil(L, -1))
			{
				options.maxObjects = static_cast<int>(luaL_checkinteger(L, -1));
			}
			lua_pop(L, 1);
			if (options.maxObjects < 1 || options.maxObjects > kHardMaxObjects)
			{
				luaL_argerror(L, index, "maxObjects must be between 1 and 10000");
			}
			return options;
		}

		inline std::string ResolveOdf(lua_State* L, const RestoreOptions& options, const std::string& original)
		{
			if (options.optionsIndex == 0)
			{
				return original;
			}
			lua_getfield(L, options.optionsIndex, "odfMap");
			if (!lua_istable(L, -1))
			{
				lua_pop(L, 1);
				return original;
			}
			lua_getfield(L, -1, original.c_str());
			std::string mapped = original;
			if (lua_isstring(L, -1))
			{
				size_t length = 0;
				const char* raw = lua_tolstring(L, -1, &length);
				mapped.assign(raw != nullptr ? raw : "", length);
			}
			lua_pop(L, 2);
			return mapped;
		}

		inline bool ReadDescriptor(lua_State* L, int index, const RestoreOptions& options,
			std::string& odf, int& team, MatrixData& matrix, bool& isPlayer,
			double& health, bool& hasHealth, double& ammo, bool& hasAmmo)
		{
			if (!lua_istable(L, index))
			{
				return false;
			}
			const int absIndex = AbsoluteStackIndex(L, index);

			lua_getfield(L, absIndex, "odf");
			if (!lua_isstring(L, -1))
			{
				lua_pop(L, 1);
				return false;
			}
			size_t length = 0;
			const char* rawOdf = lua_tolstring(L, -1, &length);
			odf.assign(rawOdf != nullptr ? rawOdf : "", length);
			lua_pop(L, 1);
			odf = ResolveOdf(L, options, odf);
			if (odf.empty())
			{
				return false;
			}

			lua_getfield(L, absIndex, "team");
			if (!lua_isnumber(L, -1))
			{
				lua_pop(L, 1);
				return false;
			}
			team = static_cast<int>(lua_tointeger(L, -1));
			lua_pop(L, 1);
			if (options.hasTeamOverride)
			{
				team = options.teamOverride;
			}

			lua_getfield(L, absIndex, "transform");
			if (!lua_istable(L, -1) || !TryReadMatrix(L, -1, matrix))
			{
				lua_pop(L, 1);
				return false;
			}
			lua_pop(L, 1);
			matrix.posit_x += options.offsetX;
			matrix.posit_y += options.offsetY;
			matrix.posit_z += options.offsetZ;

			lua_getfield(L, absIndex, "isPlayer");
			isPlayer = lua_toboolean(L, -1) != 0;
			lua_pop(L, 1);

			lua_getfield(L, absIndex, "health");
			hasHealth = lua_isnumber(L, -1);
			health = hasHealth ? static_cast<double>(lua_tonumber(L, -1)) : 0.0;
			lua_pop(L, 1);

			lua_getfield(L, absIndex, "ammo");
			hasAmmo = lua_isnumber(L, -1);
			ammo = hasAmmo ? static_cast<double>(lua_tonumber(L, -1)) : 0.0;
			lua_pop(L, 1);
			return (!hasHealth || std::isfinite(health)) && (!hasAmmo || std::isfinite(ammo));
		}

		inline void PushReport(lua_State* L, int scanned, int completed, int skipped, bool truncated)
		{
			lua_createtable(L, 0, 4);
			lua_pushinteger(L, scanned);
			lua_setfield(L, -2, "scanned");
			lua_pushinteger(L, completed);
			lua_setfield(L, -2, "captured");
			lua_pushinteger(L, skipped);
			lua_setfield(L, -2, "skipped");
			lua_pushboolean(L, truncated ? 1 : 0);
			lua_setfield(L, -2, "truncated");
		}

		inline void PushRestoreReport(lua_State* L, int scanned, int restored, int skipped, bool truncated)
		{
			lua_createtable(L, 0, 4);
			lua_pushinteger(L, scanned);
			lua_setfield(L, -2, "scanned");
			lua_pushinteger(L, restored);
			lua_setfield(L, -2, "restored");
			lua_pushinteger(L, skipped);
			lua_setfield(L, -2, "skipped");
			lua_pushboolean(L, truncated ? 1 : 0);
			lua_setfield(L, -2, "truncated");
		}

		inline void PushSnapshotHeader(lua_State* L)
		{
			lua_createtable(L, 0, 7);
			lua_pushinteger(L, kSnapshotFormatVersion);
			lua_setfield(L, -2, "formatVersion");
			lua_pushstring(L, "runtime-object-api");
			lua_setfield(L, -2, "source");
			double missionTime = 0.0;
			if (TryCallNoArgNumber(L, "GetTime", missionTime))
			{
				lua_pushnumber(L, missionTime);
				lua_setfield(L, -2, "durationSeconds");
			}
			lua_newtable(L);
			lua_setfield(L, -2, "objects");
		}

		inline bool ShouldCaptureHandle(lua_State* L, BZR::handle handle, const CaptureOptions& options)
		{
			int objectTeam = 0;
			if (!TryCallHandleInteger(L, "GetTeamNum", handle, objectTeam))
			{
				return false;
			}
			if (options.hasTeam && objectTeam != options.team)
			{
				return false;
			}
			if (!options.includePlayers && IsPlayerHandle(L, handle, objectTeam))
			{
				return false;
			}
			return true;
		}

		inline bool CaptureOneHandle(lua_State* L, int objectsIndex, BZR::handle handle,
			const CaptureOptions& options, int& captured, int& skipped)
		{
			if (!ShouldCaptureHandle(L, handle, options))
			{
				++skipped;
				return true;
			}
			if (!PushObjectDescriptor(L, handle))
			{
				++skipped;
				return true;
			}
			++captured;
			lua_rawseti(L, objectsIndex, captured);
			return true;
		}

		inline int CaptureFromHandleTable(lua_State* L, int handlesIndex, const CaptureOptions& options)
		{
			const int absHandles = AbsoluteStackIndex(L, handlesIndex);
			PushSnapshotHeader(L);
			const int snapshotIndex = AbsoluteStackIndex(L, -1);
			lua_getfield(L, snapshotIndex, "objects");
			const int objectsIndex = AbsoluteStackIndex(L, -1);

			int scanned = 0;
			int captured = 0;
			int skipped = 0;
			bool truncated = false;
			lua_pushnil(L);
			while (lua_next(L, absHandles) != 0)
			{
				if (scanned >= options.maxObjects)
				{
					lua_pop(L, 2);
					truncated = true;
					break;
				}
				++scanned;

				if (!IsHandleValue(L, -1))
				{
					++skipped;
					lua_pop(L, 1);
					continue;
				}
				const BZR::handle handle = ToHandle(L, -1);
				CaptureOneHandle(L, objectsIndex, handle, options, captured, skipped);
				lua_pop(L, 1);
			}

			lua_pop(L, 1); // objects
			lua_pushinteger(L, captured);
			lua_setfield(L, snapshotIndex, "objectCount");
			if (options.hasTeam)
			{
				lua_pushinteger(L, options.team);
				lua_setfield(L, snapshotIndex, "team");
			}
			PushReport(L, scanned, captured, skipped, truncated);
			lua_setfield(L, snapshotIndex, "captureReport");
			return 1;
		}

		inline int CaptureFromAllObjects(lua_State* L, const CaptureOptions& options)
		{
			const int base = lua_gettop(L);
			if (!GetGlobalFunction(L, "AllObjects"))
			{
				lua_pushnil(L);
				lua_pushstring(L, "Battlezone AllObjects() iterator is unavailable");
				return 2;
			}

			// Generic-for expressions are normalized to iterator/state/control.
			// Ask Lua for exactly three results so missing values become nil.
			if (lua_pcall(L, 0, 3, 0) != 0)
			{
				const char* message = lua_tostring(L, -1);
				const std::string error = message != nullptr ? message : "AllObjects() failed";
				lua_settop(L, base);
				lua_pushnil(L);
				lua_pushlstring(L, error.data(), error.size());
				return 2;
			}

			const int iteratorIndex = base + 1;
			const int stateIndex = base + 2;
			const int controlIndex = base + 3;
			if (!lua_isfunction(L, iteratorIndex))
			{
				lua_settop(L, base);
				lua_pushnil(L);
				lua_pushstring(L, "Battlezone AllObjects() did not return an iterator function");
				return 2;
			}

			PushSnapshotHeader(L);
			const int snapshotIndex = AbsoluteStackIndex(L, -1);
			lua_pushstring(L, "AllObjects");
			lua_setfield(L, snapshotIndex, "enumerator");
			lua_getfield(L, snapshotIndex, "objects");
			const int objectsIndex = AbsoluteStackIndex(L, -1);

			int scanned = 0;
			int captured = 0;
			int skipped = 0;
			bool truncated = false;

			while (true)
			{
				if (scanned >= options.maxObjects)
				{
					truncated = true;
					break;
				}

				lua_pushvalue(L, iteratorIndex);
				lua_pushvalue(L, stateIndex);
				lua_pushvalue(L, controlIndex);
				if (lua_pcall(L, 2, 1, 0) != 0)
				{
					const char* message = lua_tostring(L, -1);
					const std::string error = message != nullptr ? message : "AllObjects iterator failed";
					lua_settop(L, base);
					lua_pushnil(L);
					lua_pushlstring(L, error.data(), error.size());
					return 2;
				}
				if (lua_isnil(L, -1))
				{
					lua_pop(L, 1);
					break;
				}

				// Generic-for feeds the first return value back as the control value.
				lua_replace(L, controlIndex);
				++scanned;
				if (!IsHandleValue(L, controlIndex))
				{
					++skipped;
					continue;
				}
				const BZR::handle handle = ToHandle(L, controlIndex);
				CaptureOneHandle(L, objectsIndex, handle, options, captured, skipped);
			}

			lua_pop(L, 1); // objects
			lua_pushinteger(L, captured);
			lua_setfield(L, snapshotIndex, "objectCount");
			if (options.hasTeam)
			{
				lua_pushinteger(L, options.team);
				lua_setfield(L, snapshotIndex, "team");
			}
			PushReport(L, scanned, captured, skipped, truncated);
			lua_setfield(L, snapshotIndex, "captureReport");

			// Remove iterator/state/control, leaving only the snapshot as result.
			lua_remove(L, iteratorIndex);
			lua_remove(L, iteratorIndex);
			lua_remove(L, iteratorIndex);
			return 1;
		}

		inline bool CanRestoreWorld(lua_State* L, std::string& error)
		{
			bool networked = false;
			if (!TryCallNoArgBoolean(L, "IsNetGame", networked))
			{
				error = "could not query documented BZR IsNetGame() state";
				return false;
			}
			if (!networked)
			{
				return true;
			}

			bool host = false;
			if (!TryCallNoArgBoolean(L, "IsHosting", host))
			{
				error = "network game detected but BZR IsHosting() is unavailable";
				return false;
			}
			if (!host)
			{
				error = "mission continuity restoration must run on the multiplayer host";
				return false;
			}
			return true;
		}
	}

	inline int CaptureObject(lua_State* L)
	{
		const BZR::handle handle = CheckHandle(L, 1);
		if (!Detail::PushObjectDescriptor(L, handle))
		{
			lua_pushnil(L);
			lua_pushstring(L, "object could not be described with the documented BZR ODF/team/transform APIs");
			return 2;
		}
		lua_pushnil(L);
		return 2;
	}

	inline int CaptureObjects(lua_State* L)
	{
		luaL_checktype(L, 1, LUA_TTABLE);
		const Detail::CaptureOptions options = Detail::ReadCaptureOptions(L, 2);
		return Detail::CaptureFromHandleTable(L, 1, options);
	}

	inline int CaptureWorld(lua_State* L)
	{
		const Detail::CaptureOptions options = Detail::ReadCaptureOptions(L, 1);
		return Detail::CaptureFromAllObjects(L, options);
	}

	inline int Restore(lua_State* L)
	{
		luaL_checktype(L, 1, LUA_TTABLE);
		const int snapshotIndex = AbsoluteStackIndex(L, 1);
		const Detail::RestoreOptions options = Detail::ReadRestoreOptions(L, 2);
		std::string authorityError;
		if (!Detail::CanRestoreWorld(L, authorityError))
		{
			lua_pushnil(L);
			lua_createtable(L, 0, 4);
			lua_pushinteger(L, 0);
			lua_setfield(L, -2, "restored");
			lua_pushinteger(L, 0);
			lua_setfield(L, -2, "skipped");
			lua_pushlstring(L, authorityError.data(), authorityError.size());
			lua_setfield(L, -2, "error");
			return 2;
		}

		lua_getfield(L, snapshotIndex, "objects");
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			lua_pushnil(L);
			lua_createtable(L, 0, 2);
			lua_pushstring(L, "snapshot has no objects table");
			lua_setfield(L, -2, "error");
			return 2;
		}
		const int objectsIndex = AbsoluteStackIndex(L, -1);

		lua_newtable(L);
		const int handlesIndex = AbsoluteStackIndex(L, -1);
		int scanned = 0;
		int restored = 0;
		int skipped = 0;
		bool truncated = false;

		lua_pushnil(L);
		while (lua_next(L, objectsIndex) != 0)
		{
			if (scanned >= options.maxObjects)
			{
				lua_pop(L, 2);
				truncated = true;
				break;
			}
			++scanned;

			std::string odf;
			int team = 0;
			Detail::MatrixData matrix{};
			bool isPlayer = false;
			double health = 0.0;
			bool hasHealth = false;
			double ammo = 0.0;
			bool hasAmmo = false;
			if (!Detail::ReadDescriptor(L, -1, options, odf, team, matrix, isPlayer,
				health, hasHealth, ammo, hasAmmo) || (options.skipPlayers && isPlayer))
			{
				++skipped;
				lua_pop(L, 1);
				continue;
			}

			BZR::handle newHandle{};
			if (!Detail::TryBuildObject(L, odf, team, matrix, newHandle))
			{
				++skipped;
				lua_pop(L, 1);
				continue;
			}

			if (options.restoreHealth && hasHealth)
			{
				double maxHealth = 0.0;
				if (Detail::TryCallHandleNumber(L, "GetMaxHealth", newHandle, maxHealth) && maxHealth >= 0.0)
				{
					const double ratio = health < 0.0 ? 0.0 : (health > 1.0 ? 1.0 : health);
					Detail::TrySetHandleNumber(L, "SetCurHealth", newHandle, ratio * maxHealth);
				}
			}
			if (options.restoreAmmo && hasAmmo)
			{
				double maxAmmo = 0.0;
				if (Detail::TryCallHandleNumber(L, "GetMaxAmmo", newHandle, maxAmmo) && maxAmmo >= 0.0)
				{
					const double ratio = ammo < 0.0 ? 0.0 : (ammo > 1.0 ? 1.0 : ammo);
					Detail::TrySetHandleNumber(L, "SetCurAmmo", newHandle, ratio * maxAmmo);
				}
			}

			++restored;
			Detail::PushHandle(L, newHandle);
			lua_rawseti(L, handlesIndex, restored);
			lua_pop(L, 1);
		}

		lua_remove(L, objectsIndex);
		Detail::PushRestoreReport(L, scanned, restored, skipped, truncated);
		return 2;
	}

	inline int GetCapabilities(lua_State* L)
	{
		lua_createtable(L, 0, 10);
		lua_pushinteger(L, Detail::kSnapshotFormatVersion);
		lua_setfield(L, -2, "snapshotFormatVersion");
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "captureObject");
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "captureWorld");
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "restoreObjects");
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "translationOffset");
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "odfRemap");
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "healthRatio");
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "ammoRatio");
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "multiplayerHostGuard");
		lua_pushboolean(L, 0);
		lua_setfield(L, -2, "nativeSaveParsing");
		return 1;
	}

	inline void Install(lua_State* L)
	{
		lua_getglobal(L, "exu");
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			return;
		}

		lua_newtable(L);
		const luaL_Reg functions[] = {
			{ "CaptureObject", &CaptureObject },
			{ "CaptureObjects", &CaptureObjects },
			{ "CaptureWorld", &CaptureWorld },
			{ "Restore", &Restore },
			{ "GetCapabilities", &GetCapabilities },
			{ nullptr, nullptr },
		};
		luaL_register(L, nullptr, functions);
		lua_setfield(L, -2, "continuity");
		lua_pop(L, 1);
	}
}
