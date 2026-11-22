// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>

// Pure mask and name logic behind the exu weapon-control bindings
// (src/Game/WeaponControl.cpp). No engine access, so it is host-tested
// (tests/host/weapon_control_core_tests.cpp).
//
// Carrier masks (GOG 2.2.301, see CarrierWeaponSelectionLayout): bit i is
// weapon slot i (0-4). existing = carrier+0x2C, selected = +0x30,
// enabled = +0x34, special = +0x38 (slot index or -1).
namespace ExtraUtilities::WeaponControl
{
	constexpr int kSlotCount = 5;
	constexpr std::uint32_t kAllSlots = (1u << kSlotCount) - 1u;

	// Hardpoint names are GameObjectClass char[4] fields (1.5 weaponHard[5][4],
	// all_decompiled.c 189364); weapon ODF names are WeaponClass char[8]
	// (stock GetWeaponClass pushes exactly 8 bytes, 1.5 all_decompiled.c 44669).
	constexpr std::size_t kHardpointNameLength = 4;
	constexpr std::size_t kOdfNameLength = 8;

	constexpr bool ValidSlot(double slot) noexcept
	{
		return slot >= 0.0 && slot < kSlotCount && static_cast<double>(static_cast<int>(slot)) == slot;
	}

	constexpr std::uint32_t SlotBit(int slot) noexcept
	{
		return (slot >= 0 && slot < kSlotCount) ? (1u << slot) : 0u;
	}

	// Bits of slots whose weapon pointer is non-null.
	inline std::uint32_t NonNullMask(const void* const (&weapons)[kSlotCount]) noexcept
	{
		std::uint32_t mask = 0;
		for (int i = 0; i < kSlotCount; ++i)
			if (weapons[i] != nullptr) mask |= 1u << i;
		return mask;
	}

	// Carrier::TriggerSelected (0x00511FC0) calls weapon[i]->Trigger for every
	// selected & enabled bit with no null check. Returns the slots it would
	// trigger, or 0 when any of them is empty (EXU then refuses the call).
	constexpr std::uint32_t SafeTriggerSelectedMask(std::uint32_t selected, std::uint32_t enabled, std::uint32_t nonNull) noexcept
	{
		const std::uint32_t fire = selected & enabled & kAllSlots;
		return (fire & ~nonNull) != 0 ? 0u : fire;
	}

	// The selection Carrier::SetSelected (0x004D9880) stores: mask & existing.
	// A request outside 1..31 or with no mounted weapon yields 0 (refused).
	constexpr std::uint32_t RequestedSelection(double mask, std::uint32_t existing) noexcept
	{
		if (!(mask >= 1.0 && mask <= static_cast<double>(kAllSlots))) return 0u;
		const auto bits = static_cast<std::uint32_t>(mask);
		if (static_cast<double>(bits) != mask) return 0u;
		return bits & existing & kAllSlots;
	}

	// Carrier::TriggerSpecial (0x00608AA0) fires weapon[special] when
	// special >= 0 and its enabled bit is set, with no null check. Returns the
	// slot it would fire, or -1 when it would do nothing or hit an empty slot.
	constexpr int SafeSpecialSlot(std::int32_t special, std::uint32_t enabled, std::uint32_t nonNull) noexcept
	{
		if (special < 0 || special >= kSlotCount) return -1;
		const std::uint32_t bit = 1u << special;
		return ((enabled & bit) != 0 && (nonNull & bit) != 0) ? static_cast<int>(special) : -1;
	}

	// New enabled mask, masked by existing as Carrier::SetEnabled (0x004D9950)
	// does: *(carrier+0x34) = mask & *(carrier+0x2C).
	constexpr std::uint32_t WithEnabledBit(std::uint32_t enabled, std::uint32_t existing, int slot, bool on) noexcept
	{
		const std::uint32_t bit = SlotBit(slot);
		return (on ? (enabled | bit) : (enabled & ~bit)) & existing & kAllSlots;
	}

	// Copies a fixed-width engine name (not necessarily NUL-terminated) into
	// out[max + 1]. Stops at the first NUL; any byte outside printable ASCII
	// rejects the whole name. Returns the length (0 = empty or rejected).
	inline std::size_t CopyFixedName(const char* src, std::size_t max, char* out) noexcept
	{
		out[0] = '\0';
		if (src == nullptr) return 0;
		std::size_t n = 0;
		for (; n < max && src[n] != '\0'; ++n)
		{
			const auto c = static_cast<unsigned char>(src[n]);
			if (c < 0x20 || c > 0x7E) { out[0] = '\0'; return 0; }
			out[n] = src[n];
		}
		out[n] = '\0';
		return n;
	}
}
