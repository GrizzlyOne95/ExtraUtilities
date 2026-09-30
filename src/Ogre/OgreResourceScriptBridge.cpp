// Copyright (C) 2026 Extra Utilities contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "OgreResourceScriptBridge.h"
#include "OgreBuildSettings.h"
#include "Util/SehGuard.h"
#include "OgreProc.h"
#define register
#define _STLP_MSVC 1
#include <OgreDataStream.h>
#include <OgreScriptCompiler.h>
#undef register
#include <cstring>

namespace ExtraUtilities { namespace OgreScripts {
namespace {
bool ParseCpp(const char* text, std::size_t length, const char* source, const char* group)
{
    auto* compiler = ::Ogre::ScriptCompilerManager::getSingletonPtr();
    if (!compiler) return false;
    // Parsing is synchronous. The stream borrows the Lua string only until
    // parseScript returns; freeOnClose=false prevents Ogre freeing that buffer.
    // Construct through the DLL's exported constructor. `new MemoryDataStream`
    // in this TU emits an EXU vtable against upstream inline virtuals that the
    // shipped DLL does not export. Keep the runtime's own vtable and allocator.
    using Constructor = void(__thiscall*)(void*, const std::string&, void*, std::size_t, bool, bool);
    const auto construct = OgreDll::ResolveOgreProc<Constructor>(
        "??0MemoryDataStream@Ogre@@QAE@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PAXI_N2@Z");
    if (!construct) return false;
    void* storage = ::Ogre::StdAllocPolicy::allocateBytes(sizeof(::Ogre::MemoryDataStream));
    try { construct(storage, ::Ogre::String(source), const_cast<char*>(text), length, false, true); }
    catch (...) { ::Ogre::StdAllocPolicy::deallocateBytes(storage); throw; }
    ::Ogre::DataStreamPtr stream(static_cast<::Ogre::MemoryDataStream*>(storage));
    compiler->parseScript(stream, ::Ogre::String(group));
    return true;
}
bool HasTemplateCpp(const char* name)
{
    // Resolve at runtime like the stream constructor above; the shipped
    // OgreMain exports both, and no EXU vtables are involved.
    using GetManager = void*(__cdecl*)();
    using GetTemplate = void*(__thiscall*)(void*, const std::string&);
    const auto getManager = OgreDll::ResolveOgreProc<GetManager>(
        "?getSingletonPtr@ParticleSystemManager@Ogre@@SAPAV12@XZ");
    const auto getTemplate = OgreDll::ResolveOgreProc<GetTemplate>(
        "?getTemplate@ParticleSystemManager@Ogre@@QAEPAVParticleSystem@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
    if (!getManager || !getTemplate) return false;
    void* manager = getManager();
    return manager && getTemplate(manager, ::Ogre::String(name)) != nullptr;
}
bool HasTemplateSeh(const char* name)
{
    __try { return HasTemplateCpp(name); }
    __except (Seh::Filter(GetExceptionCode())) { return false; }
}
bool ParseSeh(const char* text, std::size_t length, const char* source, const char* group)
{
    __try { return ParseCpp(text, length, source, group); }
    __except (Seh::Filter(GetExceptionCode())) { return false; }
}
}
bool TryParse(const char* text, std::size_t length, const char* source, const char* group) noexcept
{
    if (!text || length == 0 || length > 1024 * 1024 ||
        std::memchr(text, '\0', length) || !source || !*source || !group || !*group)
        return false;
    return Seh::CatchCpp("OgreScripts::TryParse", [&] {
        const bool parsed = ParseSeh(text, length, source, group);
        Logging::LogMessage("[EXU::Script] parse source=%s group=%s bytes=%u completed=%d",
            source, group, static_cast<unsigned>(length), parsed ? 1 : 0);
        return parsed;
    }, false);
}
bool HasParticleTemplate(const char* name) noexcept
{
    if (!name || !*name) return false;
    return Seh::CatchCpp("OgreScripts::HasParticleTemplate", [&] { return HasTemplateSeh(name); }, false);
}
} }
