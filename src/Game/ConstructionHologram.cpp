// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Game/ConstructionHologram.h"
#include "Game/ConstructionPreviewCore.h"
#include "Ogre/OgrePreviewModel.h"
#include "Ogre/OgreGibRuntime.h"
#include "EntryDetour32.h"
#include "LuaHelpers.h"
#include "Util/FeatureQualification.h"
#include "Util/SehGuard.h"
#include <map>
#include <string>
#include <cstring>
#include <cctype>

namespace ExtraUtilities::Lua::ConstructionHologram
{
    namespace
    {
        namespace C=ConstructionPreviewCore;
        namespace A=EngineAddresses::ConstructionPreview;
        struct Snapshot { BZR::handle owner=0; C::State state=C::State::None; C::Vector position{}; C::Quaternion rotation{}; char odf[9]{}; };
        struct Mesh { std::string name; bool failed=false; };
        struct State
        {
            bool enabled=false, faulted=false;
            Snapshot pending;
            std::map<std::string,Mesh> meshes;
            std::string prefix;
            EntryDetour32* hook=nullptr;
            void* root=nullptr;
            OgrePreviewModel::Model model;
        };
        State& S() {static State previewState; return previewState;}
        using GetCommandFn=int(__thiscall*)(void*, const void*);
        GetCommandFn original=nullptr;
        bool Read(void* self,int result,Snapshot& s) noexcept
        {
            __try
            {
                if ((result!=8 && result!=9) || !BZR::GameObject::IsLiveArenaObject(self)) return false;
                const auto* rig=static_cast<const unsigned char*>(self);
                if (*reinterpret_cast<const int*>(rig+0x228)!=0) return false;
                const auto* cls=*reinterpret_cast<const unsigned char* const*>(rig+0x370);
                if (!cls) return false;
                const auto* display=reinterpret_cast<const std::uintptr_t*>(A::Display);
                s.state=C::FromSprite(display[0x68/4],display[0x6c/4],display[0x70/4],display[0x74/4]);
                if (s.state==C::State::None) return false;
                s.owner=BZR::GameObject::GetHandle(static_cast<BZR::GameObject*>(self));
                for (int i=0;i<8;++i) {s.odf[i]=char(std::tolower(cls[0x30+i]&0x7f)); if (!s.odf[i]) break;}
                return s.owner && s.odf[0] && C::Pose(*reinterpret_cast<const BZR::MAT_3D*>(rig+0x378),s.position,s.rotation);
            }
            __except(Seh::Filter(GetExceptionCode())) {return false;}
        }
        int __fastcall GetCommandHook(void* self,void*,const void* position)
        {
            const int result=original(self,position); // Never change native command/result.
            if (S().enabled)
            {
                // A group can query another busy rig after a valid one. Keep
                // this frame's valid native footprint; OnFrame consumes it.
                Snapshot snap;
                if (Read(self,result,snap)) S().pending=snap;
            }
            return result;
        }
        void OnFrame()
        {
            auto& s=S();
            const Snapshot snap=s.pending;
            s.pending=Snapshot{}; // Require a fresh native placement each rendered frame.
            if (!s.enabled || s.faulted || snap.state==C::State::None || !BZR::GameObject::GetObj(snap.owner))
            {OgrePreviewModel::Hide(s.model);return;}
            auto found=s.meshes.find(snap.odf);
            if (found==s.meshes.end() || found->second.failed) {OgrePreviewModel::Hide(s.model);return;}
            const char* color=snap.state==C::State::Ready?"Green":snap.state==C::State::Warning?"Yellow":"Red";
            const auto material=s.prefix+color+(C::SolidPhase(GetTickCount64())?"Solid":"Wire");
            const bool changed=s.model.mesh!=found->second.name;
            if (!OgrePreviewModel::Show(s.model,found->second.name,material,snap.position,snap.rotation))
            {
                OgrePreviewModel::Hide(s.model);
                found->second.failed=true;
                Logging::LogMessage("[EXU::ConstructionHologram] renderer declined odf=%s mesh=%s; native placement retained",snap.odf,found->second.name.c_str());
            }
            else if (changed) Logging::LogMessage("[EXU::ConstructionHologram] submitted odf=%s mesh=%s state=%s",snap.odf,found->second.name.c_str(),color);
        }
        struct FrameEvent {float sinceEvent,sinceFrame;};
        class Listener
        {
        public:
            virtual bool frameStarted(const FrameEvent&)
            {
                if (!Seh::Guard("ConstructionHologram::Frame",[]{OnFrame();}))
                {S().faulted=true;OgrePreviewModel::Hide(S().model);}
                return true;
            }
            virtual bool frameRenderingQueued(const FrameEvent&) {return true;}
            virtual bool frameEnded(const FrameEvent&) {return true;}
            virtual ~Listener()=default;
        };
        Listener listener;
        bool Install()
        {
            auto& s=S();
            if (s.hook && s.hook->IsActive() && s.root) return true;
            if (!OgrePreviewModel::Available()) return false;
            if (!s.hook)
            {
                if (!QualifyFeature("Construction hologram ",3)) return false;
                s.hook=new EntryDetour32(A::GetCommand,reinterpret_cast<void*>(&GetCommandHook),6,
                    BasicPatch::Status::INACTIVE,{0x55,0x8b,0xec,0x83,0xec,0x54});
            }
            if (!s.hook->PrepareTrampoline()) return false;
            original=reinterpret_cast<GetCommandFn>(s.hook->GetTrampoline());
            if (!s.root) s.root=OgreGibRuntime::AddFrameListener(&listener);
            if (!s.root) return false;
            s.hook->SetStatus(true);
            if (!s.hook->IsActive())
            {
                OgreGibRuntime::RemoveFrameListener(s.root,&listener);s.root=nullptr;return false;
            }
            return true;
        }
    }
    int SetEnabled(lua_State* L)
    {
        const bool on=CheckBool(L,1);
        if (!lua_isnoneornil(L,2)) luaL_checktype(L,2,LUA_TTABLE);
        if (!lua_isnoneornil(L,3)) luaL_checkstring(L,3);
        // No Lua errors below: owned std::strings/maps can unwind normally.
        auto& s=S();
        if (lua_istable(L,2))
        {
            std::map<std::string,Mesh> meshes;
            lua_pushnil(L);
            while (lua_next(L,2))
            {
                if (lua_type(L,-2)==LUA_TSTRING && lua_type(L,-1)==LUA_TSTRING && meshes.size()<256)
                {
                    std::string odf=lua_tostring(L,-2),mesh=lua_tostring(L,-1);
                    bool valid=!odf.empty() && odf.size()<=8 && !mesh.empty() && mesh.size()<=128;
                    for (char& ch:odf) {if (!std::isalnum(static_cast<unsigned char>(ch)) && ch!='_' && ch!='-') valid=false;ch=char(std::tolower(static_cast<unsigned char>(ch)));}
                    if (valid) meshes.emplace(odf,Mesh{mesh,false});
                }
                lua_pop(L,1);
            }
            s.meshes=std::move(meshes);
        }
        if (lua_type(L,3)==LUA_TSTRING) s.prefix=lua_tostring(L,3);
        bool available=!on;
        if (on && !s.faulted && !s.meshes.empty() && !s.prefix.empty()) available=Install();
        s.enabled=on && available;
        if (!s.enabled) {s.pending=Snapshot{};OgrePreviewModel::Hide(s.model);}
        lua_pushboolean(L,available);return 1;
    }
    void Shutdown() noexcept
    {
        auto& s=S();s.enabled=false;s.pending=Snapshot{};
        if (s.hook) s.hook->SetStatus(false);
        if (s.root) OgreGibRuntime::RemoveFrameListener(s.root,&listener);
        s.root=nullptr;
        OgrePreviewModel::Destroy(s.model);
        // Retain the inactive trampoline for a pinned DLL's next Lua state;
        // BasicPatch's registry also retains it for reset/unload.
        s.meshes.clear();s.prefix.clear();s.faulted=false;
    }
}
