#include "../../src/Game/ExplosionEventsCore.h"
#include <cassert>
#include <limits>
#include <iostream>
int main()
{
    using namespace ExtraUtilities::ExplosionEventsCore;
    Queue<3> q;
    Event e; e.odf[0]='x'; e.x=12; e.y=3; e.z=90000; e.radius=50;
    assert(q.Push(e)); e.x=13; assert(q.Push(e)); e.x=14; assert(q.Push(e));
    e.x=15; assert(q.Push(e)); assert(q.size==3 && q.dropped==1);
    Event out; assert(q.Pop(out) && out.x==13 && out.sequence==2);
    assert(q.Pop(out) && out.x==14); assert(q.Pop(out) && out.x==15); assert(!q.Pop(out));
    e.radius=-1;assert(!q.Push(e));e.radius=1;e.x=std::numeric_limits<double>::infinity();assert(!q.Push(e));
    e.x=0;e.y=std::numeric_limits<double>::quiet_NaN();assert(!q.Push(e));
    e.y=0;e.radius=std::numeric_limits<float>::quiet_NaN();assert(!q.Push(e));
    e.radius=0;assert(q.Push(e)); // zero-radius contact effects remain observable
    q.Clear();assert(q.size==0 && q.dropped==0 && q.sequence==0);
    e.odf[0]=0;assert(!q.Push(e));
    std::cout << "Explosion event queue tests passed\n";
}
