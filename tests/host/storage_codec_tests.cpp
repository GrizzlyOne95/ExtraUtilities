/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

// Host-side checks for the exu.storage file format. A career or campaign save
// lives in these files, so the rules that matter are the ones that keep a bad
// file from being read as a good one (checksum, length, strict tags, trailing
// bytes) and the ones that refuse to write something a later process could not
// read back (non-finite numbers, cycles, depth, size).
//
// The runners compile each tests/host program on its own, so the codec source
// is included here directly; it is the same file exu.dll builds.

#include "Util/StorageCodec.cpp"
#include "HostTest.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace ExtraUtilities::StorageCodec;
using HostTest::Expect;

namespace
{
	using Bytes = std::vector<std::uint8_t>;

	// A test-side stand-in for a Lua value.
	struct Entry;

	struct Value
	{
		enum class Kind { Nil, Boolean, Number, String, Table, Function };

		Kind kind = Kind::Nil;
		bool boolean = false;
		double number = 0.0;
		std::string string;
		std::vector<Entry> entries;
		// Lets a test make a table contain itself, as Lua can.
		const Value* alias = nullptr;
	};

	struct Entry
	{
		Value key;
		Value value;
	};

	Value Nil() { return Value{}; }

	Value Bool(bool b)
	{
		Value v;
		v.kind = Value::Kind::Boolean;
		v.boolean = b;
		return v;
	}

	Value Num(double n)
	{
		Value v;
		v.kind = Value::Kind::Number;
		v.number = n;
		return v;
	}

	Value Str(const std::string& s)
	{
		Value v;
		v.kind = Value::Kind::String;
		v.string = s;
		return v;
	}

	Value Table(std::vector<Entry> entries = {})
	{
		Value v;
		v.kind = Value::Kind::Table;
		v.entries = std::move(entries);
		return v;
	}

	Value Function()
	{
		Value v;
		v.kind = Value::Kind::Function;
		return v;
	}

	// Mirrors the Lua walk in StorageApi.cpp: the same encoder calls in the
	// same order, with a table's identity being its address.
	bool EncodeValue(const Value& value, Encoder& encoder, int depth);

	bool EncodeTable(const Value& table, Encoder& encoder, int depth)
	{
		const Value& target = table.alias != nullptr ? *table.alias : table;
		if (!encoder.CheckDepth(depth))
		{
			return false;
		}
		if (!encoder.EnterTable(&target))
		{
			return false;
		}
		for (const Entry& entry : target.entries)
		{
			if (!encoder.CountEntry())
			{
				encoder.AbandonTable(&target);
				return false;
			}
			bool keyOk = false;
			if (entry.key.kind == Value::Kind::Number)
			{
				keyOk = encoder.NumberKey(entry.key.number);
			}
			else if (entry.key.kind == Value::Kind::String)
			{
				keyOk = encoder.StringKey(entry.key.string.data(), entry.key.string.size());
			}
			else
			{
				keyOk = encoder.UnsupportedKey();
			}
			if (!keyOk || !EncodeValue(entry.value, encoder, depth))
			{
				encoder.AbandonTable(&target);
				return false;
			}
		}
		return encoder.EndTable(&target);
	}

	bool EncodeValue(const Value& value, Encoder& encoder, int depth)
	{
		switch (value.kind)
		{
		case Value::Kind::Nil:
			return encoder.Nil();
		case Value::Kind::Boolean:
			return encoder.Boolean(value.boolean);
		case Value::Kind::Number:
			return encoder.Number(value.number);
		case Value::Kind::String:
			return encoder.String(value.string.data(), value.string.size());
		case Value::Kind::Table:
			return encoder.TableTag() && EncodeTable(value, encoder, depth + 1);
		case Value::Kind::Function:
		default:
			return encoder.UnsupportedValue("function");
		}
	}

	bool Encode(const Value& value, Bytes& payload, std::string& error)
	{
		payload.clear();
		Encoder encoder(payload, error);
		return EncodeValue(value, encoder, 0);
	}

	bool EncodeFile(const Value& value, std::uint32_t schema, Bytes& file, std::string& error)
	{
		Bytes payload;
		return Encode(value, payload, error) && WrapPayload(payload, schema, file, error);
	}

	// Rebuilds the value the way the Lua sink would push it.
	class TreeSink final : public DecodeSink
	{
	public:
		bool refuseTables = false;
		std::vector<Value> stack;

		void Nil() override { stack.push_back(::Nil()); }
		void Boolean(bool value) override { stack.push_back(Bool(value)); }
		void Number(double value) override { stack.push_back(Num(value)); }
		void String(const char* value, std::size_t size) override { stack.push_back(Str(std::string(value, size))); }

		bool BeginTable(std::string& error) override
		{
			if (refuseTables)
			{
				SetError(error, "sink refused the table");
				return false;
			}
			stack.push_back(Table());
			return true;
		}

		void SetEntry() override
		{
			Entry entry;
			entry.value = std::move(stack.back());
			stack.pop_back();
			entry.key = std::move(stack.back());
			stack.pop_back();
			stack.back().entries.push_back(std::move(entry));
		}

		void EndTable() override {}
	};

	bool Equal(const Value& a, const Value& b)
	{
		if (a.kind != b.kind)
		{
			return false;
		}
		switch (a.kind)
		{
		case Value::Kind::Boolean:
			return a.boolean == b.boolean;
		case Value::Kind::Number:
			return std::memcmp(&a.number, &b.number, sizeof(double)) == 0;
		case Value::Kind::String:
			return a.string == b.string;
		case Value::Kind::Table:
			if (a.entries.size() != b.entries.size())
			{
				return false;
			}
			for (std::size_t i = 0; i < a.entries.size(); ++i)
			{
				if (!Equal(a.entries[i].key, b.entries[i].key) || !Equal(a.entries[i].value, b.entries[i].value))
				{
					return false;
				}
			}
			return true;
		default:
			return true;
		}
	}

	// Decodes and returns the error text ("" on success).
	std::string DecodeError(const Bytes& file)
	{
		TreeSink sink;
		DecodeMeta meta{};
		std::string error;
		const bool ok = DecodeFile(file, sink, meta, error);
		if (ok)
		{
			return error.empty() ? std::string() : "success with an error set: " + error;
		}
		return error.empty() ? std::string("failed with no error") : error;
	}

	// A file whose header is consistent with an arbitrary payload, so only the
	// value stream is under test.
	Bytes Wrap(const Bytes& payload)
	{
		Bytes file;
		std::string error;
		WrapPayload(payload, 1, file, error);
		return file;
	}

	void AppendDouble(Bytes& out, double value)
	{
		std::uint8_t raw[sizeof(double)];
		std::memcpy(raw, &value, sizeof(double));
		out.insert(out.end(), raw, raw + sizeof(double));
	}

	void AppendU32(Bytes& out, std::uint32_t value)
	{
		std::uint8_t raw[sizeof(value)];
		std::memcpy(raw, &value, sizeof(value));
		out.insert(out.end(), raw, raw + sizeof(value));
	}

	Value Nested(int levels)
	{
		Value inner = Table();
		for (int i = 1; i < levels; ++i)
		{
			inner = Table({ Entry{ Str("child"), std::move(inner) } });
		}
		return inner;
	}

	void TestCrc32()
	{
		const char* check = "123456789";
		Expect(Crc32(reinterpret_cast<const std::uint8_t*>(check), 9) == 0xCBF43926u,
			"CRC-32 matches the IEEE check value");
		Expect(Crc32(nullptr, 0) == 0u, "CRC-32 of nothing is 0");
	}

	// Pins the on-disk bytes: a change here breaks every existing save.
	void TestGoldenFile()
	{
		const Value value = Table({
			Entry{ Str("a"), Bool(true) },
			Entry{ Num(1.0), Num(2.5) },
		});
		const Bytes expected = {
			0x45, 0x58, 0x55, 0x44, 0x41, 0x54, 0x41, 0x31, // EXUDATA1
			0x01, 0x00, 0x00, 0x00,                         // format 1
			0x07, 0x00, 0x00, 0x00,                         // schema 7
			0x1C, 0x00, 0x00, 0x00,                         // 28 payload bytes
			0xE6, 0x98, 0x9E, 0xC8,                         // CRC-32
			0x04,                                           // table
			0x02, 0x01, 0x00, 0x00, 0x00, 0x61,             // key "a"
			0x01, 0x01,                                     // true
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F, // key 1.0
			0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x40, // 2.5
			0x00,                                           // end
		};

		Bytes file;
		std::string error;
		Expect(EncodeFile(value, 7, file, error), "the golden value encodes: " + error);
		Expect(file == expected, "the golden value encodes to the pinned bytes");

		TreeSink sink;
		DecodeMeta meta{};
		error.clear();
		Expect(DecodeFile(expected, sink, meta, error), "the pinned bytes decode: " + error);
		Expect(sink.stack.size() == 1 && Equal(sink.stack.front(), value), "the pinned bytes decode to the golden value");
		Expect(meta.schemaVersion == 7, "the schema version is read back");
		Expect(meta.sizeBytes == expected.size(), "the file size is reported");
	}

	void TestRoundTrip()
	{
		const Value value = Table({
			Entry{ Str("name"), Str("campaign") },
			Entry{ Str(""), Str(std::string("nul\0inside", 10)) },
			Entry{ Str("nothing"), Nil() },
			Entry{ Str("off"), Bool(false) },
			Entry{ Num(-0.0), Num(-1.5e300) },
			Entry{ Num(3.0), Num(std::numeric_limits<double>::denorm_min()) },
			Entry{ Str("stats"), Table({
				Entry{ Str("kills"), Num(42.0) },
				Entry{ Str("unlocks"), Table({ Entry{ Num(1.0), Str("tank") } }) },
				Entry{ Str("empty"), Table() },
			}) },
		});

		Bytes file;
		std::string error;
		Expect(EncodeFile(value, 3, file, error), "a mixed value encodes: " + error);

		TreeSink sink;
		DecodeMeta meta{};
		error.clear();
		Expect(DecodeFile(file, sink, meta, error), "a mixed value decodes: " + error);
		Expect(sink.stack.size() == 1 && Equal(sink.stack.front(), value), "a mixed value round-trips exactly");
		Expect(meta.schemaVersion == 3, "the round-trip keeps the schema version");

		for (const Value& scalar : { Nil(), Bool(true), Num(0.25), Str("top"), Table() })
		{
			Bytes scalarFile;
			std::string scalarError;
			TreeSink scalarSink;
			DecodeMeta scalarMeta{};
			const bool ok = EncodeFile(scalar, 1, scalarFile, scalarError) &&
				DecodeFile(scalarFile, scalarSink, scalarMeta, scalarError);
			Expect(ok && scalarSink.stack.size() == 1 && Equal(scalarSink.stack.front(), scalar),
				"a top-level value of any supported type round-trips: " + scalarError);
		}
	}

	void TestHeaderRejections()
	{
		Bytes good;
		std::string error;
		EncodeFile(Table({ Entry{ Str("k"), Num(1.0) } }), 1, good, error);

		Expect(DecodeError(Bytes(good.begin(), good.begin() + 23)) == "persistent data header is truncated",
			"a file shorter than the header is rejected");
		Expect(DecodeError(Bytes()) == "persistent data header is truncated", "an empty file is rejected");

		Bytes badMagic = good;
		badMagic[7] = '2';
		Expect(DecodeError(badMagic) == "persistent data magic is invalid", "a wrong magic is rejected");

		Bytes badVersion = good;
		badVersion[8] = 2;
		Expect(DecodeError(badVersion) == "unsupported EXU persistent data format version",
			"a newer format version is rejected");
		badVersion[8] = 0;
		Expect(DecodeError(badVersion) == "unsupported EXU persistent data format version",
			"format version 0 is rejected");

		Bytes longer = good;
		longer.push_back(0);
		Expect(DecodeError(longer) == "persistent payload length does not match the file",
			"bytes past the declared payload are rejected");

		Bytes shorter = good;
		shorter.pop_back();
		Expect(DecodeError(shorter) == "persistent payload length does not match the file",
			"a payload shorter than declared is rejected");

		Bytes oversizeLength = good;
		const std::uint32_t huge = 0xFFFFFFF0u;
		std::memcpy(oversizeLength.data() + 16, &huge, sizeof(huge));
		Expect(DecodeError(oversizeLength) == "persistent payload length does not match the file",
			"an oversized declared length is rejected");

		Bytes flippedPayload = good;
		flippedPayload.back() ^= 0x01;
		Expect(DecodeError(flippedPayload) == "persistent data checksum failed", "a damaged payload fails the checksum");

		Bytes flippedCrc = good;
		flippedCrc[20] ^= 0x80;
		Expect(DecodeError(flippedCrc) == "persistent data checksum failed", "a damaged checksum fails");

		Expect(DecodeError(Wrap(Bytes())) == "persistent data is truncated", "an empty payload is truncated");
	}

	void TestStreamRejections()
	{
		Bytes payload;
		std::string error;
		Encode(Table({ Entry{ Str("key"), Str("value") } }), payload, error);

		// Every proper prefix of a valid stream is truncated, never accepted.
		bool allRejected = true;
		for (std::size_t length = 1; length < payload.size(); ++length)
		{
			const std::string prefixError = DecodeError(Wrap(Bytes(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(length))));
			if (prefixError != "persistent data is truncated" && prefixError != "persistent string length is invalid")
			{
				allRejected = false;
				HostTest::Expect(false, "prefix of length " + std::to_string(length) + " gave: " + prefixError);
			}
		}
		Expect(allRejected, "every truncated value stream is rejected");

		Bytes trailing = payload;
		trailing.push_back(0x00);
		Expect(DecodeError(Wrap(trailing)) == "persistent data contains trailing bytes", "trailing bytes are rejected");

		Expect(DecodeError(Wrap({ 0x05 })) == "persistent value tag is invalid", "an unknown value tag is rejected");
		Expect(DecodeError(Wrap({ 0x04, 0x03 })) == "persistent table key tag is invalid", "an unknown key tag is rejected");
		Expect(DecodeError(Wrap({ 0x01, 0x02 })) == "persistent boolean value is invalid", "a boolean other than 0/1 is rejected");
		Expect(DecodeError(Wrap({ 0x01 })) == "persistent data is truncated", "a boolean with no byte is truncated");

		Bytes nan = { 0x02 };
		AppendDouble(nan, std::numeric_limits<double>::quiet_NaN());
		Expect(DecodeError(Wrap(nan)) == "persistent numeric value is invalid", "a stored NaN is rejected");

		Bytes inf = { 0x02 };
		AppendDouble(inf, std::numeric_limits<double>::infinity());
		Expect(DecodeError(Wrap(inf)) == "persistent numeric value is invalid", "a stored infinity is rejected");

		Bytes infKey = { 0x04, 0x01 };
		AppendDouble(infKey, -std::numeric_limits<double>::infinity());
		infKey.push_back(0x00);
		infKey.push_back(0x00);
		Expect(DecodeError(Wrap(infKey)) == "persistent numeric table key is invalid", "a stored infinite key is rejected");

		Bytes longString = { 0x03 };
		AppendU32(longString, static_cast<std::uint32_t>(kMaxStringBytes + 1));
		longString.resize(longString.size() + kMaxStringBytes + 1, 'x');
		Expect(DecodeError(Wrap(longString)) == "persistent string length is invalid",
			"a stored string over the size limit is rejected even when the bytes are present");

		Bytes pastEnd = { 0x03 };
		AppendU32(pastEnd, 4);
		pastEnd.push_back('a');
		Expect(DecodeError(Wrap(pastEnd)) == "persistent string length is invalid",
			"a string length past the end of the payload is rejected");
	}

	void TestDecodeLimits()
	{
		Bytes deepest;
		std::string error;
		Expect(Encode(Nested(kMaxDepth), deepest, error), "the deepest allowed nesting encodes: " + error);
		Expect(DecodeError(Wrap(deepest)).empty(), "the deepest allowed nesting decodes");

		// Written by hand: the encoder refuses to produce it.
		Bytes tooDeep;
		for (int i = 0; i < kMaxDepth + 1; ++i)
		{
			tooDeep.push_back(0x04);
			if (i < kMaxDepth)
			{
				tooDeep.push_back(0x01);
				AppendDouble(tooDeep, 1.0);
			}
		}
		for (int i = 0; i < kMaxDepth + 1; ++i)
		{
			tooDeep.push_back(0x00);
		}
		Expect(DecodeError(Wrap(tooDeep)) == "persistent table nesting exceeds the supported depth",
			"a stored table nested past the limit is rejected");

		Bytes manyEntries = { 0x04 };
		for (std::size_t i = 0; i <= kMaxTableEntries; ++i)
		{
			manyEntries.push_back(0x01);
			AppendDouble(manyEntries, static_cast<double>(i));
			manyEntries.push_back(0x00);
		}
		manyEntries.push_back(0x00);
		Expect(DecodeError(Wrap(manyEntries)) == "persistent table entry count exceeds the supported limit",
			"a stored table with too many entries is rejected");

		TreeSink refusing;
		refusing.refuseTables = true;
		DecodeMeta meta{};
		error.clear();
		Expect(!DecodeFile(Wrap({ 0x04, 0x00 }), refusing, meta, error) && error == "sink refused the table",
			"a sink that cannot take a table stops the decode with its own error");
	}

	void TestEncoderRejections()
	{
		Bytes payload;
		std::string error;

		Expect(!Encode(Num(std::numeric_limits<double>::quiet_NaN()), payload, error) && error == "numbers must be finite",
			"NaN is not written");
		error.clear();
		Expect(!Encode(Table({ Entry{ Str("x"), Num(-std::numeric_limits<double>::infinity()) } }), payload, error) &&
			error == "numbers must be finite", "a nested infinity is not written");
		error.clear();
		Expect(!Encode(Table({ Entry{ Num(std::numeric_limits<double>::infinity()), Bool(true) } }), payload, error) &&
			error == "table numeric keys must be finite", "an infinite key is not written");
		error.clear();
		Expect(!Encode(Table({ Entry{ Str("f"), Function() } }), payload, error) &&
			error == "unsupported Lua value type: function", "an unsupported value type is not written");
		error.clear();
		Expect(!Encode(Table({ Entry{ Bool(true), Num(1.0) } }), payload, error) &&
			error == "table keys must be strings or numbers", "a boolean key is not written");

		error.clear();
		Expect(!Encode(Nested(kMaxDepth + 1), payload, error) && error == "table nesting exceeds the EXU storage depth limit",
			"a table nested past the limit is not written");

		std::vector<Entry> entries;
		entries.reserve(kMaxTableEntries + 1);
		for (std::size_t i = 0; i <= kMaxTableEntries; ++i)
		{
			entries.push_back(Entry{ Num(static_cast<double>(i)), Bool(true) });
		}
		error.clear();
		Expect(!Encode(Table(std::move(entries)), payload, error) && error == "table entry count exceeds the EXU storage limit",
			"a table with too many entries is not written");
	}

	void TestCycles()
	{
		// t.self = t
		Value table = Table();
		Value self = Table();
		self.alias = &table;
		table.entries.push_back(Entry{ Str("self"), self });

		Bytes payload;
		std::string error;
		Expect(!Encode(table, payload, error) && error == "cyclic tables cannot be persisted", "a self-referencing table is not written");

		// a.b.a = a, one level removed.
		Value outer = Table();
		Value back = Table();
		back.alias = &outer;
		outer.entries.push_back(Entry{ Str("inner"), Table({ Entry{ Str("back"), back } }) });
		error.clear();
		Expect(!Encode(outer, payload, error) && error == "cyclic tables cannot be persisted", "an indirect cycle is not written");

		// The same table twice side by side is shared, not cyclic: it is
		// written twice.
		const Value shared = Table({ Entry{ Str("v"), Num(1.0) } });
		Value first = Table();
		first.alias = &shared;
		Value second = Table();
		second.alias = &shared;
		const Value twice = Table({ Entry{ Str("a"), first }, Entry{ Str("b"), second } });
		error.clear();
		Expect(Encode(twice, payload, error), "a table referenced twice without a cycle is written: " + error);
	}

	void TestSizeLimits()
	{
		Bytes payload;
		std::string error;
		const std::string maxString(kMaxStringBytes, 's');
		Expect(Encode(Str(maxString), payload, error), "a string at the size limit is written: " + error);
		Expect(DecodeError(Wrap(payload)).empty(), "a string at the size limit is read");

		error.clear();
		Expect(!Encode(Str(maxString + "s"), payload, error) && error == "string exceeds the EXU storage string-size limit",
			"a string over the size limit is not written");
		error.clear();
		Expect(!Encode(Table({ Entry{ Str(maxString + "s"), Nil() } }), payload, error) &&
			error == "string exceeds the EXU storage string-size limit", "a key over the size limit is not written");

		std::vector<Entry> entries;
		for (int i = 0; i < 17; ++i)
		{
			entries.push_back(Entry{ Num(static_cast<double>(i)), Str(maxString) });
		}
		error.clear();
		Expect(!Encode(Table(std::move(entries)), payload, error) && error == "serialized data exceeds the EXU storage size limit",
			"a value over the file size limit is not written");
		Expect(payload.size() <= kMaxFileBytes, "the encoder never grows past the file size limit");

		Expect(IsFileSizeAllowed(kMaxFileBytes), "a file at the size limit may be read");
		Expect(!IsFileSizeAllowed(static_cast<std::uint64_t>(kMaxFileBytes) + 1), "a file over the size limit is not read");
	}

	void TestSetErrorKeepsFirst()
	{
		std::string error;
		SetError(error, "first");
		SetError(error, "second");
		Expect(error == "first", "the first error is kept");
	}

	void TestNamespaces()
	{
		Expect(IsSafeNamespace("campaign_reimagined"), "a plain name is allowed");
		Expect(IsSafeNamespace("a"), "a one-character name is allowed");
		Expect(IsSafeNamespace("Mod-1.2_x"), "letters, digits, '-', '.', '_' are allowed");
		Expect(IsSafeNamespace(std::string(64, 'n')), "64 characters are allowed");
		Expect(!IsSafeNamespace(std::string(65, 'n')), "65 characters are refused");
		Expect(!IsSafeNamespace(""), "an empty name is refused");
		for (const char* bad : { "_a", ".a", "-a", "..", "a/b", "a\\b", "a:b", "a b", "../x", "C:x" })
		{
			Expect(!IsSafeNamespace(bad), std::string("refused: ") + bad);
		}
		Expect(!IsSafeNamespace(std::string("a\0b", 3)), "an embedded NUL is refused");
		Expect(!IsSafeNamespace("caf\xC3\xA9"), "non-ASCII is refused");
	}

	void TestLoadSelection()
	{
		using RS = ReadStatus;
		using LS = LoadSource;

		Expect(SelectLoadSource(RS::Ok, true, RS::NotFound, false) == LS::Primary, "a good primary wins");
		Expect(SelectLoadSource(RS::Ok, true, RS::Ok, true) == LS::Primary, "a good primary wins over a good backup");
		Expect(SelectLoadSource(RS::Ok, false, RS::Ok, true) == LS::Backup, "a corrupt primary falls back to the backup");
		Expect(SelectLoadSource(RS::Error, false, RS::Ok, true) == LS::Backup, "an unreadable primary falls back to the backup");
		Expect(SelectLoadSource(RS::NotFound, false, RS::Ok, true) == LS::Backup, "a missing primary falls back to the backup");
		Expect(SelectLoadSource(RS::NotFound, false, RS::NotFound, false) == LS::New, "no files is a new namespace");
		Expect(SelectLoadSource(RS::Ok, false, RS::NotFound, false) == LS::Error,
			"a corrupt primary with no backup is an error, never an empty table");
		Expect(SelectLoadSource(RS::Ok, false, RS::Ok, false) == LS::Error, "two corrupt copies are an error");
		Expect(SelectLoadSource(RS::NotFound, false, RS::Ok, false) == LS::Error, "a lone corrupt backup is an error");
		Expect(SelectLoadSource(RS::NotFound, false, RS::Error, false) == LS::Error, "an unreadable backup is an error");

		Expect(CombineLoadErrors("p", "b") == "p; backup: b", "both errors are reported");
		Expect(CombineLoadErrors("p", "") == "p", "a primary error alone is reported");
		Expect(CombineLoadErrors("", "b") == "b", "a backup error alone is reported");
		Expect(CombineLoadErrors("", "") == "persistent data could not be loaded", "no error text still explains the failure");
	}
}

int main()
{
	TestCrc32();
	TestGoldenFile();
	TestRoundTrip();
	TestHeaderRejections();
	TestStreamRejections();
	TestDecodeLimits();
	TestEncoderRejections();
	TestCycles();
	TestSizeLimits();
	TestSetErrorKeepsFirst();
	TestNamespaces();
	TestLoadSelection();
	return HostTest::Finish("storage-codec");
}
