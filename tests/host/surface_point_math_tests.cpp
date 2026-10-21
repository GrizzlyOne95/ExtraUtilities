#include "Game/SurfacePointMath.h"
#include <cassert>
#include <cmath>
#include <limits>

int main()
{
    using ExtraUtilities::SurfacePointMath::ToLocal;
    const float origin[3]={100,20,-50}, identity[4]={1,0,0,0};
    const float scale[3]={2,3,.5f}, point[3]={104,29,-48};
    float out[3];
    assert(ToLocal(point,origin,identity,scale,out));
    assert(std::fabs(out[0]-2)<.0001f && std::fabs(out[1]-3)<.0001f && std::fabs(out[2]-4)<.0001f);
    const float quarterTurn[4]={.70710678f,0,.70710678f,0};
    const float rotated[3]={102,29,-54};
    assert(ToLocal(rotated,origin,quarterTurn,scale,out));
    assert(std::fabs(out[0]-2)<.0001f && std::fabs(out[1]-3)<.0001f && std::fabs(out[2]-4)<.0001f);
    const float flat[3]={1,0,1}, badQ[4]={0,0,0,0};
    assert(!ToLocal(point,origin,identity,flat,out));
    assert(!ToLocal(point,origin,badQ,scale,out));
    const float bad[3]={std::numeric_limits<float>::infinity(),0,0};
    assert(!ToLocal(bad,origin,identity,scale,out));
}
