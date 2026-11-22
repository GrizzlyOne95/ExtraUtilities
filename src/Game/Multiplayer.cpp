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
#include "OpenShimBridge.h"
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

    namespace
    {
struct NetworkTrafficSnapshot
{
    uint32_t version = 1, structSize = sizeof(NetworkTrafficSnapshot);
    uint32_t available = 0, windowMs = 2000;
    uint64_t sampledAtMs = 0, txBytes = 0, rxBytes = 0, txPackets = 0, rxPackets = 0;
    uint32_t txBytesPerSecond = 0, rxBytesPerSecond = 0;
    uint32_t txPacketsPerSecond = 0, rxPacketsPerSecond = 0;
    uint32_t route = 0; // 0 idle/unknown, 1 relay observed, 2 direct observed, 3 mixed, 4 unclassified UDP
    uint32_t routeAgeMs = UINT32_MAX, receiveAgeMs = UINT32_MAX;
    uint32_t coverageLimited = 0; // pending completion registration overflow/unsupported path
};
static_assert(sizeof(NetworkTrafficSnapshot) == 88, "SDK traffic record layout");


    }
    int GetNetworkTrafficStats(lua_State* L)
    {
        using Fn = BOOL (WINAPI*)(void*, DWORD);
        static OpenShimBridge::CachedExport<Fn> bridge("OpenShimGetNetworkTrafficSnapshot");
        NetworkTrafficSnapshot snapshot;
        const auto read = bridge.Get();
        if (!RuntimeGate::IsSupported() || !isNetGame.Read() || !read || !read(&snapshot, sizeof(snapshot)) ||
            snapshot.version != 1 || snapshot.structSize != sizeof(snapshot) || !snapshot.available)
        { lua_pushnil(L); return 1; }
        lua_newtable(L);
        auto number = [&](const char* key, double value) { lua_pushnumber(L,value); lua_setfield(L,-2,key); };
        number("sampledAtMs",static_cast<double>(snapshot.sampledAtMs));
        number("windowMs",snapshot.windowMs);
        number("txBytes",static_cast<double>(snapshot.txBytes)); number("rxBytes",static_cast<double>(snapshot.rxBytes));
        number("txPackets",static_cast<double>(snapshot.txPackets)); number("rxPackets",static_cast<double>(snapshot.rxPackets));
        number("txBytesPerSecond",snapshot.txBytesPerSecond); number("rxBytesPerSecond",snapshot.rxBytesPerSecond);
        number("txPacketsPerSecond",snapshot.txPacketsPerSecond); number("rxPacketsPerSecond",snapshot.rxPacketsPerSecond);
        if (snapshot.receiveAgeMs != UINT32_MAX) number("receiveAgeMs",snapshot.receiveAgeMs);
        if (snapshot.routeAgeMs != UINT32_MAX) number("routeAgeMs",snapshot.routeAgeMs);
        const char* routes[] = {"idle", "relay", "direct", "mixed", "unknown"};
        lua_pushstring(L,snapshot.route < 5 ? routes[snapshot.route] : "unknown"); lua_setfield(L,-2,"route");
        lua_pushboolean(L,snapshot.coverageLimited != 0); lua_setfield(L,-2,"coverageLimited");
        return 1;
    }
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

	// Net identity
	//
	// Engine facts (GOG 2.2.301, Ghidra on battlezone98redux.exe):
	//  * DistributedObject::SetLocal 0x004B8460 gives a new local object
	//    id = (word [myNetID] << 16) + counter [0x008E80B0] at
	//    DistributedObject+0x64 and the owner id (word [0x009175BC], set to the
	//    same local user id) at +0x62; the state byte at +0x68 is 0 unowned,
	//    1 local, 2 remote. A remote copy is filed under the same id in the
	//    remote map, so the id names one object on every machine. 1.5 source:
	//    DistributedObject::GetID (0x0048A252) returns dwLocalID and
	//    GetPlayerID (0x0048A206) returns activnet_id.
	//  * myNetID is a u16: SetLocal reads it with 'movzx eax, word ptr
	//    [0x9180d4]' at 0x004B8548.
	//  * Player records (NextNetPlayer / FindNetPlayer map) keep the u16 id at
	//    +0x28 and the team byte at +0x68, the same reads ReadNetworkStatsSeh
	//    makes above.
	//  * NetSession +0x410 is start_location, three floats: the thiscall
	//    getter 0x00570470 and setter 0x005704A0 both 'add eax, 0x410' and then
	//    copy three dwords.
	namespace NetIdentity
	{
		constexpr uintptr_t kOwnerIdOffset = 0x62; // within the DistributedObject
		constexpr uintptr_t kNetIdOffset = 0x64; // within the DistributedObject
		constexpr uintptr_t kPlayerIdOffset = 0x28;
		constexpr uintptr_t kPlayerTeamOffset = 0x68;
		constexpr uintptr_t kStartLocationOffset = 0x410; // within the NetSession
		constexpr unsigned int kMaxPlayers = 64;
		constexpr unsigned int kNameCapacity = 34;
		static_assert(kOwnerIdOffset + sizeof(uint16_t) <= kNetIdOffset, "owner id precedes the net id");
		static_assert(kNetIdOffset + sizeof(uint32_t) <= BZR::Ownership::stateOffset, "net id precedes the state byte");

		struct PlayerEntry
		{
			unsigned short id = 0;
			int team = 0;
			char name[kNameCapacity] = {};
		};

		struct StartLocation
		{
			float x, y, z;
		};
		static_assert(sizeof(StartLocation) == 12, "start_location is three floats");

		// SetStartLocation keeps the engine value it first replaced so the end
		// of the mission can put it back. Only the host's DM/ST Init picks a new
		// spawn point (0x0056C320 calls 0x005D9100 then the setter), so a
		// client would otherwise carry a script's value into the next mission.
		void* savedSession = nullptr;
		StartLocation savedStart{};

		unsigned char* DistributedObjectOf(BZR::GameObject* obj)
		{
			return reinterpret_cast<unsigned char*>(obj) + BZR::Ownership::distributedObjectOffset;
		}

		void* Session()
		{
			return *reinterpret_cast<void* const*>(EngineAddresses::Multiplayer::NetSession);
		}

		uint16_t LocalNetId()
		{
			return *reinterpret_cast<const uint16_t*>(EngineAddresses::Multiplayer::myNetID);
		}

		bool Finite(float v)
		{
			return v == v && v <= 3.0e38f && v >= -3.0e38f;
		}
	}

	// exu.GetNetworkID(h) -> integer | nil
	int GetNetworkID(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		uint32_t id = 0;
		uint8_t ownership = 0;
		if (obj == nullptr || !Seh::Guard("GetNetworkID", [&] {
				const unsigned char* dobj = NetIdentity::DistributedObjectOf(obj);
				id = *reinterpret_cast<const uint32_t*>(dobj + NetIdentity::kNetIdOffset);
				ownership = dobj[BZR::Ownership::stateOffset];
			}) || id == 0 || (ownership != BZR::Ownership::stateLocal && ownership != BZR::Ownership::stateRemote))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, static_cast<lua_Number>(id));
		return 1;
	}

	// exu.GetHandleFromNetworkID(id) -> handle | nil
	int GetHandleFromNetworkID(lua_State* L)
	{
		const lua_Number value = luaL_checknumber(L, 1);
		if (!RuntimeGate::IsSupported() || !(value >= 1.0 && value <= 4294967295.0) ||
			value != static_cast<lua_Number>(static_cast<uint32_t>(value)))
		{
			lua_pushnil(L);
			return 1;
		}
		const uint32_t id = static_cast<uint32_t>(value);
		using Fn = BZR::GameObject* (__cdecl*)(uint32_t);
		BZR::handle h = 0;
		if (!Seh::Guard("GetHandleFromNetworkID", [&] {
				BZR::GameObject* obj = reinterpret_cast<Fn>(EngineAddresses::Multiplayer::DistributedObjectGetGameObject)(id);
				if (obj != nullptr && BZR::GameObject::IsLiveArenaObject(obj))
				{
					h = BZR::GameObject::GetHandle(obj);
				}
			}) || h == 0 || BZR::GameObject::GetObj(h) == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
		return 1;
	}

	// exu.GetOwnerID(h) -> integer | nil
	int GetOwnerID(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		uint16_t owner = 0;
		uint8_t ownership = 0;
		if (obj == nullptr || !Seh::Guard("GetOwnerID", [&] {
				const unsigned char* dobj = NetIdentity::DistributedObjectOf(obj);
				owner = *reinterpret_cast<const uint16_t*>(dobj + NetIdentity::kOwnerIdOffset);
				ownership = dobj[BZR::Ownership::stateOffset];
			}) || (ownership != BZR::Ownership::stateLocal && ownership != BZR::Ownership::stateRemote))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushinteger(L, owner);
		return 1;
	}

	// exu.AllPlayers() -> { {id, team, name, isLocal}, ... }
	int AllPlayers(lua_State* L)
	{
		NetIdentity::PlayerEntry players[NetIdentity::kMaxPlayers];
		unsigned int count = 0;
		uint16_t localId = 0;
		if (RuntimeGate::IsSupported() && isNetGame.Read())
		{
			using NextFn = void* (__cdecl*)(void*);
			using NameFn = const char* (__thiscall*)(void*);
			const bool ok = Seh::Guard("AllPlayers", [&] {
				localId = NetIdentity::LocalNetId();
				void* player = nullptr;
				while (count < NetIdentity::kMaxPlayers &&
					(player = reinterpret_cast<NextFn>(EngineAddresses::Multiplayer::NextNetPlayer)(player)) != nullptr)
				{
					const auto* bytes = static_cast<const unsigned char*>(player);
					NetIdentity::PlayerEntry& entry = players[count];
					entry.id = *reinterpret_cast<const uint16_t*>(bytes + NetIdentity::kPlayerIdOffset);
					entry.team = bytes[NetIdentity::kPlayerTeamOffset];
					const char* name = reinterpret_cast<NameFn>(EngineAddresses::Multiplayer::NetPlayerName)(player);
					if (name)
					{
						for (unsigned int i = 0; i < NetIdentity::kNameCapacity - 1 && name[i]; ++i) entry.name[i] = name[i];
					}
					++count;
				}
			});
			if (!ok) count = 0;
		}

		lua_createtable(L, static_cast<int>(count), 0);
		for (unsigned int i = 0; i < count; ++i)
		{
			const NetIdentity::PlayerEntry& entry = players[i];
			lua_createtable(L, 0, 4);
			lua_pushinteger(L, entry.id); lua_setfield(L, -2, "id");
			if (entry.team <= 15) { lua_pushinteger(L, entry.team); lua_setfield(L, -2, "team"); }
			lua_pushstring(L, entry.name); lua_setfield(L, -2, "name");
			lua_pushboolean(L, entry.id == localId); lua_setfield(L, -2, "isLocal");
			lua_rawseti(L, -2, static_cast<int>(i + 1));
		}
		return 1;
	}

	// exu.IsDeathmatch() -> boolean
	int IsDeathmatch(lua_State* L)
	{
		bool result = false;
		if (RuntimeGate::IsSupported() && isNetGame.Read())
		{
			result = *reinterpret_cast<const bool*>(EngineAddresses::Multiplayer::deathmatch);
		}
		lua_pushboolean(L, result);
		return 1;
	}

	// exu.GetNetGameOptions() -> table | nil
	int GetNetGameOptions(lua_State* L)
	{
		if (!RuntimeGate::IsSupported() || !isNetGame.Read())
		{
			lua_pushnil(L);
			return 1;
		}
		using namespace EngineAddresses::Multiplayer;
		auto flag = [](uintptr_t address) { return *reinterpret_cast<const bool*>(address); };
		auto integer = [](uintptr_t address) { return *reinterpret_cast<const int*>(address); };
		lua_createtable(L, 0, 8);
		lua_pushinteger(L, integer(timeLimit)); lua_setfield(L, -2, "timeLimit");
		lua_pushinteger(L, integer(killLimit)); lua_setfield(L, -2, "killLimit");
		lua_pushinteger(L, integer(startLives)); lua_setfield(L, -2, "lives");
		lua_pushboolean(L, flag(sniperOption)); lua_setfield(L, -2, "sniper");
		lua_pushboolean(L, flag(barracksOption)); lua_setfield(L, -2, "barracks");
		lua_pushboolean(L, flag(commSatOption)); lua_setfield(L, -2, "commSat");
		lua_pushboolean(L, flag(splinterOption)); lua_setfield(L, -2, "splinter");
		lua_pushboolean(L, flag(syncJoin)); lua_setfield(L, -2, "syncJoin");
		return 1;
	}

	// exu.GetStartLocation() -> vector | nil
	int GetStartLocation(lua_State* L)
	{
		NetIdentity::StartLocation value{};
		bool have = false;
		if (!RuntimeGate::IsSupported() || !Seh::Guard("GetStartLocation", [&] {
				const auto* session = static_cast<const unsigned char*>(NetIdentity::Session());
				if (session == nullptr) return;
				value = *reinterpret_cast<const NetIdentity::StartLocation*>(session + NetIdentity::kStartLocationOffset);
				have = true;
			}) || !have)
		{
			lua_pushnil(L);
			return 1;
		}
		PushVector(L, BZR::VECTOR_3D(value.x, value.y, value.z));
		return 1;
	}

	// exu.SetStartLocation(pos) -> boolean
	int SetStartLocation(lua_State* L)
	{
		const BZR::VECTOR_3D pos = CheckVectorOrSingles(L, 1);
		bool written = false;
		if (RuntimeGate::IsSupported() && NetIdentity::Finite(pos.x) && NetIdentity::Finite(pos.y) && NetIdentity::Finite(pos.z))
		{
			Seh::Guard("SetStartLocation", [&] {
				auto* session = static_cast<unsigned char*>(NetIdentity::Session());
				if (session == nullptr) return;
				auto* slot = reinterpret_cast<NetIdentity::StartLocation*>(session + NetIdentity::kStartLocationOffset);
				if (NetIdentity::savedSession != session)
				{
					NetIdentity::savedStart = *slot;
					NetIdentity::savedSession = session;
				}
				*slot = NetIdentity::StartLocation{ pos.x, pos.y, pos.z };
				written = true;
			});
		}
		lua_pushboolean(L, written);
		return 1;
	}

	void ResetMissionState() noexcept
	{
		void* const saved = NetIdentity::savedSession;
		NetIdentity::savedSession = nullptr;
		if (saved == nullptr || !RuntimeGate::IsSupported()) return;
		// The engine zeroes the global right after deleting the session
		// (0x005729ED), so a match means the saved session is still live.
		Seh::Guard("ResetNetStartLocation", [&] {
			if (NetIdentity::Session() != saved) return;
			*reinterpret_cast<NetIdentity::StartLocation*>(static_cast<unsigned char*>(saved) + NetIdentity::kStartLocationOffset) = NetIdentity::savedStart;
		});
	}
}
