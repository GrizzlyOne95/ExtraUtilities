/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/PilotAnimationProfile.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace
{
	using namespace ExtraUtilities::Lua::PilotAnimationPolicy;

	Support Everything()
	{
		Support support{};
		support.substituteSlots = 0x3F;
		support.completionModes = static_cast<std::uint8_t>(
			CompletionBit(CompletionMode::Animation) |
			CompletionBit(CompletionMode::Duration) |
			CompletionBit(CompletionMode::Manual));
		return support;
	}

	bool Contains(const char* text, const char* part)
	{
		return std::strstr(text, part) != nullptr;
	}

	// One slot with the given fields; returns the builder for inspection.
	struct Field
	{
		const char* key;
		Value value;
	};

	template <std::size_t N>
	ProfileBuilder BuildOne(const Support& support, const char* slot, const Field (&fields)[N], Policy& out, bool& ok)
	{
		ProfileBuilder builder(support);
		ok = builder.BeginSlot(slot, ValueType::Table);
		for (const Field& field : fields)
		{
			ok = builder.SetField(field.key, field.value) && ok;
		}
		ok = builder.EndSlot() && ok;
		ok = builder.Finish(out) && ok;
		return builder;
	}

	void ExpectError(const ProfileBuilder& builder, bool ok, const char* part, const std::string& what)
	{
		HostTest::Expect(!ok, what + ": rejected");
		HostTest::Expect(Contains(builder.Error(), part),
			what + ": error mentions '" + part + "' (got '" + builder.Error() + "')");
	}
}

int main()
{
	// ---- Empty profile is stock ----------------------------------------------
	{
		ProfileBuilder builder(kBuildSupport);
		Policy out{};
		out.slots[0].mode = Mode::Substitute;
		HostTest::Expect(builder.Finish(out), "empty profile is valid");
		HostTest::Expect(IsStockOnly(out), "empty profile is stock");
		HostTest::Expect(builder.Ok() && builder.Error()[0] == '\0', "no error on success");
	}

	// ---- Explicit stock is valid on every build ------------------------------
	{
		ProfileBuilder builder(kBuildSupport);
		bool ok = true;
		for (std::size_t i = 0; i < kSlotCount; ++i)
		{
			ok = builder.BeginSlot(SlotName(static_cast<Slot>(i)), ValueType::Table) && ok;
			ok = builder.SetField("mode", Value::String("stock")) && ok;
			if (SlotHasCompletion(static_cast<Slot>(i)))
			{
				ok = builder.SetField("completion", Value::String("stock")) && ok;
			}
			ok = builder.EndSlot() && ok;
		}
		Policy out{};
		ok = builder.Finish(out) && ok;
		HostTest::Expect(ok && IsStockOnly(out), "every slot explicitly stock is valid");
	}

	// ---- Owner decision: current slot names, unknown keys hard-error ----------
	{
		ProfileBuilder builder(kBuildSupport);
		const bool ok = builder.BeginSlot("standing", ValueType::Table);
		ExpectError(builder, ok, "unknown pilot animation slot 'standing'", "state name is not a slot key");
		HostTest::Expect(Contains(builder.Error(), "stand, enterCrouch, crouched, exitCrouch, jump, land"),
			"slot error lists the valid keys");
	}
	for (const char* wrong : { "Stand", "enteringCrouch", "exitingCrouch", "landing", "jumping", "" })
	{
		ProfileBuilder builder(kBuildSupport);
		HostTest::Expect(!builder.BeginSlot(wrong, ValueType::Table),
			std::string("'") + wrong + "' is not a slot key");
	}
	{
		ProfileBuilder builder(kBuildSupport);
		const bool ok = builder.BeginSlot(nullptr, 0, ValueType::Table);
		ExpectError(builder, ok, "profile keys must be slot names", "non-string top-level key");
	}
	{
		ProfileBuilder builder(kBuildSupport);
		const char embedded[] = { 's', 't', 'a', 'n', 'd', '\0', 'x' };
		const bool ok = builder.BeginSlot(embedded, sizeof(embedded), ValueType::Table);
		ExpectError(builder, ok, "profile keys must be slot names", "key with embedded NUL");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "speed", Value::Number(2.0) } };
		const ProfileBuilder builder = BuildOne(kBuildSupport, "jump", fields, out, ok);
		ExpectError(builder, ok, "unknown field 'speed'", "unknown field is an error");
		HostTest::Expect(Contains(builder.Error(), "slot jump"), "field error names the slot");
	}
	{
		ProfileBuilder builder(kBuildSupport);
		bool ok = builder.BeginSlot("land", ValueType::Table);
		ok = builder.SetField(nullptr, 0, Value::String("stock")) && ok;
		ExpectError(builder, ok, "field names must be strings", "non-string field key");
	}

	// ---- Types -----------------------------------------------------------------
	{
		ProfileBuilder builder(kBuildSupport);
		const bool ok = builder.BeginSlot("crouched", ValueType::String);
		ExpectError(builder, ok, "must be a table, got string", "slot value must be a table");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "mode", Value::Of(ValueType::Boolean) } };
		const ProfileBuilder builder = BuildOne(kBuildSupport, "stand", fields, out, ok);
		ExpectError(builder, ok, "mode must be a string, got boolean", "mode type");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "mode", Value::String("replace") } };
		const ProfileBuilder builder = BuildOne(kBuildSupport, "stand", fields, out, ok);
		ExpectError(builder, ok, "unknown mode 'replace'", "unknown mode value");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "completion", Value::String("instant") } };
		const ProfileBuilder builder = BuildOne(kBuildSupport, "exitCrouch", fields, out, ok);
		ExpectError(builder, ok, "unknown completion 'instant'", "unknown completion value");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "completion", Value::String("manual") } };
		const ProfileBuilder builder = BuildOne(Everything(), "crouched", fields, out, ok);
		ExpectError(builder, ok, "only valid for enterCrouch and exitCrouch",
			"completion on a non-transition slot, even with full support");
	}

	// ---- Cross-field rules -----------------------------------------------------
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "mode", Value::String("substitute") } };
		const ProfileBuilder builder = BuildOne(Everything(), "enterCrouch", fields, out, ok);
		ExpectError(builder, ok, "requires animation", "substitute without animation");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "animation", Value::String("myKneel") } };
		const ProfileBuilder builder = BuildOne(Everything(), "enterCrouch", fields, out, ok);
		ExpectError(builder, ok, "only allowed with mode \"substitute\"", "animation without substitute");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "completion", Value::String("duration") } };
		const ProfileBuilder builder = BuildOne(Everything(), "exitCrouch", fields, out, ok);
		ExpectError(builder, ok, "requires duration", "duration completion without duration");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "duration", Value::Number(0.5) } };
		const ProfileBuilder builder = BuildOne(Everything(), "exitCrouch", fields, out, ok);
		ExpectError(builder, ok, "only allowed with completion \"duration\"", "duration without completion");
	}
	for (const double bad : { 0.0, -1.0, 61.0,
		std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = {
			{ "completion", Value::String("duration") },
			{ "duration", Value::Number(bad) },
		};
		const ProfileBuilder builder = BuildOne(Everything(), "enterCrouch", fields, out, ok);
		ExpectError(builder, ok, "finite number of seconds", "out-of-range duration");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = {
			{ "completion", Value::String("duration") },
			{ "duration", Value::String("0.5") },
		};
		const ProfileBuilder builder = BuildOne(Everything(), "enterCrouch", fields, out, ok);
		ExpectError(builder, ok, "duration must be a number, got string", "duration type");
	}
	{
		const std::string tooLong(kMaxAnimationName + 1, 'a');
		Policy out{};
		bool ok = false;
		const Field fields[] = {
			{ "mode", Value::String("substitute") },
			{ "animation", Value::String(tooLong.c_str()) },
		};
		const ProfileBuilder builder = BuildOne(Everything(), "jump", fields, out, ok);
		ExpectError(builder, ok, "animation must be 1-63 characters", "animation name too long");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = {
			{ "mode", Value::String("substitute") },
			{ "animation", Value::String("") },
		};
		const ProfileBuilder builder = BuildOne(Everything(), "jump", fields, out, ok);
		ExpectError(builder, ok, "animation must be 1-63 characters", "empty animation name");
	}

	// ---- A build without override support refuses them --------------------------
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = {
			{ "mode", Value::String("substitute") },
			{ "animation", Value::String("myKneel") },
		};
		const ProfileBuilder builder = BuildOne(Support{}, "enterCrouch", fields, out, ok);
		ExpectError(builder, ok, "not supported by this EXU build", "substitute unsupported without support");
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "completion", Value::String("manual") } };
		const ProfileBuilder builder = BuildOne(Support{}, "exitCrouch", fields, out, ok);
		ExpectError(builder, ok, "completion \"manual\" is not supported by this EXU build",
			"completion unsupported without support");
	}

	// ---- This build accepts every implemented override ---------------------------
	{
		ProfileBuilder builder(kBuildSupport);
		bool ok = true;
		const char* const slots[] = { "stand", "enterCrouch", "crouched", "exitCrouch", "jump", "land" };
		for (const char* slot : slots)
		{
			ok = builder.BeginSlot(slot, ValueType::Table) && ok;
			ok = builder.SetField("mode", Value::String("substitute")) && ok;
			ok = builder.SetField("animation", Value::String("alt")) && ok;
			ok = builder.EndSlot() && ok;
		}
		Policy out{};
		ok = builder.Finish(out) && ok;
		HostTest::Expect(ok, std::string("every slot can substitute in this build: ") + builder.Error());
	}
	for (const char* completion : { "animation", "manual" })
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = { { "completion", Value::String(completion) } };
		const ProfileBuilder builder = BuildOne(kBuildSupport, "enterCrouch", fields, out, ok);
		HostTest::Expect(ok, std::string("completion ") + completion + " is accepted: " + builder.Error());
	}
	{
		Policy out{};
		bool ok = false;
		const Field fields[] = {
			{ "completion", Value::String("duration") },
			{ "duration", Value::Number(1.0) },
		};
		const ProfileBuilder builder = BuildOne(kBuildSupport, "exitCrouch", fields, out, ok);
		HostTest::Expect(ok && out.At(Slot::ExitCrouch).duration == 1.0f,
			std::string("duration completion is accepted: ") + builder.Error());
	}

	// ---- With support: valid profiles build the expected policy ---------------
	{
		ProfileBuilder builder(Everything());
		bool ok = builder.BeginSlot("enterCrouch", ValueType::Table);
		ok = builder.SetField("mode", Value::String("substitute")) && ok;
		ok = builder.SetField("animation", Value::String("myKneel")) && ok;
		ok = builder.SetField("completion", Value::String("duration")) && ok;
		ok = builder.SetField("duration", Value::Number(0.25)) && ok;
		ok = builder.EndSlot() && ok;
		ok = builder.BeginSlot("exitCrouch", ValueType::Table) && ok;
		ok = builder.SetField("completion", Value::String("manual")) && ok;
		ok = builder.EndSlot() && ok;
		Policy out{};
		ok = builder.Finish(out) && ok;

		HostTest::Expect(ok, std::string("full profile valid: ") + builder.Error());
		const TransitionPolicy& enter = out.At(Slot::EnterCrouch);
		HostTest::Expect(enter.mode == Mode::Substitute && std::strcmp(enter.animation, "myKneel") == 0,
			"substitute animation stored");
		HostTest::Expect(enter.completion == CompletionMode::Duration && enter.duration == 0.25f,
			"duration completion stored");
		HostTest::Expect(out.At(Slot::ExitCrouch).completion == CompletionMode::Manual,
			"manual completion stored");
		HostTest::Expect(out.At(Slot::Stand).mode == Mode::Stock && out.At(Slot::Jump).mode == Mode::Stock,
			"omitted slots are stock");
	}
	{
		const std::string longest(kMaxAnimationName, 'b');
		Policy out{};
		bool ok = false;
		const Field fields[] = {
			{ "mode", Value::String("substitute") },
			{ "animation", Value::String(longest.c_str()) },
		};
		BuildOne(Everything(), "land", fields, out, ok);
		HostTest::Expect(ok && std::strlen(out.At(Slot::Land).animation) == kMaxAnimationName,
			"a maximum-length animation name fits with its terminator");
	}

	// ---- Failure is sticky and keeps the first cause -----------------------------
	{
		ProfileBuilder builder(kBuildSupport);
		HostTest::Expect(!builder.BeginSlot("typo", ValueType::Table), "first error");
		HostTest::Expect(!builder.BeginSlot("stand", ValueType::Table), "later calls fail after an error");
		Policy out{};
		out.slots[0].mode = Mode::Substitute;
		HostTest::Expect(!builder.Finish(out), "Finish fails after an error");
		HostTest::Expect(out.slots[0].mode == Mode::Substitute, "a failed Finish leaves the output untouched");
		HostTest::Expect(Contains(builder.Error(), "'typo'"), "the first cause is kept");
	}
	{
		ProfileBuilder builder(kBuildSupport);
		bool ok = builder.BeginSlot("stand", ValueType::Table);
		ok = builder.EndSlot() && ok;
		ok = builder.BeginSlot("stand", ValueType::Table) && ok;
		ExpectError(builder, ok, "given twice", "a repeated slot is rejected");
	}
	{
		ProfileBuilder builder(kBuildSupport);
		bool ok = builder.BeginSlot("stand", ValueType::Table);
		Policy out{};
		ok = builder.Finish(out) && ok;
		ExpectError(builder, ok, "was not ended", "Finish inside a slot is a caller bug");
	}

	return HostTest::Finish("pilot animation profile");
}
