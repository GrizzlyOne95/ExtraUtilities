#include "UI/Bz2HudGeometry.h"
#include "HostTest.h"
#include <cmath>
#define TEST_CHECK(x) HostTest::Expect((x),#x)
int main()
{
    using namespace ExtraUtilities::Bz2Hud;
    const auto objective=Marker(100,200,16,1,0,true);
    const auto target=Marker(100,200,16,1,0,false);
    TEST_CHECK(std::fabs(objective[0][0].x-100)<.001f);
    TEST_CHECK(std::fabs(objective[0][0].y-216)<.001f);
    TEST_CHECK(std::fabs(objective[0][1].y-222)<.001f);
    TEST_CHECK(std::fabs(target[0][1].y-224)<.001f);
    const auto spin=Marker(100,200,16,1,1,true);
    const auto sameAngle=Marker(100,200,16,1,.25f,false);
    TEST_CHECK(std::fabs(spin[0][0].x-sameAngle[0][0].x)<.001f);
    TEST_CHECK(std::fabs(spin[0][0].y-sameAngle[0][0].y)<.001f);
    TEST_CHECK(std::fabs(spin[0][0].x-objective[0][0].x)>1);
    const auto repeated=Marker(100,200,16,1,1,true);
    TEST_CHECK(repeated[0][0].x==spin[0][0].x); // Paused native time keeps pose.
    TEST_CHECK(RowColors(false,false,false).background==0x007fffu);
    TEST_CHECK(RowColors(true,false,false).gradient==0x7f003fu);
    TEST_CHECK(RowColors(false,true,false).background==0x7fbfffu);
    TEST_CHECK(RowColors(true,true,false).gradient==0x7f3f5fu);
    TEST_CHECK(RowColors(false,false,true).background==0x3f3f3fu);
    TEST_CHECK(RowColors(true,true,true).background==0xffffffu);
    TEST_CHECK(LerpColor(0x007fffu,0x003f7fu,0)==0x007fffu);
    TEST_CHECK(LerpColor(0x007fffu,0x003f7fu,1)==0x003f7fu);
    TEST_CHECK(LerpColor(0x000000u,0xffffffu,.5f)==0x808080u);
    // Matches colorize.dds: tip on the middle lines, symmetric, deepest at the edge.
    TEST_CHECK(RoundedInset(0,16)==5);
    TEST_CHECK(RoundedInset(0,16)==RoundedInset(15,16));
    TEST_CHECK(RoundedInset(7,16)==0);
    TEST_CHECK(RoundedInset(8,16)==0);
    return HostTest::Finish("BZ2 HUD geometry and command rows");
}
