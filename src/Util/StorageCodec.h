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
 * StorageCodec.h
 *
 * The exu.storage on-disk format, with no Windows, Lua or engine dependency so
 * tests/host can check it. StorageApi.cpp owns the Lua walk and the file I/O;
 * every rule about what a file may contain lives here.
 *
 * File layout (little-endian, as written by the x86 DLL):
 *
 *   Header (24 bytes)
 *     magic[8]        "EXUDATA1"
 *     formatVersion   uint32, currently 1
 *     schemaVersion   uint32, chosen by the mod
 *     payloadBytes    uint32, must equal file size - 24
 *     payloadCrc32    uint32, CRC-32 (IEEE, reflected) of the payload
 *   Payload: one value
 *     value  := tag:uint8 body
 *       Nil     (0)  -
 *       Boolean (1)  uint8, 0 or 1
 *       Number  (2)  double, finite
 *       String  (3)  length:uint32 bytes[length]
 *       Table   (4)  entry* End
 *     entry  := keyTag:uint8 key value
 *       Number key (1) double, finite
 *       String key (2) length:uint32 bytes[length]
 *     End     := keyTag 0
 *
 * The decoder is strict: any unknown tag, non-finite number, oversized string,
 * excess nesting or entry count, length mismatch, checksum failure or trailing
 * byte rejects the whole file.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace ExtraUtilities::StorageCodec
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

	struct DecodeMeta
	{
		std::uint32_t schemaVersion = 0;
		std::size_t sizeBytes = 0;
	};

	// Result of reading one file (primary or backup) from disk.
	enum class ReadStatus
	{
		Ok,
		NotFound,
		Error,
	};

	// Which copy a Load returns.
	enum class LoadSource
	{
		Primary, // the primary decoded
		Backup,  // the primary failed and the backup decoded
		New,     // neither file exists: an empty table, exists = false
		Error,   // something exists but nothing decoded: nil, never an empty table
	};

	// Keeps the first error: the innermost failure is the useful one.
	void SetError(std::string& error, const std::string& value);

	// Namespaces are file names, never paths: 1-64 characters, alphanumeric
	// first, then only A-Z a-z 0-9 . _ -
	bool IsSafeNamespace(const std::string& name);

	std::uint32_t Crc32(const std::uint8_t* data, std::size_t size);

	// Whether a file of this many bytes may be read at all.
	bool IsFileSizeAllowed(std::uint64_t sizeBytes);

	// The backup is only consulted when the primary did not decode, so the
	// backup arguments are ignored when primaryDecoded is true.
	LoadSource SelectLoadSource(ReadStatus primaryRead, bool primaryDecoded,
		ReadStatus backupRead, bool backupDecoded);

	// The Load error text when both copies failed.
	std::string CombineLoadErrors(const std::string& primaryError, const std::string& backupError);

	// Writes a payload value stream. A caller walks its value tree in pre-order
	// and reports each node; the encoder applies every format rule (finite
	// numbers, string and file size, depth, cycles, entry count). Any false
	// return leaves the reason in the error string and the output unusable.
	class Encoder
	{
	public:
		Encoder(std::vector<std::uint8_t>& out, std::string& error);

		bool Nil();
		bool Boolean(bool value);
		bool Number(double value);
		bool String(const char* value, std::size_t size);

		// A table value is TableTag, then EnterTable, then per entry
		// CountEntry + a key + a value, then EndTable (or AbandonTable on any
		// failure after EnterTable). depth is the table's nesting level: 1 for
		// a table passed directly to Save.
		bool TableTag();
		bool CheckDepth(int depth);
		bool EnterTable(const void* identity);
		bool CountEntry();
		bool NumberKey(double key);
		bool StringKey(const char* key, std::size_t size);
		bool EndTable(const void* identity);
		void AbandonTable(const void* identity);

		// Rejections the caller detects from its own type system.
		bool UnsupportedValue(const char* typeName);
		bool UnsupportedKey();

	private:
		bool AppendByte(std::uint8_t value);
		bool AppendDouble(double value);
		bool AppendString(const char* value, std::size_t size);
		bool AppendBytes(const void* data, std::size_t size);

		std::vector<std::uint8_t>& out_;
		std::string& error_;
		std::unordered_set<const void*> activeTables_;
		std::size_t tableEntries_ = 0;
	};

	// Receives decoded values. Keys and values arrive through the same
	// Number/String calls: for each entry the sink sees a key, then a value
	// (which may be a whole table), then SetEntry.
	class DecodeSink
	{
	public:
		virtual ~DecodeSink() = default;

		virtual void Nil() = 0;
		virtual void Boolean(bool value) = 0;
		virtual void Number(double value) = 0;
		virtual void String(const char* value, std::size_t size) = 0;
		// May refuse (for example when the Lua stack cannot grow); it must
		// then set the error.
		virtual bool BeginTable(std::string& error) = 0;
		virtual void SetEntry() = 0;
		virtual void EndTable() = 0;
	};

	// Prefixes the header to an encoded payload.
	bool WrapPayload(const std::vector<std::uint8_t>& payload, std::uint32_t schemaVersion,
		std::vector<std::uint8_t>& fileBytes, std::string& error);

	// Validates a whole file and streams its value into the sink. On false the
	// sink may have received a partial value, which the caller must discard.
	bool DecodeFile(const std::vector<std::uint8_t>& bytes, DecodeSink& sink,
		DecodeMeta& meta, std::string& error);
}
