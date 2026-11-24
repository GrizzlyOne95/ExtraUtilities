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
// - DisplayMessage: Redux FUN_00624550(short sender, const char* text) is the
//   stock message display. It starts "if (DAT_00920168 != 0)" (the message box
//   pointer). Sender 0: "if (DAT_008ec708 != 0) { FUN_00821390(DAT_00920168,
//   text); FUN_007a47b0(0, text); }". Otherwise it picks a palette colour and
//   runs FUN_00821450(DAT_00920168, text, colour); FUN_007a47b0(colour, text);
//   without the 0x008EC708 test. All three calls are cdecl (call-site bytes:
//   "8B 55 FC 52 8B 45 0C 50 8B 0D 68 01 92 00 51 E8 .. 83 C4 0C" then
//   "52 50 E8 .. 83 C4 08"). The box pointer is passed by value.
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
// - Satellite override (idea credit: bzx EnableSatellite/DisableSatellite):
//   FUN_0049F500 is "cmp dword ptr [0x00979F98], 0; jne; call FUN_0049F4C0
//   (free-satellite option); jne; call FUN_00451DE0 (arcade); jne" and
//   returns 0/1 in eax. EXU detours its entry; the 11 stolen bytes
//   (push ebp; mov ebp,esp; push ecx; cmp [0x00979F98],0) have no relative
//   operand, and the trampoline resumes at the jne at 0x0049F50B. Its only
//   code xrefs (rel32 scan of .text; no absolute pointer to it anywhere in the
//   image) are Apply_Satellite_View at 0x0061BD80 (returns early on 0) and
//   the ControlPanel button loop in FUN_004A08E0 at 0x004A2A38: on 1 it sets
//   the satellite button's enable bit (+0x1184); on 0, while the view is
//   satellite (FUN_004978D0: [0x008EAAD8] == 3), it plays mnu_back.wav and
//   calls Set_View(entity, 8) (0x004A2A91..0x004A2ABE), so forcing it off
//   kicks the player out of the satellite view on the next panel update. The
//   counter 0x00979F98 (controlPanel 0x00978E20 + 0x1178) has no other
//   absolute reference; the [this+0x1178] accesses are the ControlPanel
//   increment (FUN_0049B7A0), clamped decrement (FUN_0049B7F0) and the
//   constructor zero (0x004A0428), none of them an availability test.
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
#include "ClockLogic.h"
#include "EntryDetour32.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"
#include "SmallCallsLogic.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/MsvcRtti.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "Util/StorageApi.h"
#include "bzr.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <new>
#include <vector>

namespace ExtraUtilities::Lua::SmallCalls
{
	namespace
	{
		namespace Logic = ExtraUtilities::SmallCallsLogic;
		namespace Clock = ExtraUtilities::ClockLogic;
		namespace Addr = EngineAddresses::SmallCalls;
		namespace ResourceAddr = EngineAddresses::PathBlock;

		using ChatMessageAddFn = void(__cdecl*)(int color, const char* text);
		using MessageBoxAddFn = void(__cdecl*)(void* box, const char* text);
		using MessageBoxAddColouredFn = void(__cdecl*)(void* box, const char* text, uint32_t colour);
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

		// GameObject::hitch, the "portable" flag (OBJ76*, GetHitch at
		// Addr::GameObjectGetHitch 0x0046D0C0 returns it). The tug command
		// function (FUN_005ED0B0) offers PICKUP for buildings only when it is
		// non-null; craft pickup ignores it and the engine rewrites it on craft
		// every frame.
		constexpr std::size_t kGameObjectHitchOffset = 0x220;
		// GameObject::obj, the OBJ76*. The stock setter (Building ctor 0x0047EA34,
		// "abstor") stores this value into the hitch.
		constexpr std::size_t kGameObjectObjOffset = 0xF4;

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

		// Satellite override (see the header comment). The stolen entry of
		// FUN_0049F500: push ebp; mov ebp,esp; push ecx; cmp dword ptr
		// [0x00979F98], 0. Whole, position-independent instructions; the
		// trampoline replays the cmp, so the jne at +0x0B sees its flags.
		constexpr std::array<std::uint8_t, 11> kSatelliteStolenEntry = {
			0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x98, 0x9F, 0x97, 0x00, 0x00 };
		// The jne rel8 that follows the stolen bytes: part of the identity check.
		constexpr std::array<std::uint8_t, 2> kSatelliteEntryTail = { 0x75, 0x1B };

		Logic::SatelliteOverride g_satelliteOverride = Logic::SatelliteOverride::None;
		EntryDetour32* g_satelliteDetour = nullptr;
		SatelliteAvailableFn g_satelliteOriginal = nullptr;
		bool g_satelliteEntryChecked = false;
		bool g_satelliteEntryQualified = false;

		// Callers (0x0061BD80, 0x004A2A38) pass no arguments and read eax.
		int __cdecl SatelliteAvailableHook()
		{
			int answer = 0;
			if (Logic::SatelliteOverrideAnswer(g_satelliteOverride, answer))
			{
				return answer;
			}
			const SatelliteAvailableFn original = g_satelliteOriginal;
			return original != nullptr ? original() : 0;
		}

		bool SatelliteEntryMatches() noexcept
		{
			bool matches = false;
			Seh::Guard("SmallCalls.SatelliteEntryMatches", [&] {
				const auto* code = reinterpret_cast<const std::uint8_t*>(Addr::SatelliteAvailable);
				matches = std::memcmp(code, kSatelliteStolenEntry.data(), kSatelliteStolenEntry.size()) == 0 &&
					std::memcmp(code + kSatelliteStolenEntry.size(), kSatelliteEntryTail.data(), kSatelliteEntryTail.size()) == 0;
			});
			return matches;
		}

		// Checks the entry bytes once per Lua state; logs once on failure.
		bool QualifySatelliteEntry() noexcept
		{
			if (!g_satelliteEntryChecked)
			{
				g_satelliteEntryChecked = true;
				g_satelliteEntryQualified = RuntimeGate::IsSupported() && SatelliteEntryMatches();
				if (!g_satelliteEntryQualified)
					Logging::LogMessage("exu: satellite override unavailable; FUN_0049F500 entry did not qualify");
			}
			return g_satelliteEntryQualified;
		}

		void RemoveSatelliteDetour() noexcept
		{
			// Restores the engine entry before the trampoline is freed.
			delete g_satelliteDetour;
			g_satelliteDetour = nullptr;
			g_satelliteOriginal = nullptr;
		}

		// Installs the detour on first use; it stays until the Lua state closes.
		bool EnsureSatelliteDetour() noexcept
		{
			try
			{
				if (g_satelliteDetour != nullptr && g_satelliteDetour->IsActive())
					return true;
				RemoveSatelliteDetour();
				if (!QualifySatelliteEntry())
					return false;

				g_satelliteDetour = new (std::nothrow) EntryDetour32(
					Addr::SatelliteAvailable, reinterpret_cast<const void*>(&SatelliteAvailableHook),
					kSatelliteStolenEntry.size(), BasicPatch::Status::INACTIVE,
					std::vector<std::uint8_t>(kSatelliteStolenEntry.begin(), kSatelliteStolenEntry.end()));
				if (g_satelliteDetour == nullptr || !g_satelliteDetour->PrepareTrampoline())
				{
					Logging::LogMessage("exu: satellite override unavailable; detour could not be prepared");
					RemoveSatelliteDetour();
					return false;
				}
				g_satelliteOriginal = reinterpret_cast<SatelliteAvailableFn>(g_satelliteDetour->GetTrampoline());
				g_satelliteDetour->SetStatus(true);
				if (g_satelliteOriginal == nullptr || !g_satelliteDetour->IsActive())
				{
					Logging::LogMessage("exu: satellite override detour could not activate");
					RemoveSatelliteDetour();
					return false;
				}
				Logging::LogMessage("exu: satellite override detour active");
				return true;
			}
			catch (...)
			{
				RemoveSatelliteDetour();
				return false;
			}
		}

		bool SetSatelliteOverride(Logic::SatelliteOverride requested)
		{
			if (!EnsureSatelliteDetour())
			{
				return false;
			}
			g_satelliteOverride = requested;
			return true;
		}

		// Goes through the detour when it is installed, so the answer includes
		// the override.
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
			// The engine's own satellite test (FUN_004978D0): Current_View == 3.
			// BZR::Satellite::state (0x008E8F9C) is the inverse: Apply_Satellite_View
			// (FUN_0061BD20) sets it to 0 and the cockpit/chase views set it to 1.
			Seh::Guard("SmallCalls.SatelliteActive", [&] {
				active = *reinterpret_cast<const std::int32_t*>(EngineAddresses::DeathCamera::CameraMode) == 3;
			});
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

	int DisplayMessage(lua_State* L)
	{
		const char* text = luaL_checkstring(L, 1);
		const bool hasColour = !lua_isnoneornil(L, 2);
		uint32_t colour = 0;
		if (hasColour)
		{
			const double value = luaL_checknumber(L, 2);
			if (!Logic::IsValidChatColour(value))
			{
				return luaL_argerror(L, 2, "colour must be an integer 0..0xFFFFFFFF (0xAARRGGBB)");
			}
			colour = Logic::EngineColour(static_cast<uint32_t>(value));
		}
		if (!RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, false);
			return 1;
		}
		// Stock DisplayMessage formats into a 1024-byte buffer first
		// (FUN_0056fcb0: _vsnprintf(local_408, 0x400, ...)), so the box code
		// never sees a longer line. Keep that cap. A plain char array has no
		// destructor, so it is safe across the Lua error longjmp.
		char line[1024];
		std::strncpy(line, text, sizeof(line) - 1);
		line[sizeof(line) - 1] = '\0';
		text = line;
		bool ran = false;
		const bool ok = Seh::Guard("DisplayMessage", [&] {
			void* const box = *reinterpret_cast<void* const*>(Addr::MessageBoxPointer);
			if (box == nullptr)
			{
				return;
			}
			if (hasColour)
			{
				reinterpret_cast<MessageBoxAddColouredFn>(Addr::MessageBoxAddColoured)(box, text, colour);
				reinterpret_cast<ChatMessageAddFn>(Addr::ChatMessageAdd)(static_cast<int>(colour), text);
				ran = true;
			}
			else if (*reinterpret_cast<const int*>(Addr::MessageGate) != 0)
			{
				reinterpret_cast<MessageBoxAddFn>(Addr::MessageBoxAdd)(box, text);
				reinterpret_cast<ChatMessageAddFn>(Addr::ChatMessageAdd)(0, text);
				ran = true;
			}
		});
		lua_pushboolean(L, ok && ran);
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

	int IsPortable(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		bool portable = false;
		const bool ok = RuntimeGate::IsSupported() && Seh::Guard("IsPortable", [&] {
			BZR::GameObject* obj = BZR::GameObject::GetObj(h);
			if (obj != nullptr)
			{
				portable = *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(obj) + kGameObjectHitchOffset) != nullptr;
			}
		});
		lua_pushboolean(L, ok && portable);
		return 1;
	}

	int SetPortable(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		luaL_checkany(L, 2);
		const bool on = lua_toboolean(L, 2) != 0;
		bool written = false;
		const bool ok = RuntimeGate::IsSupported() && Seh::Guard("SetPortable", [&] {
			BZR::GameObject* obj = BZR::GameObject::GetObj(h);
			if (obj == nullptr)
			{
				return;
			}
			uint8_t* base = reinterpret_cast<uint8_t*>(obj);
			void* value = on ? *reinterpret_cast<void**>(base + kGameObjectObjOffset) : nullptr;
			if (on && value == nullptr)
			{
				return; // no OBJ76 to hitch to; nothing would be enabled
			}
			*reinterpret_cast<void**>(base + kGameObjectHitchOffset) = value;
			written = true;
		});
		lua_pushboolean(L, ok && written);
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

	int EnableSatellite(lua_State* L)
	{
		lua_pushboolean(L, SetSatelliteOverride(Logic::SatelliteOverride::On));
		return 1;
	}

	int DisableSatellite(lua_State* L)
	{
		lua_pushboolean(L, SetSatelliteOverride(Logic::SatelliteOverride::Off));
		return 1;
	}

	int ResetSatellite(lua_State* L)
	{
		// Leaves the detour in place; with no override it runs the stock test.
		g_satelliteOverride = Logic::SatelliteOverride::None;
		lua_pushboolean(L, true);
		return 1;
	}

	void ShutdownSatelliteOverride() noexcept
	{
		g_satelliteOverride = Logic::SatelliteOverride::None;
		RemoveSatelliteDetour();
		g_satelliteEntryChecked = false;
		g_satelliteEntryQualified = false;
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

	int GetUnitMessage(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const int msg = CheckUnitMsg(L, 2);
		void* craft = (msg >= 0 && RuntimeGate::IsSupported()) ? GetCraft(h) : nullptr;
		if (craft == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}
		const char* slot = nullptr;
		Seh::Guard("GetUnitMessage.GetUnitMsg", [&] {
			slot = reinterpret_cast<UnitMsgFilenameFn>(Addr::UnitMsgFilename)(craft, msg);
		});
		char name[kUnitMsgNameBytes];
		if (!CopyEngineString(slot, name, sizeof(name)) || name[0] == '\0')
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushstring(L, name);
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

	// ---- bzx clock and constants -------------------------------------------
	// Values are local to each machine (system clock and time zone).

	int GetCurDate(lua_State* L)
	{
		const char* format = luaL_optstring(L, 1, "%c");
		std::time_t t = std::time(nullptr);
		if (!lua_isnoneornil(L, 2) && !Clock::TimeFromNumber(luaL_checknumber(L, 2), t))
		{
			lua_pushnil(L);
			return 1;
		}
		const bool utc = *format == '!';
		if (utc)
		{
			++format;
		}
		std::tm tm{};
		if (!Clock::BreakDown(t, utc, tm))
		{
			lua_pushnil(L);
			return 1;
		}
		if (std::strcmp(format, "*t") == 0)
		{
			lua_createtable(L, 0, 9);
			const struct { const char* key; int value; } fields[] = {
				{ "sec", tm.tm_sec }, { "min", tm.tm_min }, { "hour", tm.tm_hour },
				{ "day", tm.tm_mday }, { "month", tm.tm_mon + 1 }, { "year", tm.tm_year + 1900 },
				{ "wday", tm.tm_wday + 1 }, { "yday", tm.tm_yday + 1 },
			};
			for (const auto& field : fields)
			{
				lua_pushinteger(L, field.value);
				lua_setfield(L, -2, field.key);
			}
			lua_pushboolean(L, tm.tm_isdst > 0);
			lua_setfield(L, -2, "isdst");
			return 1;
		}
		// Validate before formatting: strftime must never see a bad conversion.
		// Output goes straight into a luaL_Buffer, so no object with a
		// destructor is alive when a Lua call raises (longjmp, LuaCppBarrier.h).
		const char bad = Clock::FindBadSpecifier(format);
		if (bad != '\0')
		{
			return luaL_argerror(L, 1, lua_pushfstring(L, "invalid conversion specifier '%%%c'", bad));
		}
		luaL_Buffer out;
		luaL_buffinit(L, &out);
		Clock::FormatValidatedDate(format, tm, [&out](const char* text, std::size_t n) { luaL_addlstring(&out, text, n); });
		luaL_pushresult(&out);
		return 1;
	}

	int GetCurTime(lua_State* L)
	{
		if (lua_isnoneornil(L, 1))
		{
			lua_pushnumber(L, static_cast<lua_Number>(std::time(nullptr)));
			return 1;
		}
		luaL_checktype(L, 1, LUA_TTABLE);
		// A negative default marks the field required.
		const struct { const char* key; int defaultValue; } keys[] = {
			{ "year", -1 }, { "month", -1 }, { "day", -1 }, { "hour", 12 }, { "min", 0 }, { "sec", 0 },
		};
		int v[6] = {};
		for (int i = 0; i < 6; ++i)
		{
			lua_getfield(L, 1, keys[i].key);
			const bool present = lua_isnumber(L, -1) != 0;
			const Clock::FieldResult r = Clock::ResolveField(present, present ? lua_tonumber(L, -1) : 0.0, keys[i].defaultValue, v[i]);
			lua_pop(L, 1);
			if (r == Clock::FieldResult::Missing)
			{
				return luaL_error(L, "field '%s' missing in date table", keys[i].key);
			}
			if (r == Clock::FieldResult::OutOfRange)
			{
				return luaL_error(L, "field '%s' out of range in date table", keys[i].key);
			}
		}
		lua_getfield(L, 1, "isdst");
		const int isdst = lua_isnil(L, -1) ? -1 : (lua_toboolean(L, -1) ? 1 : 0);
		lua_pop(L, 1);
		std::tm tm = Clock::TmFromFields(v[0], v[1], v[2], v[3], v[4], v[5], isdst);
		const std::time_t t = std::mktime(&tm);
		if (t == static_cast<std::time_t>(-1))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, static_cast<lua_Number>(t));
		return 1;
	}

	int GetTimeDiff(lua_State* L)
	{
		std::time_t t2 = 0;
		std::time_t t1 = 0;
		if (!Clock::TimeFromNumber(luaL_checknumber(L, 1), t2))
		{
			return luaL_argerror(L, 1, "time out of range");
		}
		if (!Clock::TimeFromNumber(luaL_optnumber(L, 2, 0), t1))
		{
			return luaL_argerror(L, 2, "time out of range");
		}
		lua_pushnumber(L, std::difftime(t2, t1));
		return 1;
	}

	// AI map border (idea credit: bzx). Engine facts (GOG 2.2.301, Ghidra on
	// /battlezone98redux.exe-007c64 plus raw bytes):
	// - CheckBoundary is FUN_0045CD00(GameObject*, float dt): it compares the
	//   object's x/z with the edge_path box (0x0091738C/94/90/88, written by
	//   FUN_0046AF20, 1.5 AiUtilFeature::PostLoad) and pushes the velocity
	//   back in. Its only caller is AiMission::Update FUN_0045D240 at
	//   0x0045D488. Every mission Update calls that base, including
	//   LuaMission and LuaMissionMP (both vtables, 0x0087BBC8 and 0x0087BC20,
	//   have Update FUN_0050AA10, which calls FUN_0045D240 at 0x0050AA26).
	// - Redux dropped 1.5's Net_IsNetGame branch. The loop walks
	//   Craft::craftList (0x0094E2D4; the Craft ctor FUN_004A7A20 pushes into
	//   it, and the Person ctor FUN_0059CF20 runs that ctor, so pilots are in
	//   it) and checks a craft when (no AI process at +0xFC and OBJ76 flags
	//   lack 0x200, dead) or when the flags have 0x30 (player craft; 1.5
	//   GameObjectClass::Build sets 0x10 for the user). So AI craft are
	//   skipped in single player and in multiplayer.
	// - The patch NOPs the 'jne' at 0x0045D45B that sends craft with an AI
	//   process to the 0x30 test. Every live craft then takes the stock
	//   check; dead ones still need 0x30. Player craft are unaffected.
	namespace
	{
		// INACTIVE default: BasicPatch::UnloadAllPatches restores it when the
		// Lua state closes and the requested status resets to off.
		InlinePatch g_aiMapBorderPatch(
			Addr::MapBorderAiProcessBranch,
			BasicPatch::NOP,
			2,
			BasicPatch::Status::INACTIVE,
			{ 0x75, 0x0F }); // jne +0x0F (craft with an AI process)
	}

	int GetAIMapBorder(lua_State* L)
	{
		lua_pushboolean(L, g_aiMapBorderPatch.IsActive());
		return 1;
	}

	int SetAIMapBorder(lua_State* L)
	{
		luaL_checktype(L, 1, LUA_TBOOLEAN);
		const bool enable = lua_toboolean(L, 1) != 0;
		if (enable && !RuntimeGate::IsSupported())
		{
			lua_pushboolean(L, false);
			return 1;
		}
		g_aiMapBorderPatch.SetStatus(enable);
		lua_pushboolean(L, g_aiMapBorderPatch.IsActive() == enable);
		return 1;
	}

	void PushUnitMessageTable(lua_State* L)
	{
		lua_createtable(L, 0, static_cast<int>(sizeof(Logic::kUnitMsgNames) / sizeof(Logic::kUnitMsgNames[0])));
		// Upper-case keys, like the other exu enums (the stub in ExtraUtils.lua
		// lists the same names upper-cased).
		for (const Logic::UnitMsgName& entry : Logic::kUnitMsgNames)
		{
			char key[32] = {};
			for (std::size_t i = 0; entry.name[i] != '\0' && i + 1 < sizeof(key); ++i)
			{
				const char c = entry.name[i];
				key[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
			}
			lua_pushinteger(L, entry.value);
			lua_setfield(L, -2, key);
		}
	}

	void PushChatColorTable(lua_State* L)
	{
		lua_createtable(L, 0, static_cast<int>(sizeof(Logic::kChatColours) / sizeof(Logic::kChatColours[0])));
		// Plain colours only. The engine's own self/ally/enemy chat colours
		// (self is 0xFF00FF00) are chosen by sender and are not exposed.
		for (const Logic::ChatColourName& entry : Logic::kChatColours)
		{
			lua_pushnumber(L, static_cast<lua_Number>(entry.argb));
			lua_setfield(L, -2, entry.name);
		}
	}
}
