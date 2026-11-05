// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "Ogre/OgrePreviewModel.h"
#include "Ogre/OgreGibRuntime.h"
#include "Ogre/OgreProc.h"
#include "Ogre/OgreRenderOrigin.h"
#include "Ogre/OgreRenderSpace.h"
#include "Ogre/OgreEntityRuntime.h"
#include "Util/SehGuard.h"

namespace ExtraUtilities::OgrePreviewModel
{
    namespace
    {
        using namespace ConstructionPreviewCore;
        using OgreDll::OgreProc;
        using SetVector = void(__thiscall*)(void*, const Vector&);
        using SetQuaternion = void(__thiscall*)(void*, const Quaternion&);
        using SetFlags = void(__thiscall*)(void*, unsigned);
        using HasNode = bool(__thiscall*)(void*, const std::string&);
        using CreateChild = void*(__thiscall*)(void*,const Vector&,const Quaternion&);
        using CreateEntity = void*(__thiscall*)(void*,const std::string&);
        using Attach = void(__thiscall*)(void*,void*);
        constinit OgreProc<CreateChild> createChild{"?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@ABVVector3@2@ABVQuaternion@2@@Z"};
        constinit OgreProc<CreateEntity> createEntity{"?createEntity@SceneManager@Ogre@@UAEPAVEntity@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"};
        constinit OgreProc<Attach> attach{"?attachObject@SceneNode@Ogre@@UAEXPAVMovableObject@2@@Z"};
        constinit OgreProc<Attach> detach{"?detachObject@SceneNode@Ogre@@UAEXPAVMovableObject@2@@Z"};
        constinit OgreProc<SetVector> setPosition{"?_setDerivedPosition@Node@Ogre@@UAEXABVVector3@2@@Z"};
        // Released Ogre exports the derived setter, not setOrientation. The
        // preview's named parent is an identity child of the world root.
        constinit OgreProc<SetQuaternion> setRotation{"?_setDerivedOrientation@Node@Ogre@@UAEXABVQuaternion@2@@Z"};
        constinit OgreProc<SetFlags> removeQueryFlags{"?removeQueryFlags@MovableObject@Ogre@@UAEXI@Z"};
        constinit OgreProc<HasNode> hasNode{"?hasSceneNode@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"};
        const std::string& Name() { static const std::string name="__exu_construction_hologram"; return name; }
        bool Alive(const Model& m) noexcept
        {
            return m.owner && m.owner==OgreGibRuntime::ReadSceneManager() &&
                OgreGibRuntime::NamedChildAlive(m.owner,m.parent,Name());
        }
    }
    bool Available() noexcept
    {
        // Check only this feature's dependencies; the gib runtime also has
        // skeleton/resource exports that a building preview does not need.
        const char* names[] = {
            "?createEntity@SceneManager@Ogre@@UAEPAVEntity@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z",
            "?destroyEntity@SceneManager@Ogre@@UAEXPAVEntity@2@@Z",
            "?getRootSceneNode@SceneManager@Ogre@@UAEPAVSceneNode@2@XZ",
            "?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@ABVVector3@2@ABVQuaternion@2@@Z",
            "?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@ABVVector3@2@ABVQuaternion@2@@Z",
            "?destroySceneNode@SceneManager@Ogre@@UAEXPAVSceneNode@2@@Z",
            "?getSceneNode@SceneManager@Ogre@@UBEPAVSceneNode@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z",
            "?attachObject@SceneNode@Ogre@@UAEXPAVMovableObject@2@@Z",
            "?setCastShadows@MovableObject@Ogre@@QAEX_N@Z",
            "?setMaterialName@Entity@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z",
            "?getSingletonPtr@Root@Ogre@@SAPAV12@XZ",
            "?addFrameListener@Root@Ogre@@QAEXPAVFrameListener@2@@Z",
            "?removeFrameListener@Root@Ogre@@QAEXPAVFrameListener@2@@Z"
        };
        for (const char* name:names) if (!OgreDll::ResolveOgreProc<void*>(name)) return false;
        return setPosition && setRotation && removeQueryFlags && hasNode && createChild && createEntity && attach && detach;
    }
    void Hide(Model& m) noexcept
    {
        // Visibility setters are inline-only in the released DLL. Detach our
        // renderable from its own node instead; it cannot enter any render queue.
        if (m.attached && m.entity && Alive(m) &&
            Seh::Guard("Preview::Detach",[&]{detach.Get()(m.node,m.entity);})) m.attached=false;
    }
    void Destroy(Model& m) noexcept
    {
        if (Alive(m))
        {
            if (m.entity) OgreGibRuntime::DestroyEntity(m.owner,m.entity);
            if (m.node) OgreGibRuntime::DestroySceneNode(m.owner,m.node);
            OgreGibRuntime::DestroySceneNode(m.owner,m.parent);
        }
        m=Model{};
    }
    bool Show(Model& m, const std::string& mesh, const std::string& material,
        const Vector& position, const Quaternion& rotation)
    {
        if (!Alive(m))
        {
            m=Model{};
            m.owner=OgreGibRuntime::ReadSceneManager();
            if (!m.owner) return false;
            bool occupied=true;
            if (!Seh::Guard("Preview::HasNode",[&]{occupied=hasNode.Get()(m.owner,Name());}) || occupied) return false;
            m.parent=OgreGibRuntime::EnsureNamedChild(m.owner,Name());
            if (!m.parent) return false;
        }
        if (!m.node)
        {
            if (!Seh::Guard("Preview::CreateNode",[&]{m.node=createChild.Get()(m.parent,Vector{0,0,0},Quaternion{1,0,0,0});}) || !m.node) return false;
        }
        if (m.mesh!=mesh || !m.entity)
        {
            Hide(m);
            if (m.entity && !OgreGibRuntime::DestroyEntity(m.owner,m.entity)) return false;
            m.entity=nullptr;m.attached=false;m.mesh.clear();m.material.clear();
            if (!Seh::Guard("Preview::CreateEntity",[&]{m.entity=createEntity.Get()(m.owner,mesh);}) || !m.entity) return false;
            m.mesh=mesh; m.material.clear();
        }
        if (!m.entity) return false;
        BZR::VECTOR_3D origin;
        if (!OgreRenderSpace::TryReadWorldRenderOrigin(origin) || !std::isfinite(origin.x) ||
            !std::isfinite(origin.y) || !std::isfinite(origin.z)) return false;
        const Vector rOrigin{origin.x,origin.y,origin.z};
        const auto p=OgreRenderSpace::SimPositionToRender(position,rOrigin);
        const auto q=OgreRenderSpace::SimOrientationToRender(rotation);
        if (m.material!=material)
        {
            if (!Lua::GameObject::Detail::TrySetMaterialNameEntity(m.entity,material,"General") ||
                !Lua::GameObject::Detail::TrySetCastShadows(m.entity,false)) return false;
            m.material=material;
        }
        const bool ok=Seh::Guard("Preview::Place",[&] {
            removeQueryFlags.Get()(m.entity,~0u);
            setPosition.Get()(m.node,p); setRotation.Get()(m.node,q);
            if (!m.attached) {attach.Get()(m.node,m.entity);m.attached=true;}
        });
        return ok;
    }
}
