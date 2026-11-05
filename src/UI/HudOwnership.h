// Copyright (C) 2026 GrizzlyOne95; LGPL-3.0-or-later
#pragma once
#include "OpenShimBridge.h"
#include <atomic>

namespace ExtraUtilities::HudOwnership
{
    constexpr uint32_t Hull=1, Ammo=2, Weapons=4, Scrap=8, Radar=16, Commands=32;
    inline std::atomic<uint32_t> claimed{0};
    using SetFn=uint32_t(__cdecl*)(uint32_t);
    using GetFn=uint32_t(__cdecl*)();
    inline SetFn Setter() noexcept
    {
        if (!OpenShimBridge::GetModule()) return nullptr;
        const auto provider=GetModuleHandleW(L"openshim.dll");
        return provider?reinterpret_cast<SetFn>(GetProcAddress(provider,"OpenShimSetExternalHudOwnership")):nullptr;
    }
    inline bool HasProtocol() noexcept {return Setter()!=nullptr;}
    inline bool Update(uint32_t area,bool own) noexcept
    {
        const auto set=Setter();
        if (!set) return false;
        const auto previous=claimed.load(std::memory_order_acquire);
        const auto next=own?(previous|area):(previous&~area);
        const auto granted=set(next);
        if (granted!=next) return false;
        claimed.store(next,std::memory_order_release);
        return true;
    }
    inline bool Owns(uint32_t area) noexcept {return (claimed.load(std::memory_order_acquire)&area)!=0;}
    inline void Reset() noexcept
    {
        if (const auto set=Setter()) set(0);
        claimed.store(0,std::memory_order_release);
    }
}
