// Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
// Opt-in native presentation. Selection visibility and command processing stay native.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "UI/InterfacePresentation.h"
#include "UI/Bz2HudGeometry.h"
#include "UI/HudOwnership.h"
#include "EntryDetour32.h"
#include "LuaHelpers.h"
#include "Util/BuildValidation.h"
#include "Util/FeatureQualification.h"
#include "Util/SehGuard.h"
#include "Util/EngineAddresses.generated.h"
#include <memory>
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace ExtraUtilities::Lua::InterfacePresentation
{
    using namespace ExtraUtilities::Bz2Hud;
    namespace A=EngineAddresses::InterfacePresentation;
    namespace
    {
        using Sprite=void(__cdecl*)(void*,void*,int,int,int,int,int,uint32_t,float);
        using Font=void(__cdecl*)(void*,void*,int,int,const char*);
        using SmallSprite=void(__cdecl*)(void*,int,int,int,uint32_t,float);
        using ScalePair=uint64_t(__cdecl*)();
        using Fill=void(__cdecl*)(void*,int,int,int,int,uint32_t,uint32_t);
        using Time=float(__cdecl*)();
        std::atomic<bool> active{false};
        thread_local unsigned commandDepth=0;
        std::unique_ptr<EntryDetour32> spriteHook,smallHook,fontHook,commandHook;
        SmallSprite originalSmall=nullptr;
        using Command=void(__thiscall*)(void*);
        Command originalCommand=nullptr;
        Sprite originalSprite=nullptr;
        Font originalFont=nullptr;
        constexpr uintptr_t fillAddr=A::Fill,timeAddr=A::Time,namesAddr=A::Names,countAddr=A::Count,rowsAddr=A::Rows,mouseAddr=A::Mouse;
        std::vector<std::string> names;
        std::map<std::string,int> ids;
        bool ready=false,loggedMarker=false,loggedCommand=false;

        void Rect(void* v,int x,int y,int w,int h,uint32_t color)
        {
            if(w>0 && h>0) reinterpret_cast<Fill>(fillAddr)(v,x,y,x+w,y+h,color,0);
        }
        void TriangleFill(void* v,const Triangle& t,uint32_t color)
        {
            const float ymin=std::min({t[0].y,t[1].y,t[2].y}),ymax=std::max({t[0].y,t[1].y,t[2].y});
            if(!std::isfinite(ymin)||!std::isfinite(ymax)||ymax-ymin>1024) return;
            for(int y=static_cast<int>(std::floor(ymin));y<=static_cast<int>(std::ceil(ymax));++y)
            {
                float xs[3]{};int count=0;
                for(int i=0;i<3;++i)
                {
                    const auto a=t[i],b=t[(i+1)%3];const float scan=y+.5f;
                    if((a.y<=scan && scan<b.y)||(b.y<=scan && scan<a.y)) xs[count++]=a.x+(scan-a.y)*(b.x-a.x)/(b.y-a.y);
                }
                if(count==2) Rect(v,static_cast<int>(std::floor(std::min(xs[0],xs[1]))),y,
                    std::max(1,static_cast<int>(std::ceil(std::max(xs[0],xs[1]))-std::floor(std::min(xs[0],xs[1])))),1,color);
            }
        }
        void Panel(void* v,int x,int y,int w,int h,float time,bool commands)
        {
            Rect(v,x,y,w,h,0xb8000815u);
            Rect(v,x,y,w,1,0xb81e5980u);Rect(v,x,y,1,h,0xb81e5980u);
            Rect(v,x+w-1,y,1,h,0xb81e5980u);Rect(v,x,y+h-1,w,1,0xb81e5980u);
            if(!commands) return;
            const auto* rows=reinterpret_cast<const int*>(rowsAddr);
            const auto* mouse=reinterpret_cast<const int*>(mouseAddr);
            for(int i=0;i<13;++i)
            {
                int left=rows[i*4],top=rows[i*4+1],right=rows[i*4+2],bottom=rows[i*4+3];
                if(top<y || bottom>y+h || right<=left || bottom<=top) continue;
                const bool hover=mouse[0]>=left && mouse[0]<=right && mouse[1]>=top && mouse[1]<=bottom;
                const auto alpha=static_cast<uint32_t>(hover?180+30*std::sin(time*6):180);
                Rect(v,left+2,top+1,right-left-4,bottom-top-2,(alpha<<24)|0x001426u);
                if(hover) Rect(v,left+2,top+1,2,bottom-top-2,0xff44b8ffu);
            }
        }
        bool ReplaceSprite(void* v,int id,int x,int y,int w,int h,uint32_t flags)
        {
            if(id<=0 || static_cast<size_t>(id)>=names.size() || w<=0 || h<=0 || w>8192 || h>8192) return false;
            const auto& name=names[id];
            const bool objective=name.rfind("object.",0)==0,target=name.rfind("target.",0)==0;
            const bool menu=name=="menuunderlay"||name=="smenuunderlay"||name=="fmenuunderlay";
            const bool info=name=="targetunderlay"||name=="stargetunderlay"||name=="ftargetunderlay";
            if(!objective && !target && !menu && !info) return false;
            if(flags&0x10000) x-=w/2; else if(flags&0x20000) x-=w;
            if(flags&0x40000) y-=h/2; else if(flags&0x80000) y-=h;
            if(flags&0x200000) {const auto* viewport=static_cast<const int*>(v);x+=viewport[2];y+=viewport[3];}
            const float now=reinterpret_cast<Time>(timeAddr)();
            if(!std::isfinite(now)) return false;
            if(menu||info)
            {
                Panel(v,x,y,w,h,now,menu);
                if(menu && !loggedCommand) {Logging::LogMessage("[BZ2-HUD] native command panel styled; input and labels retained\n");loggedCommand=true;}
            }
            else
            {
                uint32_t color=0xffffffffu;
                switch(name.back()) {case 'G':color=0xff00ff00u;break;case 'Y':color=0xffffff00u;break;case 'R':color=0xffff0000u;break;case 'B':color=0xff0078ffu;break;}
                const float scale=std::max(.5f,std::min(w,h)/64.f);
                const auto triangles=Marker(x+w*.5f,y+h*.5f,std::min(w,h)*.3f,scale,now,objective);
                for(int i=0;i<4;i+=(objective?1:2)) TriangleFill(v,triangles[i],color);
                if(!loggedMarker) {Logging::LogMessage("[BZ2-HUD] animated native-visible selection marker drawn\n");loggedMarker=true;}
            }
            return true;
        }
        bool TryReplace(void* v,int id,int x,int y,int w,int h,uint32_t flags) noexcept
        {
            __try{return ReplaceSprite(v,id,x,y,w,h,flags);}
            __except(Seh::Filter(GetExceptionCode())){active.store(false);return false;}
        }
        void __cdecl SpriteDraw(void* v,void* material,int id,int x,int y,int w,int h,uint32_t flags,float z)
        {
            if(active.load(std::memory_order_relaxed) && TryReplace(v,id,x,y,w,h,flags)) return;
            originalSprite(v,material,id,x,y,w,h,flags,z);
        }
        bool SpriteSize(int id,float scale,int& w,int& h,float& sx) noexcept
        {
            __try
            {
                if(id<=0 || static_cast<size_t>(id)>=names.size()) return false;
                const uint64_t bits=reinterpret_cast<ScalePair>(A::Scale)();
                float pair[2]{};std::memcpy(pair,&bits,sizeof(bits));
                if(!std::isfinite(pair[0]) || !std::isfinite(pair[1]) || pair[0]<=0 || pair[1]<=0) return false;
                const auto* rect=reinterpret_cast<const short*>(A::Rects+static_cast<uintptr_t>(id)*36);
                scale=scale>0?scale:1;
                sx=pair[0];w=static_cast<int>(rect[2]*pair[0]*scale);h=static_cast<int>(rect[3]*pair[1]*scale);
                return true;
            }
            __except(Seh::Filter(GetExceptionCode())){return false;}
        }
        void __cdecl SmallDraw(void* v,int id,int x,int y,uint32_t flags,float scale)
        {
            int w=0,h=0;float sx=1;
            if(active.load(std::memory_order_relaxed) && SpriteSize(id,scale,w,h,sx) && TryReplace(v,id,x,y,w,h,flags)) return;
            originalSmall(v,id,x,y,flags,scale);
        }
        void __cdecl FontDraw(void* font,void* v,int x,int y,const char* text)
        {
            if(active.load(std::memory_order_relaxed) && commandDepth && text)
            {
                if(const char* icon=CommandIcon(text))
                {
                    const auto found=ids.find(icon);
                    if(found!=ids.end())
                    {
                        // Glyph size follows the native row spacing; retain all native labels.
                        const auto* row=reinterpret_cast<const int*>(rowsAddr);
                        const int size=std::clamp(row[3]-row[1]-2,8,48);
                        int iw=0,ih=0;float sx=1;
                        if(SpriteSize(found->second,1,iw,ih,sx) && iw>0)
                            originalSmall(v,found->second,x,y,0,static_cast<float>(size)/iw);
                        x+=size+2;
                    }
                }
            }
            originalFont(font,v,x,y,text);
        }
        bool ReadName(unsigned index,char* name) noexcept
        {
            __try
            {
                std::memcpy(name,reinterpret_cast<const char*>(namesAddr)+index*32,32);
                return true;
            }
            __except(Seh::Filter(GetExceptionCode())){return false;}
        }
        bool ReadNames()
        {
            unsigned count=0;SIZE_T read=0;
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(countAddr),&count,sizeof(count),&read) || read!=sizeof(count) || count==0 || count>4096) return false;
            for(unsigned i=0;i<count;++i)
            {
                char name[33]{};if(!ReadName(i,name)) return false;
                names.emplace_back(name);ids[name]=static_cast<int>(i);
            }
            return true;
        }
    }
    bool Enabled() noexcept {return active.load(std::memory_order_relaxed);}
    void BeginCommands() noexcept {++commandDepth;}
    void EndCommands() noexcept {if(commandDepth) --commandDepth;}

    namespace
    {
        void __fastcall CommandDraw(void* self,void*)
        {
            BeginCommands();
            __try {originalCommand(self);}
            __finally {EndCommands();}
        }
        bool Configure(bool enabled)
        {
            if(!enabled)
            {
                active.store(false);
                if(spriteHook) spriteHook->SetStatus(false);
                if(smallHook) smallHook->SetStatus(false);
                if(fontHook) fontHook->SetStatus(false);
                if(commandHook) commandHook->SetStatus(false);
                HudOwnership::Update(HudOwnership::Commands,false);
                return true;
            }
            if(!RuntimeGate::IsSupported()) return false;
            if(!ready)
            {
                if(!QualifyFeature("Interface presentation ",7)) return false;
                names.clear();ids.clear();if(!ReadNames()) return false;
                if(!spriteHook) spriteHook=std::make_unique<EntryDetour32>(A::Sprite,reinterpret_cast<void*>(SpriteDraw),14,BasicPatch::Status::INACTIVE,
                    std::vector<uint8_t>{0x55,0x8b,0xec,0x81,0xec,0x98,0,0,0,0xa1,0,0x70,0x8e,0});
                if(!fontHook) fontHook=std::make_unique<EntryDetour32>(A::Font,reinterpret_cast<void*>(FontDraw),13,BasicPatch::Status::INACTIVE,
                    std::vector<uint8_t>{0x55,0x8b,0xec,0x83,0xec,0x1c,0xc7,0x45,0xf8,0,0,0,0});
                if(!smallHook) smallHook=std::make_unique<EntryDetour32>(A::Small,reinterpret_cast<void*>(SmallDraw),14,BasicPatch::Status::INACTIVE,
                    std::vector<uint8_t>{0x55,0x8b,0xec,0x81,0xec,0xa8,0,0,0,0xa1,0,0x70,0x8e,0});
                if(!commandHook) commandHook=std::make_unique<EntryDetour32>(A::Command,reinterpret_cast<void*>(CommandDraw),9,BasicPatch::Status::INACTIVE,
                    std::vector<uint8_t>{0x55,0x8b,0xec,0x81,0xec,0x38,0x06,0,0});
                if(!spriteHook->PrepareTrampoline() || !smallHook->PrepareTrampoline() || !fontHook->PrepareTrampoline() || !commandHook->PrepareTrampoline()) return false;
                originalSprite=reinterpret_cast<Sprite>(spriteHook->GetTrampoline());
                originalSmall=reinterpret_cast<SmallSprite>(smallHook->GetTrampoline());
                originalFont=reinterpret_cast<Font>(fontHook->GetTrampoline());
                originalCommand=reinterpret_cast<Command>(commandHook->GetTrampoline());
                ready=true;
            }
            // EXU renders independently. A capable OpenShim only yields palette
            // and idle-fade policy; an older provider must not defeat the API.
            if(HudOwnership::HasProtocol()) HudOwnership::Update(HudOwnership::Commands,true);
            for(auto* hook:{spriteHook.get(),smallHook.get(),fontHook.get(),commandHook.get()})
            {
                hook->SetStatus(true);
                if(!hook->IsActive()) {Configure(false);return false;}
            }
            active.store(true);
            Logging::LogMessage("[BZ2-HUD] EXU command/target presentation enabled; sprites=%zu",names.size());
            return true;
        }
    }
    int SetEnabled(lua_State* L)
    {
        const bool accepted=Configure(CheckBool(L,1));
        lua_pushboolean(L,accepted);return 1;
    }
    void Reset() noexcept
    {
        Configure(false);names.clear();ids.clear();ready=false;
        loggedMarker=loggedCommand=false;commandDepth=0;
    }
}
