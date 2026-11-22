/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

// Engine facts used here (Redux = GOG 2.2.301, Ghidra on
// /battlezone98redux.exe-007c64 plus raw bytes; 1.5 = PDB-matched decomp in
// Battlezone_Source/BZ1/1.5/all_decompiled.c). Addresses live in exu.json
// group SmallCalls, with the citations repeated in each entry's note.
//
// - ClearMessages: Redux FUN_007A47B0(color, text) clears the chat/message
//   list when text == NULL ("if (param_2 == 0) { FUN_007a7bb0(); ... }"); the
//   engine itself calls it with (0, 0) from Net::~Net at 0x005703EC.
// - Eject sound: Craft::EjectPilot (FUN_004ADCC0) reads the char* at
//   0x008E7C9C ("bail.wav") at 0x004ADF01 and passes it to DoAudio.
// - Hop-out sound: Craft::AbandonPilot (FUN_004ADF20) pushes the "jump.wav"
//   literal with 68 A0 83 87 00 at 0x004AE0E9.
// - Eject ratio: EjectPilot loads Craft+0x288 at 0x004ADE1A
//   (F3 0F 10 89 88 02 00 00 movss xmm1,[ecx+0x288]) and multiplies it by 2.0
//   (0x008A2750); 1.5 Craft::EjectPilot: "(fPersonEjectRatio*2)-1.0 < Random()".
// - MakeScrapField: Redux FUN_005C65A0(float* pos, int count), called cdecl by
//   ScrapField::Simulate (FUN_005C6940, ".?AVScrapField@@" vtable slot 15):
//   push count; push pos; call; add esp, 8.
// - Satellite: Set_View(entity, 8) runs Toggle_Satellite_View (FUN_0061BE90):
//   from the satellite view it returns to the last view, otherwise it applies
//   the satellite view, which first checks FUN_0049F500 (satellite available).
// - Unit messages: Redux Say FUN_005F91E0(craft, msg, unused); GetUnitMsg
//   FUN_005F8FB0(craft, msg); QueueCB FUN_0043D1D0(name, owner, priority);
//   KillCBQueue FUN_0043D4F0(0); queue head DAT_00915640. Say gets the queue
//   owner from the user object with "8B 4D FC 83 C1 18 8B 55 FC 8B 42 18
//   8B 50 30 FF D2" (ecx = user + 0x18; call [vtable + 0x30], no stack args).
// - FileExists (not from bzx): stock Lua UseItem is FUN_00506E80 (luaL_Reg
//   "UseItem" at 0x00872278). It loads with FUN_00481AF0 -> FUN_00481B10
//   (refuses an empty name or a "null" prefix) -> FUN_00481B80, which checks
//   the resource cache (FUN_004817A0) and then asks FUN_00829150 for the size
//   (file index bsearch in FUN_00828850, then a pak record or a loose fopen);
//   when that returns 0 it logs the
//   error buffer 0x009467C0 ("%s\n" via FUN_007D6B70 at 0x00481C7F), which
//   FUN_00828850 filled with "Couldn't find file \"%.16s\"". The engine's
//   ResourceFileSize, FUN_00481A60 -> FUN_00481A80, runs the same cache check
//   and the same FUN_00829150 but never calls the logger; UseItem itself
//   calls it for the length it returns. Address: exu.json PathBlock group.

#include "SmallCalls.h"

#include "Camera.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"
#include "SmallCallsLogic.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/MsvcRtti.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "Util/StorageApi.h"
#include "bzr.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ExtraUtilities::Lua::SmallCalls
{
	namespace
	{
		namespace Logic = ExtraUtilities::SmallCallsLogic;
		namespace Addr = EngineAddresses::SmallCalls;
		namespace ResourceAddr = EngineAddresses::PathBlock;

		using ChatMessageAddFn = void(__cdecl*)(int color, const char* text);
		using MakeScrapFieldFn = void(__cdecl*)(const float* position, int count);
		using SatelliteAvailableFn = int(__cdecl*)();
		using SayFn = void(__cdecl*)(void* craft, int unitMsg, int unused);
		using UnitMsgFilenameFn = const char*(__cdecl*)(void* craft, int unitMsg);
		using QueueFn = bool(__cdecl*)(const char* name, void* owner, unsigned priority);
		using KillQueueFn = void(__cdecl*)(int);
		using OwnerFn = void*(__thiscall*)(void* component);
		using ResourceFileSizeFn = int(__cdecl*)(const char* fileName);

		// Craft field the stock eject roll reads (see the header comment).
		constexpr std::size_t kCraftEjectRatioOffset = 0x288;
		// Say: ecx = user + 0x18, call [vtable + 0x30].
		constexpr std::size_t kUserOwnerComponentOffset = 0x18;
		constexpr std::size_t kOwnerVtableSlot = 0x30 / sizeof(void*);
		// GetUnitMsg returns a class char[16] slot and QueueCB copies the name
		// into a char[16] item field, so 15 chars plus the terminator is the cap.
		constexpr std::size_t kUnitMsgNameBytes = 16;
		constexpr int kSatelliteToggleView = static_cast<int>(BZR::Camera::View::TOGGLE_SATELLITE);
		constexpr int kMaxScrapFieldCount = 1024;

		char g_ejectSound[Logic::kSoundNameCapacity] = {};
		char g_hopOutSound[Logic::kSoundNameCapacity] = {};

		// Both buffers live in exu.dll, so the patches must be off before the
		// DLL can unload: BasicPatch::UnloadAllPatches restores them when the
		// Lua state closes, and the INACTIVE default keeps them off for the
		// next mission.
		InlinePatch g_ejectSoundPatch(
			Addr::EjectSoundName,
			static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_ejectSound)),
			BasicPatch::Status::INACTIVE,
			{ 0xA0, 0x82, 0x87, 0x00 }); // stock pointer to "bail.wav" (0x008782A0)
		InlinePatch g_hopOutSoundPatch(
			Addr::HopOutSoundPush + 1,
			static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_hopOutSound)),
			BasicPatch::Status::INACTIVE,
			{ 0xA0, 0x83, 0x87, 0x00 }); // stock imm32 "jump.wav" (0x008783A0)

		// Copies a C string of at most capacity-1 printable bytes; false on a
		// fault, an over-long string or a control character.
		bool CopyEngineString(const char* source, char* out, std::size_t capacity)
		{
			out[0] = '\0';
			bool ok = false;
			Seh::Guard("SmallCalls.CopyEngineString", [&] {
				if (source == nullptr)
				{
					return;
				}
				std::size_t n = 0;
				for (; n + 1 < capacity && source[n] != '\0'; ++n)
				{
					const unsigned char c = static_cast<unsigned char>(source[n]);
					if (c < 0x20 || c > 0x7E)
					{
						return;
					}
					out[n] = source[n];
				}
				if (source[n] != '\0')
				{
					return;
				}
				out[n] = '\0';
				ok = true;
			});
			if (!ok)
			{
				out[0] = '\0';
			}
			return ok;
		}

		int PushLiveString(lua_State* L, const char* const* slot)
		{
			if (!RuntimeGate::IsSupported())
			{
				lua_pushnil(L);
				return 1;
			}
			const char* live = nullptr;
			Seh::Guard("SmallCalls.ReadSoundSlot", [&] { live = *slot; });
			char copy[Logic::kSoundNameCapacity];
			if (!CopyEngineString(live, copy, sizeof(copy)))
			{
				lua_pushnil(L);
				return 1;
			}
			lua_pushstring(L, copy);
			return 1;
		}

		int SetSound(lua_State* L, InlinePatch& patch, char* buffer)
		{
			if (lua_isnoneornil(L, 1))
			{
				patch.SetStatus(false);
				lua_pushboolean(L, !patch.IsActive());
				return 1;
			}
			std::size_t length = 0;
			const char* name = luaL_checklstring(L, 1, &length);
			if (!RuntimeGate::IsSupported() || !Logic::IsValidSoundName(name, length))
			{
				lua_pushboolean(L, false);
				return 1;
			}
			std::memcpy(buffer, name, length);
			buffer[length] = '\0';
			patch.SetStatus(true);
			lua_pushboolean(L, patch.IsActive());
			return 1;
		}

		// The live object behind a handle when it is a Craft; nullptr otherwise.
		void* GetCraft(BZR::handle h)
		{
			void* craft = nullptr;
			Seh::Guard("SmallCalls.GetCraft", [&] {
				BZR::GameObject* obj = BZR::GameObject::GetObj(h);
				if (obj != nullptr && MsvcRtti::HasBaseUnsafe(obj, ".?AVCraft@@"))
				{
					craft = obj;
				}
			});
			return craft;
		}

		// UnitMsg from a name ("attack") or a number (0..26); -1 when invalid.
		int CheckUnitMsg(lua_State* L, int idx)
		{
			if (lua_type(L, idx) == LUA_TNUMBER)
			{
				const double value = lua_tonumber(L, idx);
				return Logic::IsValidUnitMsg(value) ? static_cast<int>(value) : -1;
			}
			return Logic::UnitMsgFromName(luaL_checkstring(L, idx));
		}

		void* GetUserObject()
		{
			void* user = nullptr;
			Seh::Guard("SmallCalls.GetUserObject", [&] {
				user = *reinterpret_cast<void* const*>(BZR::GameObject::p_userObject);
			});
			return user;
		}

		bool SatelliteAvailable()
		{
			int available = 0;
			Seh::Guard("SmallCalls.SatelliteAvailable", [&] {
				available = reinterpret_cast<SatelliteAvailableFn>(Addr::SatelliteAvailable)();
			});
			return available != 0;
		}

		bool SatelliteActive()
		{
			bool active = false;
			Seh::Guard("SmallCalls.SatelliteActive", [&] { active = *BZR::Satellite::state; });
			return active;
		}

		bool ToggleSatellite()
		{
			BZR::tagENTITY* entity = Camera::userEntity.Read();
			if (entity == nullptr)
			{
				return false;
			}
			return Seh::Guard("SmallCalls.ToggleSatellite", [&] {
				BZR::Camera::Set_View(entity, kSatelliteToggleView);
			});
		}

		// ---- settings -------------------------------------------------------

		enum class SettingType
		{
			Bool,
			Int,
			Float,
			String,
		};

		bool MatchesType(lua_State* L, int idx, SettingType type)
		{
			switch (type)
			{
			case SettingType::Bool:
				return lua_type(L, idx) == LUA_TBOOLEAN;
			case SettingType::Int:
				return lua_type(L, idx) == LUA_TNUMBER && Logic::IsIntegralSetting(lua_tonumber(L, idx));
			case SettingType::Float:
				return lua_type(L, idx) == LUA_TNUMBER;
			case SettingType::String:
				return lua_type(L, idx) == LUA_TSTRING;
			}
			return false;
		}

		// Section and label from the resolved positions; raises on a wrong
		// type, false on a bad key.
		bool CheckKeys(lua_State* L, const Logic::SettingArgs& args, const char*& section, const char*& label)
		{
			std::size_t sectionLength = std::strlen(Logic::kDefaultSettingSection);
			section = Logic::kDefaultSettingSection;
			if (args.section != 0)
			{
				section = luaL_checklstring(L, args.section, &sectionLength);
			}
			std::size_t labelLength = 0;
			label = luaL_checklstring(L, args.label, &labelLength);
			return Logic::IsValidSettingKey(section, sectionLength) && Logic::IsValidSettingKey(label, labelLength);
		}

		int GetSetting(lua_State* L, SettingType type)
		{
			const int argCount = lua_gettop(L);
			const Logic::SettingArgs args = Logic::ResolveGetArgs(
				argCount, argCount >= 2 && lua_type(L, 2) == LUA_TSTRING, type == SettingType::String);
			const char* section = nullptr;
			const char* label = nullptr;
			const bool keysOk = CheckKeys(L, args, section, label);
			if (args.value != 0 && !lua_isnil(L, args.value) && !MatchesType(L, args.value, type))
			{
				return luaL_argerror(L, args.value, "default does not match the setting type");
			}

			const int top = lua_gettop(L);
			if (keysOk)
			{
				StorageApi::PushLoad(L, Logic::kSettingsNamespace); // value, meta
				if (lua_istable(L, -2))
				{
					lua_getfield(L, -2, section);
					if (lua_istable(L, -1))
					{
						lua_getfield(L, -1, label);
						if (MatchesType(L, -1, type))
						{
							lua_replace(L, top + 1);
							lua_settop(L, top + 1);
							lua_pushboolean(L, true);
							return 2;
						}
					}
				}
				lua_settop(L, top);
			}

			if (args.value != 0)
			{
				lua_pushvalue(L, args.value);
			}
			else
			{
				lua_pushnil(L);
			}
			lua_pushboolean(L, false);
			return 2;
		}

		// Writes (or with valueIndex == 0 removes) one key and saves the store.
		int WriteSetting(lua_State* L, const Logic::SettingArgs& args, int valueIndex)
		{
			const char* section = nullptr;
			const char* label = nullptr;
			if (!CheckKeys(L, args, section, label))
			{
				lua_pushboolean(L, false);
				lua_pushstring(L, "setting section and label must be 1-64 printable ASCII characters");
				return 2;
			}

			const int top = lua_gettop(L);
			StorageApi::PushLoad(L, Logic::kSettingsNamespace); // value, meta
			if (!lua_istable(L, -2))
			{
				// Corrupt primary and backup: never overwrite them blindly.
				lua_getfield(L, -1, "primaryError");
				const char* error = lua_tostring(L, -1);
				lua_pushboolean(L, false);
				lua_pushstring(L, (error != nullptr && error[0] != '\0') ? error : "settings store could not be loaded");
				return 2;
			}
			const int store = top + 1;

			lua_getfield(L, store, section);
			if (!lua_istable(L, -1))
			{
				lua_pop(L, 1);
				if (valueIndex == 0)
				{
					lua_settop(L, top);
					lua_pushboolean(L, true);
					lua_pushnil(L);
					return 2;
				}
				lua_newtable(L);
				lua_pushvalue(L, -1);
				lua_setfield(L, store, section);
			}
			if (valueIndex != 0)
			{
				lua_pushvalue(L, valueIndex);
			}
			else
			{
				lua_pushnil(L);
			}
			lua_setfield(L, -2, label);

			// Drop a section left empty by a delete.
			lua_pushnil(L);
			if (lua_next(L, -2) == 0)
			{
				lua_pushnil(L);
				lua_setfield(L, store, section);
			}
			else
			{
				lua_pop(L, 2);
			}

			StorageApi::PushSave(L, Logic::kSettingsNamespace, store);
			return 2;
		}

		int SaveSetting(lua_State* L, SettingType type)
		{
			const Logic::SettingArgs args = Logic::ResolveSaveArgs(lua_gettop(L));
			if (!MatchesType(L, args.value, type))
			{
				return luaL_argerror(L, args.value, "value does not match the setting type");
			}
			if (type == SettingType::Float && !std::isfinite(lua_tonumber(L, args.value)))
			{
				return luaL_argerror(L, args.value, "value must be finite");
			}
			return WriteSetting(L, args, args.value);
		}
	}

	int ClearMessages(lua_State* L)
	{
		if (!RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, false);
			return 1;
		}
		const bool ok = Seh::Guard("ClearMessages", [] {
			reinterpret_cast<ChatMessageAddFn>(Addr::ChatMessageAdd)(0, nullptr);
		});
		lua_pushboolean(L, ok);
		return 1;
	}

	int GetEjectSound(lua_State* L)
	{
		return PushLiveString(L, reinterpret_cast<const char* const*>(Addr::EjectSoundName));
	}

	int SetEjectSound(lua_State* L)
	{
		return SetSound(L, g_ejectSoundPatch, g_ejectSound);
	}

	int GetHopOutSound(lua_State* L)
	{
		return PushLiveString(L, reinterpret_cast<const char* const*>(Addr::HopOutSoundPush + 1));
	}

	int SetHopOutSound(lua_State* L)
	{
		return SetSound(L, g_hopOutSoundPatch, g_hopOutSound);
	}

	int GetPersonEjectRatio(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		void* craft = RuntimeGate::IsSupported() ? GetCraft(h) : nullptr;
		float ratio = 0.0f;
		const bool ok = craft != nullptr && Seh::Guard("GetPersonEjectRatio", [&] {
			ratio = *reinterpret_cast<const float*>(static_cast<uint8_t*>(craft) + kCraftEjectRatioOffset);
		});
		if (!ok || !std::isfinite(ratio))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, ratio);
		return 1;
	}

	int SetPersonEjectRatio(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const double ratio = luaL_checknumber(L, 2);
		if (!std::isfinite(ratio) || ratio < 0.0 || ratio > 1.0 || !RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, false);
			return 1;
		}
		void* craft = GetCraft(h);
		const bool ok = craft != nullptr && Seh::Guard("SetPersonEjectRatio", [&] {
			*reinterpret_cast<float*>(static_cast<uint8_t*>(craft) + kCraftEjectRatioOffset) = static_cast<float>(ratio);
		});
		lua_pushboolean(L, ok);
		return 1;
	}

	int MakeScrapField(lua_State* L)
	{
		if (!lua_isuserdata(L, 1) && !lua_istable(L, 1))
		{
			return luaL_typerror(L, 1, "vector");
		}
		const BZR::VECTOR_3D where = CheckVectorOrSingles(L, 1);
		const double count = luaL_checknumber(L, 2);
		if (!std::isfinite(where.x) || !std::isfinite(where.y) || !std::isfinite(where.z) ||
			!(count >= 1.0 && count <= kMaxScrapFieldCount) || !RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, false);
			return 1;
		}
		const float position[3] = { where.x, where.y, where.z };
		const bool ok = Seh::Guard("MakeScrapField", [&] {
			reinterpret_cast<MakeScrapFieldFn>(EngineAddresses::Plant::MakeScrapField)(position, static_cast<int>(count));
		});
		lua_pushboolean(L, ok);
		return 1;
	}

	int HasSatellite(lua_State* L)
	{
		lua_pushboolean(L, RuntimeGate::IsSupported() && SatelliteAvailable());
		return 1;
	}

	int ActivateSatellite(lua_State* L)
	{
		if (!RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, false);
			return 1;
		}
		if (!SatelliteActive() && SatelliteAvailable())
		{
			ToggleSatellite();
		}
		lua_pushboolean(L, SatelliteActive());
		return 1;
	}

	int DeactivateSatellite(lua_State* L)
	{
		if (!RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, false);
			return 1;
		}
		if (SatelliteActive())
		{
			ToggleSatellite();
		}
		lua_pushboolean(L, !SatelliteActive());
		return 1;
	}

	int UnitMessage(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const int msg = CheckUnitMsg(L, 2);
		void* craft = (msg >= 0 && RuntimeGate::IsSupported()) ? GetCraft(h) : nullptr;
		// Say reads the user object; never enter it without one.
		const bool ok = craft != nullptr && GetUserObject() != nullptr && Seh::Guard("UnitMessage", [&] {
			reinterpret_cast<SayFn>(Addr::UnitSay)(craft, msg, 0);
		});
		lua_pushboolean(L, ok);
		return 1;
	}

	int UnitMessageDirect(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const int msg = CheckUnitMsg(L, 2);
		const double priority = luaL_optnumber(L, 3, 0.0);
		if (msg < 0 || !Logic::IsValidQueuePriority(priority) || !RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, false);
			return 1;
		}
		void* craft = GetCraft(h);
		void* user = GetUserObject();
		if (craft == nullptr || user == nullptr)
		{
			lua_pushboolean(L, false);
			return 1;
		}

		const char* slot = nullptr;
		Seh::Guard("UnitMessageDirect.GetUnitMsg", [&] {
			slot = reinterpret_cast<UnitMsgFilenameFn>(Addr::UnitMsgFilename)(craft, msg);
		});
		char name[kUnitMsgNameBytes];
		if (!CopyEngineString(slot, name, sizeof(name)) || name[0] == '\0')
		{
			lua_pushboolean(L, false);
			return 1;
		}

		bool queued = false;
		Seh::Guard("UnitMessageDirect.Queue", [&] {
			void* component = static_cast<uint8_t*>(user) + kUserOwnerComponentOffset;
			void** vtable = *static_cast<void***>(component);
			void* owner = reinterpret_cast<OwnerFn>(vtable[kOwnerVtableSlot])(component);
			queued = reinterpret_cast<QueueFn>(Addr::UnitVoQueue)(name, owner, static_cast<unsigned>(priority));
		});
		lua_pushboolean(L, queued);
		return 1;
	}

	int StopUnitMessage(lua_State* L)
	{
		const bool ok = RuntimeGate::IsSupported() && Seh::Guard("StopUnitMessage", [] {
			reinterpret_cast<KillQueueFn>(Addr::UnitVoKillQueue)(0);
		});
		lua_pushboolean(L, ok);
		return 1;
	}

	int IsUnitMessagePlaying(lua_State* L)
	{
		bool playing = false;
		if (RuntimeGate::IsSupported())
		{
			Seh::Guard("IsUnitMessagePlaying", [&] {
				playing = *reinterpret_cast<void* const*>(Addr::UnitVoQueueHead) != nullptr;
			});
		}
		lua_pushboolean(L, playing);
		return 1;
	}

	int GetSettingBool(lua_State* L) { return GetSetting(L, SettingType::Bool); }
	int GetSettingInt(lua_State* L) { return GetSetting(L, SettingType::Int); }
	int GetSettingFloat(lua_State* L) { return GetSetting(L, SettingType::Float); }
	int GetSettingString(lua_State* L) { return GetSetting(L, SettingType::String); }
	int SaveSettingBool(lua_State* L) { return SaveSetting(L, SettingType::Bool); }
	int SaveSettingInt(lua_State* L) { return SaveSetting(L, SettingType::Int); }
	int SaveSettingFloat(lua_State* L) { return SaveSetting(L, SettingType::Float); }
	int SaveSettingString(lua_State* L) { return SaveSetting(L, SettingType::String); }

	int DeleteSetting(lua_State* L)
	{
		return WriteSetting(L, Logic::ResolveDeleteArgs(lua_gettop(L)), 0);
	}

	int FileExists(lua_State* L)
	{
		std::size_t length = 0;
		const char* name = luaL_checklstring(L, 1, &length);
		if (!RuntimeGate::IsSupported())
		{
			lua_pushnil(L); // unknown: the caller falls back to reading
			return 1;
		}
		if (!Logic::IsLoadableResourceName(name, length))
		{
			lua_pushboolean(L, false);
			return 1;
		}
		int size = 0;
		if (!Seh::Guard("FileExists", [&] {
				size = reinterpret_cast<ResourceFileSizeFn>(ResourceAddr::ResourceFileSize)(name);
			}))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushboolean(L, Logic::IsLoadableResourceSize(size));
		return 1;
	}
}
