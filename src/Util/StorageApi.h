/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Extra Utilities is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 */

#pragma once

/*
 * StorageApi.h
 *
 * exu.storage: sandboxed persistent storage for EXU mods. The format is
 * deliberately small and independent of Battlezone's native save-game ABI: a
 * checked binary TLV stream with an integrity checksum, schema version, atomic
 * replacement, and a one-generation backup.
 *
 * Supported Lua values:
 *   nil, boolean, finite number, string, table
 * Table keys may be strings or finite numbers. Functions, threads, handles,
 * userdata, lightuserdata, and cyclic tables are rejected rather than guessed
 * at or serialized as process-local pointers.
 *
 * The file format and its rules live in Util/StorageCodec.h, which has no
 * Windows or Lua dependency; StorageApi.cpp is the Lua binding and file I/O.
 */

#include <lua.hpp>

namespace ExtraUtilities::Lua::StorageApi
{
	// Registers exu.storage on the global exu table, if it exists.
	void Install(lua_State* L);

	// Native callers (the exu settings calls) use the same store as the Lua
	// API through these. `name` must pass StorageCodec::IsSafeNamespace.
	// PushLoad pushes exactly what exu.storage.Load(name) returns (value,
	// meta); PushSave pushes what exu.storage.Save(name, value) returns (ok,
	// err) for the value at valueIndex.
	void PushLoad(lua_State* L, const char* name);
	void PushSave(lua_State* L, const char* name, int valueIndex);
}
