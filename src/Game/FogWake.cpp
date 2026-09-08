#include "FogWake.h"
#include "../FogWakeBridge.h"
#include "lua.hpp"
#include <cmath>
#include <limits>
namespace ExtraUtilities::FogWake {
namespace {
bool Number(lua_State* L, int index, double& value) {
    if (lua_type(L, index) != LUA_TNUMBER) return false;
    value = lua_tonumber(L, index);
    return std::isfinite(value);
}
bool Field(lua_State* L, const char* name, float& target) {
    lua_getfield(L, 1, name);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return true; }
    double value;
    const bool valid = Number(L, -1, value) && std::abs(value) <= (std::numeric_limits<float>::max)();
    if (valid) target = static_cast<float>(value);
    lua_pop(L, 1);
    return valid;
}
bool Token(lua_State* L, std::uint32_t& token) {
    double value;
    if (!Number(L, 1, value) || value < 1 || value > 4294967295.0 || std::floor(value) != value) return false;
    token = static_cast<std::uint32_t>(value);
    return true;
}
int Result(lua_State* L, bool ok) { lua_pushboolean(L, ok); return 1; }
}
int Configure(lua_State* L) {
    if (!lua_istable(L, 1)) return Result(L, false);
    FogWakeApi::Config c;
    const bool valid = Field(L,"centerX",c.centerX) && Field(L,"baseY",c.baseY) && Field(L,"centerZ",c.centerZ)
        && Field(L,"width",c.width) && Field(L,"cellSize",c.cellSize) && Field(L,"height",c.height)
        && Field(L,"wakeRadius",c.wakeRadius) && Field(L,"recoverySeconds",c.recoverySeconds)
        && Field(L,"windX",c.windX) && Field(L,"windZ",c.windZ) && Field(L,"density",c.density)
        && Field(L,"colorR",c.colorR) && Field(L,"colorG",c.colorG) && Field(L,"colorB",c.colorB);
    return Result(L, valid && FogWakeBridge::Configure(c));
}
int Update(lua_State* L) {
    double now;
    return Result(L, Number(L, 1, now) && now >= 0 && FogWakeBridge::Update(now));
}
int Observe(lua_State* L) {
    std::uint32_t id;
    double x = 0, y = 0, z = 0;
    const double limit = (std::numeric_limits<float>::max)();
    const bool valid = Token(L, id) && Number(L, 2, x) && Number(L, 3, y) && Number(L, 4, z)
        && std::abs(x) <= limit && std::abs(y) <= limit && std::abs(z) <= limit;
    return Result(L, valid && FogWakeBridge::Observe(id, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
}
int Remove(lua_State* L) {
    std::uint32_t id;
    return Result(L, Token(L, id) && FogWakeBridge::Remove(id));
}
int Reset(lua_State* L) { return Result(L, FogWakeBridge::Reset()); }
int Status(lua_State* L) {
    const auto s = FogWakeBridge::Status();
    lua_newtable(L);
    auto boolean = [L](const char* key, bool v) { lua_pushboolean(L, v); lua_setfield(L, -2, key); };
    auto number = [L](const char* key, double v) { lua_pushnumber(L, v); lua_setfield(L, -2, key); };
    boolean("supported", s.supported != 0);
    boolean("configured", s.configured != 0);
    boolean("renderReady", s.renderReady != 0);
    number("apiVersion", s.supported ? s.version : 0);
    number("emitterCount", s.emitterCount);
    number("activeCells", s.activeCells);
    number("maxClearance", s.maxClearance);
    number("renderedBillboards", s.renderedBillboards);
    return 1;
}
}
