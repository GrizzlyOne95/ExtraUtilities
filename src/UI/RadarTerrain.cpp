// Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
#include "Radar.h"
#include "RadarTerrainGeometry.h"
#include "InlinePatch.h"
#include "LuaHelpers.h"
#include "Ogre/OgreMaterialShim.h"
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
        using TexturedBegin=void(__cdecl*)(const void*,float);
        using TexturedFan=void(__cdecl*)(void*,const float*,int,uint32_t);
        using TexturedEnd=void(__cdecl*)();
        using RenderLive=int(__cdecl*)(int);
        using ResourceLoad=void(__thiscall*)(::Ogre::Material*,bool);
        using namespace RadarPresentation;
        struct Snapshot
        {
            float range=0,origin[3]{},matrix[9]{},ping=0,time=0;
            uintptr_t player=0;
        };
        TerrainGrid terrain;
        // Render-thread scratch, cleared per frame so capacity is reused.
        std::vector<std::array<int,2>> projected,run;
        std::vector<bool> visible;
        PulseClock pulse;
        float terrainAt=-10000,spacing=32;
        uint32_t gridRgb=0x0078ff;
        bool active=false,faulted=false,loggedGrid=false,loggedPulse=false,loggedHeight=false;
        size_t frames=0;
        std::unique_ptr<InlinePatch> terrainPatch,heightPatch,compassPatch;
        constexpr size_t kMeshFloats=5,kMeshMaxTriangles=4096;
        // Each coordinate limit keeps the rotated length under 3 ranges, the
        // minimum perspective distance, so no vertex can reach the divide by zero.
        constexpr float kMeshMaxCoordinate=1.7f;
        std::vector<float> meshVertices;
        float meshExtent=0;
        // Heap-held and never destroyed: its release calls into Ogre, which may
        // already be gone when this DLL's static destructors run.
        ::Ogre::MaterialReference* const meshMaterial=new ::Ogre::MaterialReference{};
        bool meshFaulted=false,loggedMesh=false;

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
                if(newOwner||f.time<terrainAt||f.time-terrainAt>=.1f||
                    terrain.range!=f.range||std::fabs(terrain.centerX-f.origin[0])>=spacing||
                    std::fabs(terrain.centerZ-f.origin[2])>=spacing)
                {
                    terrain=SampleTerrain(f.origin[0],f.origin[2],f.range,spacing,ReadHeight);
                    terrainAt=f.time;
                }
                if(terrain.nodes.empty())return false;
                projected.assign(terrain.nodes.size(),{});
                visible.assign(terrain.nodes.size(),false);
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
                run.clear();run.reserve(65);
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
            catch(...) { faulted=true;return false; }
        }

        // Same submit sequence as the native sprite path: live check, then one
        // begin, fan and end per primitive. The batch only flushes on a material
        // change, so per-triangle calls stay in one draw.
        bool DrawMeshTriangles(const Snapshot& f) noexcept
        {
            const float depth=*reinterpret_cast<const float*>(A::drawDepth);
            if(!(depth>0)||!reinterpret_cast<RenderLive>(A::RenderLive)(0))return false;
            const float* in=meshVertices.data();
            const size_t triangles=meshVertices.size()/(kMeshFloats*3);
            for(size_t t=0;t<triangles;++t)
            {
                float out[18];
                for(int k=0;k<3;++k,in+=kMeshFloats)
                {
                    const float x=in[0]*f.range,y=in[1]*f.range,z=in[2]*f.range;
                    const float rotated[3]={x*f.matrix[0]+y*f.matrix[1]+z*f.matrix[2],
                        x*f.matrix[3]+y*f.matrix[4]+z*f.matrix[5],
                        x*f.matrix[6]+y*f.matrix[7]+z*f.matrix[8]};
                    float* v=out+k*6;
                    if(!ProjectPointExact(rotated,f.range,v[0],v[1]))return false;
                    v[2]=1/depth;v[3]=in[3];v[4]=in[4];v[5]=1;
                }
                reinterpret_cast<TexturedBegin>(A::TexturedBegin)(meshMaterial,depth);
                reinterpret_cast<TexturedFan>(A::TexturedFan)(nullptr,out,3,0xffffffffu);
                reinterpret_cast<TexturedEnd>(A::TexturedEnd)();
            }
            return true;
        }

        bool DrawMesh(void* self) noexcept
        {
            Snapshot f;
            if(meshVertices.empty()||meshFaulted||!ReadSnapshot(self,f))return false;
            __try{return DrawMeshTriangles(f);}
            __except(Seh::Filter(GetExceptionCode())){meshFaulted=true;return false;}
        }

        bool DrawDetail(void* self) noexcept
        {
            __try{return DrawDetailCpp(self);}
            __except(Seh::Filter(GetExceptionCode())){faulted=true;return false;}
        }

        void __fastcall DrawTerrain(void* self,void*)
        {
            // SetStatus only writes code when the state changes.
            if(compassPatch&&(!active||faulted))compassPatch->SetStatus(false);
            if(active&&!faulted)
            {
                // Match the stock terrain draw's temporary depth layer.
                auto* depth=reinterpret_cast<float*>(A::drawDepth);
                const float saved=*depth;
                bool drawn=false,meshDrawn=false;
                // Before the mesh, so blips and terrain scale with the dish.
                if(!meshVertices.empty()&&!meshFaulted)FitProjection(meshExtent);
                __try { *depth=saved+.03f;meshDrawn=DrawMesh(self);drawn=DrawDetail(self); }
                __finally { *depth=saved; }
                // The baked letters on the mesh replace the compass sprites, which
                // draw after terrain, so a frame without the mesh keeps them.
                if(compassPatch)compassPatch->SetStatus(meshDrawn);
                if(meshDrawn&&!loggedMesh)
                {
                    Logging::LogMessage("[EXU::Radar] BZ2 radar mesh drawn: triangles=%zu",meshVertices.size()/(kMeshFloats*3));
                    loggedMesh=true;
                }
                if(meshFaulted&&!meshVertices.empty())
                {
                    Logging::LogMessage("[EXU::Radar] BZ2 radar mesh failed; native compass retained");
                    meshVertices.clear();
                }
                if(drawn)return;
                // A frame with nothing to draw (small scanner range) keeps native
                // terrain for that frame; only a fault ends the overlay.
                if(faulted)Logging::LogMessage("[EXU::Radar] BZ2 terrain failed; native terrain retained");
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
        if(compassPatch)compassPatch->SetStatus(false);
    }

    void ResetTerrainState() noexcept
    {
        DisableTerrain();terrain={};pulse={};terrainAt=-10000;faulted=false;
        loggedGrid=loggedPulse=loggedHeight=false;frames=0;
        meshVertices.clear();meshMaterial->Reset();meshFaulted=loggedMesh=false;
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

    int SetMesh(lua_State* L)
    {
        const auto clear=[]
        {
            if(compassPatch)compassPatch->SetStatus(false);
            meshVertices.clear();meshMaterial->Reset();meshFaulted=loggedMesh=false;
        };
        luaL_checktype(L,1,LUA_TBOOLEAN);
        if(!lua_toboolean(L,1)){clear();lua_pushboolean(L,1);return 1;}
        const char* materialName=luaL_checkstring(L,2);
        luaL_checktype(L,3,LUA_TTABLE);
        const size_t count=lua_objlen(L,3);
        if(count==0||count%(kMeshFloats*3)!=0||count>kMeshMaxTriangles*kMeshFloats*3)
            return luaL_argerror(L,3,"vertices must be 1..4096 triangles of x,y,z,u,v");
        // Lua errors unwind without destructors; nothing owning runs before the
        // parse finishes, as in StaticGeometry.
        auto* parsed=static_cast<float*>(lua_newuserdata(L,count*sizeof(float)));
        for(size_t i=0;i<count;++i)
        {
            lua_rawgeti(L,3,static_cast<int>(i+1));
            const double value=lua_isnumber(L,-1)?lua_tonumber(L,-1):NAN;
            lua_pop(L,1);
            const bool position=i%kMeshFloats<3;
            if(!std::isfinite(value)||(position&&std::fabs(value)>kMeshMaxCoordinate))
                return luaL_argerror(L,3,"each vertex needs finite numbers, with positions within 1.7 ranges");
            parsed[i]=static_cast<float>(value);
        }
        clear();
        // The call-site bytes prove this is the compass loop. The sprite entry
        // itself is not matched, because interface presentation may detour it.
        if(!active||!IsPresentationActive()||
            !SignatureResolver::MatchBytes(A::TexturedBegin,{0x55,0x8b,0xec,0x6a,0x01,0x8b,0x0d,0x34,0x9b,0xb7,0x02})||
            !SignatureResolver::MatchBytes(A::TexturedFan,{0x55,0x8b,0xec,0x83,0xec,0x40,0xa1,0x00,0x70,0x8e,0x00})||
            !SignatureResolver::MatchBytes(A::TexturedEnd,{0x55,0x8b,0xec,0x8b,0x0d,0x34,0x9b,0xb7,0x02})||
            !SignatureResolver::MatchBytes(A::RenderLive,{0x55,0x8b,0xec,0x6a,0xff,0x68,0x00,0xf9,0x84,0x00}))
        {
            Logging::LogMessage("[EXU::Radar] BZ2 radar mesh declined: inactive presentation or unexpected textured-draw bytes");
            lua_pushboolean(L,0);return 1;
        }
        static OgreDll::OgreProc<ResourceLoad> resourceLoad{"?load@Resource@Ogre@@UAEX_N@Z"};
        bool loaded=false;
        Seh::Guard("Radar::SetMesh",[&]
        {
            const std::string name(materialName),group("Autodetect");
            *meshMaterial=::Ogre::GetMaterialByName(::Ogre::GetMaterialManagerSingletonPtr(),name,group);
            if(meshMaterial->isNull()||!resourceLoad)return;
            // The sprite path never needs this: its materials load with the HUD.
            resourceLoad.Get()(meshMaterial->get(),false);
            loaded=true;
        });
        if(!loaded)
        {
            Logging::LogMessage("[EXU::Radar] BZ2 radar mesh declined: material %s did not load",materialName);
            clear();lua_pushboolean(L,0);return 1;
        }
        // Skip the loop rather than its sprite call, which OpenShim's HUD fade redirects.
        if(!compassPatch)compassPatch=std::make_unique<InlinePatch>(A::CompassLoopExit,std::vector<uint8_t>{0xe9,0x8c,0x00,0x00,0x00,0x90},BasicPatch::Status::INACTIVE,std::vector<uint8_t>{0x0f,0x83,0x8b,0x00,0x00,0x00});
        compassPatch->SetStatus(true);
        if(!compassPatch->IsActive())
        {
            Logging::LogMessage("[EXU::Radar] BZ2 radar mesh declined: compass loop did not patch");
            clear();lua_pushboolean(L,0);return 1;
        }
        meshVertices.assign(parsed,parsed+count);
        meshExtent=0;
        for(size_t i=0;i<count;i+=kMeshFloats)meshExtent=(std::max)(meshExtent,std::hypot(parsed[i],parsed[i+2]));
        Logging::LogMessage("[EXU::Radar] BZ2 radar mesh enabled: material=%s triangles=%zu",materialName,count/(kMeshFloats*3));
        lua_pushboolean(L,1);return 1;
    }
}
