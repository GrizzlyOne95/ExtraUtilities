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

// The exu.storage file format. No Windows, Lua or engine dependency: this file
// is compiled into exu.dll and, unchanged, into tests/host/storage_codec_tests.

#include "Util/StorageCodec.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace ExtraUtilities::StorageCodec
{
	namespace
	{
		struct DecodeCursor
		{
			const std::uint8_t* data = nullptr;
			std::size_t size = 0;
			std::size_t offset = 0;
			std::size_t tableEntries = 0;
		};

		template <typename T>
		bool ReadPod(DecodeCursor& cursor, T& value, std::string& error)
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

		bool DecodeString(DecodeSink& sink, DecodeCursor& cursor, std::string& error)
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
			sink.String(reinterpret_cast<const char*>(cursor.data + cursor.offset), length);
			cursor.offset += length;
			return true;
		}

		// depth is the number of tables enclosing this value.
		bool DecodeValue(DecodeSink& sink, DecodeCursor& cursor, int depth, std::string& error)
		{
			std::uint8_t rawTag = 0;
			if (!ReadPod(cursor, rawTag, error))
			{
				return false;
			}

			switch (static_cast<ValueTag>(rawTag))
			{
			case ValueTag::Nil:
				sink.Nil();
				return true;
			case ValueTag::Boolean:
			{
				std::uint8_t value = 0;
				if (!ReadPod(cursor, value, error) || value > 1u)
				{
					SetError(error, "persistent boolean value is invalid");
					return false;
				}
				sink.Boolean(value != 0);
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
				sink.Number(value);
				return true;
			}
			case ValueTag::String:
				return DecodeString(sink, cursor, error);
			case ValueTag::Table:
			{
				if (depth + 1 > kMaxDepth)
				{
					SetError(error, "persistent table nesting exceeds the supported depth");
					return false;
				}
				if (!sink.BeginTable(error))
				{
					return false;
				}

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
						sink.EndTable();
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
						sink.Number(key);
					}
					else if (keyTag == KeyTag::String)
					{
						if (!DecodeString(sink, cursor, error))
						{
							return false;
						}
					}
					else
					{
						SetError(error, "persistent table key tag is invalid");
						return false;
					}

					if (!DecodeValue(sink, cursor, depth + 1, error))
					{
						return false;
					}
					sink.SetEntry();
				}
			}
			default:
				SetError(error, "persistent value tag is invalid");
				return false;
			}
		}
	}

	void SetError(std::string& error, const std::string& value)
	{
		if (error.empty())
		{
			error = value;
		}
	}

	bool IsSafeNamespace(const std::string& name)
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

	std::uint32_t Crc32(const std::uint8_t* data, std::size_t size)
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

	bool IsFileSizeAllowed(std::uint64_t sizeBytes)
	{
		return sizeBytes <= kMaxFileBytes;
	}

	LoadSource SelectLoadSource(ReadStatus primaryRead, bool primaryDecoded,
		ReadStatus backupRead, bool backupDecoded)
	{
		if (primaryRead == ReadStatus::Ok && primaryDecoded)
		{
			return LoadSource::Primary;
		}
		if (backupRead == ReadStatus::Ok && backupDecoded)
		{
			return LoadSource::Backup;
		}
		if (primaryRead == ReadStatus::NotFound && backupRead == ReadStatus::NotFound)
		{
			return LoadSource::New;
		}
		return LoadSource::Error;
	}

	std::string CombineLoadErrors(const std::string& primaryError, const std::string& backupError)
	{
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
		return combined;
	}

	Encoder::Encoder(std::vector<std::uint8_t>& out, std::string& error)
		: out_(out), error_(error)
	{
	}

	bool Encoder::AppendBytes(const void* data, std::size_t size)
	{
		if (size > kMaxFileBytes || out_.size() + size > kMaxFileBytes)
		{
			SetError(error_, "serialized data exceeds the EXU storage size limit");
			return false;
		}
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		out_.insert(out_.end(), bytes, bytes + size);
		return true;
	}

	bool Encoder::AppendByte(std::uint8_t value)
	{
		return AppendBytes(&value, sizeof(value));
	}

	bool Encoder::AppendDouble(double value)
	{
		return AppendBytes(&value, sizeof(value));
	}

	bool Encoder::AppendString(const char* value, std::size_t size)
	{
		if (size > kMaxStringBytes || size > std::numeric_limits<std::uint32_t>::max())
		{
			SetError(error_, "string exceeds the EXU storage string-size limit");
			return false;
		}
		const std::uint32_t length = static_cast<std::uint32_t>(size);
		return AppendBytes(&length, sizeof(length)) && AppendBytes(value, size);
	}

	bool Encoder::Nil()
	{
		return AppendByte(static_cast<std::uint8_t>(ValueTag::Nil));
	}

	bool Encoder::Boolean(bool value)
	{
		return AppendByte(static_cast<std::uint8_t>(ValueTag::Boolean)) &&
			AppendByte(static_cast<std::uint8_t>(value ? 1 : 0));
	}

	bool Encoder::Number(double value)
	{
		if (!std::isfinite(value))
		{
			SetError(error_, "numbers must be finite");
			return false;
		}
		return AppendByte(static_cast<std::uint8_t>(ValueTag::Number)) && AppendDouble(value);
	}

	bool Encoder::String(const char* value, std::size_t size)
	{
		return AppendByte(static_cast<std::uint8_t>(ValueTag::String)) && AppendString(value, size);
	}

	bool Encoder::TableTag()
	{
		return AppendByte(static_cast<std::uint8_t>(ValueTag::Table));
	}

	bool Encoder::CheckDepth(int depth)
	{
		if (depth > kMaxDepth)
		{
			SetError(error_, "table nesting exceeds the EXU storage depth limit");
			return false;
		}
		return true;
	}

	bool Encoder::EnterTable(const void* identity)
	{
		if (activeTables_.find(identity) != activeTables_.end())
		{
			SetError(error_, "cyclic tables cannot be persisted");
			return false;
		}
		activeTables_.insert(identity);
		return true;
	}

	bool Encoder::CountEntry()
	{
		++tableEntries_;
		if (tableEntries_ > kMaxTableEntries)
		{
			SetError(error_, "table entry count exceeds the EXU storage limit");
			return false;
		}
		return true;
	}

	bool Encoder::NumberKey(double key)
	{
		if (!std::isfinite(key))
		{
			SetError(error_, "table numeric keys must be finite");
			return false;
		}
		return AppendByte(static_cast<std::uint8_t>(KeyTag::Number)) && AppendDouble(key);
	}

	bool Encoder::StringKey(const char* key, std::size_t size)
	{
		return AppendByte(static_cast<std::uint8_t>(KeyTag::String)) && AppendString(key, size);
	}

	bool Encoder::EndTable(const void* identity)
	{
		const bool ok = AppendByte(static_cast<std::uint8_t>(KeyTag::End));
		activeTables_.erase(identity);
		return ok;
	}

	void Encoder::AbandonTable(const void* identity)
	{
		activeTables_.erase(identity);
	}

	bool Encoder::UnsupportedValue(const char* typeName)
	{
		SetError(error_, std::string("unsupported Lua value type: ") + (typeName != nullptr ? typeName : "?"));
		return false;
	}

	bool Encoder::UnsupportedKey()
	{
		SetError(error_, "table keys must be strings or numbers");
		return false;
	}

	bool WrapPayload(const std::vector<std::uint8_t>& payload, std::uint32_t schemaVersion,
		std::vector<std::uint8_t>& fileBytes, std::string& error)
	{
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

	bool DecodeFile(const std::vector<std::uint8_t>& bytes, DecodeSink& sink,
		DecodeMeta& meta, std::string& error)
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
		if (!DecodeValue(sink, cursor, 0, error) || cursor.offset != cursor.size)
		{
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
}
