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

#include "ShotConvergence.h"
#include "ShotConvergenceMath.h"

#include "BZR.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"
#include "OpenShimBridge.h"

#include <cstddef>

namespace ExtraUtilities::Patch
{
	InlinePatch shotConvergence(wingmanWeaponAimVftableEntry, &walkerUpdateWeaponAim, 4, InlinePatch::Status::INACTIVE);
}

namespace
{
	using OpenShimGetBoolFn = BOOL(WINAPI*)();
	using OpenShimSetBoolFn = BOOL(WINAPI*)(BOOL);

#pragma pack(push, 1)
	struct WeaponLayout
	{
		uint8_t padding[0x10];
		BZR::OBJ76* obj;
		BZR::OBJ76* hard;
		uint8_t paddingToMountWorld[0x10];
		BZR::MAT_3D mountWorld;
	};
#pragma pack(pop)

	static_assert(offsetof(WeaponLayout, mountWorld) == 0x28,
		"Weapon mount-world matrix offset no longer matches the supported BZR layout");

	using HoverCraftUpdateWeaponAimFn = void(__thiscall*)(BZR::GameObject*, float);
	using CarrierGetWeaponFn = WeaponLayout*(__thiscall*)(void*, int);
	using RefreshWeaponTransformFn = void(__cdecl*)(BZR::OBJ76*, BZR::MAT_3D*);

	constexpr size_t kCarrierOffset = 0x1A0;
	constexpr int kWeaponSlots = 5;

	inline auto HoverCraftUpdateWeaponAim =
		reinterpret_cast<HoverCraftUpdateWeaponAimFn>(ExtraUtilities::Patch::hovercraftUpdateWeaponAim);
	inline auto CarrierGetWeapon =
		reinterpret_cast<CarrierGetWeaponFn>(ExtraUtilities::Patch::carrierGetWeapon);
	inline auto RefreshWeaponTransform =
		reinterpret_cast<RefreshWeaponTransformFn>(ExtraUtilities::Patch::refreshWeaponTransform);

	ExtraUtilities::ShotConvergenceMath::Matrix ToMathMatrix(const BZR::MAT_3D& value)
	{
		return {
			value.right_x, value.right_y, value.right_z,
			value.up_x, value.up_y, value.up_z,
			value.front_x, value.front_y, value.front_z,
			value.posit_x, value.posit_y, value.posit_z,
		};
	}

	BZR::MAT_3D ToBzrMatrix(const ExtraUtilities::ShotConvergenceMath::Matrix& value)
	{
		return {
			value.rightX, value.rightY, value.rightZ,
			value.upX, value.upY, value.upZ,
			value.frontX, value.frontY, value.frontZ,
			{},
			value.positionX, value.positionY, value.positionZ,
		};
	}

	bool IsLocalUserCraft(BZR::GameObject* object)
	{
		if (object == nullptr)
		{
			return false;
		}

		const auto* userObjectPtr = reinterpret_cast<BZR::GameObject* const*>(BZR::GameObject::p_userObject);
		return userObjectPtr != nullptr && *userObjectPtr == object;
	}

	void ApplyReticleConvergence(BZR::GameObject* object)
	{
		if (!IsLocalUserCraft(object))
		{
			return;
		}

		auto* carrier = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(object) + kCarrierOffset);
		if (carrier == nullptr)
		{
			return;
		}

		const BZR::VECTOR_3D target = *BZR::Reticle::position;
		const ExtraUtilities::ShotConvergenceMath::Vec3 mathTarget{
			target.x,
			target.y,
			target.z,
		};

		for (int slot = 0; slot < kWeaponSlots; ++slot)
		{
			WeaponLayout* const weapon = CarrierGetWeapon(carrier, slot);
			if (weapon == nullptr || weapon->obj == nullptr)
			{
				continue;
			}

			BZR::OBJ76* const weaponObject = weapon->obj;
			const auto mountLocal = ToMathMatrix(weaponObject->transform);
			const auto mountWorld = ToMathMatrix(weapon->mountWorld);

			ExtraUtilities::ShotConvergenceMath::Matrix solvedLocal = {};
			if (!ExtraUtilities::ShotConvergenceMath::SolveReticleConvergence(
					mountLocal,
					mountWorld,
					mathTarget,
					solvedLocal))
			{
				continue;
			}

			// weaponObject->transform is mount-local in BZR. Build the aim in
			// world space from the actual composed muzzle position, then convert
			// it back to mount-local before writing it. Treating this translation
			// as a world origin is the old EXU bug and produces large angular
			// errors when the craft is rotated or far from the map origin.
			weaponObject->transform = ToBzrMatrix(solvedLocal);
			RefreshWeaponTransform(weaponObject, &weaponObject->transform);
		}
	}

	void __fastcall HoverCraftUpdateWeaponAimForReticle(BZR::GameObject* object, void*, float dt)
	{
		HoverCraftUpdateWeaponAim(object, dt);
		ApplyReticleConvergence(object);
	}
}

namespace ExtraUtilities::Patch
{
	InlinePatch playerReticleShotConvergence(
		hovercraftWeaponAimVftableEntry,
		&HoverCraftUpdateWeaponAimForReticle,
		4,
		InlinePatch::Status::INACTIVE);
}

namespace ExtraUtilities::Lua::Patches
{
	int GetShotConvergence(lua_State* L)
	{
		const auto bridge = OpenShimBridge::Resolve<OpenShimGetBoolFn>(
			"OpenShimGetShotConvergence");
		lua_pushboolean(L, bridge ? bridge() != FALSE : Patch::shotConvergence.IsActive());
		return 1;
	}

	int SetShotConvergence(lua_State* L)
	{
		bool status = CheckBool(L, 1);
		const auto bridge = OpenShimBridge::Resolve<OpenShimSetBoolFn>(
			"OpenShimSetShotConvergence");
		if (bridge)
		{
			bridge(status ? TRUE : FALSE);
		}
		else if (status == true)
		{
			Patch::shotConvergence.Reload();
		}
		else
		{
			Patch::shotConvergence.Unload();
		}
		return 0;
	}

	int GetPlayerReticleShotConvergence(lua_State* L)
	{
		const auto bridge = OpenShimBridge::Resolve<OpenShimGetBoolFn>(
			"OpenShimGetPlayerReticleShotConvergence");
		lua_pushboolean(
			L,
			bridge ? bridge() != FALSE : Patch::playerReticleShotConvergence.IsActive());
		return 1;
	}

	int SetPlayerReticleShotConvergence(lua_State* L)
	{
		bool status = CheckBool(L, 1);
		const auto bridge = OpenShimBridge::Resolve<OpenShimSetBoolFn>(
			"OpenShimSetPlayerReticleShotConvergence");
		if (bridge)
		{
			bridge(status ? TRUE : FALSE);
		}
		else if (status == true)
		{
			Patch::playerReticleShotConvergence.Reload();
		}
		else
		{
			Patch::playerReticleShotConvergence.Unload();
		}
		return 0;
	}
}
