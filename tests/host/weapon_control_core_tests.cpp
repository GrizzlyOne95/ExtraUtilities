// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/WeaponControlCore.h"
#include "HostTest.h"
#include <cstring>

int main()
{
    using namespace ExtraUtilities::WeaponControl;
    using HostTest::Expect;

    Expect(ValidSlot(0) && ValidSlot(4) && !ValidSlot(5) && !ValidSlot(-1) && !ValidSlot(1.5), "slots are integers 0-4");
    Expect(SlotBit(0) == 1u && SlotBit(4) == 16u && SlotBit(5) == 0u && SlotBit(-1) == 0u, "slot bits");

    int a = 0, b = 0;
    const void* weapons[kSlotCount] = {&a, nullptr, &b, nullptr, nullptr};
    Expect(NonNullMask(weapons) == 0x5u, "non-null mask");

    Expect(SafeTriggerSelectedMask(0x5u, 0x1Fu, 0x5u) == 0x5u, "selected and enabled weapons fire");
    Expect(SafeTriggerSelectedMask(0x5u, 0x1u, 0x5u) == 0x1u, "disabled slot is skipped");
    Expect(SafeTriggerSelectedMask(0x7u, 0x1Fu, 0x5u) == 0u, "a selected empty slot refuses the whole call");
    Expect(SafeTriggerSelectedMask(0u, 0x1Fu, 0x1Fu) == 0u, "nothing selected fires nothing");

    Expect(RequestedSelection(3, 0x5u) == 0x1u, "request masked by existing");
    Expect(RequestedSelection(2, 0x5u) == 0u, "request for empty slots only is refused");
    Expect(RequestedSelection(0, 0x1Fu) == 0u && RequestedSelection(32, 0x1Fu) == 0u && RequestedSelection(-1, 0x1Fu) == 0u,
        "mask outside 1..31 refused");
    Expect(RequestedSelection(1.5, 0x1Fu) == 0u, "fractional mask refused");

    Expect(SafeSpecialSlot(2, 0x4u, 0x4u) == 2, "enabled special fires");
    Expect(SafeSpecialSlot(-1, 0x1Fu, 0x1Fu) == -1 && SafeSpecialSlot(5, 0x1Fu, 0x1Fu) == -1, "no special slot");
    Expect(SafeSpecialSlot(2, 0x0u, 0x4u) == -1, "disabled special does not fire");
    Expect(SafeSpecialSlot(2, 0x4u, 0x0u) == -1, "empty special slot refused");

    Expect(WithEnabledBit(0x1u, 0x5u, 2, true) == 0x5u, "enable sets bit");
    Expect(WithEnabledBit(0x5u, 0x5u, 0, false) == 0x4u, "disable clears bit");
    Expect(WithEnabledBit(0x1Fu, 0x5u, 0, true) == 0x5u, "result masked by existing like SetEnabled");

    char out[kOdfNameLength + 1];
    const char fixed[8] = {'a','v','t','a','n','k','0','1'};
    Expect(CopyFixedName(fixed, kOdfNameLength, out) == 8 && std::strcmp(out, "avtank01") == 0, "unterminated 8-char name");
    Expect(CopyFixedName("GC1", kHardpointNameLength, out) == 3 && std::strcmp(out, "GC1") == 0, "short name stops at NUL");
    Expect(CopyFixedName("", kHardpointNameLength, out) == 0 && out[0] == '\0', "empty name");
    Expect(CopyFixedName("G\x01" "1", kHardpointNameLength, out) == 0 && out[0] == '\0', "control byte rejects name");
    Expect(CopyFixedName(nullptr, kHardpointNameLength, out) == 0, "null name");

    return HostTest::Finish("weapon control core");
}
