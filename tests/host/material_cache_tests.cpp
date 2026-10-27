#include "Ogre/OgreMaterialCache.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

#ifdef _WIN32
#include "Ogre/OgreMaterialShim.h"
#include <type_traits>
#endif

namespace
{
    void Check(bool ok, const char* message)
    {
        if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
    }
    struct Handle
    {
        std::shared_ptr<int> value;
        bool isNull() const { return !value; }
    };
}

int main()
{
    ExtraUtilities::Ogre::MaterialCache<Handle> cache;
    const Handle a{std::make_shared<int>(1)}, b{std::make_shared<int>(2)};
    cache.Store("metal", "A", a);
    cache.Store("metal", "B", b);
    Handle found;
    Check(cache.Find("metal", "A", found) && found.value == a.value, "group A");
    Check(cache.Find("metal", "B", found) && found.value == b.value, "group B");
    Check(!cache.Find("metal", "Missing", found) && found.isNull(), "missing group is a miss");
    Check(!cache.Find("metal", "Autodetect", found), "Autodetect must reach native resolver on miss");
    cache.Store("metal", "Autodetect", b);
    Check(cache.Find("metal", "Autodetect", found) && found.value == b.value, "Autodetect isolated key");
    found = {};
    cache.Store("metal", "A", b);
    Check(a.value.use_count() == 1, "replacement releases previous cached owner");
    cache.Clear();
    Check(b.value.use_count() == 1, "clear releases every cached owner");
    std::weak_ptr<int> previous;
    for (int i = 0; i != 4; ++i)
    {
        const Handle mission{std::make_shared<int>(i)};
        previous = mission.value;
        cache.Store("mission", "A", mission);
        cache.Clear();
        Check(mission.value.use_count() == 1, "mission clear balances ownership");
    }
    Check(previous.expired(), "no previous mission retained");

#ifdef _WIN32
    // The adjacent test DLL models the released two-pointer / info+14h ABI.
    // This tests our actual native-return wrappers and owning holder, without
    // loading or substituting any DLL in the game installation.
    wchar_t path[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, path, MAX_PATH) != 0, "test exe path");
    auto* slash = wcsrchr(path, L'\\');
    Check(slash != nullptr, "test exe directory");
    wcscpy_s(slash + 1, static_cast<size_t>(path + MAX_PATH - slash - 1), L"OgreMain.dll");
    const auto fixture = LoadLibraryW(path);
    Check(fixture != nullptr, "load test Ogre fixture");
    using Count = int (__cdecl*)();
    const auto live = reinterpret_cast<Count>(GetProcAddress(fixture, "LiveBlocks"));
    const auto destroyed = reinterpret_cast<Count>(GetProcAddress(fixture, "DestroyedBlocks"));
    Check(live && destroyed, "fixture counters");
    static_assert(std::is_trivially_destructible_v<::Ogre::SharedPtr<::Ogre::Material>>,
        "borrowed views must not release unacquired references");
    ExtraUtilities::Ogre::MaterialCache<::Ogre::MaterialReference> nativeCache;
    auto* manager = ::Ogre::GetMaterialManagerSingletonPtr();
    Check(manager != nullptr, "fixture manager");
    for (int i = 0; i != 4; ++i)
    {
        {
            auto material = ::Ogre::GetMaterialByName(manager, "metal", "A");
            Check(!material.isNull() && *material.Count() == 1, "own native returned reference");
            nativeCache.Store("metal", "A", material);
            Check(*material.Count() == 2, "cache retains one reference");
            {
                auto copy = material;
                Check(*material.Count() == 3, "owning copy retains");
                auto moved = std::move(copy);
                Check(copy.isNull() && *moved.Count() == 3, "move transfers without retain");
            }
            Check(*material.Count() == 2, "temporary reference released");
        }
        Check(live() == 1, "cache alone keeps material alive");
        nativeCache.Clear();
        Check(live() == 0, "last reference destroys and frees native block");
    }
    Check(destroyed() == 4, "one destruction per mission");
    {
        auto source = ::Ogre::GetMaterialByName(manager, "source", "A");
        auto clone = ::Ogre::CloneMaterial(source.get(), "clone", false, "A");
        Check(live() == 2 && *clone.Count() == 1, "clone adopts native owning return");
        ::Ogre::SharedPtr<::Ogre::Material> assigned;
        Check(::Ogre::SetEntityMaterial(&assigned, source) && assigned.pRep == source.rep &&
            assigned.pInfo == source.info && *source.Count() == 1, "entity assignment borrows ABI view");
        Check(::Ogre::SetSubEntityMaterial(&assigned, clone) && assigned.pRep == clone.rep &&
            assigned.pInfo == clone.info && *clone.Count() == 1, "subentity assignment borrows ABI view");
        nativeCache.Store("same", "A", source);
        nativeCache.Store("same", "A", clone);
        Check(*source.Count() == 1 && *clone.Count() == 2, "native replacement releases old reference");
        nativeCache.Clear();
    }
    Check(live() == 0 && destroyed() == 6, "clone and source released once");
    // Keep the fixture loaded until static Ogre export holders are destroyed.
#endif
    std::puts("material group / cache ownership tests passed");
}
