// Synthetic released-ABI fixture. Never deployed to a game directory.
#include <Windows.h>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace
{
    int liveBlocks = 0;
    int destroyedBlocks = 0;
    struct Info
    {
        virtual ~Info() { --liveBlocks; ++destroyedBlocks; }
        unsigned char padding[16]{}; // vptr + padding puts count at +14h
        LONG count = 1;
    };
    struct NativeReference { void* rep; void* info; };
    void ReturnOwned(NativeReference* out)
    {
        auto* info = new Info;
        ++liveBlocks;
        // RuntimeReference never dereferences the pointee; Info owns disposal.
        out->rep = info;
        out->info = info;
        if (reinterpret_cast<unsigned char*>(&info->count) -
            reinterpret_cast<unsigned char*>(info) != 0x14) std::abort();
    }
}

namespace Ogre
{
    class __declspec(dllexport) MaterialManager
    {
    public:
        static MaterialManager* __cdecl getSingletonPtr();
    };
    MaterialManager* MaterialManager::getSingletonPtr()
    { static MaterialManager manager; return &manager; }
    class __declspec(dllexport) StdAllocPolicy
    {
    public:
        static void __cdecl deallocateBytes(void* info);
    };
    void StdAllocPolicy::deallocateBytes(void* info) { ::operator delete(info); }
}
extern "C" void __fastcall FakeGetByName(void*, void*, NativeReference* out,
    const std::string&, const std::string&) { ReturnOwned(out); }
extern "C" void __fastcall FakeClone(void*, void*, NativeReference* out,
    const std::string&, bool, const std::string&) { ReturnOwned(out); }
extern "C" void __fastcall FakeSetMaterial(NativeReference* entity, void*, const NativeReference& material)
{ *entity = material; }
extern "C" __declspec(dllexport) int __cdecl LiveBlocks() { return liveBlocks; }
extern "C" __declspec(dllexport) int __cdecl DestroyedBlocks() { return destroyedBlocks; }
