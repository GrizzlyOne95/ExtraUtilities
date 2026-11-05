// Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
#include "Radar.h"
#include "RadarTerrainGeometry.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/SehGuard.h"
#include "Util/SignatureResolver.h"
#include "Util/Logging.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>

namespace ExtraUtilities::Lua::Radar
{
    namespace
    {
        namespace A=EngineAddresses::Radar;
        using Strip=void(__cdecl*)(const int*,int,uint32_t);
        using Line=void(__cdecl*)(void*,int,int,int,int,uint32_t,int);
        using OriginalTerrain=void(__thiscall*)(void*);
        using Height=double(__cdecl*)(double,double);
        using namespace RadarPresentation;
        struct Snapshot
        {
            float range=0,origin[3]{},matrix[9]{},ping=0,time=0;
            uintptr_t player=0;
        };
        TerrainGrid terrain;
        PulseClock pulse;
        float terrainAt=-10000,spacing=32;
        uint32_t gridRgb=0x0078ff;
        bool active=false,faulted=false,loggedGrid=false,loggedPulse=false,loggedHeight=false;
        size_t frames=0;
        std::unique_ptr<InlinePatch> terrainPatch,heightPatch;

        bool ReadSnapshot(void* self,Snapshot& f) noexcept
        {
            __try
            {
                auto* p=static_cast<const uint8_t*>(self);
                f.range=*reinterpret_cast<const float*>(p+0x10);
                f.player=*reinterpret_cast<const uintptr_t*>(p+0xc);
                std::memcpy(f.origin,p+0x20,sizeof(f.origin));
                std::memcpy(f.matrix,p+0x30,sizeof(f.matrix));
                f.ping=*reinterpret_cast<const float*>(p+0x70);
                f.time=*reinterpret_cast<const float*>(EngineAddresses::Time::SimTimeSeconds);
            }
            __except(Seh::Filter(GetExceptionCode())) { return false; }
            if (!f.player||!std::isfinite(f.range)||f.range<=0||f.range>1e6f||
                !std::isfinite(f.time)||!std::isfinite(f.ping)) return false;
            for(float v:f.origin)if(!std::isfinite(v)||std::fabs(v)>1e8f)return false;
            for(float v:f.matrix)if(!std::isfinite(v)||std::fabs(v)>2)return false;
            return true;
        }

        float ReadHeight(float x,float z) noexcept
        {
            __try
            {
                return static_cast<float>(reinterpret_cast<Height>(EngineAddresses::Terrain::HeightAt)(x,z));
            }
            __except(Seh::Filter(GetExceptionCode())) { return NAN; }
        }

        void Submit(const std::vector<std::array<int,2>>& points,uint32_t color)
        {
            if(points.size()>1)reinterpret_cast<Strip>(A::DrawLineStrip)(points[0].data(),static_cast<int>(points.size()),color);
        }

        bool DrawDetailCpp(void* self)
        {
            try
            {
                Snapshot f;
                if(!ReadSnapshot(self,f))return false;
                const bool newOwner=f.player!=pulse.owner;
                pulse.Update(f.player,f.ping,f.time);
                // Height samples are world-anchored and cached for 100 ms;
                // transform, circular mask and scan alpha use the current frame.
                if(newOwner||terrain.nodes.empty()||f.time<terrainAt||f.time-terrainAt>=.1f||
                    terrain.range!=f.range||std::fabs(terrain.centerX-f.origin[0])>=spacing||
                    std::fabs(terrain.centerZ-f.origin[2])>=spacing)
                {
                    terrain=SampleTerrain(f.origin[0],f.origin[2],f.range,spacing,ReadHeight);
                    terrainAt=f.time;
                }
                if(terrain.nodes.empty())return false;
                std::vector<std::array<int,2>> projected(terrain.nodes.size());
                std::vector<bool> visible(terrain.nodes.size());
                size_t valid=0;
                for(size_t i=0;i<terrain.nodes.size();++i)
                {
                    const auto& n=terrain.nodes[i];
                    visible[i]=TerrainVisible(n,f.origin[0],f.origin[2],f.range);
                    if(!visible[i])continue;
                    const auto p=TransformTerrain(n,f.origin,f.matrix);
                    const uint64_t pixel=ProjectPoint(p.data(),f.range);
                    projected[i]={static_cast<int32_t>(pixel),static_cast<int32_t>(pixel>>32)};
                    ++valid;
                }
                if(valid<2)return false;
                const uint32_t color=pulse.GridColor(f.time,gridRgb);
                std::vector<std::array<int,2>> run;run.reserve(65);
                const size_t strips=ForEachTerrainStrip(terrain.columns,terrain.rows,visible,
                    [&](const std::vector<size_t>& indices)
                    {
                        run.clear();for(size_t i:indices)run.push_back(projected[i]);
                        Submit(run,color);
                    });
                if(strips==0)return false;
                run.clear();
                const float phase=pulse.Phase(f.time);
                if(phase>=0)
                {
                    // BZ2 Graphic_Circle is a SCREEN-space circle centered on
                    // RadarCam's viewport: 32 segments, radius (1-ping)*20 px.
                    // It is not a second terrain-space radar-range ring.
                    const int cx=*BZR::Radar::cockpitWireframeCenterX;
                    const int cy=*BZR::Radar::cockpitWireframeCenterY;
                    const float radius=phase*20;
                    for(int i=0;i<=32;++i)
                    {
                        const float a=i*6.28318530718f/32;
                        run.push_back({cx+static_cast<int>(std::lround(std::cos(a)*radius)),
                                      cy+static_cast<int>(std::lround(std::sin(a)*radius))});
                    }
                    Submit(run,(static_cast<uint32_t>((1-phase)*255)<<24)|kRgbMask);
                    if(!loggedPulse){Logging::LogMessage("[EXU::Radar] BZ2 scan ring drawn (32 segments, 20px, native scanner trigger)");loggedPulse=true;}
                }
                ++frames;
                if(!loggedGrid)
                {
                    Logging::LogMessage("[EXU::Radar] BZ2 terrain drawn: samples=%zu strips=%zu spacing=%.1f color=%06X",valid,strips,terrain.spacing,gridRgb);
                    loggedGrid=true;
                }
                return true;
            }
            catch(...) { return false; }
        }

        bool DrawDetail(void* self) noexcept
        {
            __try{return DrawDetailCpp(self);}
            __except(Seh::Filter(GetExceptionCode())){return false;}
        }

        void __fastcall DrawTerrain(void* self,void*)
        {
            if(active&&!faulted)
            {
                // Match the stock terrain draw's temporary depth layer.
                auto* depth=reinterpret_cast<float*>(A::drawDepth);
                const float saved=*depth;
                bool drawn=false;
                __try { *depth=saved+.03f;drawn=DrawDetail(self); }
                __finally { *depth=saved; }
                if(drawn)return;
                faulted=true;
                Logging::LogMessage("[EXU::Radar] BZ2 terrain failed; native terrain retained");
            }
            reinterpret_cast<OriginalTerrain>(A::DrawTerrain)(self);
        }

        void __cdecl DrawHeight(void* viewport,int x1,int y1,int x2,int y2,uint32_t color,int mode)
        {
            if(active&&!faulted)
            {
                color=(color&0xff000000u)|kRgbMask;
                if(!loggedHeight){Logging::LogMessage("[EXU::Radar] BZ2 white height marker drawn from native visible contact");loggedHeight=true;}
            }
            reinterpret_cast<Line>(A::DrawLine)(viewport,x1,y1,x2,y2,color,mode);
        }

        std::unique_ptr<InlinePatch> Redirect(uintptr_t address,uintptr_t destination,std::vector<uint8_t> expected)
        {
            auto payload=expected;
            const uint32_t rel=static_cast<uint32_t>(destination-(address+5));
            std::memcpy(payload.data()+1,&rel,4);
            return std::make_unique<InlinePatch>(address,std::move(payload),BasicPatch::Status::INACTIVE,std::move(expected));
        }
    }

    void DisableTerrain() noexcept
    {
        active=false;
        if(terrainPatch)terrainPatch->SetStatus(false);
        if(heightPatch)heightPatch->SetStatus(false);
    }

    void ResetTerrainState() noexcept
    {
        DisableTerrain();terrain={};pulse={};terrainAt=-10000;faulted=false;
        loggedGrid=loggedPulse=loggedHeight=false;frames=0;
    }

    int SetTerrain(lua_State* L)
    {
        luaL_checktype(L,1,LUA_TBOOLEAN);
        if(!lua_toboolean(L,1)){ResetTerrainState();lua_pushboolean(L,1);return 1;}
        const double requested=luaL_optnumber(L,2,32);
        const double rgb=luaL_optnumber(L,3,0x0078ff);
        if(!std::isfinite(requested)||requested<8||requested>256||
            !std::isfinite(rgb)||rgb<0||rgb>kRgbMask||std::floor(rgb)!=rgb)
            return luaL_error(L,"radar spacing must be 8..256 metres; color must be a 24-bit RGB integer");
        if(!RuntimeGate::IsSupported()||!IsPresentationActive()) {lua_pushboolean(L,0);return 1;}
        if(!SignatureResolver::MatchBytes(A::DrawLineStrip,{0x55,0x8b,0xec,0x83,0xec,0x38,0xa1,0x00,0x70,0x8e,0x00})||
            !SignatureResolver::MatchBytes(A::DrawLine,{0x55,0x8b,0xec,0x8b,0x45,0x20,0x50,0x8b,0x4d,0x1c,0x51})||
            !SignatureResolver::MatchBytes(A::DrawTerrain,{0x55,0x8b,0xec,0x83,0xec,0x08,0x89,0x4d,0xfc}))
        {lua_pushboolean(L,0);return 1;}
        if(!terrainPatch)terrainPatch=Redirect(A::TerrainDrawCall,reinterpret_cast<uintptr_t>(&DrawTerrain),{0xe8,0xe7,0xfd,0xff,0xff});
        if(!heightPatch)heightPatch=Redirect(A::HeightDrawCall,reinterpret_cast<uintptr_t>(&DrawHeight),{0xe8,0x38,0x53,0x1f,0x00});
        ResetTerrainState();
        terrainPatch->SetStatus(true);heightPatch->SetStatus(true);
        if(!terrainPatch->IsActive()||!heightPatch->IsActive()) {DisableTerrain();lua_pushboolean(L,0);return 1;}
        spacing=static_cast<float>(requested);gridRgb=static_cast<uint32_t>(rgb);active=true;
        Logging::LogMessage("[EXU::Radar] BZ2 terrain style enabled: spacing=%.1f RGB=%06X",spacing,gridRgb);
        lua_pushboolean(L,1);return 1;
    }
}
