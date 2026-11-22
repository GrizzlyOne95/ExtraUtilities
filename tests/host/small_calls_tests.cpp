// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/SmallCallsLogic.h"
#include "HostTest.h"
#include <cstring>
#include <limits>
#include <string>

int main()
{
    using namespace ExtraUtilities::SmallCallsLogic;
    using HostTest::Expect;

    Expect(IsValidSoundName("bail.wav", 8) && IsValidSoundName("my_hop-2.WAV", 12), "plain sound names pass");
    Expect(!IsValidSoundName("", 0) && !IsValidSoundName(nullptr, 4), "empty or null sound name rejected");
    Expect(!IsValidSoundName("dir/a.wav", 9) && !IsValidSoundName("dir\\a.wav", 9) && !IsValidSoundName("a b.wav", 7),
        "separators and spaces rejected");
    const std::string longest(kSoundNameCapacity - 1, 'a');
    const std::string tooLong(kSoundNameCapacity, 'a');
    Expect(IsValidSoundName(longest.c_str(), longest.size()), "63 chars fit the buffer with its terminator");
    Expect(!IsValidSoundName(tooLong.c_str(), tooLong.size()), "64 chars overflow the buffer");
    Expect(!IsValidSoundName("a\0b", 3), "embedded NUL rejected");

    Expect(UnitMsgFromName("select_wait") == 0 && UnitMsgFromName("select_other") == 9 && UnitMsgFromName("attack") == 14 &&
        UnitMsgFromName("deployed") == 22 && UnitMsgFromName("blocked") == 26, "PDB enum values");
    Expect(UnitMsgFromName("ATTACK") == 14 && UnitMsgFromName("Follow_Me") == 13, "name lookup ignores case");
    Expect(UnitMsgFromName("attac") == -1 && UnitMsgFromName("attacks") == -1 && UnitMsgFromName("") == -1 &&
        UnitMsgFromName(nullptr) == -1, "prefixes, unknown and null names rejected");
    Expect(UnitMsgFromName("follow") == 12 && UnitMsgFromName("go") == 10, "short names do not match longer ones");
    for (const UnitMsgName& entry : kUnitMsgNames)
    {
        Expect(entry.value >= 0 && entry.value <= kUnitMsgMax && UnitMsgFromName(entry.name) == entry.value,
            std::string("table entry round-trips: ") + entry.name);
    }
    Expect(IsValidUnitMsg(0) && IsValidUnitMsg(7) && IsValidUnitMsg(26), "numbers 0..26 accepted, unnamed ones too");
    Expect(!IsValidUnitMsg(-1) && !IsValidUnitMsg(27) && !IsValidUnitMsg(2.5) &&
        !IsValidUnitMsg(std::numeric_limits<double>::quiet_NaN()), "out of range, fractional and NaN rejected");
    Expect(IsValidQueuePriority(0) && IsValidQueuePriority(6) && !IsValidQueuePriority(7) && !IsValidQueuePriority(-1) &&
        !IsValidQueuePriority(1.5), "queue priority 0..6");

    Expect(IsValidSettingKey("General", 7) && IsValidSettingKey("Hard Mode!", 10), "printable keys pass");
    const std::string key64(kSettingKeyMax, 'k');
    const std::string key65(kSettingKeyMax + 1, 'k');
    Expect(IsValidSettingKey(key64.c_str(), key64.size()) && !IsValidSettingKey(key65.c_str(), key65.size()), "64-char key limit");
    Expect(!IsValidSettingKey("", 0) && !IsValidSettingKey("a\nb", 3) && !IsValidSettingKey("\xC3\xA9", 2),
        "empty, control and non-ASCII keys rejected");

    SettingArgs a = ResolveGetArgs(1, false, false);
    Expect(a.section == 0 && a.label == 1 && a.value == 0, "Get(label)");
    a = ResolveGetArgs(2, false, false);
    Expect(a.section == 0 && a.label == 1 && a.value == 2, "Get(label, default)");
    a = ResolveGetArgs(2, true, false);
    Expect(a.section == 1 && a.label == 2 && a.value == 0, "Get(section, label)");
    a = ResolveGetArgs(2, false, true);
    Expect(a.section == 1 && a.label == 2 && a.value == 0, "GetSettingString two args is (section, label)");
    a = ResolveGetArgs(3, false, false);
    Expect(a.section == 1 && a.label == 2 && a.value == 3, "Get(section, label, default)");

    a = ResolveSaveArgs(2);
    Expect(a.section == 0 && a.label == 1 && a.value == 2, "Save(label, value)");
    a = ResolveSaveArgs(3);
    Expect(a.section == 1 && a.label == 2 && a.value == 3, "Save(section, label, value)");
    a = ResolveDeleteArgs(1);
    Expect(a.section == 0 && a.label == 1 && a.value == 0, "Delete(label)");
    a = ResolveDeleteArgs(2);
    Expect(a.section == 1 && a.label == 2 && a.value == 0, "Delete(section, label)");

    Expect(IsIntegralSetting(0) && IsIntegralSetting(-5) && IsIntegralSetting(2147483647.0) && IsIntegralSetting(-2147483648.0),
        "int32 range accepted");
    Expect(!IsIntegralSetting(2.5) && !IsIntegralSetting(2147483648.0) && !IsIntegralSetting(std::numeric_limits<double>::infinity()) &&
        !IsIntegralSetting(std::numeric_limits<double>::quiet_NaN()), "fractional, out of range and non-finite rejected");
    Expect(std::strcmp(kSettingsNamespace, "exu.settings") == 0 && std::strcmp(kDefaultSettingSection, "General") == 0,
        "store names");

    Expect(IsLoadableResourceName("misn2bcr.des", 12) && IsLoadableResourceName("a", 1) && IsLoadableResourceName("nul.des", 7),
        "plain resource names are looked up");
    Expect(!IsLoadableResourceName("", 0) && !IsLoadableResourceName(nullptr, 3), "empty or null name is never loadable");
    Expect(!IsLoadableResourceName("null", 4) && !IsLoadableResourceName("NULL.des", 8) && !IsLoadableResourceName("Nullify.odf", 11),
        "UseItem refuses any name starting \"null\" in any case");
    Expect(IsLoadableResourceName("anull.des", 9), "\"null\" only matters as a prefix");
    Expect(IsLoadableResourceName("nul", 3) && !IsLoadableResourceName("NuLl", 4), "3-char names pass; mixed-case null refused");
    Expect(!IsLoadableResourceName("a\0b.des", 7), "embedded NUL rejected");
    Expect(IsLoadableResourceSize(1) && !IsLoadableResourceSize(0) && !IsLoadableResourceSize(-1),
        "only a positive size is loadable");
    return HostTest::Finish("small calls");
}
