// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/WeaponControl.h"
#include "Game/WeaponControlCore.h"
#include "GameObjectInternal.h"
#include "LuaHelpers.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/MsvcRtti.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"

#include <cstddef>
#include <cstdint>

// Engine facts (GOG 2.2.301, Ghidra on the shipping exe unless noted):
//  - GameObject+0x1A0 is the Carrier* (GameObject ctor 0x004DA0B0; also
//    kGameObjectCarrierOffset in GameObject.cpp). Carrier layout is
//    CarrierWeaponSelectionLayout (ctor 0x004D9800, 0x3C bytes, +0 owner).
//  - Weapon vtable: +8 Trigger, +0xC UnTrigger, +0x10 IsTriggered (Weapon base
//    vtable 0x0088A194). Trigger is a one-tick latch: 1.5 Cannon::Simulate
//    fires when triggered and then clears it (all_decompiled.c ~349600-349660).
//  - Carrier::TriggerSelected 0x00511FC0 and TriggerSpecial 0x00608AA0 call
//    weapon->Trigger without a null check; SetSelected 0x004D9880 null-checks
//    (Select +0x1C / Deselect +0x20 only on non-null weapons).
//  - Carrier::SetEnabled 0x004D9950: *(this+0x34) = mask & *(this+0x2C).
//    Producer::Simulate 0x005AC580 (Producer, Armory, Factory,
//    ConstructionRig) and the TurretTank/Howitzer Simulate 0x005F2A40 rewrite
//    the mask every tick: SetEnabled(obj+0x228 == 2 ? -1 : 0) at 0x005AE20C/
//    0x005AE221 and 0x005F3045/0x005F305A.
//  - Hardpoint names: GameObjectClass (GameObject+0xF8) char[4] at
//    +0xE4 + slot*4 (GameObject ctor 0x004DA0B0 "no hardpoint named '%s'"
//    error formats param_2+0xe4+slot*4; 1.5 weaponHard[5][4]).
//  - Weapon+8 is the WeaponClass*, WeaponClass+0x18 its 8-char ODF name
//    (stock GetWeaponClass 0x00500060 pushes *(weapon+8)+0x18 with length 8).
//  - SpecialItem (vtable 0x00888100; RadarDamper, ImageRefract,
//    TerrainExpose derive from it): IsTriggered 0x004F2A10 returns the byte at
//    +0xC0, which Trigger 0x005D9880 toggles and UnTrigger 0x005D98F0 clears.
namespace ExtraUtilities::Lua::WeaponControl
{
	namespace
	{
		namespace Core = ExtraUtilities::WeaponControl;
		using Carrier = GameObject::Detail::CarrierWeaponSelectionLayout;

		constexpr std::size_t kGameObjectClassOffset = 0xF8;
		constexpr std::size_t kGameObjectCarrierOffset = 0x1A0;
		constexpr std::size_t kClassWeaponHardOffset = 0xE4;
		constexpr std::size_t kWeaponClassOffset = 0x08;
		constexpr std::size_t kWeaponClassOdfOffset = 0x18;
		constexpr std::size_t kSpecialItemTriggeredOffset = 0xC0;

		static_assert(offsetof(Carrier, weapon) == 0x18, "Carrier weapons at +0x18 (0x004D9800)");
		static_assert(offsetof(Carrier, existingMask) == 0x2C, "existant mask at +0x2C (0x004A77A0)");
		static_assert(offsetof(Carrier, selectedMask) == 0x30, "selected mask at +0x30 (0x00417F90)");
		static_assert(offsetof(Carrier, enabledMask) == 0x34, "enabled mask at +0x34 (0x004D9950)");
		static_assert(offsetof(Carrier, special) == 0x38, "special slot at +0x38 (0x00472420)");

		using CarrierFn = void(__thiscall*)(void*);

		struct CarrierView
		{
			BZR::GameObject* object = nullptr;
			Carrier* carrier = nullptr;
			const void* weapon[Core::kSlotCount] = {};
			std::uint32_t existing = 0;
			std::uint32_t selected = 0;
			std::uint32_t enabled = 0;
			std::int32_t special = -1;
			std::uint32_t nonNull = 0;
		};

		// POD shell: no C++ objects with destructors inside __try.
		bool ReadCarrierUnsafe(BZR::GameObject* obj, CarrierView& v)
		{
			auto* bytes = reinterpret_cast<std::uint8_t*>(obj);
			auto* carrier = *reinterpret_cast<Carrier**>(bytes + kGameObjectCarrierOffset);
			if (carrier == nullptr || carrier->owner != obj) return false;
			v.object = obj;
			v.carrier = carrier;
			for (int i = 0; i < Core::kSlotCount; ++i) v.weapon[i] = carrier->weapon[i];
			v.existing = carrier->existingMask;
			v.selected = carrier->selectedMask;
			v.enabled = carrier->enabledMask;
			v.special = carrier->special;
			v.nonNull = Core::NonNullMask(v.weapon);
			return true;
		}

		// Live object with a Carrier that names it as owner, on the qualified
		// build only. False for dead handles, buildings and anything without
		// weapons.
		bool ReadCarrier(BZR::handle h, CarrierView& v)
		{
			v = CarrierView{};
			if (!RuntimeGate::IsSupported()) return false;
			BZR::GameObject* obj = BZR::GameObject::GetObj(h);
			if (obj == nullptr) return false;
			__try
			{
				return ReadCarrierUnsafe(obj, v);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				v = CarrierView{};
				return false;
			}
		}

		bool CallCarrier(std::uintptr_t fn, Carrier* carrier, const char* what)
		{
			__try
			{
				reinterpret_cast<CarrierFn>(fn)(carrier);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::%s] engine call faulted code=0x%08X", what, GetExceptionCode());
				return false;
			}
		}

		bool SetSelected(Carrier* carrier, std::uint32_t mask)
		{
			__try
			{
				reinterpret_cast<void(__thiscall*)(void*, std::uint32_t)>(EngineAddresses::VehicleControl::SetSelected)(carrier, mask);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::FireWeapons] SetSelected faulted code=0x%08X", GetExceptionCode());
				return false;
			}
		}

		// Weapon slot argument: luaL_argerror for a non-number, -1 for an
		// out-of-range or fractional number.
		int CheckSlot(lua_State* L, int idx)
		{
			const double slot = static_cast<double>(luaL_checknumber(L, idx));
			return Core::ValidSlot(slot) ? static_cast<int>(slot) : -1;
		}

		bool EngineOwnsEnabledMask(const BZR::GameObject* obj)
		{
			__try
			{
				// RTTI (GOG 2.2.301): Armory, Factory and ConstructionRig derive from
				// Producer; Howitzer derives from TurretTank.
				return MsvcRtti::HasBaseUnsafe(obj, ".?AVProducer@@") || MsvcRtti::HasBaseUnsafe(obj, ".?AVTurretTank@@");
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return true; // unknown class: refuse
			}
		}

		// 1 = on, 0 = off, -1 = not a SpecialItem or unreadable.
		int ReadSpecialState(const void* weapon)
		{
			__try
			{
				if (!MsvcRtti::HasBaseUnsafe(weapon, ".?AVSpecialItem@@")) return -1;
				return *(reinterpret_cast<const std::uint8_t*>(weapon) + kSpecialItemTriggeredOffset) != 0 ? 1 : 0;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return -1;
			}
		}

		bool ReadHardpointNames(const BZR::GameObject* obj, const void* weapon, int slot, char (&hardpoint)[Core::kHardpointNameLength + 1], char (&odf)[Core::kOdfNameLength + 1])
		{
			hardpoint[0] = '\0';
			odf[0] = '\0';
			__try
			{
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(obj);
				const auto* objClass = *reinterpret_cast<const std::uint8_t* const*>(bytes + kGameObjectClassOffset);
				if (objClass == nullptr) return false;
				Core::CopyFixedName(reinterpret_cast<const char*>(objClass + kClassWeaponHardOffset + slot * Core::kHardpointNameLength),
					Core::kHardpointNameLength, hardpoint);
				if (weapon != nullptr)
				{
					const auto* weaponClass = *reinterpret_cast<const std::uint8_t* const*>(reinterpret_cast<const std::uint8_t*>(weapon) + kWeaponClassOffset);
					if (weaponClass != nullptr)
						Core::CopyFixedName(reinterpret_cast<const char*>(weaponClass + kWeaponClassOdfOffset), Core::kOdfNameLength, odf);
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				hardpoint[0] = '\0';
				odf[0] = '\0';
				return false;
			}
		}

		bool WriteEnabledMask(Carrier* carrier, std::uint32_t mask)
		{
			__try
			{
				carrier->enabledMask = mask;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}
	}

	// exu.FireWeapons(h [, mask]) -> true if any weapon was triggered, false if
	// nothing could fire, nil for an invalid handle / no carrier.
	int FireWeapons(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const bool hasMask = !lua_isnoneornil(L, 2);
		const double mask = hasMask ? static_cast<double>(luaL_checknumber(L, 2)) : 0.0;

		CarrierView v;
		if (!ReadCarrier(h, v))
		{
			lua_pushnil(L);
			return 1;
		}

		std::uint32_t selected = v.selected;
		if (hasMask)
		{
			const std::uint32_t wanted = Core::RequestedSelection(mask, v.existing);
			if (wanted == 0u)
			{
				lua_pushboolean(L, 0);
				return 1;
			}
			if (wanted != v.selected)
			{
				if (!SetSelected(v.carrier, wanted) || !ReadCarrier(h, v))
				{
					lua_pushboolean(L, 0);
					return 1;
				}
				selected = v.selected;
			}
		}

		const std::uint32_t fire = Core::SafeTriggerSelectedMask(selected, v.enabled, v.nonNull);
		const bool fired = fire != 0u && CallCarrier(EngineAddresses::VehicleControl::FireSelected, v.carrier, "FireWeapons");
		lua_pushboolean(L, fired ? 1 : 0);
		return 1;
	}

	// exu.FireSpecialWeapon(h) -> true if the special slot was triggered.
	int FireSpecialWeapon(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		CarrierView v;
		if (!ReadCarrier(h, v))
		{
			lua_pushnil(L);
			return 1;
		}
		const bool fired = Core::SafeSpecialSlot(v.special, v.enabled, v.nonNull) >= 0 &&
			CallCarrier(EngineAddresses::VehicleControl::FireSpecial, v.carrier, "FireSpecialWeapon");
		lua_pushboolean(L, fired ? 1 : 0);
		return 1;
	}

	// exu.CeaseFire(h) -> true once every mounted weapon was untriggered.
	int CeaseFire(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		CarrierView v;
		if (!ReadCarrier(h, v))
		{
			lua_pushnil(L);
			return 1;
		}
		// UnTriggerAll null-checks each slot itself (0x004A7800).
		const bool done = CallCarrier(EngineAddresses::WeaponControl::UnTriggerAll, v.carrier, "CeaseFire");
		lua_pushboolean(L, done ? 1 : 0);
		return 1;
	}

	// exu.GetWeaponEnabled(h, slot) -> boolean, or nil for an empty slot.
	int GetWeaponEnabled(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const int slot = CheckSlot(L, 2);
		CarrierView v;
		const std::uint32_t bit = Core::SlotBit(slot);
		if (bit == 0u || !ReadCarrier(h, v) || (v.nonNull & v.existing & bit) == 0u)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushboolean(L, (v.enabled & bit) != 0u ? 1 : 0);
		return 1;
	}

	// exu.SetWeaponEnabled(h, slot, enabled) -> true if the mask was written.
	int SetWeaponEnabled(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const int slot = CheckSlot(L, 2);
		luaL_checktype(L, 3, LUA_TBOOLEAN);
		const bool on = lua_toboolean(L, 3) != 0;

		CarrierView v;
		const std::uint32_t bit = Core::SlotBit(slot);
		if (bit == 0u || !ReadCarrier(h, v) || (v.nonNull & v.existing & bit) == 0u || EngineOwnsEnabledMask(v.object))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		const bool written = WriteEnabledMask(v.carrier, Core::WithEnabledBit(v.enabled, v.existing, slot, on));
		lua_pushboolean(L, written ? 1 : 0);
		return 1;
	}

	// exu.IsSpecialActive(h, slot) -> boolean, or nil for an empty slot.
	int IsSpecialActive(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const int slot = CheckSlot(L, 2);
		CarrierView v;
		const std::uint32_t bit = Core::SlotBit(slot);
		if (bit == 0u || !ReadCarrier(h, v) || (v.nonNull & bit) == 0u)
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushboolean(L, ReadSpecialState(v.weapon[slot]) == 1 ? 1 : 0);
		return 1;
	}

	// exu.GetHardpoint(h, slot) -> hardpoint name or nil, weapon ODF or nil.
	int GetHardpoint(lua_State* L)
	{
		const BZR::handle h = CheckHandle(L, 1);
		const int slot = CheckSlot(L, 2);
		CarrierView v;
		char hardpoint[Core::kHardpointNameLength + 1] = {};
		char odf[Core::kOdfNameLength + 1] = {};
		if (Core::SlotBit(slot) == 0u || !ReadCarrier(h, v) ||
			!ReadHardpointNames(v.object, v.weapon[slot], slot, hardpoint, odf))
		{
			lua_pushnil(L);
			lua_pushnil(L);
			return 2;
		}
		if (hardpoint[0] != '\0') lua_pushstring(L, hardpoint); else lua_pushnil(L);
		if (odf[0] != '\0') lua_pushstring(L, odf); else lua_pushnil(L);
		return 2;
	}
}
