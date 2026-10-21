#include "OgreSurfaceParameters.h"
#include "OgreProc.h"
#include "OgreRuntimeReference.h"
#include "Util/SehGuard.h"
#include "Util/Logging.h"

#ifndef register
#define EXU_RESTORE_REGISTER
#define register
#endif
#ifndef _STLP_MSVC
#define _STLP_MSVC 1
#endif
#define OGRE_STATIC_LIB
#include <OgreGpuProgramParams.h>
#include <OgrePass.h>
#include <OgreVector4.h>
#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgreTechnique.h>
#include <OgreTextureUnitState.h>
#include <map>
#include <mutex>
#include <vector>
#include <Windows.h>
#include <cstdio>
#ifdef EXU_RESTORE_REGISTER
#undef register
#undef EXU_RESTORE_REGISTER
#endif

namespace ExtraUtilities { namespace OgreSurfaceParameters
{
    namespace
    {
        using Has = bool (__thiscall*)(const ::Ogre::Pass*);
        // Spell out the released member-function hidden return buffer. A free
        // __thiscall function pointer returning a class does not have the same
        // MSVC x86 sret/ECX ordering as an actual member function.
        using Get = void (__thiscall*)(const ::Ogre::Pass*, OgreDll::RuntimeReference<::Ogre::GpuProgramParameters>*);
        using Find = const ::Ogre::GpuConstantDefinition* (__thiscall*)(const ::Ogre::GpuProgramParameters*, const std::string&, bool);
        using Set = void (__thiscall*)(::Ogre::GpuProgramParameters*, const std::string&, const ::Ogre::Vector4&);
        OgreDll::OgreProc<Has> has{"?hasFragmentProgram@Pass@Ogre@@QBE_NXZ"};
        OgreDll::OgreProc<Get> get{"?getFragmentProgramParameters@Pass@Ogre@@QBE?AV?$SharedPtr@VGpuProgramParameters@Ogre@@@2@XZ"};
        OgreDll::OgreProc<Find> find{"?_findNamedConstantDefinition@GpuProgramParameters@Ogre@@QBEPBUGpuConstantDefinition@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@_N@Z"};
        OgreDll::OgreProc<Set> set{"?setNamedConstant@GpuProgramParameters@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@ABVVector4@2@@Z"};
        using Manager = ::Ogre::MaterialManager* (__cdecl*)();
        using Lookup = void (__thiscall*)(::Ogre::MaterialManager*, OgreDll::RuntimeReference<::Ogre::Material>*, const std::string&, const std::string&);
        using CloneFn = void (__thiscall*)(const ::Ogre::Material*, OgreDll::RuntimeReference<::Ogre::Material>*, const std::string&, bool, const std::string&);
        using Remove = void (__thiscall*)(::Ogre::MaterialManager*, const std::string&);
        using AssignFn = void (__thiscall*)(void*, const OgreDll::RuntimeReference<::Ogre::Material>&);
        using TechniqueCount = unsigned short (__thiscall*)(const ::Ogre::Material*);
        using TechniqueGet = ::Ogre::Technique* (__thiscall*)(::Ogre::Material*, unsigned short);
        using PassCount = unsigned short (__thiscall*)(const ::Ogre::Technique*);
        using PassGet = ::Ogre::Pass* (__thiscall*)(::Ogre::Technique*, unsigned short);
        using UnitCount = unsigned short (__thiscall*)(const ::Ogre::Pass*);
        using UnitGet = ::Ogre::TextureUnitState* (__thiscall*)(::Ogre::Pass*, unsigned short);
        using UnitAlias = const std::string& (__thiscall*)(const ::Ogre::TextureUnitState*);
        using UnitTexture = void (__thiscall*)(::Ogre::TextureUnitState*, const std::string&, ::Ogre::TextureType);
        OgreDll::OgreProc<Manager> manager{"?getSingletonPtr@MaterialManager@Ogre@@SAPAV12@XZ"};
        OgreDll::OgreProc<Lookup> lookup{"?getByName@MaterialManager@Ogre@@QAE?AV?$SharedPtr@VMaterial@Ogre@@@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z"};
        OgreDll::OgreProc<CloneFn> clone{"?clone@Material@Ogre@@QBE?AV?$SharedPtr@VMaterial@Ogre@@@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@_N0@Z"};
        OgreDll::OgreProc<Remove> remove{"?remove@ResourceManager@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z"};
        OgreDll::OgreProc<AssignFn> assign{"?setMaterial@SubEntity@Ogre@@QAEXABV?$SharedPtr@VMaterial@Ogre@@@2@@Z"};
        OgreDll::OgreProc<TechniqueCount> techniques{"?getNumTechniques@Material@Ogre@@QBEGXZ"};
        OgreDll::OgreProc<TechniqueGet> technique{"?getTechnique@Material@Ogre@@QAEPAVTechnique@2@G@Z"};
        OgreDll::OgreProc<PassCount> passes{"?getNumPasses@Technique@Ogre@@QBEGXZ"};
        OgreDll::OgreProc<PassGet> passAt{"?getPass@Technique@Ogre@@QAEPAVPass@2@G@Z"};
        OgreDll::OgreProc<UnitCount> units{"?getNumTextureUnitStates@Pass@Ogre@@QBEGXZ"};
        OgreDll::OgreProc<UnitGet> unit{"?getTextureUnitState@Pass@Ogre@@QAEPAVTextureUnitState@2@G@Z"};
        OgreDll::OgreProc<UnitAlias> aliasName{"?getTextureNameAlias@TextureUnitState@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"};
        OgreDll::OgreProc<UnitAlias> unitName{"?getName@TextureUnitState@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"};
        OgreDll::OgreProc<UnitTexture> textureName{"?setTextureName@TextureUnitState@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@W4TextureType@2@@Z"};
        // Released-ABI references, released at the Lua-state lifecycle boundary. These
        // materials never enter the older POD material-handle cache.
        std::map<std::string, OgreDll::RuntimeReference<::Ogre::Material>> owned;
        using Parameters = OgreDll::RuntimeReference<::Ogre::GpuProgramParameters>;
        std::map<std::string,std::map<std::string,std::vector<Parameters>>> ownedParameters;
        // Lua updates and native debris creation can arrive on different
        // engine threads. Serialize pool/cache mutation, including teardown.
        std::recursive_mutex stateMutex;
        bool chunksEnabled = false;
        std::map<std::string, std::string> chunkMaterials;
        using SubCount = unsigned int (__thiscall*)(const void*);
        using SubGet = void* (__thiscall*)(void*, unsigned int);
        using SubName = const std::string& (__thiscall*)(const void*);
        OgreDll::OgreProc<SubCount> subCount{"?getNumSubEntities@Entity@Ogre@@QBEIXZ"};
        OgreDll::OgreProc<SubGet> subGet{"?getSubEntity@Entity@Ogre@@QBEPAVSubEntity@2@I@Z"};
        OgreDll::OgreProc<SubName> subName{"?getMaterialName@SubEntity@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"};

        OgreDll::RuntimeReference<::Ogre::Material> LookupMaterial(::Ogre::MaterialManager* mgr, const std::string& name)
        {
            OgreDll::RuntimeReference<::Ogre::Material> result;
            if (mgr && lookup) lookup.Get()(mgr, &result, name, "Autodetect");
            return result;
        }

        Parameters FindParameters(void* opaque, const std::string& name)
        {
            auto* pass = static_cast<::Ogre::Pass*>(opaque);
            if (!pass || !has || !get || !find || !set || !has.Get()(pass)) return {};
            // Balance the released ABI reference, including early-return paths.
            OgreDll::RuntimeReference<::Ogre::GpuProgramParameters> parameters;
            get.Get()(pass, &parameters);
            if (parameters.isNull()) return {};
            const auto* definition = find.Get()(parameters.get(), name, false);
            if (!definition || definition->constType != ::Ogre::GCT_FLOAT4 || definition->arraySize != 1) return {};
            return parameters;
        }

        bool SetCpp(void* opaque, const std::string& name, const float value[4])
        {
            const auto parameters=FindParameters(opaque,name);
            if (parameters.isNull()) return false;
            set.Get()(parameters.get(), name, ::Ogre::Vector4(value[0], value[1], value[2], value[3]));
            return true;
        }
    }

    bool SetVector(void* pass, const std::string& name, const float value[4]) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        bool result = false;
        const bool completed = Seh::Guard("SurfaceParameters::SetVector", [&] { result = SetCpp(pass, name, value); });
        return completed && result;
    }

    bool Clone(const std::string& source, const std::string& name) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        bool result = false;
        Seh::Guard("SurfaceParameters::Clone", [&] {
            if (owned.count(name)) { result = true; return; }
            if (owned.size() >= 1024 || name.find("exu_surface_") != 0 || name.size() > 160 || !manager || !lookup || !clone) return;
            auto* mgr = manager.Get()();
            const auto existing = LookupMaterial(mgr, name);
            if (!mgr || !existing.isNull()) return; // never adopt foreign resources
            const auto original = LookupMaterial(mgr, source);
            if (original.isNull())
            {
                static int misses = 0;
                if (misses++ < 8) Logging::LogMessage("Surface clone source not found: %s", source.c_str());
                return;
            }
            OgreDll::RuntimeReference<::Ogre::Material> created;
            clone.Get()(original.get(), &created, name, true, "General");
            if (!created.isNull()) { owned.emplace(name, created); result = true; }
        }, [](unsigned long code) { Logging::LogMessage("Surface clone fault code=%08lX", code); });
        return result;
    }

    bool Assign(void* subEntity, const std::string& name) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        bool result = false;
        Seh::Guard("SurfaceParameters::Assign", [&] {
            const auto found = owned.find(name);
            if (!subEntity || !assign || found == owned.end()) return;
            assign.Get()(subEntity, found->second); result = true;
        });
        return result;
    }

    void NotifyVariant(void* entity, unsigned int index, const std::string& before, const std::string& after) noexcept
    {
        if (before.find("exu_surface_")!=0 && after.find("exu_surface_")!=0) return;
        HMODULE module=nullptr;
        if (!GetModuleHandleExW(0,L"openshim.dll",&module)) return;
        using Notify=int (__cdecl*)(void*,unsigned int,const char*,const char*);
        auto fn=reinterpret_cast<Notify>(GetProcAddress(module,"OpenShimAdoptSurfaceVariant"));
        if (!fn) fn=reinterpret_cast<Notify>(GetProcAddress(module,"_OpenShimAdoptSurfaceVariant"));
        if (fn) Seh::Guard("SurfaceParameters::NotifyVariant",[&] { fn(entity,index,before.c_str(),after.c_str()); });
        FreeLibrary(module);
    }

    int SetOwnedVector(const std::string& material, const std::string& name, const float value[4]) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        int changed = 0;
        Seh::Guard("SurfaceParameters::SetOwnedVector", [&] {
            const auto found = owned.find(material);
            if (found == owned.end() || !techniques || !technique || !passes || !passAt) return;
            auto& cache=ownedParameters[material];
            auto entry=cache.find(name);
            if (entry==cache.end())
            {
                if (cache.size()>=32) return;
                auto* mat = found->second.get();
                const unsigned short count = techniques.Get()(mat);
                // CR's world base has 110 techniques (renderers/schemes/LODs).
                if (count > 128) return;
                std::vector<Parameters> parameters;
                for (unsigned short t=0;t<count;++t)
                {
                    auto* tech=technique.Get()(mat,t);
                    const auto pc=passes.Get()(tech);
                    if (pc>32) return;
                    for (unsigned short p=0;p<pc;++p)
                    {
                        auto valueParameters=FindParameters(passAt.Get()(tech,p),name);
                        if (!valueParameters.isNull()) parameters.emplace_back(std::move(valueParameters));
                    }
                }
                entry=cache.emplace(name,std::move(parameters)).first;
            }
            // Cache holds released-ABI references for this owned material's lifetime;
            // hot updates avoid technique walks, lookups and refcount churn.
            for (const auto& parameters:entry->second)
            { set.Get()(parameters.get(),name,::Ogre::Vector4(value[0],value[1],value[2],value[3]));++changed; }
        });
        return changed;
    }

    int SetTexture(const std::string& material, const std::string& alias, const std::string& texture) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        int changed = 0;
        Seh::Guard("SurfaceParameters::SetTexture", [&] {
            const auto found = owned.find(material);
            if (found == owned.end() || !techniques || !technique || !passes || !passAt || !units || !unit || !aliasName || !unitName || !textureName) return;
            auto* mat = found->second.get();
            const unsigned short count = techniques.Get()(mat);
            if (count > 128) return;
            for (unsigned short t = 0; t < count; ++t)
            {
                auto* tech = technique.Get()(mat, t);
                const unsigned short pc = passes.Get()(tech);
                if (pc > 32) return;
                for (unsigned short p = 0; p < pc; ++p)
                {
                    auto* current = passAt.Get()(tech, p);
                    const unsigned short uc = units.Get()(current);
                    if (uc > 32) return;
                    for (unsigned short u = 0; u < uc; ++u)
                    {
                        auto* state = unit.Get()(current, u);
                        if (_stricmp(aliasName.Get()(state).c_str(), alias.c_str()) != 0 &&
                            _stricmp(unitName.Get()(state).c_str(), alias.c_str()) != 0) continue;
                        textureName.Get()(state, texture, ::Ogre::TEX_TYPE_2D); ++changed;
                    }
                }
            }
        });
        return changed;
    }

    bool Release(const std::string& name) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        bool result = false;
        Seh::Guard("SurfaceParameters::Release", [&] {
            const auto found = owned.find(name);
            if (found == owned.end() || !manager || !remove) return;
            auto* mgr = manager.Get()();
            if (!mgr) return;
            remove.Get()(mgr, name);
            ownedParameters.erase(name);
            owned.erase(found); result = true;
        });
        return result;
    }

    void Clear() noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        chunksEnabled = false;
        chunkMaterials.clear();
        ownedParameters.clear();
        while (!owned.empty())
        {
            const auto name = owned.begin()->first;
            if (!Release(name)) { owned.clear(); break; }
        }
    }

    void EnableChunks(bool enabled) noexcept
    { std::lock_guard<std::recursive_mutex> lock(stateMutex); chunksEnabled = enabled; }

    std::string ChunkMaterial(const std::string& source) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (!chunksEnabled || source.empty() || source.find("exu_surface_") == 0) return source;
        // Pilot flesh already has its own presentation; this finish is metal.
        std::string lower = source;
        for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        if (lower.find("flesh") != std::string::npos || lower.find("blood") != std::string::npos) return source;
        const auto found = chunkMaterials.find(source);
        if (found != chunkMaterials.end()) return found->second;
        if (chunkMaterials.size() >= 128) return source;
        const std::string name = "exu_surface_chunk_" + std::to_string(chunkMaterials.size());
        if (!Clone(source, name)) return source;
        const float mode[4] = {1,0,0,0};
        const float zero[4] = {};
        // Unsupported materials keep their original techniques and textures.
        if (!SetOwnedVector(name, "surfaceMode", mode)) { Release(name); return source; }
        SetOwnedVector(name, "surfaceState", zero);
        chunkMaterials.emplace(source, name);
        return name;
    }

    void FinishChunk(void* entity) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (!chunksEnabled || !entity) return;
        Seh::Guard("SurfaceParameters::FinishChunk", [&] {
            if (!subCount || !subGet || !subName) return;
            const auto count = subCount.Get()(entity);
            if (count > 64) return;
            for (unsigned int i = 0; i < count; ++i)
            {
                auto* sub = subGet.Get()(entity, i);
                if (!sub) continue;
                const auto source = subName.Get()(sub);
                const auto finish = ChunkMaterial(source);
                if (finish != source) Assign(sub, finish);
            }
        });
    }
} }

// Optional OpenShim bridge: no retained callback or object pointer crosses
// modules. The caller leases this DLL for the duration of each creation call.
extern "C" __declspec(dllexport) void __cdecl EXU_ApplyDestructionFinish(void* entity)
{
    ExtraUtilities::OgreSurfaceParameters::FinishChunk(entity);
}
extern "C" __declspec(dllexport) bool __cdecl EXU_GetDestructionMaterial(const char* source, char* output, unsigned int size)
{
    if (!source || !output || size < 1) return false;
    const auto result = ExtraUtilities::OgreSurfaceParameters::ChunkMaterial(source);
    if (result.size() >= size) return false;
    strcpy_s(output, size, result.c_str());
    return result != source;
}
