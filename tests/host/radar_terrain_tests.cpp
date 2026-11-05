#include "../../src/UI/RadarTerrainGeometry.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace ExtraUtilities::RadarPresentation;
int main()
{
    auto height=[](float x,float z){return x*.1f+z*.2f;};
    const auto g=SampleTerrain(20,-55,500,32,height);
    assert(!g.nodes.empty()&&g.columns<=65&&g.rows<=65&&g.spacing==32);
    for(const auto& n:g.nodes)
    {
        assert(n.valid&&std::fmod(n.x,32)==0&&std::fmod(n.z,32)==0);
        assert(n.y==height(n.x,n.z));
    }
    const auto huge=SampleTerrain(0,0,1e6f,32,height);
    assert(!huge.nodes.empty()&&huge.nodes.size()<=65*65&&huge.spacing>32);
    assert(SampleTerrain(NAN,0,50,32,height).nodes.empty());
    assert(SampleTerrain(0,0,0,32,height).nodes.empty());
    const auto holes=SampleTerrain(0,0,100,32,[](float,float){return NAN;});
    for(const auto& n:holes.nodes)assert(!TerrainVisible(n,0,0,100));
    std::vector<bool> mask(25,true);mask[12]=false;
    int edges=0;
    const auto strips=ForEachTerrainStrip(5,5,mask,[&](const std::vector<size_t>& run)
    {
        for(size_t i=1;i<run.size();++i)
        {
            assert(run[i]!=12&&run[i-1]!=12);
            const auto d=run[i]-run[i-1];assert(d==1||d==5);++edges;
        }
    });
    assert(strips==12&&edges==36); // four edges touching the center are absent
    const float origin[]={10,20,30},matrix[]={1,0,0,0,.8660254f,.5f,0,-.5f,.8660254f};
    const auto p=TransformTerrain({10,20,130,true},origin,matrix);
    assert(p[0]==0&&p[1]==50&&std::fabs(p[2]-86.60254f)<.001f);
    PulseClock pulse;
    assert(!pulse.Update(1,0,10)&&pulse.Phase(10)<0);
    assert(pulse.Update(1,14,11)&&pulse.Phase(11)==0);
    assert(!pulse.Update(1,4,11.5f)&&pulse.Phase(11.5f)==.5f);
    assert((pulse.GridColor(11.5f,0x0078ff)>>24)==120);
    assert(pulse.Phase(12)<0&&(pulse.GridColor(12,0x0078ff)>>24)==64);
    assert(pulse.Update(1,14,13));
    assert(!pulse.Update(2,0,13.5f)&&pulse.Phase(13.5f)<0);
    assert(pulse.Update(2,14,14));
    assert(!pulse.Update(2,14,14)&&pulse.Phase(14)==0); // paused sim time
    assert(!pulse.Update(2,0,1)&&pulse.Phase(1)<0); // restart/load clock reversal
    std::puts("Radar terrain: PASS; world anchoring, heights, range/complexity limits, hole-safe topology, transforms, native scan trigger/decay/pause/owner/load");
}
