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

#include "Culling.h"
#include "Ogre/Ogre.h"
#include "Camera.h"
#include "LuaHelpers.h"
#include "Util/RuntimeGate.h"

#include <cmath>
#include <unordered_set>

namespace ExtraUtilities::Culling
{
    namespace
    {
        // Units culling itself hid, by handle. Only these are ever shown
        // again: disabling culling or raising the distance re-shows them, and
        // units the game hid for its own reasons are left alone.
        std::unordered_set<BZR::handle> g_hiddenByCulling;

        void SetEntityVisible(BZR::GameObject* obj, bool visible)
        {
            if (void* entity = obj->GetOgreEntity())
            {
                Ogre::SetVisible(entity, visible);
            }
        }
    }

    void UpdateUnit(BZR::GameObject* obj)
    {
        // Also reached through the EXU_UpdateCullingForUnit export, outside
        // any gated patch.
        if (obj == nullptr || !RuntimeGate::IsSupported())
        {
            return;
        }

        if (!enabled && g_hiddenByCulling.empty())
        {
            return;
        }

        const BZR::handle h = BZR::GameObject::GetHandle(obj);
        const auto hidden = g_hiddenByCulling.find(h);

        if (!enabled)
        {
            if (hidden != g_hiddenByCulling.end())
            {
                SetEntityVisible(obj, true);
                g_hiddenByCulling.erase(hidden);
            }
            return;
        }

        BZR::BZR_Camera* cam = Lua::Camera::mainCam.Get();
        if (cam == nullptr)
        {
            return;
        }

        // Calculate distance (squared for performance)
        const double dx = obj->pos.x - cam->Matrix.posit_x;
        const double dy = obj->pos.y - cam->Matrix.posit_y;
        const double dz = obj->pos.z - cam->Matrix.posit_z;
        const double distSq = dx * dx + dy * dy + dz * dz;
        const bool inRange = distSq < static_cast<double>(cullDistance) * cullDistance;

        if (!inRange && hidden == g_hiddenByCulling.end())
        {
            SetEntityVisible(obj, false);
            g_hiddenByCulling.insert(h);
        }
        else if (inRange && hidden != g_hiddenByCulling.end())
        {
            SetEntityVisible(obj, true);
            g_hiddenByCulling.erase(hidden);
        }
    }

    void ResetMissionState() noexcept
    {
        cullDistance = 500.0f;
        enabled = false;
        g_hiddenByCulling.clear();
    }
}

namespace ExtraUtilities::Lua::Culling
{
    int SetCullDistance(lua_State* L)
    {
        const float distance = static_cast<float>(luaL_checknumber(L, 1));
        luaL_argcheck(L, std::isfinite(distance) && distance >= 0.0f, 1, "cull distance must be a finite, non-negative number");
        ExtraUtilities::Culling::cullDistance = distance;
        return 0;
    }

    int GetCullDistance(lua_State* L)
    {
        lua_pushnumber(L, ExtraUtilities::Culling::cullDistance);
        return 1;
    }

    int SetCullingEnabled(lua_State* L)
    {
        ExtraUtilities::Culling::enabled = CheckBool(L, 1);
        return 0;
    }

    int GetCullingEnabled(lua_State* L)
    {
        lua_pushboolean(L, ExtraUtilities::Culling::enabled);
        return 1;
    }
}

extern "C" __declspec(dllexport) void __cdecl EXU_UpdateCullingForUnit(void* object)
{
    ExtraUtilities::Culling::UpdateUnit(
        static_cast<BZR::GameObject*>(object));
}
