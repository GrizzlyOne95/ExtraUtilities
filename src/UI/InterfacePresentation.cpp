// Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
// Opt-in native presentation. Selection visibility and command processing stay native.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "UI/InterfacePresentation.h"
#include "UI/Bz2HudGeometry.h"
#include "UI/HudOwnership.h"
#include "EntryDetour32.h"
#include "InlinePatch.h"
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
        thread_local unsigned commandDepth=0,drawnRows=0;
        thread_local void* commandSelf=nullptr;
        std::unique_ptr<EntryDetour32> spriteHook,smallHook,fontHook,commandHook;
        std::unique_ptr<InlinePatch> selectionPatch;
        SmallSprite originalSmall=nullptr;
        using Command=void(__thiscall*)(void*);
        Command originalCommand=nullptr;
        Sprite originalSprite=nullptr;
        Font originalFont=nullptr;
        constexpr uintptr_t fillAddr=A::Fill,timeAddr=A::Time,namesAddr=A::Names,countAddr=A::Count,rowsAddr=A::Rows,mouseAddr=A::Mouse;
        // colorize.dds: opaque bar edge, alpha 100 interior, under COMMANDTEXT alpha 127.
        constexpr uint32_t kRowEdge=0x7f000000u,kRowInterior=0x32000000u;
        std::vector<std::string> names;
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
        void Panel(void* v,int x,int y,int w,int h)
        {
            Rect(v,x,y,w,h,0xb8000815u);
            Rect(v,x,y,w,1,0xb81e5980u);Rect(v,x,y,1,h,0xb81e5980u);
            Rect(v,x+w-1,y,1,h,0xb81e5980u);Rect(v,x,y+h-1,w,1,0xb81e5980u);
        }
        // Fill coordinates are inclusive (FUN_0068af70:8-9), so one scanline never overlaps the next.
        void Span(void* v,int x1,int y,int x2,uint32_t color)
        {
            if(x2>=x1) reinterpret_cast<Fill>(fillAddr)(v,x1,y,x2,y,color,0);
        }
        // One bar in the shape of colorize.dds: a brighter edge around a faint interior.
        void Bar(void* v,int left,int top,int right,int bottom,const CommandRowColors& colors,bool roundLeft,bool roundRight)
        {
            const int height=bottom-top+1;
            for(int line=0;line<height;++line)
            {
                const int inset=RoundedInset(line,height);
                const int edge=std::max({1,RoundedInset(line-1,height)-inset,RoundedInset(line+1,height)-inset});
                const uint32_t rgb=LerpColor(colors.background,colors.gradient,height>1?static_cast<float>(line)/(height-1):0);
                int x1=left+(roundLeft?inset:0),x2=right-(roundRight?inset:0);
                const int y=top+line;
                if(line==0 || line==height-1) {Span(v,x1,y,x2,kRowEdge|rgb);continue;}
                if(roundLeft) {Span(v,x1,y,x1+edge-1,kRowEdge|rgb);x1+=edge;}
                if(roundRight) {Span(v,x2-edge+1,y,x2,kRowEdge|rgb);x2-=edge;}
                Span(v,x1,y,x2,kRowInterior|rgb);
            }
        }
        // Draws a populated row's bars when its native label draws. Sits behind the
        // gauges (+.01) and text, at the depth of the native selection fill.
        void TryDrawRow(void* v,int x,int y) noexcept
        {
            auto* depth=reinterpret_cast<float*>(A::Depth);
            float saved=0;bool raised=false;
            __try
            {
                const auto* rows=reinterpret_cast<const int*>(rowsAddr);
                const auto* labels=reinterpret_cast<const int*>(A::LabelPoints);
                for(int i=0;i<13;++i)
                {
                    const int left=rows[i*4],top=rows[i*4+1],rowRight=rows[i*4+2],right=rowRight-*reinterpret_cast<const int*>(A::RowSideWidth),bottom=rows[i*4+3];
                    if(labels[i*2]!=x || y<top || y>bottom || ((drawnRows>>i)&1)) continue;
                    drawnRows|=1u<<i;
                    if(right<=left || bottom-top>=128) return;
                    const uint32_t rowState=*reinterpret_cast<const uint32_t*>(static_cast<const char*>(commandSelf)+0x1030+i*0x18);
                    const int cursor=*reinterpret_cast<const int*>(static_cast<const char*>(commandSelf)+0x1198);
                    const auto* mouse=reinterpret_cast<const int*>(mouseAddr);
                    const bool hover=mouse[0]>=left && mouse[0]<=rowRight && mouse[1]>=top && mouse[1]<=bottom;
                    const auto colors=RowColors(rowState==*reinterpret_cast<const uint32_t*>(A::Current),hover||cursor==i,
                        rowState==*reinterpret_cast<const uint32_t*>(A::Disabled));
                    const int keyRight=left+*reinterpret_cast<const int*>(A::KeyWidth);
                    saved=*depth;*depth=saved+.03f;raised=true;
                    if(keyRight>left) Bar(v,left,top,keyRight-1,bottom,colors,true,false);
                    Bar(v,(keyRight+labels[i*2])/2,top,right,bottom,colors,false,true);
                    *depth=saved;raised=false;
                    return;
                }
            }
            __except(Seh::Filter(GetExceptionCode()))
            {
                if(raised) *depth=saved;
                active.store(false);
            }
        }
        // Replaces the white selection fill; the selected row is colored by its bars.
        void __cdecl SelectionFill(void* v,int x1,int y1,int x2,int y2,uint32_t color,uint32_t mode)
        {
            if(!active.load(std::memory_order_relaxed)) reinterpret_cast<Fill>(A::Fill)(v,x1,y1,x2,y2,color,mode);
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
            if(menu) return true; // BZ2's command rows float without a backing panel.
            if(info) Panel(v,x,y,w,h);
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
            if(active.load(std::memory_order_relaxed) && commandDepth==1 && text) TryDrawRow(v,x,y);
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
                names.emplace_back(name);
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
            commandSelf=self;drawnRows=0;
            if(!loggedCommand && active.load(std::memory_order_relaxed)) {Logging::LogMessage("[BZ2-HUD] native command rows styled; input and labels retained\n");loggedCommand=true;}
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
                if(selectionPatch) selectionPatch->SetStatus(false);
                HudOwnership::Update(HudOwnership::Commands,false);
                return true;
            }
            if(!RuntimeGate::IsSupported()) return false;
            if(!ready)
            {
                if(!QualifyFeature("Interface presentation ",7)) return false;
                names.clear();if(!ReadNames()) return false;
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
                if(!selectionPatch)
                {
                    const std::vector<uint8_t> expected{0xe8,0xda,0xa1,0x1e,0x00};
                    auto payload=expected;
                    const uint32_t rel=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SelectionFill)-(A::SelectionFillCall+5));
                    std::memcpy(payload.data()+1,&rel,4);
                    selectionPatch=std::make_unique<InlinePatch>(A::SelectionFillCall,std::move(payload),BasicPatch::Status::INACTIVE,expected);
                }
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
            selectionPatch->SetStatus(true);
            if(!selectionPatch->IsActive()) {Configure(false);return false;}
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
        Configure(false);names.clear();ready=false;
        loggedMarker=loggedCommand=false;commandDepth=0;
    }
}
