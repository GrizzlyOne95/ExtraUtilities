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

#include "Reticle.h"

#include "LuaHelpers.h"
#include "OpenShimBridge.h"

#include <cmath>

namespace
{
	using OpenShimGetSmartReticleRangeFn = float(WINAPI*)();
	using OpenShimSetSmartReticleRangeFn = BOOL(WINAPI*)(float);

}

namespace ExtraUtilities::Lua::Reticle
{
	int GetHit(lua_State* L)
	{
		if (!RuntimeGate::IsSupported())
			return PushUnsupportedBuild(L);

		// One game-thread snapshot. Position alone retains the previous terrain
		// hit when the sight moves onto an object, the sky, or outside range.
		auto* currentObject = selectObject.Read();
		if (currentObject != nullptr)
		{
			const BZR::handle h = object.Read();
			if (h != 0 && BZR::GameObject::GetObj(h) == currentObject)
			{
				lua_pushliteral(L, "object");
				lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
				lua_pushnil(L);
				return 3;
			}
			return PushUnsupportedBuild(L);
		}
		if (groundHit.Read() == 0)
			return PushUnsupportedBuild(L);
		const auto pos = position.Read();
		if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
			return PushUnsupportedBuild(L);
		lua_pushliteral(L, "terrain");
		lua_pushnil(L);
		PushVector(L, pos);
		return 3;
	}

	int GetPosition(lua_State* L)
	{
		BZR::VECTOR_3D pos = position.Read();

		PushVector(L, pos);

		return 1;
	}

	int GetRange(lua_State* L)
	{
		const auto bridge = OpenShimBridge::Resolve<OpenShimGetSmartReticleRangeFn>(
			"OpenShimGetSmartReticleRange");
		lua_pushnumber(L, bridge ? bridge() : range.Read());
		return 1;
	}

	int SetRange(lua_State* L)
	{
		const float newRange = static_cast<float>(luaL_checknumber(L, 1));
		if (!std::isfinite(newRange) || newRange < 0.f)
		{
			return luaL_argerror(L, 1, "reticle range must be a finite, non-negative number");
		}

		const auto bridge = OpenShimBridge::Resolve<OpenShimSetSmartReticleRangeFn>(
			"OpenShimSetSmartReticleRange");
		if (bridge)
		{
			bridge(newRange);
		}
		else
		{
			range.Write(newRange);
		}
		return 0;
	}

	int GetObject(lua_State* L)
	{
		BZR::handle h = object.Read();

		if (h == 0)
		{
			lua_pushnil(L);
			return 1;
		}
		else
		{
			lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
		}
		
		return 1;
	}

	int GetMatrix(lua_State* L)
	{
		BZR::MAT_3D reticleMat = matrix.Read();

		PushMatrix(L, reticleMat);

		return 1;
	}
}
