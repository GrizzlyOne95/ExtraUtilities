#pragma once
struct lua_State;
namespace ExtraUtilities::FogWake {
int Configure(lua_State* L);
int Update(lua_State* L);
int Observe(lua_State* L);
int Remove(lua_State* L);
int Reset(lua_State* L);
int Status(lua_State* L);
}
