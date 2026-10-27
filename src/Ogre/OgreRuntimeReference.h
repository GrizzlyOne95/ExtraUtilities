#pragma once
#include "OgreProc.h"
#include <Windows.h>
#include <cstdint>
#include <utility>

namespace ExtraUtilities { namespace OgreDll
{
    // Released OgreMain x86 ABI, also qualified by OpenShim's import cache:
    // SharedPtr is two pointers; native copy/release uses lock xadd at info+14h.
    // The public 1.10 header's +4 counter is NOT the released binary layout.
    // Never instantiate SDK SharedPtr to own a reference returned by the DLL.
    template<class T> struct RuntimeReference
    {
        T* rep = nullptr;
        void* info = nullptr;
        RuntimeReference() noexcept = default;
        RuntimeReference(const RuntimeReference& other) noexcept : rep(other.rep), info(other.info)
        { if (ValidCount()) InterlockedIncrement(Count()); }
        RuntimeReference(RuntimeReference&& other) noexcept : rep(other.rep), info(other.info)
        { other.rep=nullptr; other.info=nullptr; }
        RuntimeReference& operator=(RuntimeReference other) noexcept
        { std::swap(rep,other.rep);std::swap(info,other.info);return *this; }
        ~RuntimeReference() { Reset(); }
        T* get() const noexcept { return rep; }
        T* getPointer() const noexcept { return rep; }
        bool isNull() const noexcept { return !rep; }
        volatile LONG* Count() const noexcept
        { return reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(info)+0x14); }
        bool ValidCount() const noexcept
        { return rep && info && *Count()>0 && *Count()<0x10000000; }
        void Reset() noexcept
        {
            using Free = void (__cdecl*)(void*);
            using Destroy = void (__thiscall*)(void*,unsigned);
            static OgreProc<Free> freeInfo{"?deallocateBytes@StdAllocPolicy@Ogre@@SAXPAX@Z"};
            if (ValidCount() && freeInfo && InterlockedDecrement(Count())==0)
            {
                // The non-deleting virtual destructor owns the pointee; the
                // control block itself goes back to the DLL's allocator.
                auto destroy=reinterpret_cast<Destroy>((*static_cast<void***>(info))[0]);
                destroy(info,0);freeInfo.Get()(info);
            }
            rep=nullptr;info=nullptr;
        }
    };
    static_assert(sizeof(RuntimeReference<void>)==8,"Released Ogre SharedPtr is x86/two pointers");
} }
