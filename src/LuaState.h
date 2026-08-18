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

#pragma once

#include <lua.hpp>

#include <cstdint>

namespace ExtraUtilities::Lua
{
	// Implemented by PublicAPI.cpp. Assignment to a new mission VM installs the
	// per-state lifecycle sentinel and performs runtime initialization outside
	// DllMain/loader lock.
	void HandleLuaStateAttached(lua_State* L);
	void HandleLuaStateClosing(lua_State* L) noexcept;

	// Implemented by luaexport.cpp so state-owned registry references can be
	// released while the originating VM is still valid.
	void ReleaseLuaStateBindings(lua_State* L) noexcept;

	class LuaStateHandle
	{
	private:
		lua_State* m_state = nullptr;
		uint64_t m_generation = 0;

	public:
		LuaStateHandle() = default;
		LuaStateHandle(const LuaStateHandle&) = delete;
		LuaStateHandle& operator=(const LuaStateHandle&) = delete;

		LuaStateHandle& operator=(lua_State* L)
		{
			if (m_state == L)
			{
				return *this;
			}

			m_state = L;
			if (L != nullptr)
			{
				++m_generation;
				HandleLuaStateAttached(L);
			}
			return *this;
		}

		operator lua_State*() const noexcept
		{
			return m_state;
		}

		lua_State* Get() const noexcept
		{
			return m_state;
		}

		uint64_t Generation() const noexcept
		{
			return m_generation;
		}

		void Clear(lua_State* expected = nullptr) noexcept
		{
			if (expected == nullptr || m_state == expected)
			{
				m_state = nullptr;
			}
		}
	};

	inline LuaStateHandle state;
}
