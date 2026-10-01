/* Copyright (C) 2026 GrizzlyOne95
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "UI/NativeHud.h"
#include "OpenShimBridge.h"

namespace ExtraUtilities::NativeHud
{
    namespace
    {
        using CapabilitiesFn = DWORD(WINAPI*)();
        using GetRectFn = BOOL(WINAPI*)(LPCSTR, int*, int*, int*, int*);
        using SetRectFn = BOOL(WINAPI*)(LPCSTR, int, int, int, int);
        using VisibleFn = BOOL(WINAPI*)(LPCSTR, BOOL);
        using RestoreFn = BOOL(WINAPI*)(LPCSTR);

        class OpenShimProvider final : public Provider
        {
        public:
            uint32_t Capabilities() override
            {
                const auto query = OpenShimBridge::Resolve<CapabilitiesFn>("OpenShimGetNativeHudLayoutCapabilities");
                // Presence in bootstrap winmm.dll alone is never availability.
                // A partial/older provider must also expose the full contract.
                if (!query || !OpenShimBridge::HasExport("OpenShimGetNativeHudMeterRect") ||
                    !OpenShimBridge::HasExport("OpenShimGetNativeHudMeterDefaultRect") ||
                    !OpenShimBridge::HasExport("OpenShimSetNativeHudMeterRect") ||
                    !OpenShimBridge::HasExport("OpenShimSetNativeHudMeterVisible") ||
                    !OpenShimBridge::HasExport("OpenShimRestoreNativeHudMeter")) return 0;
                return query();
            }
            bool GetRect(const char* name, bool stock, Rect& rect) override
            {
                const auto get = OpenShimBridge::Resolve<GetRectFn>(stock ?
                    "OpenShimGetNativeHudMeterDefaultRect" : "OpenShimGetNativeHudMeterRect");
                return get && get(name, &rect.x, &rect.y, &rect.w, &rect.h) != FALSE;
            }
            bool SetRect(const char* name, Rect rect) override
            {
                const auto set = OpenShimBridge::Resolve<SetRectFn>("OpenShimSetNativeHudMeterRect");
                return set && set(name, rect.x, rect.y, rect.w, rect.h) != FALSE;
            }
            bool SetVisible(const char* name, bool visible) override
            {
                const auto set = OpenShimBridge::Resolve<VisibleFn>("OpenShimSetNativeHudMeterVisible");
                return set && set(name, visible ? TRUE : FALSE) != FALSE;
            }
            bool Restore(const char* name) override
            {
                const auto restore = OpenShimBridge::Resolve<RestoreFn>("OpenShimRestoreNativeHudMeter");
                return restore && restore(name) != FALSE;
            }
        };

        // Constructed on first use outside loader lock. No optional-module
        // lookup or engine state is retained by either constructor.
        Controls& Owner()
        {
            static OpenShimProvider provider;
            static Controls controls(provider);
            return controls;
        }
    }

    uint32_t Capabilities() { return Owner().Capabilities(); }
    bool Available(Meter meter) { return Owner().Available(meter); }
    bool GetRect(Meter meter, bool stock, Rect& rect) { return Owner().GetRect(meter, stock, rect); }
    Result SetRect(Meter meter, Rect rect) { return Owner().SetRect(meter, rect); }
    Result SetVisible(Meter meter, bool visible) { return Owner().SetVisible(meter, visible); }
    Result Restore(Meter meter) { return Owner().Restore(meter); }
    Result RestoreAll() { return Owner().RestoreOwned(); }
    void ResetMissionState() noexcept
    {
        try { Owner().ResetMission(); }
        catch (...) { /* A disappearing optional provider cannot block shutdown. */ }
    }
}
