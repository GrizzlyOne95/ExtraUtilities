// Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
// Engine-independent adaptation of BZ2 radar terrain topology and pulse timing.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ExtraUtilities::RadarPresentation
{
    inline constexpr uint32_t kRgbMask=(1u<<24)-1;
    struct TerrainNode { float x=0, y=0, z=0; bool valid=false; };
    struct TerrainGrid
    {
        int columns=0, rows=0;
        float spacing=32, centerX=0, centerZ=0, range=0;
        std::vector<TerrainNode> nodes;
    };

    template<class Height>
    TerrainGrid SampleTerrain(float x, float z, float range, float spacing, Height height)
    {
        TerrainGrid grid;
        if (!std::isfinite(x)||!std::isfinite(z)||std::fabs(x)>1e8f||std::fabs(z)>1e8f||
            !std::isfinite(range)||range<=0||range>1e6f||!std::isfinite(spacing)||spacing<8) return grid;
        // BZ2 uses one sample per terrain cluster (4 grids, normally 4*8 m).
        // Coarsen exceptionally large scanners rather than overrun draw buffers.
        while (std::ceil(2*range/spacing)+5>65) spacing*=2;
        const int x0=static_cast<int>(std::floor((x-range)/spacing))-1;
        const int z0=static_cast<int>(std::floor((z-range)/spacing))-1;
        const int x1=static_cast<int>(std::ceil((x+range)/spacing))+1;
        const int z1=static_cast<int>(std::ceil((z+range)/spacing))+1;
        grid.columns=x1-x0+1; grid.rows=z1-z0+1;
        if (grid.columns>65||grid.rows>65||grid.columns<=0||grid.rows<=0) return {};
        grid.spacing=spacing;grid.centerX=x;grid.centerZ=z;grid.range=range;
        grid.nodes.resize(static_cast<size_t>(grid.columns)*grid.rows);
        for(int row=0;row<grid.rows;++row) for(int col=0;col<grid.columns;++col)
        {
            auto& n=grid.nodes[static_cast<size_t>(row)*grid.columns+col];
            n.x=(x0+col)*spacing;n.z=(z0+row)*spacing;
            // Cache a rectangular world-anchored patch. Its circle mask is
            // recalculated for the current player position on every draw.
            n.y=height(n.x,n.z);n.valid=std::isfinite(n.y)&&std::fabs(n.y)<1e6f;
        }
        return grid;
    }

    inline bool TerrainVisible(const TerrainNode& n,float x,float z,float range)
    {
        const float dx=n.x-x,dz=n.z-z;
        return n.valid&&dx*dx+dz*dz<range*range;
    }

    template<class Emit>
    size_t ForEachTerrainStrip(int columns,int rows,const std::vector<bool>& visible,Emit emit)
    {
        if(columns<=0||rows<=0||visible.size()!=static_cast<size_t>(columns)*rows)return 0;
        std::vector<size_t> run;run.reserve(static_cast<size_t>(columns>rows?columns:rows));
        size_t count=0;
        auto flush=[&](){if(run.size()>1){emit(run);++count;}run.clear();};
        for(int axis=0;axis<2;++axis)
        {
            const int outer=axis?columns:rows,inner=axis?rows:columns;
            for(int a=0;a<outer;++a)
            {
                for(int b=0;b<inner;++b)
                {
                    const size_t i=axis?static_cast<size_t>(b)*columns+a:static_cast<size_t>(a)*columns+b;
                    if(visible[i])run.push_back(i);else flush();
                }
                flush();
            }
        }
        return count;
    }

    inline std::array<float,3> TransformTerrain(const TerrainNode& n,
        const float* origin, const float* matrix)
    {
        const float x=n.x-origin[0],y=n.y-origin[1],z=n.z-origin[2];
        return {x*matrix[0]+y*matrix[1]+z*matrix[2],
                x*matrix[3]+y*matrix[4]+z*matrix[5],
                x*matrix[6]+y*matrix[7]+z*matrix[8]};
    }

    // The original Redux scanner sets a 14-frame palette pulse and decays it
    // by dt*20. Observe that trigger; BZ2 presentation lasts one simulation second.
    struct PulseClock
    {
        uintptr_t owner=0;
        float lastPing=0,start=-10000,lastTime=-10000;
        bool Update(uintptr_t player,float nativePing,float now)
        {
            if (!std::isfinite(now)||!std::isfinite(nativePing)) return false;
            if (player!=owner||now<lastTime) { owner=player;lastPing=0;start=-10000; }
            const bool triggered=nativePing>lastPing+0.25f;
            if (triggered)
            {
                float age=(14-nativePing)/20;
                if(age<0)age=0;
                if(age>.7f)age=.7f;
                start=now-age;
            }
            lastPing=nativePing;lastTime=now;
            return triggered;
        }
        float Phase(float now) const
        {
            const float p=now-start;
            return std::isfinite(p)&&p>=0&&p<1?p:-1;
        }
        uint32_t GridColor(float now,uint32_t rgb) const
        {
            const float p=Phase(now);
            const auto alpha=static_cast<uint32_t>(64+(p<0?0:112*(1-p)));
            return (rgb&kRgbMask)|(alpha<<24);
        }
    };
}
