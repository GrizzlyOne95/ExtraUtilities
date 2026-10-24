// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Ogre/OgreCameraRuntime.h"
#include "Ogre/Ogre.h"
#include "Ogre/OgreRenderOrigin.h"
#include "Ogre/OgreRenderSpace.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"

namespace ExtraUtilities::OgreCameraRuntime
{
    namespace
    {
        using MatrixFn = const CameraProjection::Matrix*(__thiscall*)(void*);
        using DimensionFn = int(__thiscall*)(void*);
        using VisibleFn = bool(__thiscall*)(void*, const BZR::VECTOR_3D&, int*);
        constinit OgreDll::OgreProc<MatrixFn> view{"?getViewMatrix@Camera@Ogre@@UBEABVMatrix4@2@XZ"};
        constinit OgreDll::OgreProc<MatrixFn> projection{"?getProjectionMatrix@Frustum@Ogre@@UBEABVMatrix4@2@XZ"};
        constinit OgreDll::OgreProc<DimensionFn> left{"?getActualLeft@Viewport@Ogre@@QBEHXZ"};
        constinit OgreDll::OgreProc<DimensionFn> top{"?getActualTop@Viewport@Ogre@@QBEHXZ"};
        constinit OgreDll::OgreProc<DimensionFn> width{"?getActualWidth@Viewport@Ogre@@QBEHXZ"};
        constinit OgreDll::OgreProc<DimensionFn> height{"?getActualHeight@Viewport@Ogre@@QBEHXZ"};
        constinit OgreDll::OgreProc<VisibleFn> visible{"?isVisible@Camera@Ogre@@UBE_NABVVector3@2@PAW4FrustumPlane@2@@Z"};

        bool QuerySeh(const BZR::VECTOR_3D& position, CameraProjection::Result* result, bool* inView)
        {
            if (!RuntimeGate::IsSupported() || !std::isfinite(position.x) ||
                !std::isfinite(position.y) || !std::isfinite(position.z)) return false;
            __try
            {
                // A scene manager can survive mission teardown; require a live
                // simulation view and fetch the camera anew on every call.
                if (!*BZR::GameObject::user_entity_ptr) return false;
                void* manager = Ogre::sceneManager.Read();
                if (!manager) return false;
                void* viewport = Ogre::GetCurrentViewport(manager);
                if (!viewport) return false;
                void* camera = Ogre::GetViewportCamera(viewport);
                BZR::VECTOR_3D origin{};
                if (!camera || !OgreRenderSpace::TryReadWorldRenderOrigin(origin) ||
                    !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return false;
                const auto render = OgreRenderSpace::SimPositionToRender(position, origin);
                if (inView)
                {
                    const auto fn = visible.Get();
                    if (!fn) return false;
                    *inView = fn(camera, render, nullptr);
                    return true;
                }
                const auto v = view.Get(), p = projection.Get();
                const auto l = left.Get(), t = top.Get(), w = width.Get(), h = height.Get();
                if (!v || !p || !l || !t || !w || !h) return false;
                const auto* vm = v(camera);
                const auto* pm = p(camera);
                return vm && pm && CameraProjection::Project(render, *vm, *pm,
                    {l(viewport), t(viewport), w(viewport), h(viewport)}, *result);
            }
            __except (Seh::Filter(GetExceptionCode())) { return false; }
        }
    }

    bool Project(const BZR::VECTOR_3D& position, CameraProjection::Result& out) noexcept
    {
        out = {};
        return Seh::CatchCpp("WorldToScreen", [&] { return QuerySeh(position, &out, nullptr); }, false);
    }

    bool IsPointInView(const BZR::VECTOR_3D& position, bool& out) noexcept
    {
        out = false;
        return Seh::CatchCpp("IsPointInView", [&] { return QuerySeh(position, nullptr, &out); }, false);
    }
}
