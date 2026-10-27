#include "OgreSurfaceParameters.h"
#include "OgreProc.h"
#include "OgreRuntimeReference.h"
#include "SurfaceMaterialPolicy.h"
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
        // Vertex-stage twins: a surface vector the vertex program also declares
        // (snow thickness push) gets the same value.
        OgreDll::OgreProc<Has> hasVertex{"?hasVertexProgram@Pass@Ogre@@QBE_NXZ"};
        OgreDll::OgreProc<Get> getVertex{"?getVertexProgramParameters@Pass@Ogre@@QBE?AV?$SharedPtr@VGpuProgramParameters@Ogre@@@2@XZ"};
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
        // Each binding remembers its pass so a write can notice that Ogre
        // recreated the fragment program parameters (reload/device reset).
        struct Binding { ::Ogre::Pass* pass; Parameters parameters; };
        struct CachedBindings { unsigned short techniqueCount; std::vector<Binding> bindings; };
        std::map<std::string,std::map<std::string,CachedBindings>> ownedParameters;
        std::map<std::string, std::string> ownedSource;
        // Names whose manager entry outlived a failed Release; Clone may adopt them.
        SurfaceMaterialPolicy::BoundedNameSet previouslyOwned{2048};
        // Chunk sources that cannot be finished: tried (and logged) once per mission.
        SurfaceMaterialPolicy::BoundedNameSet failedChunkSources{256};
        // Lua updates and native debris creation can arrive on different
        // engine threads. Serialize pool/cache mutation, including teardown.
        std::recursive_mutex stateMutex;
        bool chunksEnabled = false;
        unsigned chunkSerial = 0;
        std::map<std::string, std::string> chunkMaterials;
        using SubCount = unsigned int (__thiscall*)(const void*);
        using SubGet = void* (__thiscall*)(void*, unsigned int);
        using SubName = const std::string& (__thiscall*)(const void*);
        OgreDll::OgreProc<SubCount> subCount{"?getNumSubEntities@Entity@Ogre@@QBEIXZ"};
        OgreDll::OgreProc<SubGet> subGet{"?getSubEntity@Entity@Ogre@@QBEPAVSubEntity@2@I@Z"};
        OgreDll::OgreProc<SubName> subName{"?getMaterialName@SubEntity@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"};

        OgreDll::RuntimeReference<::Ogre::Material> LookupMaterial(::Ogre::MaterialManager* mgr, const std::string& name, const std::string& group = "Autodetect")
        {
            OgreDll::RuntimeReference<::Ogre::Material> result;
            if (mgr && lookup) lookup.Get()(mgr, &result, name, group);
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

        Parameters FindVertexParameters(void* opaque, const std::string& name)
        {
            auto* pass = static_cast<::Ogre::Pass*>(opaque);
            if (!pass || !hasVertex || !getVertex || !find || !set || !hasVertex.Get()(pass)) return {};
            OgreDll::RuntimeReference<::Ogre::GpuProgramParameters> parameters;
            getVertex.Get()(pass, &parameters);
            if (parameters.isNull()) return {};
            const auto* definition = find.Get()(parameters.get(), name, false);
            if (!definition || definition->constType != ::Ogre::GCT_FLOAT4 || definition->arraySize != 1) return {};
            return parameters;
        }

        bool SetCpp(void* opaque, const std::string& name, const float value[4])
        {
            const ::Ogre::Vector4 vector(value[0], value[1], value[2], value[3]);
            const auto parameters=FindParameters(opaque,name);
            if (!parameters.isNull()) set.Get()(parameters.get(), name, vector);
            const auto vertexParameters=FindVertexParameters(opaque,name);
            if (!vertexParameters.isNull()) set.Get()(vertexParameters.get(), name, vector);
            return !parameters.isNull() || !vertexParameters.isNull();
        }
    }

    bool SetVector(void* pass, const std::string& name, const float value[4]) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        bool result = false;
        const bool completed = Seh::Guard("SurfaceParameters::SetVector", [&] { result = SetCpp(pass, name, value); });
        return completed && result;
    }

    namespace
    {
        bool CloneImpl(const std::string& source, const std::string& name)
        {
            bool result = false;
            Seh::Guard("SurfaceParameters::Clone", [&] {
                // An existing owned clone is only reusable for the same source.
                const auto have = owned.find(name);
                if (have != owned.end()) { result = ownedSource[name] == source; return; }
                if (owned.size() >= 1024 || name.find("exu_surface_") != 0 || name.size() > 160 || !manager || !lookup || !clone) return;
                auto* mgr = manager.Get()();
                if (!mgr) return;
                const auto existing = LookupMaterial(mgr, name);
                if (!existing.isNull())
                {
                    // Only a leftover from a failed Release is ours to replace;
                    // never adopt foreign resources.
                    if (!previouslyOwned.Contains(name) || !remove) return;
                    remove.Get()(mgr, name);
                    if (!LookupMaterial(mgr, name).isNull()) return;
                }
                previouslyOwned.Erase(name);
                const auto original = LookupMaterial(mgr, source);
                if (original.isNull())
                {
                    static int misses = 0;
                    if (misses++ < 8) Logging::LogMessage("Surface clone source not found: %s", source.c_str());
                    return;
                }
                OgreDll::RuntimeReference<::Ogre::Material> created;
                clone.Get()(original.get(), &created, name, true, "General");
                if (!created.isNull()) { owned.emplace(name, created); ownedSource[name] = source; result = true; }
            }, [](unsigned long code) { Logging::LogMessage("Surface clone fault code=%08lX", code); });
            return result;
        }
    }

    bool Clone(const std::string& source, const std::string& name) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        // exu_surface_chunk_* belongs to the native debris finish.
        if (SurfaceMaterialPolicy::IsNativeChunkName(name)) return false;
        return CloneImpl(source, name);
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

    namespace
    {
        // The pass's current fragment parameters object, or null.
        ::Ogre::GpuProgramParameters* CurrentParameters(::Ogre::Pass* pass)
        {
            if (!pass || !has || !get || !has.Get()(pass)) return nullptr;
            Parameters current;
            get.Get()(pass, &current);
            return current.get();
        }

        bool BindingsCurrent(::Ogre::Material* mat, const CachedBindings& cached)
        {
            if (techniques.Get()(mat) != cached.techniqueCount) return false;
            for (const auto& binding : cached.bindings)
                if (CurrentParameters(binding.pass) != binding.parameters.get()) return false;
            return true;
        }

        int SetOwnedVectorImpl(const std::string& material, const std::string& name, const float value[4])
        {
            int changed = 0;
            Seh::Guard("SurfaceParameters::SetOwnedVector", [&] {
                const auto found = owned.find(material);
                if (found == owned.end() || !techniques || !technique || !passes || !passAt || !has || !get || !set) return;
                auto* mat = found->second.get();
                auto& cache=ownedParameters[material];
                auto entry=cache.find(name);
                // Ogre may recreate the program parameters (reload, device reset):
                // re-resolve instead of writing through a stale reference.
                if (entry!=cache.end() && !BindingsCurrent(mat, entry->second)) { cache.erase(entry); entry=cache.end(); }
                if (entry==cache.end())
                {
                    if (cache.size()>=32) return;
                    const unsigned short count = techniques.Get()(mat);
                    // CR's world base has 110 techniques (renderers/schemes/LODs).
                    if (count > 128) return;
                    CachedBindings fresh{count, {}};
                    for (unsigned short t=0;t<count;++t)
                    {
                        auto* tech=technique.Get()(mat,t);
                        const auto pc=passes.Get()(tech);
                        if (pc>32) return;
                        for (unsigned short p=0;p<pc;++p)
                        {
                            auto* pass=passAt.Get()(tech,p);
                            auto valueParameters=FindParameters(pass,name);
                            if (!valueParameters.isNull()) fresh.bindings.push_back({pass,std::move(valueParameters)});
                        }
                    }
                    // An empty result is never cached; a later write may retry.
                    if (fresh.bindings.empty()) return;
                    entry=cache.emplace(name,std::move(fresh)).first;
                }
                // Cache holds released-ABI references for this owned material's lifetime;
                // hot updates avoid technique walks and lookups.
                for (const auto& binding:entry->second.bindings)
                { set.Get()(binding.parameters.get(),name,::Ogre::Vector4(value[0],value[1],value[2],value[3]));++changed; }
            });
            return changed;
        }
    }

    int SetOwnedVector(const std::string& material, const std::string& name, const float value[4]) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (SurfaceMaterialPolicy::IsNativeChunkName(material)) return 0;
        return SetOwnedVectorImpl(material, name, value);
    }

    namespace
    {
        using SchemeName = const std::string& (__thiscall*)(const ::Ogre::Technique*);
        using SetProgram = void (__thiscall*)(::Ogre::Pass*, const std::string&, bool);
        OgreDll::OgreProc<SchemeName> schemeName{"?getSchemeName@Technique@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"};
        OgreDll::OgreProc<SetProgram> setProgram{"?setFragmentProgram@Pass@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@_N@Z"};

        // Every texture unit whose alias or name matches, across all techniques.
        int SetTextureOn(::Ogre::Material* mat, const std::string& alias, const std::string& texture)
        {
            int changed = 0;
            const unsigned short count = techniques.Get()(mat);
            if (count > 128) return 0;
            for (unsigned short t = 0; t < count; ++t)
            {
                auto* tech = technique.Get()(mat, t);
                const unsigned short pc = passes.Get()(tech);
                if (pc > 32) return changed;
                for (unsigned short p = 0; p < pc; ++p)
                {
                    auto* current = passAt.Get()(tech, p);
                    const unsigned short uc = units.Get()(current);
                    if (uc > 32) return changed;
                    for (unsigned short u = 0; u < uc; ++u)
                    {
                        auto* state = unit.Get()(current, u);
                        if (_stricmp(aliasName.Get()(state).c_str(), alias.c_str()) != 0 &&
                            _stricmp(unitName.Get()(state).c_str(), alias.c_str()) != 0) continue;
                        textureName.Get()(state, texture, ::Ogre::TEX_TYPE_2D); ++changed;
                    }
                }
            }
            return changed;
        }
    }

    int SetTexture(const std::string& material, const std::string& alias, const std::string& texture) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        int changed = 0;
        if (SurfaceMaterialPolicy::IsNativeChunkName(material)) return 0;
        Seh::Guard("SurfaceParameters::SetTexture", [&] {
            const auto found = owned.find(material);
            if (found == owned.end() || !techniques || !technique || !passes || !passAt || !units || !unit || !aliasName || !unitName || !textureName) return;
            changed = SetTextureOn(found->second.get(), alias, texture);
        });
        return changed;
    }

    int SetTextureAlias(const std::string& material, const std::string& group, const std::string& alias, const std::string& texture) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        int changed = 0;
        if (material.empty() || SurfaceMaterialPolicy::IsNativeChunkName(material)) return 0;
        Seh::Guard("SurfaceParameters::SetTextureAlias", [&] {
            if (!manager || !lookup || !techniques || !technique || !passes || !passAt || !units || !unit || !aliasName || !unitName || !textureName) return;
            const auto mat = LookupMaterial(manager.Get()(), material, group);
            if (mat.isNull()) return;
            changed = SetTextureOn(mat.get(), alias, texture);
        }, [](unsigned long code) { Logging::LogMessage("Surface texture alias fault code=%08lX", code); });
        return changed;
    }

    int SetFragmentProgram(const std::string& material, const std::string& scheme, const std::string& program) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        int changed = 0;
        if (material.find("exu_surface_") != 0 || SurfaceMaterialPolicy::IsNativeChunkName(material) || program.empty() || program.size() > 128) return 0;
        Seh::Guard("SurfaceParameters::SetFragmentProgram", [&] {
            const auto found = owned.find(material);
            if (found == owned.end() || !techniques || !technique || !passes || !passAt || !has || !schemeName || !setProgram) return;
            auto* mat = found->second.get();
            const unsigned short count = techniques.Get()(mat);
            if (count > 128) return;
            for (unsigned short t = 0; t < count; ++t)
            {
                auto* tech = technique.Get()(mat, t);
                if (schemeName.Get()(tech) != scheme) continue;
                const unsigned short pc = passes.Get()(tech);
                if (pc > 32) return;
                for (unsigned short p = 0; p < pc; ++p)
                {
                    auto* current = passAt.Get()(tech, p);
                    if (!has.Get()(current)) continue;
                    setProgram.Get()(current, program, true); ++changed;
                }
            }
            // resetParams replaced the parameter objects: force SetOwnedVector to re-resolve.
            if (changed) ownedParameters.erase(material);
        }, [](unsigned long code) { Logging::LogMessage("Surface fragment program fault code=%08lX", code); });
        return changed;
    }

    namespace
    {
        bool ReleaseImpl(const std::string& name)
        {
            bool result = false;
            Seh::Guard("SurfaceParameters::Release", [&] {
                const auto found = owned.find(name);
                if (found == owned.end() || !manager || !remove) return;
                auto* mgr = manager.Get()();
                if (!mgr) return;
                remove.Get()(mgr, name);
                ownedParameters.erase(name);
                ownedSource.erase(name);
                owned.erase(found); result = true;
            });
            return result;
        }

        // Teardown makes raw released-ABI vtable calls (RuntimeReference::Reset);
        // run it behind the guard. The container is moved to the heap first so a
        // fault cannot make the destructor run a second time outside the guard.
        template <typename Map> void Destroy(Map& map, const char* site)
        {
            auto* doomed = new Map();
            doomed->swap(map);
            Seh::Guard(site, [&] { delete doomed; });
        }
    }

    bool Release(const std::string& name) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        if (SurfaceMaterialPolicy::IsNativeChunkName(name)) return false;
        return ReleaseImpl(name);
    }

    void Clear() noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        chunksEnabled = false;
        chunkSerial = 0;
        chunkMaterials.clear();
        failedChunkSources.Clear();
        Destroy(ownedParameters, "SurfaceParameters::Clear(parameters)");
        while (!owned.empty())
        {
            const auto name = owned.begin()->first;
            if (ReleaseImpl(name)) continue;
            // The manager entry may survive: remember the names so Clone can
            // adopt and remove them instead of rejecting them as foreign.
            for (const auto& entry : owned) previouslyOwned.Add(entry.first);
            break;
        }
        Destroy(owned, "SurfaceParameters::Clear(owned)");
        ownedSource.clear();
    }

    void EnableChunks(bool enabled) noexcept
    { std::lock_guard<std::recursive_mutex> lock(stateMutex); chunksEnabled = enabled; }

    namespace
    {
        std::string ChunkMaterialImpl(const std::string& source)
        {
            if (!chunksEnabled || source.empty() || source.find("exu_surface_") == 0) return source;
            // Pilot flesh already has its own presentation; this finish is metal.
            std::string lower = source;
            for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
            if (lower.find("flesh") != std::string::npos || lower.find("blood") != std::string::npos) return source;
            const auto found = chunkMaterials.find(source);
            if (found != chunkMaterials.end()) return found->second;
            // A source that already failed is not cloned, logged or released again.
            if (failedChunkSources.Contains(source)) return source;
            if (chunkMaterials.size() >= 128) return source;
            const std::string name = "exu_surface_chunk_" + std::to_string(chunkSerial++);
            const float mode[4] = {1,0,0,0};
            const float zero[4] = {};
            // Unsupported materials keep their original techniques and textures.
            if (!CloneImpl(source, name) || !SetOwnedVectorImpl(name, "surfaceMode", mode))
            {
                ReleaseImpl(name);
                if (failedChunkSources.Add(source)) Logging::LogMessage("Surface chunk finish unavailable for material: %s", source.c_str());
                return source;
            }
            SetOwnedVectorImpl(name, "surfaceState", zero);
            chunkMaterials.emplace(source, name);
            return name;
        }
    }

    std::string ChunkMaterial(const std::string& source) noexcept
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);
        std::string result;
        const bool completed = Seh::Guard("SurfaceParameters::ChunkMaterial", [&] { result = ChunkMaterialImpl(source); });
        return completed ? result : source;
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
    // Same barrier as FinishChunk: no bad_alloc or fault crosses the DLL boundary.
    bool replaced = false;
    ExtraUtilities::Seh::Guard("EXU_GetDestructionMaterial", [&] {
        const std::string original(source);
        const auto result = ExtraUtilities::OgreSurfaceParameters::ChunkMaterial(original);
        if (result.size() >= size || result == original) return;
        strcpy_s(output, size, result.c_str());
        replaced = true;
    }, [](unsigned long code) { ExtraUtilities::Logging::LogMessage("Destruction material fault code=%08lX", code); });
    return replaced;
}
