#pragma once
#include "OpenShimBridge.h"
#include "FogWakeApi.h"
namespace ExtraUtilities::FogWakeBridge {
inline bool Configure(const FogWakeApi::Config& c) {
    auto fn = OpenShimBridge::Resolve<FogWakeApi::ConfigureFn>("OpenShimConfigureFogWake");
    return fn && fn(&c) == 1;
}
inline bool Update(double now) {
    auto fn = OpenShimBridge::Resolve<FogWakeApi::UpdateFn>("OpenShimUpdateFogWake");
    return fn && fn(now) == 1;
}
inline bool Observe(std::uint32_t id, float x, float y, float z) {
    auto fn = OpenShimBridge::Resolve<FogWakeApi::ObserveFn>("OpenShimObserveFogWake");
    return fn && fn(id, x, y, z) == 1;
}
inline bool Remove(std::uint32_t id) {
    auto fn = OpenShimBridge::Resolve<FogWakeApi::RemoveFn>("OpenShimRemoveFogWakeEmitter");
    return fn && fn(id) == 1;
}
inline bool Reset() {
    auto fn = OpenShimBridge::Resolve<FogWakeApi::ResetFn>("OpenShimResetFogWake");
    return fn && fn() == 1;
}
inline FogWakeApi::Status Status() {
    FogWakeApi::Status result;
    auto fn = OpenShimBridge::Resolve<FogWakeApi::StatusFn>("OpenShimGetFogWakeStatus");
    if (!fn || fn(&result) != 1 || result.version != FogWakeApi::Version)
        result = FogWakeApi::Status{};
    return result;
}
}
