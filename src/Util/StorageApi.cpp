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

// exu.storage: the Lua walk over values and the Win32 file handling. The file
// format itself is Util/StorageCodec.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Util/StorageApi.h"

#include "LuaHelpers.h"
#include "Util/StorageCodec.h"

#include <Windows.h>
#include <lua.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace ExtraUtilities::Lua::StorageApi
{
	namespace
	{
		namespace Codec = ExtraUtilities::StorageCodec;
		using Codec::SetError;

		struct Paths
		{
			std::string directory;
			std::string primary;
			std::string backup;
			std::string temporary;
		};

		bool DirectoryExists(const std::string& path)
		{
			const DWORD attributes = GetFileAttributesA(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		bool FileExists(const std::string& path)
		{
			const DWORD attributes = GetFileAttributesA(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		std::uint64_t FileSizeOrZero(const std::string& path)
		{
			WIN32_FILE_ATTRIBUTE_DATA data{};
			if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
			{
				return 0;
			}
			ULARGE_INTEGER size{};
			size.HighPart = data.nFileSizeHigh;
			size.LowPart = data.nFileSizeLow;
			return size.QuadPart;
		}

		bool EnsureDirectory(const std::string& path, std::string& error)
		{
			if (DirectoryExists(path))
			{
				return true;
			}
			if (CreateDirectoryA(path.c_str(), nullptr))
			{
				return true;
			}
			const DWORD code = GetLastError();
			if (code == ERROR_ALREADY_EXISTS && DirectoryExists(path))
			{
				return true;
			}
			SetError(error, "failed to create EXU storage directory (Win32 error " + std::to_string(code) + ")");
			return false;
		}

		bool GetStoragePaths(const std::string& name, Paths& paths, std::string& error)
		{
			DWORD required = GetEnvironmentVariableA("LOCALAPPDATA", nullptr, 0);
			if (required == 0)
			{
				SetError(error, "LOCALAPPDATA is unavailable");
				return false;
			}

			std::vector<char> buffer(static_cast<std::size_t>(required));
			if (GetEnvironmentVariableA("LOCALAPPDATA", buffer.data(), required) == 0)
			{
				SetError(error, "failed to resolve LOCALAPPDATA");
				return false;
			}

			const std::string localAppData(buffer.data());
			const std::string bzRoot = localAppData + "\\Battlezone 98 Redux";
			const std::string exuRoot = bzRoot + "\\ExtraUtilities";
			const std::string storageRoot = exuRoot + "\\Storage";

			if (!EnsureDirectory(bzRoot, error) ||
				!EnsureDirectory(exuRoot, error) ||
				!EnsureDirectory(storageRoot, error))
			{
				return false;
			}

			paths.directory = storageRoot;
			paths.primary = storageRoot + "\\" + name + ".exudata";
			paths.backup = paths.primary + ".bak";
			paths.temporary = paths.primary + ".tmp";
			return true;
		}

		bool EncodeValue(lua_State* L, int index, Codec::Encoder& encoder, int depth, std::string& error);

		// depth is this table's nesting level: 1 for the value passed to Save.
		bool EncodeTable(lua_State* L, int index, Codec::Encoder& encoder, int depth, std::string& error)
		{
			if (!encoder.CheckDepth(depth))
			{
				return false;
			}

			// Each level keeps a key and a value on the stack. Lua 5.1 does not
			// grow the stack on push, so reserve it or fail cleanly.
			if (!lua_checkstack(L, 4))
			{
				SetError(error, "table nesting exceeds the available Lua stack");
				return false;
			}

			const int absIndex = AbsoluteStackIndex(L, index);
			const void* identity = lua_topointer(L, absIndex);
			if (!encoder.EnterTable(identity))
			{
				return false;
			}

			lua_pushnil(L);
			while (lua_next(L, absIndex) != 0)
			{
				if (!encoder.CountEntry())
				{
					lua_pop(L, 2);
					encoder.AbandonTable(identity);
					return false;
				}

				const int keyType = lua_type(L, -2);
				bool keyOk = false;
				if (keyType == LUA_TNUMBER)
				{
					keyOk = encoder.NumberKey(static_cast<double>(lua_tonumber(L, -2)));
				}
				else if (keyType == LUA_TSTRING)
				{
					size_t length = 0;
					const char* key = lua_tolstring(L, -2, &length);
					keyOk = encoder.StringKey(key, length);
				}
				else
				{
					keyOk = encoder.UnsupportedKey();
				}

				if (!keyOk || !EncodeValue(L, -1, encoder, depth, error))
				{
					lua_pop(L, 2);
					encoder.AbandonTable(identity);
					return false;
				}
				lua_pop(L, 1);
			}

			return encoder.EndTable(identity);
		}

		bool EncodeValue(lua_State* L, int index, Codec::Encoder& encoder, int depth, std::string& error)
		{
			const int type = lua_type(L, index);
			switch (type)
			{
			case LUA_TNIL:
				return encoder.Nil();
			case LUA_TBOOLEAN:
				return encoder.Boolean(lua_toboolean(L, index) != 0);
			case LUA_TNUMBER:
				return encoder.Number(static_cast<double>(lua_tonumber(L, index)));
			case LUA_TSTRING:
			{
				size_t length = 0;
				const char* value = lua_tolstring(L, index, &length);
				return encoder.String(value, length);
			}
			case LUA_TTABLE:
				return encoder.TableTag() && EncodeTable(L, index, encoder, depth + 1, error);
			default:
				return encoder.UnsupportedValue(lua_typename(L, type));
			}
		}

		// Pushes decoded values; each table stays on the stack under its
		// key/value pair until SetEntry stores the pair.
		class LuaDecodeSink final : public Codec::DecodeSink
		{
		public:
			explicit LuaDecodeSink(lua_State* L) : L_(L) {}

			void Nil() override { lua_pushnil(L_); }
			void Boolean(bool value) override { lua_pushboolean(L_, value ? 1 : 0); }
			void Number(double value) override { lua_pushnumber(L_, static_cast<lua_Number>(value)); }
			void String(const char* value, std::size_t size) override { lua_pushlstring(L_, value, size); }

			bool BeginTable(std::string& error) override
			{
				if (!lua_checkstack(L_, 4))
				{
					SetError(error, "persistent table nesting exceeds the available Lua stack");
					return false;
				}
				lua_newtable(L_);
				return true;
			}

			void SetEntry() override { lua_settable(L_, -3); }
			void EndTable() override {}

		private:
			lua_State* L_;
		};

		bool WriteWholeFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error)
		{
			HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				SetError(error, "failed to open persistent data for writing (Win32 error " + std::to_string(GetLastError()) + ")");
				return false;
			}

			std::size_t offset = 0;
			bool ok = true;
			while (offset < bytes.size())
			{
				const DWORD chunk = static_cast<DWORD>((std::min)(bytes.size() - offset,
					static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
				DWORD written = 0;
				if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr) || written != chunk)
				{
					SetError(error, "failed while writing persistent data (Win32 error " + std::to_string(GetLastError()) + ")");
					ok = false;
					break;
				}
				offset += written;
			}
			if (ok && !FlushFileBuffers(file))
			{
				SetError(error, "failed to flush persistent data (Win32 error " + std::to_string(GetLastError()) + ")");
				ok = false;
			}
			CloseHandle(file);
			return ok;
		}

		Codec::ReadStatus ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error)
		{
			HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				const DWORD code = GetLastError();
				if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
				{
					return Codec::ReadStatus::NotFound;
				}
				SetError(error, "failed to open persistent data (Win32 error " + std::to_string(code) + ")");
				return Codec::ReadStatus::Error;
			}

			LARGE_INTEGER size{};
			if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
				!Codec::IsFileSizeAllowed(static_cast<std::uint64_t>(size.QuadPart)))
			{
				SetError(error, "persistent data file size is invalid or exceeds the EXU limit");
				CloseHandle(file);
				return Codec::ReadStatus::Error;
			}

			bytes.resize(static_cast<std::size_t>(size.QuadPart));
			std::size_t offset = 0;
			while (offset < bytes.size())
			{
				const DWORD chunk = static_cast<DWORD>((std::min)(bytes.size() - offset,
					static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
				DWORD read = 0;
				if (!ReadFile(file, bytes.data() + offset, chunk, &read, nullptr) || read != chunk)
				{
					SetError(error, "failed while reading persistent data (Win32 error " + std::to_string(GetLastError()) + ")");
					CloseHandle(file);
					return Codec::ReadStatus::Error;
				}
				offset += read;
			}
			CloseHandle(file);
			return Codec::ReadStatus::Ok;
		}

		bool BuildFile(lua_State* L, int valueIndex, std::uint32_t schemaVersion,
			std::vector<std::uint8_t>& fileBytes, std::string& error)
		{
			std::vector<std::uint8_t> payload;
			Codec::Encoder encoder(payload, error);
			const int top = lua_gettop(L);
			const bool encoded = EncodeValue(L, valueIndex, encoder, 0, error);
			lua_settop(L, top);
			if (!encoded)
			{
				return false;
			}
			return Codec::WrapPayload(payload, schemaVersion, fileBytes, error);
		}

		// Leaves the decoded value on the stack, or nothing on failure.
		bool DecodeFile(lua_State* L, const std::vector<std::uint8_t>& bytes, Codec::DecodeMeta& meta, std::string& error)
		{
			LuaDecodeSink sink(L);
			const int top = lua_gettop(L);
			if (!Codec::DecodeFile(bytes, sink, meta, error))
			{
				lua_settop(L, top);
				return false;
			}
			return true;
		}

		bool SaveAtomic(const Paths& paths, const std::vector<std::uint8_t>& bytes, std::string& error)
		{
			DeleteFileA(paths.temporary.c_str());
			if (!WriteWholeFile(paths.temporary, bytes, error))
			{
				DeleteFileA(paths.temporary.c_str());
				return false;
			}

			if (FileExists(paths.primary))
			{
				if (!CopyFileA(paths.primary.c_str(), paths.backup.c_str(), FALSE))
				{
					SetError(error, "failed to update persistent-data backup (Win32 error " + std::to_string(GetLastError()) + ")");
					DeleteFileA(paths.temporary.c_str());
					return false;
				}
			}

			if (!MoveFileExA(paths.temporary.c_str(), paths.primary.c_str(),
				MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				SetError(error, "failed to atomically replace persistent data (Win32 error " + std::to_string(GetLastError()) + ")");
				DeleteFileA(paths.temporary.c_str());
				return false;
			}
			return true;
		}

		int PushIoResult(lua_State* L, bool ok, const std::string& error)
		{
			lua_pushboolean(L, ok ? 1 : 0);
			if (ok)
			{
				lua_pushnil(L);
			}
			else
			{
				lua_pushlstring(L, error.data(), error.size());
			}
			return 2;
		}

		void PushLoadMeta(lua_State* L, bool exists, const char* source, bool recovered,
			const Codec::DecodeMeta& meta, const std::string& primaryError)
		{
			lua_createtable(L, 0, 8);
			lua_pushboolean(L, exists ? 1 : 0);
			lua_setfield(L, -2, "exists");
			lua_pushstring(L, source);
			lua_setfield(L, -2, "source");
			lua_pushboolean(L, recovered ? 1 : 0);
			lua_setfield(L, -2, "recovered");
			lua_pushinteger(L, static_cast<lua_Integer>(Codec::kFormatVersion));
			lua_setfield(L, -2, "formatVersion");
			lua_pushinteger(L, static_cast<lua_Integer>(meta.schemaVersion));
			lua_setfield(L, -2, "schemaVersion");
			lua_pushnumber(L, static_cast<lua_Number>(meta.sizeBytes));
			lua_setfield(L, -2, "sizeBytes");
			if (!primaryError.empty())
			{
				lua_pushlstring(L, primaryError.data(), primaryError.size());
				lua_setfield(L, -2, "primaryError");
			}
		}

		std::string CheckNamespace(lua_State* L, int index)
		{
			size_t length = 0;
			const char* raw = luaL_checklstring(L, index, &length);
			const std::string name(raw, length);
			if (!Codec::IsSafeNamespace(name))
			{
				luaL_argerror(L, index, "storage namespace must be 1-64 chars, start alphanumeric, and contain only A-Z a-z 0-9 . _ -");
			}
			return name;
		}

		int Save(lua_State* L)
		{
			const std::string name = CheckNamespace(L, 1);
			const std::uint32_t schemaVersion = lua_isnoneornil(L, 3)
				? 1u
				: static_cast<std::uint32_t>(luaL_checkinteger(L, 3));
			if (!lua_isnoneornil(L, 3) && lua_tonumber(L, 3) < 0)
			{
				return luaL_argerror(L, 3, "schema version must be non-negative");
			}

			std::string error;
			Paths paths{};
			if (!GetStoragePaths(name, paths, error))
			{
				return PushIoResult(L, false, error);
			}

			// A 16 MiB buffer can fail to allocate in a fragmented 32-bit process;
			// a C++ exception must not cross the Lua C boundary.
			bool saved = false;
			try
			{
				std::vector<std::uint8_t> bytes;
				saved = BuildFile(L, 2, schemaVersion, bytes, error) &&
					SaveAtomic(paths, bytes, error);
			}
			catch (const std::bad_alloc&)
			{
				error = "out of memory while saving persistent data";
				saved = false;
			}
			return PushIoResult(L, saved, error);
		}

		int LoadUnguarded(lua_State* L)
		{
			const std::string name = CheckNamespace(L, 1);
			std::string pathError;
			Paths paths{};
			if (!GetStoragePaths(name, paths, pathError))
			{
				lua_pushnil(L);
				Codec::DecodeMeta meta{};
				PushLoadMeta(L, false, "error", false, meta, pathError);
				return 2;
			}

			// A failed decode leaves the stack as it was, so the loaded value
			// is always the only thing above the arguments.
			Codec::DecodeMeta meta{};
			std::string primaryError;
			bool primaryDecoded = false;
			std::vector<std::uint8_t> bytes;
			const Codec::ReadStatus primaryRead = ReadWholeFile(paths.primary, bytes, primaryError);
			if (primaryRead == Codec::ReadStatus::Ok)
			{
				primaryDecoded = DecodeFile(L, bytes, meta, primaryError);
			}

			std::string backupError;
			bool backupDecoded = false;
			Codec::ReadStatus backupRead = Codec::ReadStatus::NotFound;
			if (!primaryDecoded)
			{
				std::vector<std::uint8_t> backupBytes;
				backupRead = ReadWholeFile(paths.backup, backupBytes, backupError);
				if (backupRead == Codec::ReadStatus::Ok)
				{
					backupDecoded = DecodeFile(L, backupBytes, meta, backupError);
				}
			}

			switch (Codec::SelectLoadSource(primaryRead, primaryDecoded, backupRead, backupDecoded))
			{
			case Codec::LoadSource::Primary:
				PushLoadMeta(L, true, "primary", false, meta, std::string{});
				return 2;
			case Codec::LoadSource::Backup:
				PushLoadMeta(L, true, "backup", true, meta, primaryError);
				return 2;
			case Codec::LoadSource::New:
			{
				lua_newtable(L);
				const Codec::DecodeMeta empty{};
				PushLoadMeta(L, false, "new", false, empty, std::string{});
				return 2;
			}
			case Codec::LoadSource::Error:
			default:
			{
				lua_pushnil(L);
				const Codec::DecodeMeta empty{};
				PushLoadMeta(L, false, "error", false, empty, Codec::CombineLoadErrors(primaryError, backupError));
				return 2;
			}
			}
		}

		int Load(lua_State* L)
		{
			const int baseTop = lua_gettop(L);
			try
			{
				return LoadUnguarded(L);
			}
			catch (const std::bad_alloc&)
			{
				lua_settop(L, baseTop);
				lua_pushnil(L);
				Codec::DecodeMeta meta{};
				PushLoadMeta(L, false, "error", false, meta, "out of memory while loading persistent data");
				return 2;
			}
		}

		int Exists(lua_State* L)
		{
			const std::string name = CheckNamespace(L, 1);
			std::string error;
			Paths paths{};
			if (!GetStoragePaths(name, paths, error))
			{
				lua_pushboolean(L, 0);
				return 1;
			}
			lua_pushboolean(L, (FileExists(paths.primary) || FileExists(paths.backup)) ? 1 : 0);
			return 1;
		}

		int Delete(lua_State* L)
		{
			const std::string name = CheckNamespace(L, 1);
			std::string error;
			Paths paths{};
			if (!GetStoragePaths(name, paths, error))
			{
				return PushIoResult(L, false, error);
			}

			bool ok = true;
			for (const std::string* path : { &paths.primary, &paths.backup, &paths.temporary })
			{
				if (FileExists(*path) && !DeleteFileA(path->c_str()))
				{
					ok = false;
					SetError(error, "failed to delete persistent data (Win32 error " + std::to_string(GetLastError()) + ")");
				}
			}
			return PushIoResult(L, ok, error);
		}

		int GetInfo(lua_State* L)
		{
			const std::string name = CheckNamespace(L, 1);
			std::string error;
			Paths paths{};
			if (!GetStoragePaths(name, paths, error))
			{
				lua_pushnil(L);
				lua_pushlstring(L, error.data(), error.size());
				return 2;
			}

			lua_createtable(L, 0, 7);
			lua_pushboolean(L, FileExists(paths.primary) ? 1 : 0);
			lua_setfield(L, -2, "primaryExists");
			lua_pushboolean(L, FileExists(paths.backup) ? 1 : 0);
			lua_setfield(L, -2, "backupExists");
			lua_pushboolean(L, FileExists(paths.temporary) ? 1 : 0);
			lua_setfield(L, -2, "temporaryExists");
			lua_pushnumber(L, static_cast<lua_Number>(FileSizeOrZero(paths.primary)));
			lua_setfield(L, -2, "primaryBytes");
			lua_pushnumber(L, static_cast<lua_Number>(FileSizeOrZero(paths.backup)));
			lua_setfield(L, -2, "backupBytes");
			lua_pushinteger(L, static_cast<lua_Integer>(Codec::kFormatVersion));
			lua_setfield(L, -2, "formatVersion");
			lua_pushnil(L);
			return 2;
		}

		int GetCapabilities(lua_State* L)
		{
			lua_createtable(L, 0, 9);
			lua_pushinteger(L, static_cast<lua_Integer>(Codec::kFormatVersion));
			lua_setfield(L, -2, "formatVersion");
			lua_pushnumber(L, static_cast<lua_Number>(Codec::kMaxFileBytes));
			lua_setfield(L, -2, "maxFileBytes");
			lua_pushnumber(L, static_cast<lua_Number>(Codec::kMaxStringBytes));
			lua_setfield(L, -2, "maxStringBytes");
			lua_pushinteger(L, Codec::kMaxDepth);
			lua_setfield(L, -2, "maxDepth");
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, "tables");
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, "atomicReplace");
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, "backupRecovery");
			lua_pushboolean(L, 1);
			lua_setfield(L, -2, "checksum");
			lua_pushboolean(L, 0);
			lua_setfield(L, -2, "nativeSaveParsing");
			return 1;
		}
	}

	void Install(lua_State* L)
	{
		lua_getglobal(L, "exu");
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			return;
		}

		lua_newtable(L);
		const luaL_Reg functions[] = {
			{ "Save", &Save },
			{ "Load", &Load },
			{ "Exists", &Exists },
			{ "Delete", &Delete },
			{ "GetInfo", &GetInfo },
			{ "GetCapabilities", &GetCapabilities },
			{ nullptr, nullptr },
		};
		luaL_register(L, nullptr, functions);
		lua_setfield(L, -2, "storage");
		lua_pop(L, 1);
	}
}
