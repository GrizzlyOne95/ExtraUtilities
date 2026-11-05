#include "UI/Bz2HudGeometry.h"
#include "HostTest.h"
#include <cmath>
#include <cstring>
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
    TEST_CHECK(std::strcmp(CommandIcon("Recycler"),"iu_recy")==0);
    TEST_CHECK(std::strcmp(CommandIcon("Recycle"),"iu_recycle")==0);
    TEST_CHECK(std::strcmp(CommandIcon("Attack Tank"),"iu_attack")==0);
    TEST_CHECK(std::strcmp(CommandIcon("Undeploy"),"iu_undeploy")==0);
    TEST_CHECK(CommandIcon("Unknown label")==nullptr);
    return HostTest::Finish("BZ2 HUD geometry and command icons");
}
