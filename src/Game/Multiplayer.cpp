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

#include "Multiplayer.h"

#include "LuaHelpers.h"
#include "Util/Logging.h"
#include "Util/SehGuard.h"

#include <Windows.h>

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <lua.hpp>

namespace ExtraUtilities::Lua::Multiplayer
{
	namespace
	{
		enum class AdoptResult
		{
			Adopted,
			AlreadyLocal,
			NotRemote,
			ForeignProcess,
			ClaimRefused,
			ClaimFailed,
			NoAi,
			Fault,
		};

		const char* Describe(AdoptResult result)
		{
			switch (result)
			{
			case AdoptResult::Adopted: return "adopted";
			case AdoptResult::AlreadyLocal: return "already local";
			case AdoptResult::NotRemote: return "object is not a remote copy";
			case AdoptResult::ForeignProcess: return "remote copy carries an unexpected AI process";
			case AdoptResult::ClaimRefused: return "engine refuses to claim this object";
			case AdoptResult::ClaimFailed: return "SetLocal did not take";
			case AdoptResult::NoAi: return "no AI class resolved for this object";
			default: return "engine fault";
			}
		}

		using GetSimFn = void* (__thiscall*)(void* distributedObject);
		using SetTeamFn = void(__thiscall*)(void* distributedObject, int team);
		using DeletingDtorFn = void(__thiscall*)(void* self, int flags);

		// The engine's own pattern (hop-in, 0x005A1550; and SetLocal followed
		// by AiProcess::Attach at 0x00479790 / 0x004ADB00), done on the
		// machine that is taking the object over:
		//   1. refuse what SetLocal would refuse (sim flag 0x80), before
		//      changing anything;
		//   2. delete the do-nothing NetProcess a remote copy carries
		//      (its destructor clears GameObject::aiProcess);
		//   3. SetLocal: claim ownership; the old owner's SetRemote then
		//      deletes its AI process, so only one machine runs the unit;
		//   4. optionally set the team (Attach picks the AI class from it);
		//   5. Attach a real AI process from the object's ODF class.
		AdoptResult AdoptSeh(BZR::GameObject* obj, void* mission, bool setTeam, int team, unsigned int& faultCode)
		{
			__try
			{
				auto* dist = reinterpret_cast<uint8_t*>(obj) + BZR::Ownership::distributedObjectOffset;
				const uint8_t ownership = dist[BZR::Ownership::stateOffset];
				void* process = obj->aiProcess;

				if (ownership == BZR::Ownership::stateLocal && process != nullptr)
				{
					return AdoptResult::AlreadyLocal;
				}
				if (ownership != BZR::Ownership::stateLocal && ownership != BZR::Ownership::stateRemote)
				{
					return AdoptResult::NotRemote;
				}

				if (ownership == BZR::Ownership::stateRemote)
				{
					auto getSim = reinterpret_cast<GetSimFn>((*reinterpret_cast<uintptr_t**>(dist))[0x30 / 4]);
					auto* sim = static_cast<uint8_t*>(getSim(dist));
					if (sim != nullptr && (*reinterpret_cast<uint32_t*>(sim + 0x14) & 0x80) != 0)
					{
						return AdoptResult::ClaimRefused;
					}

					if (process != nullptr)
					{
						if (*reinterpret_cast<uintptr_t*>(process) != BZR::Ownership::netProcessVftable)
						{
							return AdoptResult::ForeignProcess;
						}
						auto deleting = reinterpret_cast<DeletingDtorFn>((*reinterpret_cast<uintptr_t**>(process))[0]);
						deleting(process, 1);
						// The destructor clears it through SetAIProcess(0); never
						// leave a dangling pointer if a build ever differs.
						obj->aiProcess = nullptr;
					}

					BZR::Ownership::SetLocal(dist);
					if (dist[BZR::Ownership::stateOffset] != BZR::Ownership::stateLocal)
					{
						return AdoptResult::ClaimFailed;
					}
				}

				if (setTeam)
				{
					auto setTeamFn = reinterpret_cast<SetTeamFn>((*reinterpret_cast<uintptr_t**>(dist))[0x8 / 4]);
					setTeamFn(dist, team);
				}

				BZR::Ownership::AiProcessAttach(mission, obj);
				return obj->aiProcess != nullptr ? AdoptResult::Adopted : AdoptResult::NoAi;
			}
			__except (Seh::Filter(GetExceptionCode(), faultCode))
			{
				return AdoptResult::Fault;
			}
		}

		AdoptResult Adopt(BZR::GameObject* obj, void* mission, bool setTeam, int team)
		{
			unsigned int faultCode = 0;
			const AdoptResult result = Seh::CatchCpp("AdoptObject",
				[&] { return AdoptSeh(obj, mission, setTeam, team, faultCode); }, AdoptResult::Fault);
			if (result == AdoptResult::Fault)
			{
				Logging::LogMessage("[EXU::AdoptObject] fault obj=%p code=0x%08X", obj, faultCode);
			}
			return result;
		}

		// Run stock BuildObject with a temporary patch active. The call must be
		// protected: an error would longjmp past Unload() and leave the patch
		// on for the rest of the mission, forcing every later BuildObject
		// async or sync.
		int BuildObjectWithPatch(lua_State* L, InlinePatch& patch)
		{
			const int argC = lua_gettop(L);

			lua_getglobal(L, "BuildObject");
			lua_insert(L, 1);

			patch.Reload();
			const int status = lua_pcall(L, argC, 1, 0);
			patch.Unload();

			if (status != 0)
			{
				return lua_error(L);
			}
			return 1;
		}
	}

	int BuildAsyncObject(lua_State* L)
	{
		return BuildObjectWithPatch(L, buildObjectAlwaysAsync);
	}

	int BuildSyncObject(lua_State* L)
	{
		return BuildObjectWithPatch(L, buildObjectAlwaysSync);
	}

	namespace
	{
		struct NetworkStats
		{
			int team = 0, deaths = 0, kills = 0, lives = 0;
			bool hasDeaths = false, hasKills = false, hasLives = false, remote = false;
			unsigned int ping = 0;
			int loss = 0, pingSamples = 0;
			char name[34] = {};
		};
		bool QualifyNetworkStats()
		{
			static bool qualified = false;
			if (qualified) return true;
			if (!RuntimeGate::IsSupported()) return false;
			unsigned int matched = 0;
			for (const auto& anchor : BzrBuildProfile::kRuntimeAnchors)
			{
				if (std::strncmp(anchor.name, "Multiplayer HUD ", 16) != 0 ||
					std::strcmp(anchor.name, "Multiplayer HUD KillNotice") == 0) continue;
				if (!BuildValidation::Detail::MatchAnchor(GetModuleHandleA(nullptr), anchor)) return false;
				++matched;
			}
			return qualified = matched == 6;
		}
		bool ReadNetworkStatsSeh(unsigned short id, NetworkStats& out, unsigned int& faultCode)
		{
			using namespace EngineAddresses::Multiplayer;
			using FindFn = void* (__cdecl*)(unsigned short);
			using DataFn = void* (__thiscall*)(void*, int);
			using NameFn = const char* (__thiscall*)(void*);
			using PingFn = unsigned int (__thiscall*)(void*);
			using LossFn = int (__thiscall*)(void*);
			__try
			{
				auto* player = static_cast<unsigned char*>(reinterpret_cast<FindFn>(FindNetPlayer)(id));
				if (!player || *reinterpret_cast<unsigned short*>(player + 0x28) != id) return false;
				out.team = player[0x68];
				if (out.team > 15) return false;
				const char* name = reinterpret_cast<NameFn>(NetPlayerName)(player);
				if (name) for (unsigned int i = 0; i < sizeof(out.name)-1 && name[i]; ++i) out.name[i] = name[i];
				const int slots[] = {16,15,19};
				int* values[] = {&out.deaths,&out.kills,&out.lives};
				bool* present[] = {&out.hasDeaths,&out.hasKills,&out.hasLives};
				for (unsigned int i=0;i<3;++i)
				{
					auto** data = static_cast<unsigned char**>(reinterpret_cast<DataFn>(NetPlayerData)(player, slots[i]));
					// The stock scoreboard accepts precisely one 32-bit scalar.
					if (data && reinterpret_cast<uintptr_t>(data[1]) - reinterpret_cast<uintptr_t>(data[0]) == sizeof(int))
					{
						*values[i] = *reinterpret_cast<int*>(data[0]);
						*present[i] = true;
					}
				}
				out.remote = id != myNetID.Read();
				if (out.remote)
				{
					// Bound the native reader's iteration before calling it.
					const int count = *reinterpret_cast<int*>(player + 0x94);
					if (count < 0 || count > 10) return false;
					out.pingSamples = count;
					out.ping = (std::min)(9999u, reinterpret_cast<PingFn>(NetPlayerPing)(player));
					out.loss = reinterpret_cast<LossFn>(NetPlayerLoss)(player);
					if (out.loss < 0 || out.loss > 100) return false;
				}
				return true;
			}
			__except(Seh::Filter(GetExceptionCode(),faultCode)) { return false; }
		}
	}
	// A copied snapshot of the same data the stock scoreboard reads. It does
	// not toggle/refresh the scoreboard, modify scores or retain NetPlayer pointers.
	int GetPlayerNetworkStats(lua_State* L)
	{
		const int id = luaL_checkinteger(L, 1);
		if (id < 0 || id > 65534) return luaL_argerror(L, 1, "network id must be 0-65534");
		NetworkStats snapshot;
		unsigned int faultCode = 0;
		if (!QualifyNetworkStats() || !isNetGame.Read() ||
			!Seh::CatchCpp("GetPlayerNetworkStats", [&] { return ReadNetworkStatsSeh(static_cast<unsigned short>(id),snapshot,faultCode); }, false))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_newtable(L);
		auto integer = [&](const char* key, int value) { lua_pushinteger(L,value); lua_setfield(L,-2,key); };
		integer("id",id); integer("team",snapshot.team);
		lua_pushstring(L,snapshot.name); lua_setfield(L,-2,"name");
		integer("deaths",snapshot.deaths);
		integer("kills",snapshot.kills);
		integer("lives",snapshot.lives);
		if (snapshot.remote) { integer("pingMs",snapshot.ping); integer("lossPercent",snapshot.loss); integer("pingSamples",snapshot.pingSamples); }
		return 1;
	}

	int GetLives(lua_State* L)
	{
		lua_pushinteger(L, lives.Read());
		return 1;
	}

	int SetLives(lua_State* L)
	{
		int newLives = luaL_checkinteger(L, 1);
		lives.Write(newLives);

		// This will crash if done in singleplayer,
		// not like lives do anything in SP anyways tho
		if (RuntimeGate::IsSupported() && isNetGame.Read() == true)
		{
			BZR::Multiplayer::UpdateLives();
		}

		return 0;
	}

	int GetMyNetID(lua_State* L)
	{
		lua_pushinteger(L, myNetID.Read());
		return 1;
	}

	int GetShowScoreboard(lua_State* L)
	{
		lua_pushboolean(L, showScoreboard.Read());
		return 1;
	}

	int SetShowScoreboard(lua_State* L)
	{
		bool status = CheckBool(L, 1);
		showScoreboard.Write(status);
		return 0;
	}

	int DisableStartingRecycler(lua_State*)
	{
		skipStartingRecycler.Reload();
		return 0;
	}

	// exu.AdoptObject(h [, team]) -> true | false, reason
	// Network games only, on the machine taking the object over. Claims a
	// remote AI object and gives it a working AI process here, the way the
	// engine does when a pilot boards an empty craft. Pass team to move it to
	// that team first (the AI class depends on it).
	int AdoptObject(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const bool setTeam = !lua_isnoneornil(L, 2);
		const int team = setTeam ? static_cast<int>(luaL_checkinteger(L, 2)) : 0;
		if (setTeam && (team < 0 || team > 15))
		{
			return luaL_argerror(L, 2, "team must be 0-15");
		}

		const char* reason = nullptr;
		if (!RuntimeGate::IsSupported()) reason = "unsupported game build";
		else if (isNetGame.Read() != true) reason = "not a network game";

		BZR::GameObject* obj = reason == nullptr ? BZR::GameObject::GetObj(h) : nullptr;
		void* mission = nullptr;
		if (reason == nullptr && obj == nullptr) reason = "invalid handle";
		else if (reason == nullptr && obj == *reinterpret_cast<BZR::GameObject**>(BZR::GameObject::p_userObject)) reason = "cannot adopt the player's own craft";
		else if (reason == nullptr && (mission = *BZR::Ownership::p_currentMission) == nullptr) reason = "no mission running";

		if (reason == nullptr)
		{
			const AdoptResult result = Adopt(obj, mission, setTeam, team);
			if (result == AdoptResult::Adopted || result == AdoptResult::AlreadyLocal)
			{
				lua_pushboolean(L, 1);
				return 1;
			}
			reason = Describe(result);
		}

		lua_pushboolean(L, 0);
		lua_pushstring(L, reason);
		return 2;
	}
}
