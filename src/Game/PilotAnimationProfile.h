/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

// Strict validator for a mod-supplied pilot animation profile.
//
// The Lua binding walks the profile table and reports each key and value here;
// this class decides everything, so the rules are host-tested and the binding
// stays a thin translation. The owner decisions it encodes:
//
//   - Slot keys are exactly the SlotName() values: stand, enterCrouch,
//     crouched, exitCrouch, jump, land.
//   - Unknown keys are an error, at the top level and inside a slot. So is a
//     field this build cannot apply (reported as "not supported by this EXU
//     build" so a mod can tell a typo from an older EXU).
//
// A profile replaces the whole policy: omitted slots and omitted fields are
// stock.
//
// Slot fields:
//   mode       "stock" | "substitute"
//   animation  string, 1..kMaxAnimationName chars; required by, and only
//              allowed with, mode = "substitute"
//   completion "stock" | "animation" | "duration" | "manual"; only on
//              enterCrouch / exitCrouch
//   duration   seconds, finite and > 0; required by, and only allowed with,
//              completion = "duration"
//
// Exposed to Lua as exu.fps.SetPilotAnimationProfile, built against
// kBuildSupport. The "not supported by this EXU build" path stays for a
// narrower Support (host tests) and for future fields.
//
// No Windows, Ogre, Lua, or engine dependencies; no allocation.

#include "Game/PilotAnimationPolicy.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace ExtraUtilities::Lua::PilotAnimationPolicy
{
	// The Lua type of a reported value, as far as validation cares.
	enum class ValueType : std::uint8_t
	{
		Nil = 0,
		Boolean,
		Number,
		String,
		Table,
		Other,
	};

	struct Value
	{
		ValueType type = ValueType::Nil;
		double number = 0.0;
		// Valid for the duration of the call only; not retained.
		const char* string = nullptr;
		std::size_t length = 0;

		static Value Number(double n) noexcept
		{
			Value v{};
			v.type = ValueType::Number;
			v.number = n;
			return v;
		}

		static Value String(const char* s, std::size_t len) noexcept
		{
			Value v{};
			v.type = ValueType::String;
			v.string = s;
			v.length = len;
			return v;
		}

		static Value String(const char* s) noexcept
		{
			return String(s, s != nullptr ? std::strlen(s) : 0);
		}

		static Value Of(ValueType type) noexcept
		{
			Value v{};
			v.type = type;
			return v;
		}
	};

	inline const char* ValueTypeName(ValueType type) noexcept
	{
		switch (type)
		{
		case ValueType::Nil: return "nil";
		case ValueType::Boolean: return "boolean";
		case ValueType::Number: return "number";
		case ValueType::String: return "string";
		case ValueType::Table: return "table";
		case ValueType::Other: return "other";
		}
		return "unknown";
	}

	class ProfileBuilder
	{
	public:
		static constexpr std::size_t kErrorCapacity = 192;

		explicit ProfileBuilder(const Support& support) noexcept
			: m_support(support)
		{
		}

		// A top-level key. key is null (or keyLength 0) for a non-string key.
		bool BeginSlot(const char* key, std::size_t keyLength, ValueType type) noexcept
		{
			if (!Ok())
			{
				return false;
			}
			if (m_inSlot)
			{
				return Fail("internal: slot %s was not ended", SlotName(m_slot));
			}
			if (key == nullptr || keyLength == 0 || std::strlen(key) != keyLength)
			{
				return Fail("profile keys must be slot names "
					"(stand, enterCrouch, crouched, exitCrouch, jump, land)");
			}

			Slot slot = Slot::Stand;
			if (!SlotFromName(key, slot))
			{
				return Fail("unknown pilot animation slot '%.40s' "
					"(expected stand, enterCrouch, crouched, exitCrouch, jump, land)", key);
			}
			if ((m_seenSlots & SlotBit(slot)) != 0)
			{
				return Fail("slot %s given twice", SlotName(slot));
			}
			if (type != ValueType::Table)
			{
				return Fail("slot %s must be a table, got %s", SlotName(slot), ValueTypeName(type));
			}

			m_seenSlots = static_cast<std::uint8_t>(m_seenSlots | SlotBit(slot));
			m_inSlot = true;
			m_slot = slot;
			m_entry = TransitionPolicy{};
			m_fields = 0;
			return true;
		}

		bool BeginSlot(const char* key, ValueType type) noexcept
		{
			return BeginSlot(key, key != nullptr ? std::strlen(key) : 0, type);
		}

		// A key inside the current slot table.
		bool SetField(const char* key, std::size_t keyLength, const Value& value) noexcept
		{
			if (!Ok())
			{
				return false;
			}
			if (!m_inSlot)
			{
				return Fail("internal: field outside a slot");
			}
			const char* slotName = SlotName(m_slot);
			if (key == nullptr || keyLength == 0 || std::strlen(key) != keyLength)
			{
				return Fail("slot %s: field names must be strings "
					"(mode, animation, completion, duration)", slotName);
			}

			if (std::strcmp(key, "mode") == 0)
			{
				if (!ExpectString(key, value))
				{
					return false;
				}
				if (std::strcmp(value.string, "stock") == 0)
				{
					m_entry.mode = Mode::Stock;
				}
				else if (std::strcmp(value.string, "substitute") == 0)
				{
					m_entry.mode = Mode::Substitute;
				}
				else
				{
					return Fail("slot %s: unknown mode '%.40s' (expected stock, substitute)",
						slotName, value.string);
				}
				m_fields |= kHasMode;
				return true;
			}

			if (std::strcmp(key, "animation") == 0)
			{
				if (!ExpectString(key, value))
				{
					return false;
				}
				if (value.length == 0 || value.length > kMaxAnimationName ||
					std::strlen(value.string) != value.length)
				{
					return Fail("slot %s: animation must be 1-%u characters with no NUL",
						slotName, static_cast<unsigned>(kMaxAnimationName));
				}
				std::memcpy(m_entry.animation, value.string, value.length);
				m_entry.animation[value.length] = '\0';
				m_fields |= kHasAnimation;
				return true;
			}

			if (std::strcmp(key, "completion") == 0)
			{
				if (!SlotHasCompletion(m_slot))
				{
					return Fail("slot %s: completion is only valid for enterCrouch and exitCrouch",
						slotName);
				}
				if (!ExpectString(key, value))
				{
					return false;
				}
				if (!CompletionFromName(value.string, m_entry.completion))
				{
					return Fail("slot %s: unknown completion '%.40s' "
						"(expected stock, animation, duration, manual)", slotName, value.string);
				}
				m_fields |= kHasCompletion;
				return true;
			}

			if (std::strcmp(key, "duration") == 0)
			{
				if (value.type != ValueType::Number)
				{
					return Fail("slot %s: duration must be a number, got %s",
						slotName, ValueTypeName(value.type));
				}
				if (!std::isfinite(value.number) || !(value.number > 0.0) ||
					value.number > static_cast<double>(kMaxDuration))
				{
					return Fail("slot %s: duration must be a finite number of seconds in (0, %g]",
						slotName, static_cast<double>(kMaxDuration));
				}
				m_entry.duration = static_cast<float>(value.number);
				m_fields |= kHasDuration;
				return true;
			}

			return Fail("slot %s: unknown field '%.40s' (expected mode, animation, completion, duration)",
				slotName, key);
		}

		bool SetField(const char* key, const Value& value) noexcept
		{
			return SetField(key, key != nullptr ? std::strlen(key) : 0, value);
		}

		// Cross-field rules, then what this build supports.
		bool EndSlot() noexcept
		{
			if (!Ok())
			{
				return false;
			}
			if (!m_inSlot)
			{
				return Fail("internal: no slot to end");
			}
			const char* slotName = SlotName(m_slot);

			if (m_entry.mode == Mode::Substitute && (m_fields & kHasAnimation) == 0)
			{
				return Fail("slot %s: mode \"substitute\" requires animation", slotName);
			}
			if (m_entry.mode != Mode::Substitute && (m_fields & kHasAnimation) != 0)
			{
				return Fail("slot %s: animation is only allowed with mode \"substitute\"", slotName);
			}
			if (m_entry.completion == CompletionMode::Duration && (m_fields & kHasDuration) == 0)
			{
				return Fail("slot %s: completion \"duration\" requires duration", slotName);
			}
			if (m_entry.completion != CompletionMode::Duration && (m_fields & kHasDuration) != 0)
			{
				return Fail("slot %s: duration is only allowed with completion \"duration\"", slotName);
			}

			if (m_entry.mode == Mode::Substitute &&
				(m_support.substituteSlots & SlotBit(m_slot)) == 0)
			{
				return Fail("slot %s: mode \"substitute\" is not supported by this EXU build", slotName);
			}
			if (m_entry.completion != CompletionMode::Stock &&
				(m_support.completionModes & CompletionBit(m_entry.completion)) == 0)
			{
				return Fail("slot %s: completion \"%s\" is not supported by this EXU build",
					slotName, CompletionName(m_entry.completion));
			}

			m_policy.slots[static_cast<std::size_t>(m_slot)] = m_entry;
			m_inSlot = false;
			return true;
		}

		// The validated policy. False if any step failed.
		bool Finish(Policy& out) noexcept
		{
			if (!Ok())
			{
				return false;
			}
			if (m_inSlot)
			{
				return Fail("internal: slot %s was not ended", SlotName(m_slot));
			}
			if (!IsSupported(m_policy, m_support))
			{
				return Fail("internal: validated policy is not supported");
			}
			out = m_policy;
			return true;
		}

		bool Ok() const noexcept
		{
			return m_error[0] == '\0';
		}

		const char* Error() const noexcept
		{
			return m_error;
		}

	private:
		// Generous upper bound so a unit mistake (milliseconds) is caught.
		static constexpr float kMaxDuration = 60.0f;

		static constexpr unsigned kHasMode = 1u << 0;
		static constexpr unsigned kHasAnimation = 1u << 1;
		static constexpr unsigned kHasCompletion = 1u << 2;
		static constexpr unsigned kHasDuration = 1u << 3;

		static bool SlotFromName(const char* name, Slot& out) noexcept
		{
			for (std::size_t i = 0; i < kSlotCount; ++i)
			{
				const Slot slot = static_cast<Slot>(i);
				if (std::strcmp(name, SlotName(slot)) == 0)
				{
					out = slot;
					return true;
				}
			}
			return false;
		}

		static bool CompletionFromName(const char* name, CompletionMode& out) noexcept
		{
			const CompletionMode all[] = {
				CompletionMode::Stock, CompletionMode::Animation,
				CompletionMode::Duration, CompletionMode::Manual,
			};
			for (const CompletionMode completion : all)
			{
				if (std::strcmp(name, CompletionName(completion)) == 0)
				{
					out = completion;
					return true;
				}
			}
			return false;
		}

		bool ExpectString(const char* key, const Value& value) noexcept
		{
			if (value.type != ValueType::String || value.string == nullptr)
			{
				return Fail("slot %s: %s must be a string, got %s",
					SlotName(m_slot), key, ValueTypeName(value.type));
			}
			return true;
		}

		template <typename... Args>
		bool Fail(const char* format, Args... args) noexcept
		{
			// Keep the first error: it names the actual cause.
			if (m_error[0] == '\0')
			{
				std::snprintf(m_error, sizeof(m_error), format, args...);
				if (m_error[0] == '\0')
				{
					std::snprintf(m_error, sizeof(m_error), "invalid pilot animation profile");
				}
			}
			return false;
		}

		bool Fail(const char* message) noexcept
		{
			if (m_error[0] == '\0')
			{
				std::snprintf(m_error, sizeof(m_error), "%s", message);
			}
			return false;
		}

		Support m_support{};
		Policy m_policy{};
		TransitionPolicy m_entry{};
		Slot m_slot = Slot::Stand;
		bool m_inSlot = false;
		std::uint8_t m_seenSlots = 0;
		unsigned m_fields = 0;
		char m_error[kErrorCapacity]{};
	};
}
