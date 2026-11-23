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

// Pure rules behind the "small calls" Lua API (Game/SmallCalls.cpp): sound
// name validation, unit-message names and the [section] argument shuffle of
// the typed settings calls. No Windows, Lua or engine dependency, so
// tests/host/small_calls_tests.cpp can check them.

#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>

namespace ExtraUtilities::SmallCallsLogic
{
	// The engine buffers that hold the stock names ("bail.wav", "jump.wav")
	// are plain C strings; EXU's replacement buffer holds this many bytes
	// including the terminator.
	constexpr std::size_t kSoundNameCapacity = 64;

	// A sound name is a bare file name the engine resolves through its own
	// asset search: 1-63 chars of A-Z a-z 0-9 . _ - with no path separators.
	inline bool IsValidSoundName(const char* name, std::size_t length) noexcept
	{
		if (name == nullptr || length == 0 || length >= kSoundNameCapacity)
		{
			return false;
		}
		for (std::size_t i = 0; i < length; ++i)
		{
			const unsigned char c = static_cast<unsigned char>(name[i]);
			if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-'))
			{
				return false;
			}
		}
		return true;
	}

	// UnitMsg values, from the LF_ENUMERATE records in the BZ 1.5 PDB
	// (Battlezone_Source/BZ1/1.5/Battlezone_Install/bzint.pdb). Values 7, 8,
	// 19 and 20 have no name there; Redux GetUnitMsg (0x005F8FB0) still maps
	// every value 0..26 to a class sound slot, so they stay reachable by
	// number.
	constexpr int kUnitMsgMax = 26;

	struct UnitMsgName
	{
		const char* name;
		int value;
	};

	inline constexpr UnitMsgName kUnitMsgNames[] = {
		{ "select_wait", 0 },
		{ "select_go", 1 },
		{ "select_follow", 2 },
		{ "select_attack", 3 },
		{ "select_pickup", 4 },
		{ "select_dropoff", 5 },
		{ "select_deploy", 6 },
		{ "select_other", 9 },
		{ "go", 10 },
		{ "go_object", 11 },
		{ "follow", 12 },
		{ "follow_me", 13 },
		{ "attack", 14 },
		{ "repair", 15 },
		{ "reload", 16 },
		{ "rescue", 17 },
		{ "recycle", 18 },
		{ "other", 21 },
		{ "deployed", 22 },
		{ "packed", 23 },
		{ "killed", 24 },
		{ "died", 25 },
		{ "blocked", 26 },
	};

	// Case-insensitive name lookup; -1 when unknown.
	inline int UnitMsgFromName(const char* name) noexcept
	{
		if (name == nullptr)
		{
			return -1;
		}
		for (const UnitMsgName& entry : kUnitMsgNames)
		{
			const char* a = entry.name;
			const char* b = name;
			while (*a != '\0' && *b != '\0' &&
				std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b)))
			{
				++a;
				++b;
			}
			if (*a == '\0' && *b == '\0')
			{
				return entry.value;
			}
		}
		return -1;
	}

	inline bool IsValidUnitMsg(double value) noexcept
	{
		return value >= 0.0 && value <= kUnitMsgMax && static_cast<double>(static_cast<int>(value)) == value;
	}

	// QueueCB (0x0043D1D0) treats priority 1, 2, 5 and 6 specially and any
	// other value as a plain append; EXU accepts 0..6.
	inline bool IsValidQueuePriority(double value) noexcept
	{
		return value >= 0.0 && value <= 6.0 && static_cast<double>(static_cast<int>(value)) == value;
	}

	// Chat/message colours. The Lua API takes 0xAARRGGBB. The engine's own
	// palette values (FUN_00624550: self 0xFF00FF00, 0xFFFF00FF, 0xFF0080FF,
	// 0xFFFFFFFF, 0xFFFF0000) are plain 32-bit words handed to FUN_00821450 and
	// ChatMessageAdd. Read as ARGB, 0xFF0080FF is BZ 1.5's
	// DisplayInterface::colorBlue, RGB(0, 0.5, 1); OpenShim's live-checked HUD
	// patches (scrap_pilot_hud, producer_rally_points) pass ARGB to the same
	// renderer. Inferred, not yet seen in game: the message box is null in the
	// single-player test mission, so a check needs a multiplayer session.
	// EngineColour is the one place to flip if it turns out ABGR.
	inline constexpr unsigned EngineColour(unsigned argb) noexcept
	{
		return argb;
	}

	// A Lua colour argument: an integer 0..0xFFFFFFFF.
	inline bool IsValidChatColour(double value) noexcept
	{
		return value >= 0.0 && value <= 4294967295.0 &&
			static_cast<double>(static_cast<unsigned long long>(value)) == value;
	}

	struct ChatColourName
	{
		const char* name;
		unsigned argb;
	};

	inline constexpr ChatColourName kChatColours[] = {
		{ "WHITE", 0xFFFFFFFFu },
		{ "GREEN", 0xFF00FF00u },
		{ "RED", 0xFFFF0000u },
		{ "BLUE", 0xFF0000FFu },
		{ "YELLOW", 0xFFFFFF00u },
		{ "CYAN", 0xFF00FFFFu },
		{ "MAGENTA", 0xFFFF00FFu },
		{ "ORANGE", 0xFFFF8000u },
	};

	// Settings sections and labels: 1-64 printable ASCII characters.
	constexpr std::size_t kSettingKeyMax = 64;
	inline constexpr const char* kDefaultSettingSection = "General";
	inline constexpr const char* kSettingsNamespace = "exu.settings";

	inline bool IsValidSettingKey(const char* key, std::size_t length) noexcept
	{
		if (key == nullptr || length == 0 || length > kSettingKeyMax)
		{
			return false;
		}
		for (std::size_t i = 0; i < length; ++i)
		{
			const unsigned char c = static_cast<unsigned char>(key[i]);
			if (c < 0x20 || c > 0x7E)
			{
				return false;
			}
		}
		return true;
	}

	// Where the section, label and value/default sit in a settings call.
	// Positions are 1-based Lua stack indices; 0 means absent.
	struct SettingArgs
	{
		int section = 0;
		int label = 0;
		int value = 0;
	};

	// Get*([section], label[, default]):
	//   1 arg                 -> label
	//   3+ args               -> section, label, default
	//   2 args, second string -> section, label (the default is nil)
	//   2 args, otherwise     -> label, default
	// GetSettingString's default is itself a string, so a two-argument
	// GetSettingString call is always (section, label); pass the section to
	// give it a default.
	inline SettingArgs ResolveGetArgs(int argCount, bool secondIsString, bool stringSetting) noexcept
	{
		SettingArgs args{};
		if (argCount <= 1)
		{
			args.label = 1;
		}
		else if (argCount >= 3 || secondIsString || stringSetting)
		{
			args.section = 1;
			args.label = 2;
			args.value = argCount >= 3 ? 3 : 0;
		}
		else
		{
			args.label = 1;
			args.value = 2;
		}
		return args;
	}

	// Save*([section], label, value): 3+ args -> section, label, value;
	// otherwise label, value. DeleteSetting([section], label) uses the same
	// rule with no value: 2+ args -> section, label.
	inline SettingArgs ResolveSaveArgs(int argCount) noexcept
	{
		SettingArgs args{};
		if (argCount >= 3)
		{
			args.section = 1;
			args.label = 2;
			args.value = 3;
		}
		else
		{
			args.label = 1;
			args.value = 2;
		}
		return args;
	}

	inline SettingArgs ResolveDeleteArgs(int argCount) noexcept
	{
		SettingArgs args{};
		if (argCount >= 2)
		{
			args.section = 1;
			args.label = 2;
		}
		else
		{
			args.label = 1;
		}
		return args;
	}

	// Int settings hold integral numbers only; a stored 2.5 is not an int.
	inline bool IsIntegralSetting(double value) noexcept
	{
		return value >= -2147483648.0 && value <= 2147483647.0 &&
			static_cast<double>(static_cast<long long>(value)) == value;
	}

	// exu.FileExists name gate. Stock UseItem (0x00506E80) loads through
	// 0x00481B10, which returns no data, without a lookup, for an empty name
	// or one whose first four characters are "null" in any case
	// (_strnicmp(name, "null", 4)). A Lua string with an embedded NUL reaches
	// the engine cut short, so it is refused rather than answered for a
	// different name.
	inline bool IsLoadableResourceName(const char* name, std::size_t length) noexcept
	{
		if (name == nullptr || length == 0 || std::strlen(name) != length)
		{
			return false;
		}
		if (length >= 4)
		{
			const char* const kNull = "null";
			bool matchesNull = true;
			for (std::size_t i = 0; i < 4; ++i)
			{
				if (std::tolower(static_cast<unsigned char>(name[i])) != kNull[i])
				{
					matchesNull = false;
					break;
				}
			}
			if (matchesNull)
			{
				return false;
			}
		}
		return true;
	}

	// The resource loader (0x00481B80) logs "Couldn't find file" when the
	// size lookup returns 0 (cmp [ebp-0x24], 0; ja at 0x00481C6F). Every miss
	// path of FUN_00829150 returns 0 (xor eax, eax at 0x0082919F, 0x0082925E,
	// 0x0082931C, 0x00829379 and 0x0082937D; a missing pak record included).
	// Only a positive size counts, so a stray negative is never "present".
	inline bool IsLoadableResourceSize(int size) noexcept
	{
		return size > 0;
	}
}
