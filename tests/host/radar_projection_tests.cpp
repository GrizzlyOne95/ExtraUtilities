#include "../../src/UI/RadarProjection.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace ExtraUtilities::RadarPresentation;
int main()
{
    const float center[] = {0,0,0}, right[] = {100,0,0}, up[] = {0,50,86.60254f};
    assert(Project(center,100,100,8,200,300)==Pack(200,300));
    assert(Project(right,100,100,8,200,300)==Pack(300,300));
    assert(Project(up,100,100,8,200,300)==Pack(200,255));
    const float scaled[] = {0,100,173.20508f};
    assert(Project(up,100,100,8,200,300)==Project(scaled,200,100,8,200,300));
    // Radius/placement scaling and signed packed return are independent.
    assert(Project(right,100,200,8,-400,-200)==Pack(-200,-200));
    const float bad[] = {std::numeric_limits<float>::infinity(),0,0};
    const float behind[] = {10,10,-800};
    assert(Project(bad,100,100,8,200,300)==Pack(200,300));
    assert(Project(behind,100,100,8,200,300)==Pack(200,300));
    assert(Project(nullptr,100,100,8,200,300)==Pack(200,300));
    assert(Project(right,0,100,8,200,300)==Pack(200,300));
    assert(Project(right,100,100,0,200,300)==Pack(200,300));
    // The unsnapped projection agrees with the packed one and fails closed.
    float exactX=0,exactY=0;
    assert(ProjectExact(right,100,100,8,200,300,exactX,exactY)&&exactX==300&&exactY==300);
    assert(ProjectExact(up,100,100,8,200,300,exactX,exactY)&&exactX==200);
    assert(exactY>254.5f&&exactY<255.5f);
    assert(!ProjectExact(behind,100,100,8,200,300,exactX,exactY));
    assert(!ProjectExact(right,100,100,0,200,300,exactX,exactY));
    // Stock proportions at 1440p: the lip's left edge decides, and lands on screen.
    const int fitted=FitRadius(303,1.48f,.5f,.8660254f,10.667f,341,211);
    assert(fitted<303&&fitted*1.48f<341&&fitted*1.48f>320);
    assert(FitRadius(100,1.48f,.5f,.8660254f,10.667f,341,211)==100); // already fits
    assert(FitRadius(303,1.48f,.5f,.8660254f,10.667f,1000,100)<120); // bottom decides
    // Sweep the entire ground ring: relative screen angle keeps its quadrant,
    // all points stay bounded, and opposite sides have perspective asymmetry.
    for(int i=0;i<360;++i)
    {
        const float a=static_cast<float>(i*3.141592653589793/180);
        const float p[]={100*std::cos(a),50*std::sin(a),86.60254f*std::sin(a)};
        const uint64_t q=Project(p,100,100,8,200,300);
        const int x=static_cast<int32_t>(q),y=static_cast<int32_t>(q>>32);
        assert(x>=86 && x<=314 && y>=243 && y<=357);
        if(p[0]>1)assert(x>=200);
        if(p[1]>1)assert(y<=300);
    }
    std::puts("Radar projection: PASS (center, cardinal, range/size/placement, invalid input, 360-degree ring)");
}
