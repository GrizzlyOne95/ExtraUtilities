/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "EnvironmentInternal.h"
#include "Util/SehGuard.h"

// Legacy viewport lighting modes (default, enhanced, retro) applied through
// the viewport material scheme, the game's setMaterialScheme call-site hook,
// Glow gating for retro, and the OpenShim render-profile bindings that
// supersede them when the shim provides that ABI.

namespace ExtraUtilities::Lua::Environment
{
	namespace
	{
		std::string g_lastModernMaterialScheme = "high-pssm";

		constexpr std::string_view kDefaultModernMaterialScheme = "high-pssm";

		bool IsModernMaterialScheme(std::string_view scheme)
		{
			return scheme == "high-pssm"
				|| scheme == "high"
				|| scheme == "high-noshadow"
				|| scheme == "medium-pssm"
				|| scheme == "medium"
				|| scheme == "medium-noshadow"
				|| scheme == "low-pssm"
				|| scheme == "low"
				|| scheme == "low-noshadow"
				|| scheme == "lowest-pssm"
				|| scheme == "lowest"
				|| scheme == "lowest-noshadow";
		}

		bool HasPrefix(std::string_view value, std::string_view prefix)
		{
			return value.rfind(prefix, 0) == 0;
		}

		std::string NormalizeModernMaterialScheme(std::string_view scheme)
		{
			if (scheme.empty())
			{
				return g_lastModernMaterialScheme.empty()
					? std::string(kDefaultModernMaterialScheme)
					: g_lastModernMaterialScheme;
			}

			if (scheme.rfind("og-", 0) == 0)
			{
				scheme.remove_prefix(3);
			}
			else if (scheme.rfind("en-", 0) == 0)
			{
				scheme.remove_prefix(3);
			}

			if (IsModernMaterialScheme(scheme))
			{
				return std::string(scheme);
			}

			return g_lastModernMaterialScheme.empty()
				? std::string(kDefaultModernMaterialScheme)
				: g_lastModernMaterialScheme;
		}

		std::string BuildRetroMaterialScheme(const std::string& modernScheme)
		{
			return "og-" + modernScheme;
		}

		std::string BuildEnhancedMaterialScheme(const std::string& modernScheme)
		{
			return "en-" + modernScheme;
		}

		ViewportLightingMode GetLightingModeForScheme(std::string_view scheme)
		{
			if (HasPrefix(scheme, "og-"))
			{
				return ViewportLightingMode::Retro;
			}
			if (HasPrefix(scheme, "en-"))
			{
				return ViewportLightingMode::Enhanced;
			}
			return ViewportLightingMode::Default;
		}

		const char* GetLightingModeName(ViewportLightingMode mode)
		{
			switch (mode)
			{
			case ViewportLightingMode::Enhanced:
				return "enhanced";
			case ViewportLightingMode::Retro:
				return "retro";
			case ViewportLightingMode::Default:
			default:
				return "default";
			}
		}

		std::string BuildLightingMaterialScheme(ViewportLightingMode mode, const std::string& modernScheme)
		{
			switch (mode)
			{
			case ViewportLightingMode::Enhanced:
				return BuildEnhancedMaterialScheme(modernScheme);
			case ViewportLightingMode::Retro:
				return BuildRetroMaterialScheme(modernScheme);
			case ViewportLightingMode::Default:
			default:
				return modernScheme;
			}
		}

		bool TryParseLightingModeArg(lua_State* L, int idx, ViewportLightingMode& outMode)
		{
			switch (lua_type(L, idx))
			{
			case LUA_TBOOLEAN:
				outMode = CheckBool(L, idx) ? ViewportLightingMode::Retro : ViewportLightingMode::Default;
				return true;
			case LUA_TNUMBER:
			{
				const int numericMode = static_cast<int>(luaL_checkinteger(L, idx));
				switch (numericMode)
				{
				case 2:
					outMode = ViewportLightingMode::Enhanced;
					return true;
				case 3:
					outMode = ViewportLightingMode::Retro;
					return true;
				case 1:
				default:
					outMode = ViewportLightingMode::Default;
					return true;
				}
			}
			case LUA_TSTRING:
			{
				const std::string_view rawMode = luaL_checkstring(L, idx);
				if (rawMode == "default" || rawMode == "modern")
				{
					outMode = ViewportLightingMode::Default;
					return true;
				}
				if (rawMode == "enhanced" || rawMode == "en")
				{
					outMode = ViewportLightingMode::Enhanced;
					return true;
				}
				if (rawMode == "retro" || rawMode == "og")
				{
					outMode = ViewportLightingMode::Retro;
					return true;
				}
				luaL_error(L, "lighting mode must be default, enhanced, or retro");
				return false;
			}
			default:
				luaL_error(L, "lighting mode must be a string, integer, or boolean");
				return false;
			}
		}
	}

	namespace Detail
	{
		// The engine recreates/reactivates viewports (satellite view, sniper
		// scope, resolution or scene reload) and starts each fresh viewport on
		// its native (modern) material scheme. A one-shot apply therefore
		// "flashes back" to modern the next time a viewport is rebuilt. We keep
		// the user's chosen mode here so it can be re-asserted; enforcement is
		// idempotent and only rewrites a viewport whose scheme has drifted.
		ViewportLightingMode g_desiredLightingMode = ViewportLightingMode::Default;
	}

	int GetRetroLightingMode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		// Canonical path: report OpenShim's effective profile so scripts see
		// the same truth the renderer acts on, including user INI preferences
		// this process never touched through Set* calls.
		if (RenderProfileBridge::HasRenderProfileApi())
		{
			lua_pushboolean(
				L,
				RenderProfileBridge::GetEffective() == RenderProfileBridge::Profile::Retro
					? 1
					: 0);
			return 1;
		}

		auto* viewport = GetCurrentViewport();
		std::string scheme;
		TryGetViewportMaterialScheme(viewport, scheme);
		lua_pushboolean(L, GetLightingModeForScheme(scheme) == ViewportLightingMode::Retro ? 1 : 0);
		return 1;
	}

	int GetLightingMode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		if (RenderProfileBridge::HasRenderProfileApi())
		{
			switch (RenderProfileBridge::GetEffective())
			{
			case RenderProfileBridge::Profile::Enhanced:
				lua_pushstring(L, "enhanced");
				return 1;
			case RenderProfileBridge::Profile::Retro:
				lua_pushstring(L, "retro");
				return 1;
			case RenderProfileBridge::Profile::Redux:
				lua_pushstring(L, "default");
				return 1;
			default:
				break; // fall through to the legacy scheme read
			}
		}

		auto* viewport = GetCurrentViewport();
		std::string scheme;
		TryGetViewportMaterialScheme(viewport, scheme);
		lua_pushstring(L, GetLightingModeName(GetLightingModeForScheme(scheme)));
		return 1;
	}

	// ---- Glow compositor gating ------------------------------------------
	// The bloom compositor ("Glow", BZ_ASSETS_CORE/common/programs/
	// glow.compositor) renders every material's `scheme glow` technique into
	// the emissive bloom overlay regardless of which viewport scheme is
	// active, so retro mode kept glowing. Retro is flat shading only: keep the
	// compositor disabled while retro is the desired mode.
	using FnCompositorManagerGetSingletonPtr = void*(__cdecl*)();
	using FnCompositorManagerSetCompositorEnabled =
		void(__thiscall*)(void*, void*, const std::string&, bool);

	bool CallSetCompositorEnabledGuardedSeh(
		FnCompositorManagerSetCompositorEnabled fn,
		void* manager,
		void* viewport,
		const std::string* name,
		bool enabled)
	{
		__try
		{
			fn(manager, viewport, *name, enabled);
			return true;
		}
		__except (Seh::Filter(GetExceptionCode()))
		{
			LogEnvironmentFault(
				"[EXU::Viewport] setCompositorEnabled crashed viewport=%p enabled=%d code=0x%08X",
				viewport,
				enabled ? 1 : 0,
				GetExceptionCode());
			return false;
		}
	}

	bool CallSetCompositorEnabledGuarded(
		FnCompositorManagerSetCompositorEnabled fn,
		void* manager,
		void* viewport,
		const std::string* name,
		bool enabled)
	{
		return Seh::CatchCpp("CallSetCompositorEnabledGuarded", [&] { return CallSetCompositorEnabledGuardedSeh(fn, manager, viewport, name, enabled); }, false);
	}

	bool TrySetGlowCompositorEnabled(void* viewport, bool enabled)
	{
		static FnCompositorManagerGetSingletonPtr getSingletonPtr =
			ResolveOgreProc<FnCompositorManagerGetSingletonPtr>(
				"?getSingletonPtr@CompositorManager@Ogre@@SAPAV12@XZ");
		static FnCompositorManagerSetCompositorEnabled setCompositorEnabled =
			ResolveOgreProc<FnCompositorManagerSetCompositorEnabled>(
				"?setCompositorEnabled@CompositorManager@Ogre@@QAEXPAVViewport@2@"
				"ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@_N@Z");
		if (viewport == nullptr || getSingletonPtr == nullptr || setCompositorEnabled == nullptr)
		{
			return false;
		}

		void* manager = getSingletonPtr();
		if (manager == nullptr)
		{
			return false;
		}

		static const std::string glowCompositorName("Glow");
		return CallSetCompositorEnabledGuarded(
			setCompositorEnabled, manager, viewport, &glowCompositorName, enabled);
	}

	// ---- Game viewport scheme call-site takeover ---------------------------
	// The exe re-asserts each viewport's material scheme from its own graphics
	// settings roughly once a second (0x00680FE0 walks every viewport of the
	// render target) and on viewport creation (0x00682AA0/0x00682EA7, literal
	// "low-noshadow"), always through `call [IAT 0x00869810]` =
	// Ogre::Viewport::setMaterialScheme. Re-enforcing our scheme afterwards
	// produced a visible default<->enhanced flip every second. Rewriting the
	// scheme inside the game's own call removes the fight entirely and makes
	// enhanced/retro safely hot-swappable.
	constexpr uintptr_t kViewportSetMaterialSchemeIat = 0x00869810;
	constexpr uintptr_t kViewportSchemeCallSites[] = {
		0x00681585, // settings reassert loop over all viewports
		0x00682AA0, // secondary viewport creation ("low-noshadow")
		0x00682EA7, // tertiary viewport creation ("low-noshadow")
	};

	using FnViewportSetMaterialScheme = void(__thiscall*)(void*, const std::string&);

	std::string g_lastSchemeRewriteLogged;

	// __fastcall(this, edx, one stack arg) matches the __thiscall call site:
	// ecx carries the viewport, the string ref stays on the stack, and the
	// callee cleans the 4 stack bytes exactly like the original would.
	void __fastcall GameViewportSetMaterialSchemeHook(
		void* viewport,
		void* /*unusedEdx*/,
		const std::string* scheme)
	{
		const FnViewportSetMaterialScheme original =
			*reinterpret_cast<FnViewportSetMaterialScheme*>(kViewportSetMaterialSchemeIat);

		std::string incoming;
		if (scheme != nullptr)
		{
			incoming = *scheme;
		}

		// Quality changes flow through here; remember the game's modern base
		// so mode switches keep tracking the user's graphics settings.
		if (IsModernMaterialScheme(incoming))
		{
			g_lastModernMaterialScheme = incoming;
		}

		std::string finalScheme = incoming;
		if (g_desiredLightingMode != ViewportLightingMode::Default && !incoming.empty())
		{
			finalScheme = BuildLightingMaterialScheme(
				g_desiredLightingMode,
				NormalizeModernMaterialScheme(incoming));
		}

		if (finalScheme != incoming && g_lastSchemeRewriteLogged != finalScheme)
		{
			g_lastSchemeRewriteLogged = finalScheme;
			LogEnvironmentDebug(
				"[EXU::Viewport] game scheme call rewritten viewport=%p incoming=%s final=%s",
				viewport,
				incoming.c_str(),
				finalScheme.c_str());
		}

		if (original != nullptr)
		{
			original(viewport, finalScheme);
		}
		else
		{
			TrySetViewportMaterialScheme(viewport, finalScheme);
		}
	}

	// The call sites are patched to `call [&this pointer]`, so it must outlive
	// the process (it does: namespace-scope static).
	void* const g_GameViewportSetMaterialSchemeHookPtr =
		reinterpret_cast<void*>(&GameViewportSetMaterialSchemeHook);

	// Shared apply path for SetLightingMode / SetRetroLightingMode / enforcement.
	// Resolves the modern base scheme from the primary viewport, builds the
	// target scheme for the requested mode, and applies it to every active
	// viewport. Returns true if at least one viewport was updated.
	bool ApplyLightingModeToActiveViewports(
		const ActiveViewportSet& activeViewports,
		ViewportLightingMode requestedMode,
		std::string* outCurrentScheme = nullptr,
		std::string* outTargetScheme = nullptr)
	{
		if (activeViewports.count == 0)
		{
			return false;
		}

		std::string currentScheme;
		TryGetViewportMaterialScheme(activeViewports.viewports[0], currentScheme);
		const std::string modernScheme = NormalizeModernMaterialScheme(currentScheme);
		const std::string targetScheme = BuildLightingMaterialScheme(requestedMode, modernScheme);

		if (IsModernMaterialScheme(modernScheme))
		{
			g_lastModernMaterialScheme = modernScheme;
		}

		bool appliedAny = false;
		for (size_t i = 0; i < activeViewports.count; ++i)
		{
			void* viewport = activeViewports.viewports[i];
			if (!TrySetViewportMaterialScheme(viewport, targetScheme))
			{
				LogEnvironmentDebug(
					"[EXU::Viewport] failed to set material scheme viewport=%p current=%s target=%s",
					viewport,
					currentScheme.c_str(),
					targetScheme.c_str());
				continue;
			}

			appliedAny = true;
			TryRefreshViewport(viewport);
			TrySetGlowCompositorEnabled(viewport, requestedMode != ViewportLightingMode::Retro);
		}

		if (outCurrentScheme != nullptr)
		{
			*outCurrentScheme = currentScheme;
		}
		if (outTargetScheme != nullptr)
		{
			*outTargetScheme = targetScheme;
		}
		return appliedAny;
	}

	// Re-asserts the user's chosen lighting mode on any active viewport whose
	// material scheme has drifted back to a different mode (which happens when
	// the engine rebuilds/reactivates a viewport). Idempotent and cheap: it is a
	// no-op unless a viewport actually drifted, so it is safe to call every
	// frame. Default mode is the engine's own scheme, so nothing is enforced.
	void EnforceDesiredLightingMode()
	{
		// With the render-profile bridge present, OpenShim owns scheme
		// application and drift correction (its takeover rewrites every
		// engine scheme call); enforcing here would fight the canonical
		// owner.
		if (RenderProfileBridge::HasRenderProfileApi())
		{
			return;
		}

		if (g_desiredLightingMode == ViewportLightingMode::Default)
		{
			return;
		}

		const ActiveViewportSet activeViewports = GetActiveViewports();
		if (activeViewports.count == 0)
		{
			return;
		}

		// The game re-enables the Glow compositor when it rebuilds a viewport,
		// which would leak emissive bloom back into retro without any scheme
		// drift to trigger a reapply. setCompositorEnabled is idempotent, so
		// assert it on every enforcement tick.
		if (g_desiredLightingMode == ViewportLightingMode::Retro)
		{
			for (size_t i = 0; i < activeViewports.count; ++i)
			{
				TrySetGlowCompositorEnabled(activeViewports.viewports[i], false);
			}
		}

		bool drifted = false;
		for (size_t i = 0; i < activeViewports.count; ++i)
		{
			std::string scheme;
			if (!TryGetViewportMaterialScheme(activeViewports.viewports[i], scheme))
			{
				continue;
			}
			if (GetLightingModeForScheme(scheme) != g_desiredLightingMode)
			{
				drifted = true;
				break;
			}
		}
		if (!drifted)
		{
			return;
		}

		std::string currentScheme;
		std::string targetScheme;
		if (ApplyLightingModeToActiveViewports(activeViewports, g_desiredLightingMode, &currentScheme, &targetScheme))
		{
			LogEnvironmentDebug(
				"[EXU::Viewport] re-enforced lighting mode=%s currentScheme=%s targetScheme=%s viewportCount=%zu",
				GetLightingModeName(g_desiredLightingMode),
				currentScheme.c_str(),
				targetScheme.c_str(),
				activeViewports.count);
		}
	}

	// Legacy ViewportLightingMode -> canonical content request. "default" was
	// always the engine-native modern scheme, which the profile model names
	// Redux; it is a real request (not Inherit) so legacy scripts keep their
	// exact override semantics across the bridge.
	RenderProfileBridge::Request ToContentRequest(ViewportLightingMode mode)
	{
		switch (mode)
		{
		case ViewportLightingMode::Enhanced:
			return RenderProfileBridge::Request::Enhanced;
		case ViewportLightingMode::Retro:
			return RenderProfileBridge::Request::Retro;
		case ViewportLightingMode::Default:
		default:
			return RenderProfileBridge::Request::Redux;
		}
	}

	int SetLightingMode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		ViewportLightingMode requestedMode = ViewportLightingMode::Default;
		if (!TryParseLightingModeArg(L, 1, requestedMode))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		// Canonical path: OpenShim owns renderer policy and scheme application
		// once its render-profile bridge is present; EXU only forwards intent.
		if (RenderProfileBridge::HasRenderProfileApi())
		{
			g_desiredLightingMode = requestedMode;
			const bool forwarded =
				RenderProfileBridge::Forward(ToContentRequest(requestedMode));
			LogEnvironmentDebug(
				"[EXU::Viewport] lighting mode=%s forwardedToShim=%d",
				GetLightingModeName(requestedMode),
				forwarded ? 1 : 0);
			lua_pushboolean(L, forwarded ? 1 : 0);
			return 1;
		}

		// Remember the choice unconditionally: at mission start this can run
		// before the first viewport exists, and EnforceDesiredLightingMode /
		// the next call must still know what the user asked for. Dropping the
		// request here is what used to leave enhanced mode stuck on default.
		g_desiredLightingMode = requestedMode;

		const ActiveViewportSet activeViewports = GetActiveViewports();
		if (activeViewports.count == 0)
		{
			LogEnvironmentDebug(
				"[EXU::Viewport] lighting mode=%s deferred (no active viewport yet)",
				GetLightingModeName(requestedMode));
			lua_pushboolean(L, 0);
			return 1;
		}

		std::string currentScheme;
		std::string targetScheme;
		if (!ApplyLightingModeToActiveViewports(activeViewports, requestedMode, &currentScheme, &targetScheme))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		LogEnvironmentDebug(
			"[EXU::Viewport] lighting mode=%s currentScheme=%s targetScheme=%s viewportCount=%zu primary=%p secondary=%p",
			GetLightingModeName(requestedMode),
			currentScheme.c_str(),
			targetScheme.c_str(),
			activeViewports.count,
			activeViewports.count > 0 ? activeViewports.viewports[0] : nullptr,
			activeViewports.count > 1 ? activeViewports.viewports[1] : nullptr);
		lua_pushboolean(L, 1);
		return 1;
	}

	int SetRetroLightingMode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		const bool enabled = CheckBool(L, 1);
		const ViewportLightingMode requestedMode = enabled
			? ViewportLightingMode::Retro
			: ViewportLightingMode::Default;

		if (RenderProfileBridge::HasRenderProfileApi())
		{
			g_desiredLightingMode = requestedMode;
			const bool forwarded =
				RenderProfileBridge::Forward(ToContentRequest(requestedMode));
			LogEnvironmentDebug(
				"[EXU::Viewport] retro lighting=%d forwardedToShim=%d",
				enabled ? 1 : 0,
				forwarded ? 1 : 0);
			lua_pushboolean(L, forwarded ? 1 : 0);
			return 1;
		}

		// Remember the choice before touching viewports so a pre-viewport call
		// still takes effect once EnforceDesiredLightingMode sees a viewport.
		g_desiredLightingMode = requestedMode;

		const ActiveViewportSet activeViewports = GetActiveViewports();
		if (activeViewports.count == 0)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		std::string currentScheme;
		std::string targetScheme;
		if (!ApplyLightingModeToActiveViewports(activeViewports, requestedMode, &currentScheme, &targetScheme))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		LogEnvironmentDebug(
			"[EXU::Viewport] retro lighting=%d currentScheme=%s targetScheme=%s viewportCount=%zu primary=%p secondary=%p",
			enabled ? 1 : 0,
			currentScheme.c_str(),
			targetScheme.c_str(),
			activeViewports.count,
			activeViewports.count > 0 ? activeViewports.viewports[0] : nullptr,
			activeViewports.count > 1 ? activeViewports.viewports[1] : nullptr);
		lua_pushboolean(L, 1);
		return 1;
	}

	// Lua-callable: exu.EnforceLightingMode(). Re-asserts the saved lighting mode
	// if a viewport has drifted. Intended to be called from the mod's per-frame
	// update so retro/enhanced lighting persists across satellite view, sniper
	// scope, and resolution/scene reloads.
	int EnforceLightingMode(lua_State* L)
	{
		(void)L;
		Patch::TryInitializeOgre();
		EnforceDesiredLightingMode();
		return 0;
	}

	// ---- OpenShim render-profile API (canonical renderer ownership) -------
	//
	// These exports let content speak in profile terms directly. Every query
	// degrades gracefully when the shim is absent or too old to expose the
	// render-profile ABI, so scripts can probe without risking failure.

	bool TryParseRenderProfileArg(lua_State* L, int idx,
								  RenderProfileBridge::Request& outRequest)
	{
		switch (lua_type(L, idx))
		{
		case LUA_TSTRING:
		{
			const std::string_view raw = luaL_checkstring(L, idx);
			if (raw == "inherit")
			{
				outRequest = RenderProfileBridge::Request::Inherit;
				return true;
			}
			if (raw == "retro" || raw == "og")
			{
				outRequest = RenderProfileBridge::Request::Retro;
				return true;
			}
			if (raw == "redux" || raw == "default" || raw == "modern")
			{
				outRequest = RenderProfileBridge::Request::Redux;
				return true;
			}
			if (raw == "enhanced" || raw == "en")
			{
				outRequest = RenderProfileBridge::Request::Enhanced;
				return true;
			}
			luaL_error(L, "render profile must be inherit, retro, redux, or enhanced");
			return false;
		}
		case LUA_TNUMBER:
		{
			switch (luaL_checkinteger(L, idx))
			{
			case 0:
				outRequest = RenderProfileBridge::Request::Inherit;
				return true;
			case 1:
				outRequest = RenderProfileBridge::Request::Retro;
				return true;
			case 2:
				outRequest = RenderProfileBridge::Request::Redux;
				return true;
			case 3:
				outRequest = RenderProfileBridge::Request::Enhanced;
				return true;
			default:
				luaL_error(L, "render profile integer must be 0..3");
				return false;
			}
		}
		default:
			luaL_error(L, "render profile must be a string or integer");
			return false;
		}
	}

	const char* ProfileToLegacyName(RenderProfileBridge::Profile profile)
	{
		switch (profile)
		{
		case RenderProfileBridge::Profile::Retro:
			return "retro";
		case RenderProfileBridge::Profile::Redux:
			return "redux";
		case RenderProfileBridge::Profile::Enhanced:
			return "enhanced";
		default:
			return "inherit";
		}
	}

	int RequestRenderProfile(lua_State* L)
	{
		RenderProfileBridge::Request request;
		if (!TryParseRenderProfileArg(L, 1, request))
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		if (!RenderProfileBridge::HasRenderProfileApi())
		{
			// Old/absent shim: map onto the legacy lighting-mode path so
			// content written against this API still works everywhere.
			switch (request)
			{
			case RenderProfileBridge::Request::Retro:
				g_desiredLightingMode = ViewportLightingMode::Retro;
				break;
			case RenderProfileBridge::Request::Enhanced:
				g_desiredLightingMode = ViewportLightingMode::Enhanced;
				break;
			case RenderProfileBridge::Request::Redux:
				g_desiredLightingMode = ViewportLightingMode::Default;
				break;
			case RenderProfileBridge::Request::Inherit:
			default:
				g_desiredLightingMode = ViewportLightingMode::Default;
				break;
			}
			EnforceDesiredLightingMode();
			lua_pushboolean(L, 1);
			return 1;
		}
		const bool ok = RenderProfileBridge::Forward(request);
		lua_pushboolean(L, ok ? 1 : 0);
		return 1;
	}

	int GetRequestedRenderProfile(lua_State* L)
	{
		if (!RenderProfileBridge::HasRenderProfileApi())
		{
			lua_pushstring(L, ProfileToLegacyName(RenderProfileBridge::Profile::Unknown));
			return 1;
		}
		lua_pushstring(
			L,
			ProfileToLegacyName(RenderProfileBridge::GetRequestedContentOverride()));
		return 1;
	}

	int GetEffectiveRenderProfile(lua_State* L)
	{
		if (!RenderProfileBridge::HasRenderProfileApi())
		{
			lua_pushstring(L, ProfileToLegacyName(RenderProfileBridge::Profile::Unknown));
			return 1;
		}
		lua_pushstring(L, ProfileToLegacyName(RenderProfileBridge::GetEffective()));
		return 1;
	}

	int GetUserRenderProfile(lua_State* L)
	{
		if (!RenderProfileBridge::HasRenderProfileApi())
		{
			lua_pushstring(L, ProfileToLegacyName(RenderProfileBridge::Profile::Unknown));
			return 1;
		}
		lua_pushstring(L, ProfileToLegacyName(RenderProfileBridge::GetUserPreference()));
		return 1;
	}

	int SupportsRenderProfile(lua_State* L)
	{
		RenderProfileBridge::Request request;
		if (!TryParseRenderProfileArg(L, 1, request) ||
			request == RenderProfileBridge::Request::Inherit)
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		const auto profile = static_cast<RenderProfileBridge::Profile>(request);
		lua_pushboolean(L, RenderProfileBridge::Supports(profile) ? 1 : 0);
		return 1;
	}

	int GetRenderCapabilities(lua_State* L)
	{
		const std::uint32_t mask = RenderProfileBridge::Capabilities();
		lua_newtable(L);
		const auto setField = [L](const char* name, bool value)
		{
			lua_pushboolean(L, value ? 1 : 0);
			lua_setfield(L, -2, name);
		};
		setField("schemeRewrite", mask & RenderProfileBridge::CapSchemeRewrite);
		setField("normalSharpening", mask & RenderProfileBridge::CapNormalSharpening);
		setField("linearLighting", mask & RenderProfileBridge::CapLinearLighting);
		setField("terrainEnhanced", mask & RenderProfileBridge::CapTerrainEnhanced);
		setField("objectEnhanced", mask & RenderProfileBridge::CapObjectEnhanced);
		setField("modernPssm", mask & RenderProfileBridge::CapModernPssm);
		setField("lightSelection", mask & RenderProfileBridge::CapLightSelection);
		setField("iblResources", mask & RenderProfileBridge::CapIblResources);
		lua_pushinteger(L, static_cast<lua_Integer>(mask));
		lua_setfield(L, -2, "mask");
		return 1;
	}

	void InstallGameViewportSchemeHooks()
	{
		static bool attempted = false;
		if (attempted)
		{
			return;
		}
		attempted = true;

		// OpenShim with the render-profile ABI owns the scheme call-site
		// takeover; patching the same sites here would fight the canonical
		// owner. Legacy shims (or no shim) keep the proven local hook below.
		if (RenderProfileBridge::HasRenderProfileApi())
		{
			LogEnvironmentDebug(
				"[EXU::Viewport] OpenShim render-profile bridge present; local viewport scheme hooks skipped");
			return;
		}

		for (uintptr_t site : kViewportSchemeCallSites)
		{
			const auto* bytes = reinterpret_cast<const uint8_t*>(site);
			uint32_t disp = 0;
			std::memcpy(&disp, bytes + 2, sizeof(disp));
			if (bytes[0] != 0xFF || bytes[1] != 0x15 || disp != kViewportSetMaterialSchemeIat)
			{
				LogEnvironmentDebug(
					"[EXU::Viewport] scheme call site mismatch at 0x%08X (%02X %02X disp=0x%08X); hook skipped",
					static_cast<unsigned>(site),
					bytes[0],
					bytes[1],
					disp);
				continue;
			}

			const uint32_t hookPtrAddress = static_cast<uint32_t>(
				reinterpret_cast<uintptr_t>(&g_GameViewportSetMaterialSchemeHookPtr));
			new InlinePatch(site + 2, &hookPtrAddress, sizeof(hookPtrAddress), BasicPatch::Status::ACTIVE);
			LogEnvironmentDebug(
				"[EXU::Viewport] game scheme call site hooked at 0x%08X",
				static_cast<unsigned>(site));
		}
	}
}
