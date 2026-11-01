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

// Per-receiver damage multipliers as data. No Windows, engine or Lua
// dependency, so tests/host can check it.
//
// A pool address alone is not identity: the engine reuses it after death. Each
// entry also records the full generation-bearing handle. Lua APIs and damage
// dispatch run on the simulation thread; this table never calls Lua.

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace ExtraUtilities::UnitDamage
{
	class Policy
	{
		struct Entry
		{
			uint32_t handle;
			float multiplier;
		};

		static constexpr size_t kMaxEntries = 4096;

		std::unordered_map<uintptr_t, Entry> m_entries;

	public:
		// A multiplier of 1 removes the override.
		bool Set(uintptr_t object, uint32_t handle, float multiplier)
		{
			if (object == 0 || handle == 0 || !std::isfinite(multiplier) || multiplier < 0.0f || multiplier > 1.0f)
			{
				return false;
			}
			if (multiplier == 1.0f)
			{
				m_entries.erase(object);
				return true;
			}
			if (m_entries.size() >= kMaxEntries && m_entries.find(object) == m_entries.end())
			{
				return false;
			}
			m_entries.insert_or_assign(object, Entry{ handle, multiplier });
			return true;
		}

		float Get(uintptr_t object, uint32_t handle) const noexcept
		{
			const auto it = m_entries.find(object);
			return it != m_entries.end() && it->second.handle == handle ? it->second.multiplier : 1.0f;
		}

		// Healing (negative) and zero damage pass through unchanged.
		float Apply(uintptr_t object, uint32_t handle, float damage) const noexcept
		{
			if (!std::isfinite(damage) || damage <= 0.0f)
			{
				return damage;
			}
			return damage * Get(object, handle);
		}

		void Clear(uint32_t handle)
		{
			for (auto it = m_entries.begin(); it != m_entries.end();)
			{
				if (it->second.handle == handle)
				{
					it = m_entries.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void Reset() noexcept { m_entries.clear(); }
		bool Empty() const noexcept { return m_entries.empty(); }
	};
}
