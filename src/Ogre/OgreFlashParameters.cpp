#include "OgreFlashParameters.h"
#include "OgreProc.h"
#include "Util/SehGuard.h"

#ifndef register
#define EXU_RESTORE_REGISTER
#define register
#endif
#ifndef _STLP_MSVC
#define _STLP_MSVC 1
#endif
// Only declarations, inheritance conversion and inline Vector4 math are used;
// every operation on a runtime Ogre object goes through the released export.
#define OGRE_STATIC_LIB
#include <OgreSubEntity.h>
#include <OgreVector4.h>
#ifdef EXU_RESTORE_REGISTER
#undef register
#undef EXU_RESTORE_REGISTER
#endif

namespace ExtraUtilities { namespace OgreFlashParameters
{
    namespace
    {
        using Count = unsigned (__thiscall*)(void*);
        using Get = ::Ogre::SubEntity* (__thiscall*)(void*, unsigned);
        using SetParameter = void (__thiscall*)(::Ogre::Renderable*, size_t, const ::Ogre::Vector4&);
        OgreDll::OgreProc<Count> count{"?getNumSubEntities@Entity@Ogre@@QBEIXZ"};
        OgreDll::OgreProc<Get> get{"?getSubEntity@Entity@Ogre@@QBEPAVSubEntity@2@I@Z"};
        OgreDll::OgreProc<SetParameter> set{"?setCustomParameter@Renderable@Ogre@@QAEXIABVVector4@2@@Z"};
    }
    bool Available() noexcept { return count && get && set; }
    bool Set(void* entity, const MuzzleFlashVisualState::Sample& state) noexcept
    {
        if (!entity || !Available()) return false;
        bool applied = false;
        const bool completed = Seh::Guard("FlashParameters::Set", [&] {
            const unsigned size = count.Get()(entity);
            if (!size || size > 64) return;
            const ::Ogre::Vector4 value(state.phase, state.seed, state.valid, state.duration);
            for (unsigned i = 0; i < size; ++i)
            {
                auto* sub = get.Get()(entity, i);
                if (!sub) return;
                // Use the authoritative SDK inheritance conversion, then the
                // released exported setter; never mutate Ogre's STL map here.
                set.Get()(static_cast<::Ogre::Renderable*>(sub), MuzzleFlashVisualState::kCustomParameter, value);
            }
            applied = true;
        });
        return completed && applied;
    }
} }
