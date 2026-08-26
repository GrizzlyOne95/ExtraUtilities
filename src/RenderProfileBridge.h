/* Optional OpenShim renderer-profile bridge shared by ExtraUtilities modules.
 *
 * OpenShim owns renderer capability/policy state; EXU owns content intent.
 * This header mirrors OpenShim's stable winmm ABI (see
 * BZROpenShim include/render_profile.h namespace Abi) without linking against
 * any OpenShim headers, exactly like the storefront/nickname/music bridges.
 * Every query fails closed to "unavailable" so an older or absent shim can
 * never break mission scripts. */
#pragma once

#include "OpenShimBridge.h"

#include <cstdint>

namespace ExtraUtilities::RenderProfileBridge
{
	// Stable request space (OpenShimRequestRenderProfile argument).
	enum class Request : std::uint32_t
	{
		Inherit = 0,
		Retro = 1,
		Redux = 2,
		Enhanced = 3,
	};

	// Stable profile space (getter results).
	enum class Profile : std::uint32_t
	{
		Unknown = 0,
		Retro = 1,
		Redux = 2,
		Enhanced = 3,
	};

	// Stable Enhanced capability bits (subset mirrored for Lua reporting).
	enum Capability : std::uint32_t
	{
		CapNone = 0u,
		CapSchemeRewrite = 1u << 0,
		CapNormalSharpening = 1u << 1,
		CapLinearLighting = 1u << 2,
		CapTerrainEnhanced = 1u << 3,
		CapObjectEnhanced = 1u << 4,
		CapModernPssm = 1u << 5,
		CapLightSelection = 1u << 6,
		CapIblResources = 1u << 7,
		// Keep the mirror append-only with OpenShim. Bit 8 is the mandatory
		// renderer-resource gate added by the finalized render-profile ABI.
		CapEnhancedResources = 1u << 8,
	};

	using GetApiVersionFn = std::uint32_t(__cdecl*)();
	using RequestFn = DWORD(WINAPI*)(DWORD);
	using GetUserFn = DWORD(WINAPI*)();
	using GetRequestedFn = DWORD(WINAPI*)();
	using GetEffectiveFn = DWORD(WINAPI*)();
	using GetBackendFn = DWORD(WINAPI*)();
	using GetCapabilitiesFn = DWORD(WINAPI*)();
	using SupportsFn = BOOL(WINAPI*)(DWORD);

	inline bool HasRenderProfileApi() noexcept
	{
		return OpenShimBridge::HasExport("OpenShimRequestRenderProfile");
	}

	inline std::uint32_t ApiVersion() noexcept
	{
		const auto fn = OpenShimBridge::Resolve<GetApiVersionFn>("OpenShimGetRenderApiVersion");
		return fn ? fn() : 0u;
	}

	// Forwards a content render-profile request to OpenShim. Returns false
	// when the bridge is unavailable or the value was rejected; callers fall
	// back to the legacy local lighting-mode path in that case.
	inline bool Forward(Request request) noexcept
	{
		const auto fn = OpenShimBridge::Resolve<RequestFn>("OpenShimRequestRenderProfile");
		if (!fn)
		{
			return false;
		}
		const DWORD status = fn(static_cast<DWORD>(request));
		// AppliedLive(0) and StoredDeferred(1) are successes; anything else is
		// a rejection and the caller must not assume the override took effect.
		return status == 0u || status == 1u;
	}

	inline Profile GetUserPreference() noexcept
	{
		const auto fn = OpenShimBridge::Resolve<GetUserFn>("OpenShimGetUserRenderProfile");
		return fn ? static_cast<Profile>(fn()) : Profile::Unknown;
	}

	inline Profile GetRequestedContentOverride() noexcept
	{
		const auto fn = OpenShimBridge::Resolve<GetRequestedFn>("OpenShimGetRequestedContentRenderProfile");
		if (!fn)
		{
			return Profile::Unknown;
		}
		const DWORD raw = fn();
		if (raw == 0u)
		{
			return Profile::Unknown; // Inherit: no content override present
		}
		return static_cast<Profile>(raw);
	}

	inline Profile GetEffective() noexcept
	{
		const auto fn = OpenShimBridge::Resolve<GetEffectiveFn>("OpenShimGetEffectiveRenderProfile");
		return fn ? static_cast<Profile>(fn()) : Profile::Unknown;
	}

	inline std::uint32_t GetActiveBackend() noexcept
	{
		const auto fn = OpenShimBridge::Resolve<GetBackendFn>("OpenShimGetActiveRendererBackend");
		return fn ? fn() : 0xFFFFFFFFu; // Unknown backend sentinel
	}

	inline std::uint32_t Capabilities() noexcept
	{
		const auto fn = OpenShimBridge::Resolve<GetCapabilitiesFn>("OpenShimGetRenderCapabilities");
		return fn ? fn() : CapNone;
	}

	inline bool Supports(Profile profile) noexcept
	{
		const auto fn = OpenShimBridge::Resolve<SupportsFn>("OpenShimSupportsRenderProfile");
		return fn ? fn(static_cast<DWORD>(profile)) != FALSE : false;
	}
}
