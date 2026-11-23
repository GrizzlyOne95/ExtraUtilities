// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/ClockLogic.h"
#include "Game/SmallCallsLogic.h"
#include "HostTest.h"
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

int main()
{
    using namespace ExtraUtilities::ClockLogic;
    using HostTest::Expect;

    for (const char c : std::string("aAbBcdHIjmMpSUwWxXyYZ%"))
    {
        Expect(IsAllowedSpecifier(c), std::string("allowed specifier ") + c);
    }
    for (const char c : std::string("EOqQ#-ekCDeFgGhnrRstTuVz1 "))
    {
        Expect(!IsAllowedSpecifier(c), std::string("rejected specifier ") + c);
    }

    // 2023-11-14 22:13:20 UTC (a Tuesday).
    const std::time_t fixed = 1700000000;
    std::tm utc{};
    Expect(BreakDown(fixed, true, utc), "gmtime works");
    Expect(utc.tm_year + 1900 == 2023 && utc.tm_mon + 1 == 11 && utc.tm_mday == 14 && utc.tm_hour == 22 &&
        utc.tm_min == 13 && utc.tm_sec == 20 && utc.tm_wday + 1 == 3 && utc.tm_yday + 1 == 318, "!*t fields for the fixed time");

    std::string out;
    char bad = 0;
    Expect(FormatDate("%Y-%m-%d %H:%M:%S %%", utc, out, bad) && out == "2023-11-14 22:13:20 %", "format expands");
    Expect(FormatDate("plain 100%", utc, out, bad) && out == "plain 100%", "trailing lone % is literal");
    Expect(FormatDate("", utc, out, bad) && out.empty(), "empty format");
    Expect(!FormatDate("a%Qb", utc, out, bad) && bad == 'Q', "unknown specifier rejected");
    Expect(!FormatDate("%#d", utc, out, bad) && bad == '#', "MSVC # flag rejected");
    Expect(!FormatDate("%Ec", utc, out, bad) && bad == 'E', "locale modifier rejected");

    // Round trip: the local broken-down time of a fixed instant maps back to it.
    std::tm local{};
    Expect(BreakDown(fixed, false, local), "localtime works");
    int v[6] = {};
    const int dflt[6] = { -1, -1, -1, 12, 0, 0 };
    const double raw[6] = { local.tm_year + 1900.0, local.tm_mon + 1.0, 1.0 * local.tm_mday, 1.0 * local.tm_hour, 1.0 * local.tm_min, 1.0 * local.tm_sec };
    bool ok = true;
    for (int i = 0; i < 6; ++i)
    {
        ok = ok && ResolveField(true, raw[i], dflt[i], v[i]) == FieldResult::Ok;
    }
    Expect(ok, "fields resolve");
    std::tm back = TmFromFields(v[0], v[1], v[2], v[3], v[4], v[5], local.tm_isdst > 0 ? 1 : 0);
    Expect(std::mktime(&back) == fixed, "*t table round-trips through mktime");

    int out1 = 0;
    Expect(ResolveField(false, 0, 12, out1) == FieldResult::Ok && out1 == 12, "hour defaults to 12");
    Expect(ResolveField(false, 0, 0, out1) == FieldResult::Ok && out1 == 0, "min defaults to 0");
    Expect(ResolveField(false, 0, -1, out1) == FieldResult::Missing, "year is required");
    Expect(ResolveField(true, 1e12, -1, out1) == FieldResult::OutOfRange, "huge field rejected");
    Expect(ResolveField(true, 5.9, -1, out1) == FieldResult::Ok && out1 == 5, "fraction truncates");

    std::time_t t = 0;
    Expect(TimeFromNumber(10, t) && t == 10 && !TimeFromNumber(1e300, t) && !TimeFromNumber(std::nan(""), t),
        "time number conversion");
    Expect(std::difftime(100, 40) == 60.0 && std::difftime(5, 0) == 5.0, "difftime");

    // exu.UNIT_MESSAGE is built from kUnitMsgNames; the documented stub lists
    // the same names (upper-cased) with the same numbers.
    std::ifstream file("Definitions/ExtraUtils.lua");
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string lua = buffer.str();
    const std::size_t start = lua.find("exu.UNIT_MESSAGE = {");
    const std::size_t end = start == std::string::npos ? start : lua.find('}', start);
    Expect(start != std::string::npos && end != std::string::npos, "UNIT_MESSAGE stub found");
    if (start != std::string::npos && end != std::string::npos)
    {
        const std::string block = lua.substr(start, end - start);
        std::size_t lines = 0;
        for (std::size_t p = block.find(" = "); p != std::string::npos; p = block.find(" = ", p + 1))
        {
            ++lines;
        }
        // one for "exu.UNIT_MESSAGE = {" plus one per entry
        Expect(lines == 1 + sizeof(ExtraUtilities::SmallCallsLogic::kUnitMsgNames) / sizeof(ExtraUtilities::SmallCallsLogic::kUnitMsgNames[0]),
            "stub entry count matches the parser table");
        for (const auto& entry : ExtraUtilities::SmallCallsLogic::kUnitMsgNames)
        {
            std::string name = entry.name;
            for (char& c : name)
            {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            Expect(block.find("    " + name + " = " + std::to_string(entry.value) + ",") != std::string::npos,
                "stub lists " + name);
        }
    }
    return HostTest::Finish("clock");
}
