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

#include "GameObject.h"
#include "GameObjectInternal.h"

#include "Patches/GlobalTurbo.h"
#include "Util/Logging.h"
#include "LuaHelpers.h"
#include "Ogre/Ogre.h"
#include "Ogre/OgreMaterialShim.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdarg>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <lua.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ExtraUtilities::Lua::GameObject
{
	namespace
	{
		constexpr size_t kGameObjectCarrierOffset = 0x1A0; // GOG: PDB 0x198 drifted +8 (0x198 is the scanner); see bzr.h
		constexpr size_t kGameObjectModeListOffset = 0x1A4;            // GOG: 1.5 PDB 0x19C +8 (verified via MODEPROBE 2026-07-14)
		constexpr size_t kGameObjectModeListEnabledMaskOffset = 0x1D0; // GOG: 1.5 PDB 0x1C8 +8 (real bitmask; 0x38F wingman, 0x0F producers)
		constexpr size_t kGameObjectModeListActiveSlotOffset = 0x1D4;  // GOG: 1.5 PDB 0x1CC +8 (valid small slot index)
		constexpr size_t kGameObjectWeaponMaskOffset = 0x210;
		constexpr uint32_t kWeaponMaskDecodeXor = 0x33333333u;
		constexpr uint32_t kModeListEntryCount = 11u;
		constexpr uint32_t kConstructionRigBuildModeThreshold = 0x18u;
		constexpr uint32_t kConstructionRigBuildFirstSlot = 3u;
		constexpr uint32_t kConstructionRigBuildLastSlot = 9u;

		bool TryGetWeaponSelectionState(
			BZR::GameObject* obj,
			uint32_t& outStoredRawMask,
			uint32_t& outStoredMask,
			uint32_t& outModeListEnabledMask,
			uint32_t& outModeListActiveSlot,
			CarrierWeaponSelectionLayout*& outCarrier)
		{
			outStoredRawMask = 0;
			outStoredMask = 0;
			outModeListEnabledMask = 0;
			outModeListActiveSlot = 0xFFFFFFFFu;
			outCarrier = nullptr;
			if (obj == nullptr)
			{
				return false;
			}

			__try
			{
				auto* objBytes = reinterpret_cast<uint8_t*>(obj);
				outStoredRawMask = *reinterpret_cast<uint32_t*>(objBytes + kGameObjectWeaponMaskOffset);
				outStoredMask = outStoredRawMask ^ kWeaponMaskDecodeXor;
				outModeListEnabledMask = *reinterpret_cast<uint32_t*>(objBytes + kGameObjectModeListEnabledMaskOffset);
				outModeListActiveSlot = *reinterpret_cast<uint32_t*>(objBytes + kGameObjectModeListActiveSlotOffset);
				outCarrier = *reinterpret_cast<CarrierWeaponSelectionLayout**>(objBytes + kGameObjectCarrierOffset);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outStoredRawMask = 0;
				outStoredMask = 0;
				outModeListEnabledMask = 0;
				outModeListActiveSlot = 0xFFFFFFFFu;
				outCarrier = nullptr;
				return false;
			}
		}

		bool TryGetModeListState(
			BZR::GameObject* obj,
			uint32_t& outEnabledMask,
			uint32_t& outActiveSlot,
			uint32_t& outActiveMode)
		{
			outEnabledMask = 0;
			outActiveSlot = 0xFFFFFFFFu;
			outActiveMode = 0;
			if (obj == nullptr)
			{
				return false;
			}

			__try
			{
				auto* objBytes = reinterpret_cast<uint8_t*>(obj);
				outEnabledMask = *reinterpret_cast<uint32_t*>(objBytes + kGameObjectModeListEnabledMaskOffset);
				outActiveSlot = *reinterpret_cast<uint32_t*>(objBytes + kGameObjectModeListActiveSlotOffset);
				if (outActiveSlot < kModeListEntryCount)
				{
					outActiveMode = *reinterpret_cast<uint32_t*>(objBytes + kGameObjectModeListOffset + (outActiveSlot * sizeof(uint32_t)));
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outEnabledMask = 0;
				outActiveSlot = 0xFFFFFFFFu;
				outActiveMode = 0;
				return false;
			}
		}

		BZR::Scanner* GetLiveScanner(BZR::handle h)
		{
			BZR::GameObject* obj = BZR::GameObject::GetObj(h);
			return obj != nullptr ? obj->GetScanner() : nullptr;
		}

		BZR::Jammer* GetLiveJammer(BZR::handle h)
		{
			BZR::GameObject* obj = BZR::GameObject::GetObj(h);
			return obj != nullptr ? obj->GetJammer() : nullptr;
		}

		bool TrySetAsUser(BZR::GameObject* obj)
		{
			__try
			{
				BZR::GameObject::SetAsUser(obj);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialFault("[EXU::SetAsUser] crashed obj=%p code=0x%08X", obj, GetExceptionCode());
				return false;
			}
		}

		bool TryGetCommTowerPowerHandle(BZR::GameObject* obj, BZR::handle& outHandle)
		{
			__try
			{
				__asm
				{
					mov eax, [obj]
					mov eax, [eax+0x238]
					mov [outHandle], eax;
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialFault("[EXU::IsCommTowerPowered] crashed obj=%p code=0x%08X", obj, GetExceptionCode());
				outHandle = 0;
				return false;
			}
		}
	}

	int SetAsUser(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		if (obj != nullptr)
		{
			TrySetAsUser(obj);
		}
		return 0;
	}

	int IsCommTowerPowered(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);

		lua_getglobal(L, "GetClassLabel");
		lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
		lua_call(L, 1, 1);

		std::string classLabel = luaL_checkstring(L, -1);

		if (classLabel != "commtower")
		{
			luaL_error(L, "Extra Utilities: object is not a comm tower");
			return 0;
		}

		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		BZR::handle powerHandle = 0;
		if (obj != nullptr)
		{
			TryGetCommTowerPowerHandle(obj, powerHandle);
		}
		
		lua_pushboolean(L, powerHandle == 0 ? 0 : 1);

		return 1;
	}

	int GetHandle(lua_State* L)
	{
		auto gameObject = reinterpret_cast<BZR::GameObject*>(CheckHandle(L, 1));
		if (!BZR::GameObject::IsLiveArenaObject(gameObject))
		{
			lua_pushnil(L);
			return 1;
		}
		BZR::handle h = BZR::GameObject::GetHandle(gameObject);
		lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
		return 1;
	}

	int GetMass(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float mass = 0.0f;
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		if (obj == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}
		__try
		{
			mass = obj->euler.mass;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogMaterialFault("[EXU::GetMass] crashed handle=%p code=0x%08X", reinterpret_cast<void*>(h), GetExceptionCode());
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, mass);
		return 1;
	}

	int SetMass(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float mass = static_cast<float>(luaL_checknumber(L, 2));
		if (!std::isfinite(mass) || mass <= 0.0f)
		{
			return luaL_argerror(L, 2, "mass must be a finite number greater than 0");
		}
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		if (obj == nullptr)
		{
			return 0;
		}
		__try
		{
			obj->euler.mass = mass;
			obj->euler.mass_inv = 1 / mass;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogMaterialFault("[EXU::SetMass] crashed handle=%p code=0x%08X", reinterpret_cast<void*>(h), GetExceptionCode());
		}
		return 0;
	}

	int GetObj(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		if (obj == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushlightuserdata(L, obj);
		return 1;
	}

	int GetConstructionRigSelectionInfo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		std::string classLabel;
		uint32_t enabledMask = 0;
		uint32_t activeSlot = 0xFFFFFFFFu;
		uint32_t activeMode = 0;

		if (obj == nullptr || !TryGetClassLabelFromLua(L, h, classLabel) || _stricmp(classLabel.c_str(), "constructionrig") != 0)
		{
			lua_pushnil(L);
			return 1;
		}

		if (!TryGetModeListState(obj, enabledMask, activeSlot, activeMode))
		{
			lua_pushnil(L);
			return 1;
		}

		const bool hasBuildSelection = activeMode > kConstructionRigBuildModeThreshold;

		lua_createtable(L, 0, 6);

		lua_pushboolean(L, hasBuildSelection ? 1 : 0);
		lua_setfield(L, -2, "hasBuildSelection");

		if (activeSlot < kModeListEntryCount)
		{
			lua_pushinteger(L, static_cast<lua_Integer>(activeSlot));
			lua_setfield(L, -2, "activeSlot");

			lua_pushinteger(L, static_cast<lua_Integer>(activeMode));
			lua_setfield(L, -2, "activeMode");
		}
		else
		{
			lua_pushnil(L);
			lua_setfield(L, -2, "activeSlot");
			lua_pushnil(L);
			lua_setfield(L, -2, "activeMode");
		}

		lua_pushinteger(L, static_cast<lua_Integer>(enabledMask));
		lua_setfield(L, -2, "modeListEnabledMask");

		if (hasBuildSelection)
		{
			lua_pushlightuserdata(L, reinterpret_cast<void*>(activeMode));
			lua_setfield(L, -2, "selectedClass");
		}
		else
		{
			lua_pushnil(L);
			lua_setfield(L, -2, "selectedClass");
		}

		if (hasBuildSelection && activeSlot >= kConstructionRigBuildFirstSlot && activeSlot <= kConstructionRigBuildLastSlot)
		{
			lua_pushinteger(L, static_cast<lua_Integer>(activeSlot - (kConstructionRigBuildFirstSlot - 1u)));
		}
		else
		{
			lua_pushnil(L);
		}
		lua_setfield(L, -2, "buildItemIndex");

		return 1;
	}

	int GetSelectedWeaponMask(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		uint32_t storedRawMask = 0;
		uint32_t storedMask = 0;
		uint32_t modeListEnabledMask = 0;
		uint32_t modeListActiveSlot = 0;
		CarrierWeaponSelectionLayout* carrier = nullptr;

		if (!TryGetWeaponSelectionState(obj, storedRawMask, storedMask, modeListEnabledMask, modeListActiveSlot, carrier) || carrier == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint32_t selectedMountedMask = 0;
		__try
		{
			selectedMountedMask = carrier->selectedMask & carrier->existingMask;
			lua_pushinteger(L, static_cast<lua_Integer>(selectedMountedMask));
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogMaterialFault("[EXU::GetSelectedWeaponMask] crashed handle=%p code=0x%08X", reinterpret_cast<void*>(h), GetExceptionCode());
			lua_pushnil(L);
		}
		return 1;
	}

	int GetWeaponSelectionInfo(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::GameObject* obj = BZR::GameObject::GetObj(h);
		uint32_t storedRawMask = 0;
		uint32_t storedMask = 0;
		uint32_t modeListEnabledMask = 0;
		uint32_t modeListActiveSlot = 0;
		CarrierWeaponSelectionLayout* carrier = nullptr;

		if (!TryGetWeaponSelectionState(obj, storedRawMask, storedMask, modeListEnabledMask, modeListActiveSlot, carrier))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_createtable(L, 0, 9);

		lua_pushinteger(L, static_cast<lua_Integer>(storedRawMask));
		lua_setfield(L, -2, "storedWeaponMaskRaw");
		lua_pushinteger(L, static_cast<lua_Integer>(storedMask));
		lua_setfield(L, -2, "storedWeaponMask");
		lua_pushinteger(L, static_cast<lua_Integer>(modeListEnabledMask));
		lua_setfield(L, -2, "modeListEnabledMask");
		if (modeListActiveSlot <= 4u)
		{
			lua_pushinteger(L, static_cast<lua_Integer>(modeListActiveSlot));
		}
		else
		{
			lua_pushnil(L);
		}
		lua_setfield(L, -2, "modeListActiveSlot");

		if (carrier != nullptr)
		{
			__try
			{
				const uint32_t selectedMountedMask = carrier->selectedMask & carrier->existingMask;
				const uint32_t selectedReadyMask = selectedMountedMask & carrier->enabledMask;

				lua_pushinteger(L, static_cast<lua_Integer>(carrier->existingMask));
				lua_setfield(L, -2, "carrierExistingMask");
				lua_pushinteger(L, static_cast<lua_Integer>(carrier->selectedMask));
				lua_setfield(L, -2, "carrierSelectedMask");
				lua_pushinteger(L, static_cast<lua_Integer>(carrier->enabledMask));
				lua_setfield(L, -2, "carrierEnabledMask");
				lua_pushinteger(L, static_cast<lua_Integer>(selectedMountedMask));
				lua_setfield(L, -2, "selectedMountedMask");
				lua_pushinteger(L, static_cast<lua_Integer>(selectedReadyMask));
				lua_setfield(L, -2, "selectedReadyMask");
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogMaterialFault("[EXU::GetWeaponSelectionInfo] crashed while reading carrier handle=%p code=0x%08X", reinterpret_cast<void*>(h), GetExceptionCode());
			}
		}

		return 1;
	}


	int GetRadarPeriod(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::Scanner* scanner = GetLiveScanner(h);
		if (scanner == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, scanner->period);
		return 1;
	}

	int SetRadarPeriod(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float period = static_cast<float>(luaL_checknumber(L, 2));
		luaL_argcheck(L, std::isfinite(period) && period > 0.0f, 2, "radar period must be a finite number greater than 0");
		BZR::Scanner* scanner = GetLiveScanner(h);
		if (scanner == nullptr)
		{
			return 0;
		}
		scanner->period = period;
		scanner->sweep = 0.01f; // this causes it to immediately update and skip the current sweep

		return 0;
	}

	int GetRadarRange(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::Scanner* scanner = GetLiveScanner(h);
		if (scanner == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushnumber(L, scanner->range);
		return 1;
	}

	int SetRadarRange(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float range = static_cast<float>(luaL_checknumber(L, 2));
		luaL_argcheck(L, std::isfinite(range) && range >= 0.0f, 2, "radar range must be a finite, non-negative number");
		BZR::Scanner* scanner = GetLiveScanner(h);
		if (scanner == nullptr)
		{
			return 0;
		}
		scanner->range = range;
		return 0;
	}

	int GetVelocJam(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		BZR::Jammer* jammer = GetLiveJammer(h);
		if (jammer != nullptr) // sometimes your gameobject may not have a jammer, like if you're a pilot for example
		{
			lua_pushnumber(L, jammer->maxSpeed);
		}
		else
		{
			lua_pushnil(L);
		}
		return 1;
	}

	int SetVelocJam(lua_State* L)
	{
		BZR::handle h = CheckHandle(L, 1);
		float maxSpeed = static_cast<float>(luaL_checknumber(L, 2));
		luaL_argcheck(L, std::isfinite(maxSpeed) && maxSpeed >= 0.0f, 2, "velocity jam speed must be a finite, non-negative number");
		BZR::Jammer* jammer = GetLiveJammer(h);
		if (jammer != nullptr)
		{
			jammer->maxSpeed = maxSpeed;
		}
		return 0;
	}
}
