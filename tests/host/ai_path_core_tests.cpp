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
    // Terrain pathing grid: bounds [-4,4) x [-2,2), width 8.
    const PathGridBounds b{-4, 4, -2, 2};
    std::size_t idx = 99;
    Expect(PathCellIndex(b,-4,-2,idx) && idx == 0, "min corner is index 0");
    Expect(PathCellIndex(b,3,1,idx) && idx == 31, "last cell is index width*height-1");
    Expect(PathCellIndex(b,0,0,idx) && idx == 2 * 8 + 4, "row-major index with negative mins");
    Expect(!PathCellIndex(b,4,0,idx) && !PathCellIndex(b,0,2,idx) && !PathCellIndex(b,-5,0,idx) && !PathCellIndex(b,0,-3,idx),
        "max exclusive, below min rejected");
    Expect(!PathCellIndex(PathGridBounds{0,0,0,5},0,0,idx) && !PathCellIndex(PathGridBounds{5,0,0,5},1,1,idx), "empty bounds rejected");
    Expect(WorldToPathIndex(b,1.0f,-0.5f,-0.5f,idx) && idx == 1 * 8 + 3, "negative world coords floor toward -inf");
    Expect(WorldToPathIndex(b,2.0f,1.9f,0.4f,idx) && idx == 2 * 8 + 7, "scale 2: x 1.9 -> cell 3, z 0.4 -> cell 0");
    Expect(!WorldToPathIndex(b,1.0f,4.0f,0.0f,idx) && !WorldToPathIndex(b,0.0f,0.0f,0.0f,idx), "bad scale or out of bounds");
    Expect(!WorldToPathIndex(b,1.0f,std::numeric_limits<float>::quiet_NaN(),0.0f,idx) && !WorldToPathIndex(b,1.0f,1.0e30f,0.0f,idx),
        "non-finite or huge world coord rejected");
    Expect(RegionsReachable(3,3) && !RegionsReachable(0,0) && !RegionsReachable(3,4) && !RegionsReachable(0,3), "region reachability");

    // Plan copy keeps points for a type-3 plan; only the type flag says bad.
    Expect(CopyPath(2,pts,0,s) && s.copied == 2, "fallback plan still copies");
    Expect(!PlanTypeOk(3) && PlanTypeOk(0) && PlanTypeOk(1) && PlanTypeOk(2), "type 3 is the bad plan");

    // AI strategic grid: origin (-100, 40), 4 rows x 5 cols.
    AiGridCoord g;
    Expect(WorldToAiCell(-100.0f,40.0f,-100.0f,40.0f,4,5,g) && g.row == 0 && g.col == 0 && g.minX == -100.0f && g.maxZ == 120.0f,
        "origin cell");
    Expect(WorldToAiCell(219.0f,359.0f,-100.0f,40.0f,4,5,g) && g.row == 3 && g.col == 3 && g.minX == 140.0f && g.minZ == 280.0f &&
        g.maxX == 220.0f && g.maxZ == 360.0f, "last row, col 3 bounds");
    Expect(WorldToAiCell(-100.5f,40.0f,-100.0f,40.0f,4,5,g) == false && !WorldToAiCell(300.0f,50.0f,-100.0f,40.0f,4,5,g) &&
        !WorldToAiCell(0.0f,360.0f,-100.0f,40.0f,4,5,g) && !WorldToAiCell(0.0f,39.0f,-100.0f,40.0f,4,5,g), "outside grid rejected");
    Expect(!WorldToAiCell(0.0f,50.0f,-100.0f,40.0f,0,5,g) && !WorldToAiCell(std::numeric_limits<float>::infinity(),50.0f,0,0,4,5,g),
        "empty or non-finite rejected");
    Expect(AiCellUsed(0.0f) && AiCellUsed(-99999.0f) && !AiCellUsed(-100000.0f) && !AiCellUsed(-1.0e9f), "unused priority sentinel");
    return HostTest::Finish("ai path core");
}
