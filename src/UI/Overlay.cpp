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

#include "Overlay.h"
#include "OverlayInternal.h"

#include "Hook.h"
#include "LuaHelpers.h"
#include "Util/Logging.h"
#include "Util/SignatureResolver.h"
#include "Ogre/OgreNativeFontBridge.h"
#include "Ogre/Ogre.h"
#include "Ogre/OgreOverlayShim.h"
#include "Game/game_state.h"

#include <Windows.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace ExtraUtilities::Lua::Overlay
{
	namespace
	{
		::Ogre::OverlayManager* GetOverlayManager();

		::Ogre::Overlay* FindOverlay(const std::string& name)
		{
			::Ogre::OverlayManager* manager = GetOverlayManager();
			if (manager == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return manager->getByName(name);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		::Ogre::OverlayElement* FindOverlayElement(const std::string& name)
		{
			::Ogre::OverlayManager* manager = GetOverlayManager();
			if (manager == nullptr)
			{
				return nullptr;
			}

			__try
			{
				if (!manager->hasOverlayElement(name, false))
				{
					return nullptr;
				}
				return manager->getOverlayElement(name, false);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

		::Ogre::OverlayContainer* FindOverlayContainer(const std::string& name)
		{
			const auto it = knownElements.find(name);
			if (it == knownElements.end() || !IsContainerKind(it->second))
			{
				return nullptr;
			}

			return reinterpret_cast<::Ogre::OverlayContainer*>(FindOverlayElement(name));
		}

		bool IsFiniteColor(const ExtraUtilities::Ogre::Color& color)
		{
			return std::isfinite(color.r)
				&& std::isfinite(color.g)
				&& std::isfinite(color.b)
				&& std::isfinite(color.a);
		}

		const char* DescribeOptionalBool(bool available, bool value)
		{
			if (!available)
			{
				return "unavailable";
			}

			return value ? "true" : "false";
		}

		::Ogre::OverlayManager* GetOverlayManager()
		{
			EnsureOverlayPauseHooksInstalled();
			::Ogre::OverlayManager* manager = GetOverlayManagerRaw();
			if (manager != nullptr)
			{
				return manager;
			}

			EnsureOverlaySupport();
			return GetOverlayManagerRaw();
		}

		bool SetOverlayParameter(const std::string& elementName, ::Ogre::OverlayElement* element, const std::string& name, const std::string& value)
		{
			using SetParameterFn = bool(__thiscall*)(void*, const std::string&, const std::string&);
			static constinit OgreProc<SetParameterFn> setParameterProc{ "?setParameter@StringInterface@Ogre@@UAE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z" };
			const SetParameterFn setParameter = setParameterProc.Get();

			if (element == nullptr)
			{
				Logging::LogMessage(
					"[EXU::Overlay] setParameter unavailable element=%p name=%s value=%s setParameterAvailable=%d",
					element,
					name.c_str(),
					value.c_str(),
					setParameter != nullptr ? 1 : 0);
				return false;
			}

			if (name == "font_name")
			{
				EnsureOverlayRuntimeFont();
			}

			bool directHandled = false;
			const bool directSuccess = TrySetOverlayParameterDirect(elementName, element, name, value, directHandled);
			if (directHandled)
			{
				return directSuccess;
			}

			if (setParameter == nullptr)
			{
				Logging::LogMessage(
					"[EXU::Overlay] setParameter unavailable element=%p name=%s value=%s setParameterAvailable=0",
					element,
					name.c_str(),
					value.c_str());
				return false;
			}

			bool success = false;
			unsigned int exceptionCode = 0;
			if (!TryCallSetOverlayParameter(setParameter, element, name, value, success, exceptionCode))
			{
				Logging::LogMessage(
					"[EXU::Overlay] setParameter crashed element=%p name=%s value=%s code=0x%08X",
					element,
					name.c_str(),
					value.c_str(),
					exceptionCode);
				return false;
			}

			if (!success || Logging::IsDebugLoggingEnabled())
			{
				Logging::LogMessage(
					"[EXU::Overlay] setParameter element=%p name=%s value=%s success=%d",
					element,
					name.c_str(),
					value.c_str(),
					success ? 1 : 0);
			}
			return success;
		}

		bool TryGetOverlayParameterValue(lua_State* L, int index, std::string& outValue)
		{
			const int type = lua_type(L, index);
			switch (type)
			{
			case LUA_TSTRING:
			{
				size_t length = 0;
				const char* value = lua_tolstring(L, index, &length);
				outValue.assign(value != nullptr ? value : "", length);
				return true;
			}
			case LUA_TNUMBER:
			{
				const lua_Number value = lua_tonumber(L, index);
				if (!std::isfinite(static_cast<double>(value)))
				{
					return false;
				}

				outValue = std::to_string(static_cast<double>(value));
				return true;
			}
			case LUA_TBOOLEAN:
				outValue = lua_toboolean(L, index) ? "true" : "false";
				return true;
			default:
				return false;
			}
		}

		void ResetOverlayRuntimeCaches() noexcept
		{
			overlayRuntimeResourcesReady = false;
			overlayRuntimeResourcesAttempted = false;
			overlayRuntimeFontReady = false;
			overlayRuntimeFontAttempted = false;
			overlayRuntimeFontScriptPath.clear();
		}

		bool TryDestroyOverlayByName(::Ogre::OverlayManager* manager, const std::string& name, bool& outDestroyed)
		{
			outDestroyed = false;
			if (manager == nullptr)
			{
				return false;
			}

			__try
			{
				if (manager->getByName(name) == nullptr)
				{
					return true;
				}

				manager->destroy(name);
				outDestroyed = true;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] destroy overlay crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryDestroyOverlayElementByName(::Ogre::OverlayManager* manager, const std::string& name, bool& outDestroyed)
		{
			outDestroyed = false;
			if (manager == nullptr)
			{
				return false;
			}

			__try
			{
				if (!manager->hasOverlayElement(name, false))
				{
					return true;
				}

				manager->destroyOverlayElement(name, false);
				outDestroyed = true;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] destroy overlay element crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool ResetOverlaySupportInternal(const char* reason)
		{
			const char* resetReason = reason != nullptr ? reason : "unspecified";
			Logging::LogMessage(
				"[EXU::Overlay] ResetOverlaySupport begin reason=%s trackedOverlays=%u trackedElements=%u attachedSceneManagers=%u overlaySystem=%p",
				resetReason,
				static_cast<unsigned>(overlayVisibilityStates.size()),
				static_cast<unsigned>(knownElements.size()),
				static_cast<unsigned>(attachedOverlaySceneManagers.size()),
				overlaySystemInstance);

			bool success = true;
			::Ogre::OverlayManager* manager = GetOverlayManagerRaw();
			size_t destroyedOverlays = 0;
			size_t destroyedElements = 0;

			if (manager != nullptr)
			{
				std::vector<std::string> overlayNames;
				overlayNames.reserve(overlayVisibilityStates.size());
				for (const auto& [name, _] : overlayVisibilityStates)
				{
					overlayNames.push_back(name);
				}

				for (const std::string& overlayName : overlayNames)
				{
					bool destroyed = false;
					if (!TryDestroyOverlayByName(manager, overlayName, destroyed))
					{
						success = false;
					}
					else if (destroyed)
					{
						++destroyedOverlays;
					}
				}

				std::vector<std::pair<std::string, ElementKind>> elementNames;
				elementNames.reserve(knownElements.size());
				for (const auto& entry : knownElements)
				{
					elementNames.push_back(entry);
				}

				std::stable_sort(
					elementNames.begin(),
					elementNames.end(),
					[](const auto& lhs, const auto& rhs)
					{
						const bool lhsContainer = IsContainerKind(lhs.second);
						const bool rhsContainer = IsContainerKind(rhs.second);
						return lhsContainer == rhsContainer ? lhs.first < rhs.first : (!lhsContainer && rhsContainer);
					});

				for (const auto& [elementName, _] : elementNames)
				{
					bool destroyed = false;
					if (!TryDestroyOverlayElementByName(manager, elementName, destroyed))
					{
						success = false;
					}
					else if (destroyed)
					{
						++destroyedElements;
					}
				}
			}

			DetachAndDestroyOverlaySystem();
			overlayVisibilityStates.clear();
			knownElements.clear();
			overlaySuppressionActive = false;
			ResetOverlayRuntimeCaches();

			Logging::LogMessage(
				"[EXU::Overlay] ResetOverlaySupport end reason=%s success=%d destroyedOverlays=%u destroyedElements=%u",
				resetReason,
				success ? 1 : 0,
				static_cast<unsigned>(destroyedOverlays),
				static_cast<unsigned>(destroyedElements));

			return success;
		}
	}

	void ShutdownOverlaySupport() noexcept
	{
		// Destroy what the mission created by name (children before
		// containers). Clearing the tracking maps alone left the overlays alive
		// but unreachable whenever EXU did not own the OverlayManager, and the
		// next mission's CreateOverlay with the same names then failed.
		try
		{
			ResetOverlaySupportInternal("lua-state-close");
		}
		catch (...)
		{
			knownElements.clear();
			overlayVisibilityStates.clear();
		}
		DestroyOverlayPauseHooks();
	}

	void NotifyMissionSimulationState(bool active) noexcept
	{
		const long previous = InterlockedExchange(
			&overlayMissionSimulationState, active ? 1L : 0L);
		Logging::LogMessage(
			"[EXU::Overlay] mission simulation state active=%d previous=%ld tracked=%u",
			active ? 1 : 0,
			previous,
			static_cast<unsigned>(overlayVisibilityStates.size()));

		// Hiding on exit is mandatory. Entering a new mission must not resurrect
		// requested-visible overlays owned by the previous mission; the new Lua
		// state will explicitly show the overlays it creates.
		if (!active)
		{
			ForgetMissionShowRequests();
		}
		RefreshOverlaySuppressionState(
			active ? "mission-simulation-enter" : "mission-simulation-exit",
			!active);
	}

	int ResetOverlaySupport(lua_State* L)
	{
		const char* reason = luaL_optstring(L, 1, "lua-reset");
		lua_pushboolean(L, ResetOverlaySupportInternal(reason) ? 1 : 0);
		return 1;
	}

	int CreateOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		EnsureOverlayPauseHooksInstalled();
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] CreateOverlay failed name=%s manager=null", name.c_str());
			lua_pushboolean(L, 0);
			return 1;
		}

		if (manager->getByName(name) != nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		::Ogre::Overlay* overlay = manager->create(name);
		overlayVisibilityStates[name] = {};
		RefreshOverlaySuppressionState("create-overlay");
		Logging::LogMessage("[EXU::Overlay] CreateOverlay name=%s manager=%p overlay=%p", name.c_str(), manager, overlay);
		lua_pushboolean(L, overlay != nullptr);
		return 1;
	}

	int DestroyOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		EnsureOverlayPauseHooksInstalled();
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			return 0;
		}

		bool destroyed = false;
		const bool destroyCompleted = TryDestroyOverlayByName(manager, name, destroyed);
		if (destroyCompleted)
		{
			overlayVisibilityStates.erase(name);
		}
		else
		{
			// A failed Ogre destroy can leave the overlay alive. Keep it tracked so
			// mission/shell suppression can hide it again instead of turning the
			// surviving object into an invisible-to-EXU end-screen leak.
			OverlayVisibilityState& visibilityState = overlayVisibilityStates[name];
			visibilityState.requestedVisible = false;
			SyncOverlayVisibilityState(name, visibilityState, "destroy-overlay-failed");
			Logging::LogMessage(
				"[EXU::Overlay] DestroyOverlay retained failed destruction name=%s effective=%d",
				name.c_str(),
				visibilityState.effectiveVisible ? 1 : 0);
		}

		return 0;
	}

	int ShowOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		EnsureOverlayPauseHooksInstalled();
		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] ShowOverlay missing name=%s", name.c_str());
			return 0;
		}

		OverlayVisibilityState& visibilityState = overlayVisibilityStates[name];
		visibilityState.requestedVisible = true;
		RefreshOverlaySuppressionState("show-overlay");

		void* viewport = GetCurrentViewportForOverlay();
		void* root = nullptr;
		TryGetRootSingleton(root);
		void* renderSystem = nullptr;
		TryGetRootRenderSystem(root, renderSystem);
		void* sharedListener = nullptr;
		const bool sharedListenerAvailable = TryGetRenderSystemSharedListener(sharedListener);
		void* renderSystemViewport = nullptr;
		const bool renderSystemViewportAvailable = TryGetRenderSystemViewport(renderSystem, renderSystemViewport);
		bool overlaysEnabledBefore = false;
		const bool overlaysEnabledBeforeAvailable = TryGetViewportOverlaysEnabled(viewport, overlaysEnabledBefore);

		Logging::LogMessage(
			"[EXU::Overlay] ShowOverlay pre name=%s overlay=%p viewport=%p root=%p renderSystem=%p sharedListener=%p sharedListenerAvailable=%d renderSystemViewport=%p renderSystemViewportAvailable=%d overlaysBefore=%s",
			name.c_str(),
			overlay,
			viewport,
			root,
			renderSystem,
			sharedListener,
			sharedListenerAvailable ? 1 : 0,
			renderSystemViewport,
			renderSystemViewportAvailable ? 1 : 0,
			DescribeOptionalBool(overlaysEnabledBeforeAvailable, overlaysEnabledBefore));

		SyncOverlayVisibilityState(name, visibilityState, "show-overlay");
		Logging::LogMessage(
			"[EXU::Overlay] ShowOverlay done name=%s overlay=%p viewport=%p requested=%d effective=%d suppressed=%d",
			name.c_str(),
			overlay,
			viewport,
			visibilityState.requestedVisible ? 1 : 0,
			visibilityState.effectiveVisible ? 1 : 0,
			overlaySuppressionActive ? 1 : 0);
		return 0;
	}

	int HideOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		EnsureOverlayPauseHooksInstalled();
		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			return 0;
		}

		OverlayVisibilityState& visibilityState = overlayVisibilityStates[name];
		visibilityState.requestedVisible = false;
		SyncOverlayVisibilityState(name, visibilityState, "hide-overlay");
		Logging::LogMessage(
			"[EXU::Overlay] HideOverlay name=%s overlay=%p requested=%d effective=%d",
			name.c_str(),
			overlay,
			visibilityState.requestedVisible ? 1 : 0,
			visibilityState.effectiveVisible ? 1 : 0);
		return 0;
	}

	int SetOverlayZOrder(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const int zOrder = static_cast<int>(luaL_checkinteger(L, 2));
		if (zOrder < 0 || zOrder > 650)
		{
			return luaL_argerror(L, 2, "z-order must be between 0 and 650");
		}

		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			return 0;
		}

		overlay->setZOrder(static_cast<unsigned short>(zOrder));
		return 0;
	}

	int SetOverlayScroll(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const float x = static_cast<float>(luaL_checknumber(L, 2));
		const float y = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(x))
		{
			return luaL_argerror(L, 2, "scroll x must be finite");
		}
		if (!std::isfinite(y))
		{
			return luaL_argerror(L, 3, "scroll y must be finite");
		}

		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			return 0;
		}

		overlay->setScroll(x, y);
		return 0;
	}

	int CreateOverlayElement(lua_State* L)
	{
		const std::string typeName = luaL_checkstring(L, 1);
		const std::string instanceName = luaL_checkstring(L, 2);
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] CreateOverlayElement failed type=%s name=%s manager=null", typeName.c_str(), instanceName.c_str());
			lua_pushboolean(L, 0);
			return 1;
		}

		if (manager->hasOverlayElement(instanceName, false))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		::Ogre::OverlayElement* element = nullptr;
		try
		{
			element = manager->createOverlayElement(typeName, instanceName, false);
		}
		catch (...)
		{
			// Ogre throws for an unknown element type; a C++ exception must not
			// cross the Lua C boundary.
			Logging::LogMessage("[EXU::Overlay] CreateOverlayElement threw type=%s name=%s", typeName.c_str(), instanceName.c_str());
			lua_pushboolean(L, 0);
			return 1;
		}

		if (element != nullptr)
		{
			knownElements[instanceName] = GetElementKindByTypeName(typeName);
		}

		Logging::LogMessage(
			"[EXU::Overlay] CreateOverlayElement type=%s name=%s manager=%p element=%p",
			typeName.c_str(),
			instanceName.c_str(),
			manager,
			element);
		lua_pushboolean(L, element != nullptr);
		return 1;
	}

	int DestroyOverlayElement(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			return 0;
		}

		bool destroyed = false;
		TryDestroyOverlayElementByName(manager, name, destroyed);
		knownElements.erase(name);

		return 0;
	}

	int HasOverlayElement(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		lua_pushboolean(L, manager->hasOverlayElement(name, false));
		return 1;
	}

	int AddOverlay2D(lua_State* L)
	{
		const std::string overlayName = luaL_checkstring(L, 1);
		const std::string containerName = luaL_checkstring(L, 2);

		::Ogre::Overlay* overlay = FindOverlay(overlayName);
		::Ogre::OverlayContainer* container = FindOverlayContainer(containerName);
		if (overlay == nullptr || container == nullptr)
		{
			Logging::LogMessage(
				"[EXU::Overlay] AddOverlay2D skipped overlay=%s overlayPtr=%p container=%s containerPtr=%p",
				overlayName.c_str(),
				overlay,
				containerName.c_str(),
				container);
			return 0;
		}

		overlay->add2D(container);
		Logging::LogMessage(
			"[EXU::Overlay] AddOverlay2D overlay=%s overlayPtr=%p container=%s containerPtr=%p",
			overlayName.c_str(),
			overlay,
			containerName.c_str(),
			container);
		return 0;
	}

	int RemoveOverlay2D(lua_State* L)
	{
		const std::string overlayName = luaL_checkstring(L, 1);
		const std::string containerName = luaL_checkstring(L, 2);

		::Ogre::Overlay* overlay = FindOverlay(overlayName);
		::Ogre::OverlayContainer* container = FindOverlayContainer(containerName);
		if (overlay == nullptr || container == nullptr)
		{
			return 0;
		}

		overlay->remove2D(container);
		return 0;
	}

	int AddOverlayElementChild(lua_State* L)
	{
		const std::string parentName = luaL_checkstring(L, 1);
		const std::string childName = luaL_checkstring(L, 2);

		::Ogre::OverlayContainer* parent = FindOverlayContainer(parentName);
		::Ogre::OverlayElement* child = FindOverlayElement(childName);
		if (parent == nullptr || child == nullptr)
		{
			return 0;
		}

		try
		{
			parent->::Ogre::OverlayContainer::addChild(child);
		}
		catch (...)
		{
			// Duplicate child names throw in Ogre.
			Logging::LogMessage("[EXU::Overlay] AddOverlayElementChild threw parent=%s child=%s", parentName.c_str(), childName.c_str());
		}
		return 0;
	}

	int RemoveOverlayElementChild(lua_State* L)
	{
		const std::string parentName = luaL_checkstring(L, 1);
		const std::string childName = luaL_checkstring(L, 2);

		::Ogre::OverlayContainer* parent = FindOverlayContainer(parentName);
		if (parent == nullptr)
		{
			return 0;
		}

		try
		{
			parent->::Ogre::OverlayContainer::removeChild(childName);
		}
		catch (...)
		{
			// A missing child throws in Ogre.
			Logging::LogMessage("[EXU::Overlay] RemoveOverlayElementChild threw parent=%s child=%s", parentName.c_str(), childName.c_str());
		}
		return 0;
	}

	int ShowOverlayElement(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] ShowOverlayElement missing name=%s", name.c_str());
			return 0;
		}

		element->::Ogre::OverlayElement::show();
		Logging::LogMessage("[EXU::Overlay] ShowOverlayElement name=%s element=%p", name.c_str(), element);
		return 0;
	}

	int HideOverlayElement(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		element->::Ogre::OverlayElement::hide();
		return 0;
	}

	int SetOverlayMetricsMode(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const int mode = static_cast<int>(luaL_checkinteger(L, 2));
		if (mode < static_cast<int>(::Ogre::GMM_RELATIVE) || mode > static_cast<int>(::Ogre::GMM_RELATIVE_ASPECT_ADJUSTED))
		{
			return luaL_argerror(L, 2, "metrics mode must be a valid exu.OVERLAY_METRICS value");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		SetElementMetricsMode(element, static_cast<::Ogre::GuiMetricsMode>(mode));
		return 0;
	}

	int SetOverlayPosition(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const float left = static_cast<float>(luaL_checknumber(L, 2));
		const float top = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(left))
		{
			return luaL_argerror(L, 2, "left must be finite");
		}
		if (!std::isfinite(top))
		{
			return luaL_argerror(L, 3, "top must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		element->setPosition(left, top);
		return 0;
	}

	int SetOverlayDimensions(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const float width = static_cast<float>(luaL_checknumber(L, 2));
		const float height = static_cast<float>(luaL_checknumber(L, 3));
		if (!std::isfinite(width))
		{
			return luaL_argerror(L, 2, "width must be finite");
		}
		if (!std::isfinite(height))
		{
			return luaL_argerror(L, 3, "height must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		element->setDimensions(width, height);
		return 0;
	}

	int SetOverlayMaterial(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const std::string materialName = luaL_checkstring(L, 2);

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayMaterial missing name=%s material=%s", name.c_str(), materialName.c_str());
			return 0;
		}

		try
		{
			SetElementMaterialName(element, materialName);
		}
		catch (...)
		{
			// A missing material throws in Ogre.
			Logging::LogMessage("[EXU::Overlay] SetOverlayMaterial threw name=%s material=%s", name.c_str(), materialName.c_str());
			return 0;
		}

		if (Logging::IsDebugLoggingEnabled())
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayMaterial name=%s element=%p material=%s", name.c_str(), element, materialName.c_str());
		}
		return 0;
	}

	int SetOverlayParameter(lua_State* L)
	{
		const std::string elementName = luaL_checkstring(L, 1);
		const std::string parameterName = luaL_checkstring(L, 2);
		std::string parameterValue;
		if (!TryGetOverlayParameterValue(L, 3, parameterValue))
		{
			return luaL_argerror(L, 3, "parameter value must be a finite number, boolean, or string");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(elementName);
		if (element == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = SetOverlayParameter(elementName, element, parameterName, parameterValue);
		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int SetOverlayColor(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const ExtraUtilities::Ogre::Color color = CheckColorOrSingles(L, 2);
		if (!IsFiniteColor(color))
		{
			return luaL_argerror(L, 2, "color components must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		const ::Ogre::ColourValue ogreColor{ color.r, color.g, color.b, color.a };
		SetElementColour(element, ogreColor);
		return 0;
	}

	int SetOverlayCaption(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const std::string text = luaL_checkstring(L, 2);

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		if (Native::TrySetTextAreaCaption(element, text.c_str()))
		{
			return 0;
		}

		SetOverlayParameter(name, element, "caption", text);
		return 0;
	}

	int SetOverlayTextFont(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const std::string fontName = luaL_checkstring(L, 2);

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		// The native setter writes TextArea-only members; a Panel or foreign
		// element would be corrupted.
		if (GetKnownElementKind(name) != ElementKind::TextArea)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayTextFont rejected name=%s reason=not-a-TextArea", name.c_str());
			lua_pushboolean(L, 0);
			return 1;
		}

		EnsureOverlayRuntimeFont();
		if (!overlayRuntimeFontReady)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayTextFont skipped name=%s font=%s runtimeFontReady=0", name.c_str(), fontName.c_str());
			lua_pushboolean(L, 0);
			return 1;
		}

		const bool success = Native::TrySetTextAreaFontName(element, fontName.c_str());
		if (!success)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayTextFont failed name=%s font=%s", name.c_str(), fontName.c_str());
		}
		lua_pushboolean(L, success ? 1 : 0);
		return 1;
	}

	int SetOverlayTextColor(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const ExtraUtilities::Ogre::Color color = CheckColorOrSingles(L, 2);
		if (!IsFiniteColor(color))
		{
			return luaL_argerror(L, 2, "color components must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		if (GetKnownElementKind(name) != ElementKind::TextArea)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayTextColor rejected name=%s reason=not-a-TextArea", name.c_str());
			return 0;
		}

		if (!Native::TrySetTextAreaColor(element, color.r, color.g, color.b, color.a))
		{
			const std::string colorValue = std::to_string(color.r) + " "
				+ std::to_string(color.g) + " "
				+ std::to_string(color.b) + " "
				+ std::to_string(color.a);
			SetOverlayParameter(name, element, "colour_top", colorValue);
			SetOverlayParameter(name, element, "colour_bottom", colorValue);
		}
		return 0;
	}

	int SetOverlayTextCharHeight(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const float charHeight = static_cast<float>(luaL_checknumber(L, 2));
		if (!std::isfinite(charHeight))
		{
			return luaL_argerror(L, 2, "char height must be finite");
		}

		::Ogre::OverlayElement* element = FindOverlayElement(name);
		if (element == nullptr)
		{
			return 0;
		}

		if (GetKnownElementKind(name) != ElementKind::TextArea)
		{
			Logging::LogMessage("[EXU::Overlay] SetOverlayTextCharHeight rejected name=%s reason=not-a-TextArea", name.c_str());
			return 0;
		}

		if (!Native::TrySetTextAreaCharHeight(element, charHeight))
		{
			SetOverlayParameter(name, element, "char_height", std::to_string(charHeight));
		}
		return 0;
	}
}
