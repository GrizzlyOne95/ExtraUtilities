// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once
struct lua_State;

// Per-unit weapon control: fire, cease fire, per-slot enable, special-item
// state and hardpoint inspection through the unit's Carrier.
namespace ExtraUtilities::Lua::WeaponControl
{
	int FireWeapons(lua_State* L);
	int FireSpecialWeapon(lua_State* L);
	int CeaseFire(lua_State* L);
	int GetWeaponEnabled(lua_State* L);
	int SetWeaponEnabled(lua_State* L);
	int IsSpecialActive(lua_State* L);
	int GetHardpoint(lua_State* L);
}
