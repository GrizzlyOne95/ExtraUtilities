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

#include "BZR.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"

#include <cmath>

namespace ExtraUtilities::Patch
{
	InlinePatch shotConvergence(wingmanWeaponAimVftableEntry, &walkerUpdateWeaponAim, 4, InlinePatch::Status::INACTIVE);
}

namespace
{
#pragma pack(push, 1)
	struct WeaponLayout
	{
		uint8_t padding[0x10];
		BZR::OBJ76* obj;
		BZR::OBJ76* hard;
	};
#pragma pack(pop)

	using HoverCraftUpdateWeaponAimFn = void(__thiscall*)(BZR::GameObject*, float);
	using CarrierGetWeaponFn = WeaponLayout*(__thiscall*)(void*, int);
	using RefreshWeaponTransformFn = void(__cdecl*)(BZR::OBJ76*, BZR::MAT_3D*);

	constexpr size_t kCarrierOffset = 0x1A0;
	constexpr int kWeaponSlots = 5;
	constexpr float kDirectionEpsilon = 0.001f;

	inline auto HoverCraftUpdateWeaponAim =
		reinterpret_cast<HoverCraftUpdateWeaponAimFn>(ExtraUtilities::Patch::hovercraftUpdateWeaponAim);
	inline auto CarrierGetWeapon =
		reinterpret_cast<CarrierGetWeaponFn>(ExtraUtilities::Patch::carrierGetWeapon);
	inline auto RefreshWeaponTransform =
		reinterpret_cast<RefreshWeaponTransformFn>(ExtraUtilities::Patch::refreshWeaponTransform);

	BZR::VECTOR_3D CrossProduct(const BZR::VECTOR_3D& lhs, const BZR::VECTOR_3D& rhs)
	{
		return {
			lhs.y * rhs.z - lhs.z * rhs.y,
			lhs.z * rhs.x - lhs.x * rhs.z,
			lhs.x * rhs.y - lhs.y * rhs.x
		};
	}

	BZR::MAT_3D BuildDirectionalMatrix(const BZR::VECTOR_3D& origin, BZR::VECTOR_3D direction)
	{
		direction.Normalize();

		BZR::VECTOR_3D right;
		if ((direction.x * direction.x) + (direction.z * direction.z) >= 0.02f)
		{
			right = CrossProduct({ 0.0f, 1.0f, 0.0f }, direction);
			right.Normalize();
		}
		else
		{
			right = { 1.0f, 0.0f, 0.0f };
		}

		const BZR::VECTOR_3D up = CrossProduct(direction, right);

		return {
			right.x, right.y, right.z,
			up.x, up.y, up.z,
			direction.x, direction.y, direction.z,
			{},
			origin.x, origin.y, origin.z
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

		for (int slot = 0; slot < kWeaponSlots; ++slot)
		{
			WeaponLayout* const weapon = CarrierGetWeapon(carrier, slot);
			if (weapon == nullptr || weapon->obj == nullptr)
			{
				continue;
			}

			BZR::OBJ76* const weaponObject = weapon->obj;
			const BZR::VECTOR_3D origin(
				weaponObject->transform.posit_x,
				weaponObject->transform.posit_y,
				weaponObject->transform.posit_z);
			BZR::VECTOR_3D direction = target - origin;
			if (direction.Length() <= kDirectionEpsilon)
			{
				continue;
			}

			weaponObject->transform = BuildDirectionalMatrix(origin, direction);
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
		lua_pushboolean(L, Patch::shotConvergence.IsActive());
		return 1;
	}

	int SetShotConvergence(lua_State* L)
	{
		bool status = CheckBool(L, 1);
		if (status == true)
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
		lua_pushboolean(L, Patch::playerReticleShotConvergence.IsActive());
		return 1;
	}

	int SetPlayerReticleShotConvergence(lua_State* L)
	{
		bool status = CheckBool(L, 1);
		if (status == true)
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
