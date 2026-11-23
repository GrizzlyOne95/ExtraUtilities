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

// Pure rules behind exu.GetCurDate / GetCurTime / GetTimeDiff (Game/SmallCalls.cpp).
// BZR's Lua 5.1.5 has no os library, so these reproduce os.date, os.time and
// os.difftime. The logic follows Lua 5.1.5 loslib.c (os_date, os_time,
// os_difftime; (c) PUC-Rio, MIT license), with one hardening: MSVC's strftime
// aborts the process (invalid-parameter handler) on an unknown conversion
// specifier, so every "%x" is checked against the C89 set (Lua 5.2's
// LUA_STRFTIMEOPTIONS) before strftime sees it. No Lua or engine dependency, so
// tests/host/exu_date_tests.cpp can check it.

#include <cmath>
#include <cstddef>
#include <ctime>
#include <string>

namespace ExtraUtilities::ClockLogic
{
	// C89 strftime conversions. Locale modifiers (%E, %O) and MSVC's '#' flag
	// are not in the set, so they are rejected.
	inline bool IsAllowedSpecifier(char c) noexcept
	{
		switch (c)
		{
		case 'a': case 'A': case 'b': case 'B': case 'c': case 'd': case 'H': case 'I':
		case 'j': case 'm': case 'M': case 'p': case 'S': case 'U': case 'w': case 'W':
		case 'x': case 'X': case 'y': case 'Y': case 'Z': case '%':
			return true;
		default:
			return false;
		}
	}

	// Seconds to a broken-down time; false when the time is out of range for
	// the C library. Thread-safe variants on both toolchains.
	inline bool BreakDown(std::time_t t, bool utc, std::tm& out) noexcept
	{
#ifdef _WIN32
		return (utc ? gmtime_s(&out, &t) : localtime_s(&out, &t)) == 0;
#else
		return (utc ? gmtime_r(&t, &out) : localtime_r(&t, &out)) != nullptr;
#endif
	}

	// A Lua number as a time_t; false for NaN, infinity or beyond 64-bit.
	inline bool TimeFromNumber(double value, std::time_t& out) noexcept
	{
		if (!std::isfinite(value) || value < -9.2e18 || value > 9.2e18)
		{
			return false;
		}
		out = static_cast<std::time_t>(value);
		return true;
	}

	// The first unlisted conversion in a format, or '\0' when all are allowed.
	// A '%' followed by NUL is literal (as Lua 5.1 does), not a conversion.
	inline char FindBadSpecifier(const char* format) noexcept
	{
		for (const char* s = format; *s != '\0'; ++s)
		{
			if (*s == '%' && s[1] != '\0')
			{
				const char spec = *++s;
				if (!IsAllowedSpecifier(spec))
				{
					return spec;
				}
			}
		}
		return '\0';
	}

	// os_date's non-table branch for a format FindBadSpecifier accepted.
	// append(const char*, std::size_t) receives the output in pieces. The frame
	// holds only trivially destructible locals, so append may raise a Lua
	// error (longjmp) safely.
	template <typename Append>
	void FormatValidatedDate(const char* format, const std::tm& tm, Append&& append)
	{
		for (const char* s = format; *s != '\0'; ++s)
		{
			if (*s != '%' || s[1] == '\0')
			{
				append(s, 1);
				continue;
			}
			const char conversion[3] = { '%', *++s, '\0' };
			char buffer[256];
			const std::size_t n = std::strftime(buffer, sizeof(buffer), conversion, &tm);
			append(buffer, n);
		}
	}

	// String form for tests: false and badSpec set for an unlisted conversion.
	inline bool FormatDate(const char* format, const std::tm& tm, std::string& out, char& badSpec)
	{
		out.clear();
		badSpec = FindBadSpecifier(format);
		if (badSpec != '\0')
		{
			return false;
		}
		FormatValidatedDate(format, tm, [&](const char* text, std::size_t n) { out.append(text, n); });
		return true;
	}

	// One field of the os.time table: absent -> default (negative default means
	// required), else the integer part, which must fit an int.
	enum class FieldResult { Ok, Missing, OutOfRange };

	inline FieldResult ResolveField(bool present, double value, int defaultValue, int& out) noexcept
	{
		if (!present)
		{
			if (defaultValue < 0)
			{
				return FieldResult::Missing;
			}
			out = defaultValue;
			return FieldResult::Ok;
		}
		// The lower bound leaves room for TmFromFields' year - 1900 and
		// month - 1 without int overflow.
		if (!std::isfinite(value) || value < -2147483648.0 + 1900.0 || value > 2147483647.0)
		{
			return FieldResult::OutOfRange;
		}
		out = static_cast<int>(value);
		return FieldResult::Ok;
	}

	// os_time's struct tm from already-resolved fields (month and year as the
	// user wrote them, 1-based and full year). isdst < 0 lets mktime decide.
	inline std::tm TmFromFields(int year, int month, int day, int hour, int min, int sec, int isdst) noexcept
	{
		std::tm tm{};
		tm.tm_year = year - 1900;
		tm.tm_mon = month - 1;
		tm.tm_mday = day;
		tm.tm_hour = hour;
		tm.tm_min = min;
		tm.tm_sec = sec;
		tm.tm_isdst = isdst;
		return tm;
	}
}
