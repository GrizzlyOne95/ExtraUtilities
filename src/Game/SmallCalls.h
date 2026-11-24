/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

// Small one-call Lua APIs (idea credit: GoombaBZ's bzx plugin for BZ 1.5;
// reimplemented from the BZ 1.5 / Redux decompiles, no bzx code used):
// message list clear, eject/hop-out sounds, pilot eject ratio, scrap fields,
// satellite view, unit voice messages and typed persistent settings.

#include <lua.hpp>

namespace ExtraUtilities::Lua::SmallCalls
{
	int ClearMessages(lua_State* L);
	int DisplayMessage(lua_State* L);

	int GetEjectSound(lua_State* L);
	int SetEjectSound(lua_State* L);
	int GetHopOutSound(lua_State* L);
	int SetHopOutSound(lua_State* L);

	int GetPersonEjectRatio(lua_State* L);
	int SetPersonEjectRatio(lua_State* L);

	int IsPortable(lua_State* L);
	int SetPortable(lua_State* L);

	int MakeScrapField(lua_State* L);

	int HasSatellite(lua_State* L);
	int ActivateSatellite(lua_State* L);
	int DeactivateSatellite(lua_State* L);
	// Satellite-available override (bzx EnableSatellite/DisableSatellite;
	// ResetSatellite is EXU's own). Local to this machine.
	int EnableSatellite(lua_State* L);
	int DisableSatellite(lua_State* L);
	int ResetSatellite(lua_State* L);
	// Clears the override and removes its detour; called when the Lua state closes.
	void ShutdownSatelliteOverride() noexcept;

	int UnitMessage(lua_State* L);
	int UnitMessageDirect(lua_State* L);
	int GetUnitMessage(lua_State* L);
	int StopUnitMessage(lua_State* L);
	int IsUnitMessagePlaying(lua_State* L);

	int GetSettingBool(lua_State* L);
	int GetSettingInt(lua_State* L);
	int GetSettingFloat(lua_State* L);
	int GetSettingString(lua_State* L);
	int SaveSettingBool(lua_State* L);
	int SaveSettingInt(lua_State* L);
	int SaveSettingFloat(lua_State* L);
	int SaveSettingString(lua_State* L);
	int DeleteSetting(lua_State* L);

	// Not from bzx: a quiet game-file existence check on the engine's own
	// resource lookup (see SmallCalls.cpp).
	int FileExists(lua_State* L);

	// bzx AI map border: the stock soft map-edge push for every craft, not
	// only craft without an AI process (see SmallCalls.cpp).
	int GetAIMapBorder(lua_State* L);
	int SetAIMapBorder(lua_State* L);

	// bzx clock and constants: os.date/os.time/os.difftime equivalents (BZR's Lua
	// has no os library), the UNIT_MESSAGE table.
	int GetCurDate(lua_State* L);
	int GetCurTime(lua_State* L);
	int GetTimeDiff(lua_State* L);
	// Pushes the exu.UNIT_MESSAGE table (name -> number) built from the parser's name table.
	void PushUnitMessageTable(lua_State* L);
	// Pushes the exu.CHAT_COLOR table (name -> 0xAARRGGBB).
	void PushChatColorTable(lua_State* L);
}
