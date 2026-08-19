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
 * Sandboxed persistent storage for EXU mods. The format is deliberately small
 * and independent of Battlezone's native save-game ABI: a checked binary TLV
 * stream with an integrity checksum, schema version, atomic replacement, and a
 * one-generation backup.
 *
 * Supported Lua values:
 *   nil, boolean, finite number, string, table
 * Table keys may be strings or finite numbers. Functions, threads, handles,
 * userdata, lightuserdata, and cyclic tables are rejected rather than guessed
 * at or serialized as process-local pointers.
 */

#include "LuaHelpers.h"

#include <Windows.h>
#include <lua.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace ExtraUtilities::Lua::StorageApi
{
	namespace Detail
	{
		constexpr std::uint8_t kMagic[8] = { 'E', 'X', 'U', 'D', 'A', 'T', 'A', '1' };
		constexpr std::uint32_t kFormatVersion = 1;
		constexpr std::size_t kMaxFileBytes = 16u * 1024u * 1024u;
		constexpr std::size_t kMaxStringBytes = 1024u * 1024u;
		constexpr std::size_t kMaxTableEntries = 100000u;
		constexpr int kMaxDepth = 32;

		enum class ValueTag : std::uint8_t
		{
			Nil = 0,
			Boolean = 1,
			Number = 2,
			String = 3,
			Table = 4,
		};

		enum class KeyTag : std::uint8_t
		{
			End = 0,
			Number = 1,
			String = 2,
		};

		struct Header
		{
			std::uint8_t magic[8];
			std::uint32_t formatVersion;
			std::uint32_t schemaVersion;
			std::uint32_t payloadBytes;
			std::uint32_t payloadCrc32;
		};

		static_assert(sizeof(Header) == 24, "EXU storage header layout changed");

		struct Paths
		{
			std::string directory;
			std::string primary;
			std::string backup;
			std::string temporary;
		};

		enum class ReadStatus
		{
			Ok,
			NotFound,
			Error,
		};

		struct DecodeMeta
		{
			std::uint32_t schemaVersion = 0;
			std::size_t sizeBytes = 0;
		};

		struct EncodeContext
		{
			std::unordered_set<const void*> activeTables;
			std::size_t tableEntries = 0;
		};

		struct DecodeCursor
		{
			const std::uint8_t* data = nullptr;
			std::size_t size = 0;
			std::size_t offset = 0;
			std::size_t tableEntries = 0;
		};

		inline void SetError(std::string& error, const std::string& value)
		{
			if (error.empty())
			{
				error = value;
			}
		}

		inline bool IsSafeNamespace(const std::string& name)
		{
			if (name.empty() || name.size() > 64)
			{
				return false;
			}

			auto isAlphaNum = [](unsigned char c)
			{
				return (c >= 'a' && c <= 'z') ||
					(c >= 'A' && c <= 'Z') ||
					(c >= '0' && c <= '9');
			};

			if (!isAlphaNum(static_cast<unsigned char>(name.front())))
			{
				return false;
			}

			for (char raw : name)
			{
				const unsigned char c = static_cast<unsigned char>(raw);
				if (!isAlphaNum(c) && c != '_' && c != '-' && c != '.')
				{
					return false;
				}
			}
			return true;
		}

		inline bool DirectoryExists(const std::string& path)
		{
			const DWORD attributes = GetFileAttributesA(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		inline bool FileExists(const std::string& path)
		{
			const DWORD attributes = GetFileAttributesA(path.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		inline std::uint64_t FileSizeOrZero(const std::string& path)
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

		inline bool EnsureDirectory(const std::string& path, std::string& error)
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

		inline bool GetStoragePaths(const std::string& name, Paths& paths, std::string& error)
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

		inline std::uint32_t Crc32(const std::uint8_t* data, std::size_t size)
		{
			std::uint32_t crc = 0xFFFFFFFFu;
			for (std::size_t i = 0; i < size; ++i)
			{
				crc ^= data[i];
				for (int bit = 0; bit < 8; ++bit)
				{
					const std::uint32_t mask = static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc & 1u)));
					crc = (crc >> 1) ^ (0xEDB88320u & mask);
				}
			}
			return ~crc;
		}

		template <typename T>
		inline bool AppendPod(std::vector<std::uint8_t>& out, const T& value, std::string& error)
		{
			if (out.size() + sizeof(T) > kMaxFileBytes)
			{
				SetError(error, "serialized data exceeds the EXU storage size limit");
				return false;
			}
			const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(&value);
			out.insert(out.end(), bytes, bytes + sizeof(T));
			return true;
		}

		inline bool AppendBytes(std::vector<std::uint8_t>& out, const void* data, std::size_t size, std::string& error)
		{
			if (size > kMaxFileBytes || out.size() + size > kMaxFileBytes)
			{
				SetError(error, "serialized data exceeds the EXU storage size limit");
				return false;
			}
			const auto* bytes = static_cast<const std::uint8_t*>(data);
			out.insert(out.end(), bytes, bytes + size);
			return true;
		}

		inline bool EncodeString(const char* value, std::size_t size, std::vector<std::uint8_t>& out, std::string& error)
		{
			if (size > kMaxStringBytes || size > std::numeric_limits<std::uint32_t>::max())
			{
				SetError(error, "string exceeds the EXU storage string-size limit");
				return false;
			}
			const std::uint32_t length = static_cast<std::uint32_t>(size);
			return AppendPod(out, length, error) && AppendBytes(out, value, size, error);
		}

		inline bool EncodeValue(lua_State* L, int index, std::vector<std::uint8_t>& out,
			EncodeContext& context, int depth, std::string& error);

		inline bool EncodeTable(lua_State* L, int index, std::vector<std::uint8_t>& out,
			EncodeContext& context, int depth, std::string& error)
		{
			if (depth > kMaxDepth)
			{
				SetError(error, "table nesting exceeds the EXU storage depth limit");
				return false;
			}

			const int absIndex = AbsoluteStackIndex(L, index);
			const void* identity = lua_topointer(L, absIndex);
			if (context.activeTables.find(identity) != context.activeTables.end())
			{
				SetError(error, "cyclic tables cannot be persisted");
				return false;
			}
			context.activeTables.insert(identity);

			lua_pushnil(L);
			while (lua_next(L, absIndex) != 0)
			{
				++context.tableEntries;
				if (context.tableEntries > kMaxTableEntries)
				{
					lua_pop(L, 2);
					context.activeTables.erase(identity);
					SetError(error, "table entry count exceeds the EXU storage limit");
					return false;
				}

				const int keyType = lua_type(L, -2);
				if (keyType == LUA_TNUMBER)
				{
					const double key = static_cast<double>(lua_tonumber(L, -2));
					if (!std::isfinite(key))
					{
						lua_pop(L, 2);
						context.activeTables.erase(identity);
						SetError(error, "table numeric keys must be finite");
						return false;
					}
					const std::uint8_t tag = static_cast<std::uint8_t>(KeyTag::Number);
					if (!AppendPod(out, tag, error) || !AppendPod(out, key, error))
					{
						lua_pop(L, 2);
						context.activeTables.erase(identity);
						return false;
					}
				}
				else if (keyType == LUA_TSTRING)
				{
					size_t length = 0;
					const char* key = lua_tolstring(L, -2, &length);
					const std::uint8_t tag = static_cast<std::uint8_t>(KeyTag::String);
					if (!AppendPod(out, tag, error) || !EncodeString(key, length, out, error))
					{
						lua_pop(L, 2);
						context.activeTables.erase(identity);
						return false;
					}
				}
				else
				{
					lua_pop(L, 2);
					context.activeTables.erase(identity);
					SetError(error, "table keys must be strings or numbers");
					return false;
				}

				if (!EncodeValue(L, -1, out, context, depth + 1, error))
				{
					lua_pop(L, 2);
					context.activeTables.erase(identity);
					return false;
				}
				lua_pop(L, 1);
			}

			const std::uint8_t end = static_cast<std::uint8_t>(KeyTag::End);
			const bool ok = AppendPod(out, end, error);
			context.activeTables.erase(identity);
			return ok;
		}

		inline bool EncodeValue(lua_State* L, int index, std::vector<std::uint8_t>& out,
			EncodeContext& context, int depth, std::string& error)
		{
			const int type = lua_type(L, index);
			switch (type)
			{
			case LUA_TNIL:
			{
				const std::uint8_t tag = static_cast<std::uint8_t>(ValueTag::Nil);
				return AppendPod(out, tag, error);
			}
			case LUA_TBOOLEAN:
			{
				const std::uint8_t tag = static_cast<std::uint8_t>(ValueTag::Boolean);
				const std::uint8_t value = lua_toboolean(L, index) ? 1u : 0u;
				return AppendPod(out, tag, error) && AppendPod(out, value, error);
			}
			case LUA_TNUMBER:
			{
				const double value = static_cast<double>(lua_tonumber(L, index));
				if (!std::isfinite(value))
				{
					SetError(error, "numbers must be finite");
					return false;
				}
				const std::uint8_t tag = static_cast<std::uint8_t>(ValueTag::Number);
				return AppendPod(out, tag, error) && AppendPod(out, value, error);
			}
			case LUA_TSTRING:
			{
				size_t length = 0;
				const char* value = lua_tolstring(L, index, &length);
				const std::uint8_t tag = static_cast<std::uint8_t>(ValueTag::String);
				return AppendPod(out, tag, error) && EncodeString(value, length, out, error);
			}
			case LUA_TTABLE:
			{
				const std::uint8_t tag = static_cast<std::uint8_t>(ValueTag::Table);
				return AppendPod(out, tag, error) && EncodeTable(L, index, out, context, depth + 1, error);
			}
			default:
				SetError(error, std::string("unsupported Lua value type: ") + lua_typename(L, type));
				return false;
			}
		}

		template <typename T>
		inline bool ReadPod(DecodeCursor& cursor, T& value, std::string& error)
		{
			if (cursor.offset > cursor.size || sizeof(T) > cursor.size - cursor.offset)
			{
				SetError(error, "persistent data is truncated");
				return false;
			}
			std::memcpy(&value, cursor.data + cursor.offset, sizeof(T));
			cursor.offset += sizeof(T);
			return true;
		}

		inline bool DecodeString(lua_State* L, DecodeCursor& cursor, std::string& error)
		{
			std::uint32_t length = 0;
			if (!ReadPod(cursor, length, error))
			{
				return false;
			}
			if (length > kMaxStringBytes || cursor.offset > cursor.size || length > cursor.size - cursor.offset)
			{
				SetError(error, "persistent string length is invalid");
				return false;
			}
			lua_pushlstring(L, reinterpret_cast<const char*>(cursor.data + cursor.offset), length);
			cursor.offset += length;
			return true;
		}

		inline bool DecodeValue(lua_State* L, DecodeCursor& cursor, int depth, std::string& error)
		{
			if (depth > kMaxDepth)
			{
				SetError(error, "persistent table nesting exceeds the supported depth");
				return false;
			}

			std::uint8_t rawTag = 0;
			if (!ReadPod(cursor, rawTag, error))
			{
				return false;
			}

			switch (static_cast<ValueTag>(rawTag))
			{
			case ValueTag::Nil:
				lua_pushnil(L);
				return true;
			case ValueTag::Boolean:
			{
				std::uint8_t value = 0;
				if (!ReadPod(cursor, value, error) || value > 1u)
				{
					SetError(error, "persistent boolean value is invalid");
					return false;
				}
				lua_pushboolean(L, value ? 1 : 0);
				return true;
			}
			case ValueTag::Number:
			{
				double value = 0.0;
				if (!ReadPod(cursor, value, error) || !std::isfinite(value))
				{
					SetError(error, "persistent numeric value is invalid");
					return false;
				}
				lua_pushnumber(L, static_cast<lua_Number>(value));
				return true;
			}
			case ValueTag::String:
				return DecodeString(L, cursor, error);
			case ValueTag::Table:
			{
				lua_newtable(L);
				const int tableIndex = AbsoluteStackIndex(L, -1);
				for (;;)
				{
					std::uint8_t rawKeyTag = 0;
					if (!ReadPod(cursor, rawKeyTag, error))
					{
						return false;
					}
					const KeyTag keyTag = static_cast<KeyTag>(rawKeyTag);
					if (keyTag == KeyTag::End)
					{
						return true;
					}

					++cursor.tableEntries;
					if (cursor.tableEntries > kMaxTableEntries)
					{
						SetError(error, "persistent table entry count exceeds the supported limit");
						return false;
					}

					if (keyTag == KeyTag::Number)
					{
						double key = 0.0;
						if (!ReadPod(cursor, key, error) || !std::isfinite(key))
						{
							SetError(error, "persistent numeric table key is invalid");
							return false;
						}
						lua_pushnumber(L, static_cast<lua_Number>(key));
					}
					else if (keyTag == KeyTag::String)
					{
						if (!DecodeString(L, cursor, error))
						{
							return false;
						}
					}
					else
					{
						SetError(error, "persistent table key tag is invalid");
						return false;
					}

					if (!DecodeValue(L, cursor, depth + 1, error))
					{
						return false;
					}
					lua_settable(L, tableIndex);
				}
			}
			default:
				SetError(error, "persistent value tag is invalid");
				return false;
			}
		}

		inline bool WriteWholeFile(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error)
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

		inline ReadStatus ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& bytes, std::string& error)
		{
			HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				const DWORD code = GetLastError();
				if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
				{
					return ReadStatus::NotFound;
				}
				SetError(error, "failed to open persistent data (Win32 error " + std::to_string(code) + ")");
				return ReadStatus::Error;
			}

			LARGE_INTEGER size{};
			if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > kMaxFileBytes)
			{
				SetError(error, "persistent data file size is invalid or exceeds the EXU limit");
				CloseHandle(file);
				return ReadStatus::Error;
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
					return ReadStatus::Error;
				}
				offset += read;
			}
			CloseHandle(file);
			return ReadStatus::Ok;
		}

		inline bool BuildFile(lua_State* L, int valueIndex, std::uint32_t schemaVersion,
			std::vector<std::uint8_t>& fileBytes, std::string& error)
		{
			std::vector<std::uint8_t> payload;
			EncodeContext context{};
			const int top = lua_gettop(L);
			const bool encoded = EncodeValue(L, valueIndex, payload, context, 0, error);
			lua_settop(L, top);
			if (!encoded)
			{
				return false;
			}

			if (payload.size() > std::numeric_limits<std::uint32_t>::max())
			{
				SetError(error, "serialized payload is too large");
				return false;
			}

			Header header{};
			std::memcpy(header.magic, kMagic, sizeof(kMagic));
			header.formatVersion = kFormatVersion;
			header.schemaVersion = schemaVersion;
			header.payloadBytes = static_cast<std::uint32_t>(payload.size());
			header.payloadCrc32 = Crc32(payload.data(), payload.size());

			fileBytes.resize(sizeof(Header) + payload.size());
			std::memcpy(fileBytes.data(), &header, sizeof(Header));
			if (!payload.empty())
			{
				std::memcpy(fileBytes.data() + sizeof(Header), payload.data(), payload.size());
			}
			return true;
		}

		inline bool DecodeFile(lua_State* L, const std::vector<std::uint8_t>& bytes, DecodeMeta& meta, std::string& error)
		{
			if (bytes.size() < sizeof(Header))
			{
				SetError(error, "persistent data header is truncated");
				return false;
			}

			Header header{};
			std::memcpy(&header, bytes.data(), sizeof(Header));
			if (std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0)
			{
				SetError(error, "persistent data magic is invalid");
				return false;
			}
			if (header.formatVersion != kFormatVersion)
			{
				SetError(error, "unsupported EXU persistent data format version");
				return false;
			}
			if (header.payloadBytes != bytes.size() - sizeof(Header))
			{
				SetError(error, "persistent payload length does not match the file");
				return false;
			}

			const std::uint8_t* payload = bytes.data() + sizeof(Header);
			if (Crc32(payload, header.payloadBytes) != header.payloadCrc32)
			{
				SetError(error, "persistent data checksum failed");
				return false;
			}

			DecodeCursor cursor{ payload, header.payloadBytes, 0, 0 };
			const int top = lua_gettop(L);
			if (!DecodeValue(L, cursor, 0, error) || cursor.offset != cursor.size)
			{
				lua_settop(L, top);
				if (error.empty())
				{
					SetError(error, "persistent data contains trailing bytes");
				}
				return false;
			}

			meta.schemaVersion = header.schemaVersion;
			meta.sizeBytes = bytes.size();
			return true;
		}

		inline bool SaveAtomic(const Paths& paths, const std::vector<std::uint8_t>& bytes, std::string& error)
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

		inline int PushIoResult(lua_State* L, bool ok, const std::string& error)
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

		inline void PushLoadMeta(lua_State* L, bool exists, const char* source, bool recovered,
			const DecodeMeta& meta, const std::string& primaryError)
		{
			lua_createtable(L, 0, 8);
			lua_pushboolean(L, exists ? 1 : 0);
			lua_setfield(L, -2, "exists");
			lua_pushstring(L, source);
			lua_setfield(L, -2, "source");
			lua_pushboolean(L, recovered ? 1 : 0);
			lua_setfield(L, -2, "recovered");
			lua_pushinteger(L, static_cast<lua_Integer>(kFormatVersion));
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

		inline std::string CheckNamespace(lua_State* L, int index)
		{
			size_t length = 0;
			const char* raw = luaL_checklstring(L, index, &length);
			const std::string name(raw, length);
			if (!IsSafeNamespace(name))
			{
				luaL_argerror(L, index, "storage namespace must be 1-64 chars, start alphanumeric, and contain only A-Z a-z 0-9 . _ -");
			}
			return name;
		}
	}

	inline int Save(lua_State* L)
	{
		const std::string name = Detail::CheckNamespace(L, 1);
		const std::uint32_t schemaVersion = lua_isnoneornil(L, 3)
			? 1u
			: static_cast<std::uint32_t>(luaL_checkinteger(L, 3));
		if (!lua_isnoneornil(L, 3) && lua_tonumber(L, 3) < 0)
		{
			return luaL_argerror(L, 3, "schema version must be non-negative");
		}

		std::string error;
		Detail::Paths paths{};
		if (!Detail::GetStoragePaths(name, paths, error))
		{
			return Detail::PushIoResult(L, false, error);
		}

		std::vector<std::uint8_t> bytes;
		if (!Detail::BuildFile(L, 2, schemaVersion, bytes, error))
		{
			return Detail::PushIoResult(L, false, error);
		}
		return Detail::PushIoResult(L, Detail::SaveAtomic(paths, bytes, error), error);
	}

	inline int Load(lua_State* L)
	{
		const std::string name = Detail::CheckNamespace(L, 1);
		std::string pathError;
		Detail::Paths paths{};
		if (!Detail::GetStoragePaths(name, paths, pathError))
		{
			lua_pushnil(L);
			Detail::DecodeMeta meta{};
			Detail::PushLoadMeta(L, false, "error", false, meta, pathError);
			return 2;
		}

		const int baseTop = lua_gettop(L);
		std::vector<std::uint8_t> bytes;
		std::string primaryError;
		const Detail::ReadStatus primaryRead = Detail::ReadWholeFile(paths.primary, bytes, primaryError);
		if (primaryRead == Detail::ReadStatus::Ok)
		{
			Detail::DecodeMeta meta{};
			if (Detail::DecodeFile(L, bytes, meta, primaryError))
			{
				Detail::PushLoadMeta(L, true, "primary", false, meta, std::string{});
				return 2;
			}
			lua_settop(L, baseTop);
		}

		std::vector<std::uint8_t> backupBytes;
		std::string backupError;
		const Detail::ReadStatus backupRead = Detail::ReadWholeFile(paths.backup, backupBytes, backupError);
		if (backupRead == Detail::ReadStatus::Ok)
		{
			Detail::DecodeMeta meta{};
			if (Detail::DecodeFile(L, backupBytes, meta, backupError))
			{
				Detail::PushLoadMeta(L, true, "backup", true, meta, primaryError);
				return 2;
			}
			lua_settop(L, baseTop);
		}

		if (primaryRead == Detail::ReadStatus::NotFound && backupRead == Detail::ReadStatus::NotFound)
		{
			lua_newtable(L);
			Detail::DecodeMeta meta{};
			Detail::PushLoadMeta(L, false, "new", false, meta, std::string{});
			return 2;
		}

		std::string combined = primaryError;
		if (!backupError.empty())
		{
			if (!combined.empty())
			{
				combined += "; backup: ";
			}
			combined += backupError;
		}
		if (combined.empty())
		{
			combined = "persistent data could not be loaded";
		}
		lua_pushnil(L);
		Detail::DecodeMeta meta{};
		Detail::PushLoadMeta(L, false, "error", false, meta, combined);
		return 2;
	}

	inline int Exists(lua_State* L)
	{
		const std::string name = Detail::CheckNamespace(L, 1);
		std::string error;
		Detail::Paths paths{};
		if (!Detail::GetStoragePaths(name, paths, error))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		lua_pushboolean(L, (Detail::FileExists(paths.primary) || Detail::FileExists(paths.backup)) ? 1 : 0);
		return 1;
	}

	inline int Delete(lua_State* L)
	{
		const std::string name = Detail::CheckNamespace(L, 1);
		std::string error;
		Detail::Paths paths{};
		if (!Detail::GetStoragePaths(name, paths, error))
		{
			return Detail::PushIoResult(L, false, error);
		}

		bool ok = true;
		for (const std::string* path : { &paths.primary, &paths.backup, &paths.temporary })
		{
			if (Detail::FileExists(*path) && !DeleteFileA(path->c_str()))
			{
				ok = false;
				Detail::SetError(error, "failed to delete persistent data (Win32 error " + std::to_string(GetLastError()) + ")");
			}
		}
		return Detail::PushIoResult(L, ok, error);
	}

	inline int GetInfo(lua_State* L)
	{
		const std::string name = Detail::CheckNamespace(L, 1);
		std::string error;
		Detail::Paths paths{};
		if (!Detail::GetStoragePaths(name, paths, error))
		{
			lua_pushnil(L);
			lua_pushlstring(L, error.data(), error.size());
			return 2;
		}

		lua_createtable(L, 0, 7);
		lua_pushboolean(L, Detail::FileExists(paths.primary) ? 1 : 0);
		lua_setfield(L, -2, "primaryExists");
		lua_pushboolean(L, Detail::FileExists(paths.backup) ? 1 : 0);
		lua_setfield(L, -2, "backupExists");
		lua_pushboolean(L, Detail::FileExists(paths.temporary) ? 1 : 0);
		lua_setfield(L, -2, "temporaryExists");
		lua_pushnumber(L, static_cast<lua_Number>(Detail::FileSizeOrZero(paths.primary)));
		lua_setfield(L, -2, "primaryBytes");
		lua_pushnumber(L, static_cast<lua_Number>(Detail::FileSizeOrZero(paths.backup)));
		lua_setfield(L, -2, "backupBytes");
		lua_pushinteger(L, static_cast<lua_Integer>(Detail::kFormatVersion));
		lua_setfield(L, -2, "formatVersion");
		lua_pushnil(L);
		return 2;
	}

	inline int GetCapabilities(lua_State* L)
	{
		lua_createtable(L, 0, 9);
		lua_pushinteger(L, static_cast<lua_Integer>(Detail::kFormatVersion));
		lua_setfield(L, -2, "formatVersion");
		lua_pushnumber(L, static_cast<lua_Number>(Detail::kMaxFileBytes));
		lua_setfield(L, -2, "maxFileBytes");
		lua_pushnumber(L, static_cast<lua_Number>(Detail::kMaxStringBytes));
		lua_setfield(L, -2, "maxStringBytes");
		lua_pushinteger(L, Detail::kMaxDepth);
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

	inline void Install(lua_State* L)
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
