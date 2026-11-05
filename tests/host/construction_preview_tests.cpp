#include "Game/ConstructionPreviewCore.h"
#include "Ogre/OgreRenderSpace.h"
#include <cassert>
#include <limits>

using namespace ExtraUtilities;
using namespace ConstructionPreviewCore;
struct Matrix
{
    float up_x=0,up_y=1,up_z=0,front_x=0,front_y=0,front_z=1;
    double posit_x=1000,posit_y=15,posit_z=-3000;
};
static bool near(float a,float b) {return std::fabs(a-b)<.0001f;}
int main()
{
    assert(FromSprite(0,1,2,3)==State::None);
    assert(FromSprite(1,1,2,3)==State::Ready);
    assert(FromSprite(2,1,2,3)==State::Warning);
    assert(FromSprite(3,1,2,3)==State::Blocked);
    assert(FromSprite(4,1,2,3)==State::None);
    Matrix m; Vector p; Quaternion q;
    assert(Pose(m,p,q));assert(near(p.y,15.01f));assert(near(q.w,1));
    auto render=OgreRenderSpace::SimPositionToRender(p,Vector{900,2,2500});
    assert(near(render.x,100) && near(render.y,13.01f) && near(render.z,500));
    m.front_x=1;m.front_z=0;
    assert(Pose(m,p,q));assert(near(q.w,float(std::sqrt(.5))) && near(q.y,float(std::sqrt(.5))));
    auto rq=OgreRenderSpace::SimOrientationToRender(q);assert(near(rq.y,-q.y));
    m.front_x=-1;assert(Pose(m,p,q));assert(q.y<0);
    m.front_x=0;m.front_z=-1;assert(Pose(m,p,q));assert(near(std::fabs(q.y),1));
    m.front_z=0;assert(!Pose(m,p,q));
    m.front_z=1;m.front_y=.5f;assert(!Pose(m,p,q));
    m.front_y=0;m.up_y=0;assert(!Pose(m,p,q));
    m.up_y=std::numeric_limits<float>::quiet_NaN();assert(!Pose(m,p,q));
    m.up_y=1;m.posit_x=std::numeric_limits<double>::infinity();assert(!Pose(m,p,q));
    m.posit_x=1000;m.front_x=std::numeric_limits<float>::quiet_NaN();assert(!Pose(m,p,q));
    assert(!SolidPhase(0) && !SolidPhase(511));
    assert(SolidPhase(512) && SolidPhase(1023));assert(!SolidPhase(1024));
}
