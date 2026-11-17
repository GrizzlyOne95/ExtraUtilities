// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.
#include "Game/AiPathCore.h"
#include "HostTest.h"
#include <cstring>
#include <limits>
#include <vector>

int main()
{
    using namespace ExtraUtilities::AiPath;
    using HostTest::Expect;

    Expect(std::strcmp(UnitProcessStateName(0),"NO_STATE") == 0 && std::strcmp(UnitProcessStateName(4),"GOTO") == 0 &&
        std::strcmp(UnitProcessStateName(17),"USTATE2") == 0, "process state names");
    Expect(UnitProcessStateName(18) == nullptr && UnitProcessStateName(-1) == nullptr, "process names stop at 17");
    Expect(std::strcmp(UnitTaskStateName(2),"GOTO") == 0 && std::strcmp(UnitTaskStateName(3),"STUCK") == 0 &&
        std::strcmp(UnitTaskStateName(4),"FOLLOW") == 0 && std::strcmp(UnitTaskStateName(13),"DONE") == 0,
        "task enum, not the stock mislabeled table");
    Expect(UnitTaskStateName(14) == nullptr && UnitTaskStateName(-1) == nullptr, "task names stop at 13");

    const float pts[] = {1,2, 3,4, 5,6};
    PathSnapshot s;
    Expect(CopyPath(3,pts,1,s) && s.count == 3 && s.copied == 3 && !s.truncated && s.planPoint == 1, "small path copies");
    Expect(s.xz[0] == 1 && s.xz[1] == 2 && s.xz[4] == 5 && s.xz[5] == 6, "x,z pairs preserved in order");
    Expect(!CopyPath(3,nullptr,0,s), "null array rejected");
    Expect(!CopyPath(0,pts,0,s) && !CopyPath(-1,pts,0,s), "empty or negative count rejected");
    Expect(!CopyPath(kSaneCount + 1,pts,0,s), "corrupt count rejected without reading");
    const float bad[] = {1,2, std::numeric_limits<float>::quiet_NaN(),4};
    Expect(!CopyPath(2,bad,0,s) && s.copied == 0, "non-finite coordinate rejected and output cleared");

    std::vector<float> big((kMaxPoints + 10) * 2);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<float>(i);
    Expect(CopyPath(static_cast<std::int32_t>(kMaxPoints + 10),big.data(),5,s) && s.truncated &&
        s.copied == static_cast<std::int32_t>(kMaxPoints) && s.count == static_cast<std::int32_t>(kMaxPoints + 10),
        "long path truncated at the cap, true count kept");
    Expect(s.xz[kMaxPoints * 2 - 1] == static_cast<float>(kMaxPoints * 2 - 1), "last kept point is point cap-1");
    Expect(CopyPath(static_cast<std::int32_t>(kMaxPoints),big.data(),0,s) && !s.truncated, "exactly the cap is not truncated");

    Expect(LuaPlanIndex(0,3) == 1 && LuaPlanIndex(2,3) == 3, "Lua index is 1-based");
    Expect(LuaPlanIndex(3,3) == 0 && LuaPlanIndex(0,0) == 0 && LuaPlanIndex(0xFFFFFFFFu,3) == 0, "out-of-range index is 0");
    Expect(CopyPath(static_cast<std::int32_t>(kMaxPoints + 10),big.data(),static_cast<std::uint32_t>(kMaxPoints + 2),s) &&
        SnapshotLuaIndex(s) == 0, "index past the truncated points is absent");
    Expect(CopyPath(static_cast<std::int32_t>(kMaxPoints + 10),big.data(),static_cast<std::uint32_t>(kMaxPoints - 1),s) &&
        SnapshotLuaIndex(s) == static_cast<int>(kMaxPoints), "last kept point still has an index");
    Expect(CopyPath(3,pts,2,s) && SnapshotLuaIndex(s) == 3 && CopyPath(3,pts,3,s) && SnapshotLuaIndex(s) == 0,
        "untruncated index in and out of range");
    Expect(PlanLooksValid(1,pts) && PlanLooksValid(kSaneCount,pts), "valid plan bounds accepted");
    Expect(!PlanLooksValid(0,pts) && !PlanLooksValid(-1,pts) && !PlanLooksValid(kSaneCount + 1,pts) && !PlanLooksValid(3,nullptr),
        "invalid plan rejected");
    return HostTest::Finish("ai path core");
}
