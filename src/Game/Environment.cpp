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

#include "Environment.h"

#include "../RenderProfileBridge.h"
#include "InlinePatch.h"
#include "Util/Logging.h"
#include "GameObject.h"
#include "LuaHelpers.h"

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace ExtraUtilities::Lua::Environment
{
	namespace
	{
		std::mutex g_environmentLogMutex;
		std::string g_lastModernMaterialScheme = "high-pssm";
		void LogEnvironmentDebug(const char* fmt, ...);

		enum class ViewportLightingMode
		{
			Default = 1,
			Enhanced = 2,
			Retro = 3,
		};

		// The engine recreates/reactivates viewports (satellite view, sniper
		// scope, resolution or scene reload) and starts each fresh viewport on
		// its native (modern) material scheme. A one-shot apply therefore
		// "flashes back" to modern the next time a viewport is rebuilt. We keep
		// the user's chosen mode here so it can be re-asserted; enforcement is
		// idempotent and only rewrites a viewport whose scheme has drifted.
		ViewportLightingMode g_desiredLightingMode = ViewportLightingMode::Default;

		struct ViewportMaterialSchemeLayout
		{
			void* vtable = nullptr;
			void* camera = nullptr;
			void* target = nullptr;
			float relLeft = 0.0f;
			float relTop = 0.0f;
			float relWidth = 0.0f;
			float relHeight = 0.0f;
			int actLeft = 0;
			int actTop = 0;
			int actWidth = 0;
			int actHeight = 0;
			int zOrder = 0;
			Ogre::Color backColour{};
			float depthClearValue = 1.0f;
			bool clearEveryFrame = false;
			unsigned int clearBuffers = 0;
			bool updated = false;
			bool showOverlays = false;
			bool showSkies = false;
			bool showShadows = false;
			uint32_t visibilityMask = 0;
			std::string renderQueueSequenceName;
			void* renderQueueSequence = nullptr;
			std::string materialSchemeName;
		};

		struct ActiveViewportSet
		{
			std::array<void*, 2> viewports{};
			size_t count = 0;
		};

		constexpr std::string_view kDefaultModernMaterialScheme = "high-pssm";

		using GetRootSingletonFn = void*(*)();
		using GetRootRenderSystemFn = void*(__thiscall*)(void*);
		using GetRenderSystemViewportFn = void*(__thiscall*)(void*);

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

		bool TryGetViewportMaterialScheme(void* viewport, std::string& outScheme)
		{
			outScheme.clear();
			if (viewport == nullptr)
			{
				return false;
			}

			__try
			{
				const auto* layout = reinterpret_cast<const ViewportMaterialSchemeLayout*>(viewport);
				outScheme = layout->materialSchemeName;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug(
					"[EXU::Viewport] get material scheme crashed viewport=%p code=0x%08X",
					viewport,
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetViewportMaterialScheme(void* viewport, const std::string& scheme)
		{
			if (viewport == nullptr)
			{
				return false;
			}

			__try
			{
				auto* layout = reinterpret_cast<ViewportMaterialSchemeLayout*>(viewport);
				layout->materialSchemeName = scheme;
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug(
					"[EXU::Viewport] set material scheme crashed viewport=%p target=%s code=0x%08X",
					viewport,
					scheme.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		void WriteEnvironmentDebug(const std::string& message)
		{
			std::lock_guard lock(g_environmentLogMutex);

			OutputDebugStringA(message.c_str());
			OutputDebugStringA("\n");

			ExtraUtilities::Logging::ResetLogFileForCurrentProcess("exu_environment_debug.log");
			std::ofstream file(
				ExtraUtilities::Logging::GetLogFilePath("exu_environment_debug.log"),
				std::ios::app);
			if (file.is_open())
			{
				file << message << '\n';
			}
		}

		void LogEnvironmentDebug(const char* fmt, ...)
		{
			char buffer[1024]{};
			va_list args;
			va_start(args, fmt);
			vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, args);
			va_end(args);
			WriteEnvironmentDebug(buffer);
		}

		HMODULE GetOgreMainModule()
		{
			return GetModuleHandleA("OgreMain.dll");
		}

		GetRootSingletonFn ResolveGetRootSingleton()
		{
			static GetRootSingletonFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetRootSingletonFn>(nullptr);
				}

				return reinterpret_cast<GetRootSingletonFn>(
					GetProcAddress(ogreMain, "?getSingletonPtr@Root@Ogre@@SAPAV12@XZ"));
			}();

			return fn;
		}

		GetRootRenderSystemFn ResolveGetRootRenderSystem()
		{
			static GetRootRenderSystemFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetRootRenderSystemFn>(nullptr);
				}

				return reinterpret_cast<GetRootRenderSystemFn>(
					GetProcAddress(ogreMain, "?getRenderSystem@Root@Ogre@@QAEPAVRenderSystem@2@XZ"));
			}();

			return fn;
		}

		GetRenderSystemViewportFn ResolveGetRenderSystemViewport()
		{
			static GetRenderSystemViewportFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetRenderSystemViewportFn>(nullptr);
				}

				return reinterpret_cast<GetRenderSystemViewportFn>(
					GetProcAddress(ogreMain, "?_getViewport@RenderSystem@Ogre@@UAEPAVViewport@2@XZ"));
			}();

			return fn;
		}

		bool TryGetRootSingleton(void*& outRoot)
		{
			outRoot = nullptr;
			const auto fn = ResolveGetRootSingleton();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outRoot = fn();
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Viewport] Root::getSingletonPtr crashed code=0x%08X", GetExceptionCode());
				return false;
			}
		}

		bool TryGetRootRenderSystem(void* root, void*& outRenderSystem)
		{
			outRenderSystem = nullptr;
			if (root == nullptr)
			{
				return false;
			}

			const auto fn = ResolveGetRootRenderSystem();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outRenderSystem = fn(root);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Viewport] Root::getRenderSystem crashed root=%p code=0x%08X", root, GetExceptionCode());
				return false;
			}
		}

		bool TryGetRenderSystemViewport(void* renderSystem, void*& outViewport)
		{
			outViewport = nullptr;
			if (renderSystem == nullptr)
			{
				return false;
			}

			const auto fn = ResolveGetRenderSystemViewport();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outViewport = fn(renderSystem);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Viewport] RenderSystem::_getViewport crashed renderSystem=%p code=0x%08X", renderSystem, GetExceptionCode());
				return false;
			}
		}

		void* GetRenderSystemCurrentViewport()
		{
			void* root = nullptr;
			if (!TryGetRootSingleton(root))
			{
				return nullptr;
			}

			void* renderSystem = nullptr;
			if (!TryGetRootRenderSystem(root, renderSystem))
			{
				return nullptr;
			}

			void* viewport = nullptr;
			if (!TryGetRenderSystemViewport(renderSystem, viewport))
			{
				return nullptr;
			}

			return viewport;
		}

		void* GetSceneManagerCurrentViewport()
		{
			auto* sceneManager = Ogre::sceneManager.Read();
			if (sceneManager == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return Ogre::GetCurrentViewport(sceneManager);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Viewport] get current viewport crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
				return nullptr;
			}
		}

		void AppendViewportIfUnique(ActiveViewportSet& set, void* viewport)
		{
			if (viewport == nullptr)
			{
				return;
			}

			for (size_t i = 0; i < set.count; ++i)
			{
				if (set.viewports[i] == viewport)
				{
					return;
				}
			}

			if (set.count < set.viewports.size())
			{
				set.viewports[set.count++] = viewport;
			}
		}

		ActiveViewportSet GetActiveViewports()
		{
			ActiveViewportSet result{};
			AppendViewportIfUnique(result, GetRenderSystemCurrentViewport());
			AppendViewportIfUnique(result, GetSceneManagerCurrentViewport());
			return result;
		}
	}

	std::string DescribeLuaCaller(lua_State* L)
	{
		lua_Debug ar{};
		if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sln", &ar))
		{
			char caller[512];
			sprintf_s(
				caller,
				sizeof(caller),
				"%s:%d (%s)",
				ar.short_src[0] ? ar.short_src : "?",
				ar.currentline,
				ar.name ? ar.name : "anonymous");
			return caller;
		}

		return "unknown";
	}

	bool IsFiniteColor(const Ogre::Color& color)
	{
		return std::isfinite(color.r)
			&& std::isfinite(color.g)
			&& std::isfinite(color.b)
			&& std::isfinite(color.a);
	}

	bool IsExpectedColorRange(const Ogre::Color& color)
	{
		return color.r >= 0.0f && color.r <= 1.0f
			&& color.g >= 0.0f && color.g <= 1.0f
			&& color.b >= 0.0f && color.b <= 1.0f;
	}

	bool IsFiniteVector(const BZR::VECTOR_3D& vector)
	{
		return std::isfinite(vector.x)
			&& std::isfinite(vector.y)
			&& std::isfinite(vector.z);
	}

	bool IsFiniteScalar(float value)
	{
		return std::isfinite(value);
	}

	bool IsValidTimeOfDay(int timeOfDay)
	{
		return timeOfDay >= 0
			&& timeOfDay <= 2359
			&& (timeOfDay % 100) < 60;
	}

	bool TryGetSunAmbientColor(void* sceneManager, Ogre::Color& outColor)
	{
		__try
		{
			auto* sunAmbient = Ogre::GetAmbientLight(sceneManager);
			if (sunAmbient != nullptr)
			{
				outColor = *sunAmbient;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::GetSunAmbient] crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

	bool TrySetSunAmbientColor(void* sceneManager, const Ogre::Color& color)
	{
		__try
		{
			Ogre::SetAmbientLight(sceneManager, const_cast<Ogre::Color*>(&color));
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::SetSunAmbient] crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunDiffuseColor(void* terrainMasterLight, Ogre::Color& outColor)
	{
		__try
		{
			auto* color = Ogre::GetDiffuseColor(terrainMasterLight);
			if (color != nullptr)
			{
				outColor = *color;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::GetSunDiffuse] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TrySetSunDiffuseColor(void* terrainMasterLight, const Ogre::Color& color)
	{
		__try
		{
			Ogre::SetDiffuseColor(terrainMasterLight, color.r, color.g, color.b);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::SetSunDiffuse] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunSpecularColor(void* terrainMasterLight, Ogre::Color& outColor)
	{
		__try
		{
			auto* color = Ogre::GetSpecularColor(terrainMasterLight);
			if (color != nullptr)
			{
				outColor = *color;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::GetSunSpecular] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TrySetSunSpecularColor(void* terrainMasterLight, const Ogre::Color& color)
	{
		__try
		{
			Ogre::SetSpecularColor(terrainMasterLight, color.r, color.g, color.b);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::SetSunSpecular] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunDirection(void* terrainMasterLight, BZR::VECTOR_3D& outDirection)
	{
		__try
		{
			auto* direction = Ogre::GetDirection(terrainMasterLight);
			if (direction != nullptr)
			{
				outDirection = *direction;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::GetSunDirection] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TrySetSunDirection(void* terrainMasterLight, const BZR::VECTOR_3D& direction)
	{
		__try
		{
			Ogre::SetDirection(terrainMasterLight, direction.x, direction.y, direction.z);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::SetSunDirection] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			return false;
		}
	}

	bool TrySetNativeTimeOfDay(int timeOfDay)
	{
		__try
		{
			*BZR::Environment::timeOfDay = timeOfDay;
			BZR::Environment::SetTimeOfDay(timeOfDay / 100);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::SetTimeOfDay] crashed timeOfDay=%d code=0x%08X", timeOfDay, GetExceptionCode());
			return false;
		}
	}

	bool TryRefreshTerrainMasterLight()
	{
		__try
		{
			BZR::Environment::RefreshTerrainMasterLight();
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::SetTimeOfDay] refresh crashed code=0x%08X", GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunPowerScale(void* terrainMasterLight, float& outValue)
	{
		__try
		{
			outValue = Ogre::GetPowerScale(terrainMasterLight);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::GetSunPowerScale] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			outValue = 0.0f;
			return false;
		}
	}

	bool TrySetSunPowerScale(void* terrainMasterLight, float value)
	{
		__try
		{
			Ogre::SetPowerScale(terrainMasterLight, value);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::SetSunPowerScale] crashed terrainMasterLight=%p value=%g code=0x%08X", terrainMasterLight, value, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSunShadowFarDistance(void* terrainMasterLight, float& outValue)
	{
		__try
		{
			outValue = Ogre::GetShadowFarDistance(terrainMasterLight);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::GetSunShadowFarDistance] crashed terrainMasterLight=%p code=0x%08X", terrainMasterLight, GetExceptionCode());
			outValue = 0.0f;
			return false;
		}
	}

	bool TrySetSunShadowFarDistance(void* terrainMasterLight, float value)
	{
		__try
		{
			Ogre::SetShadowFarDistance(terrainMasterLight, value);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] crashed terrainMasterLight=%p value=%g code=0x%08X", terrainMasterLight, value, GetExceptionCode());
			return false;
		}
	}

	using SkyNodeGetter = void*(*)(void*);

	bool TryHasSkyNode(void* sceneManager, SkyNodeGetter getter, bool& outHasNode, const char* label)
	{
		__try
		{
			outHasNode = getter(sceneManager) != nullptr;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Sky] %s node probe crashed sceneManager=%p code=0x%08X", label, sceneManager, GetExceptionCode());
			outHasNode = false;
			return false;
		}
	}

	bool TryGetSkyBoxGenParameters(void* sceneManager, Ogre::SkyBoxGenParameters& outParams)
	{
		__try
		{
			return Ogre::GetSkyBoxGenParameters(sceneManager, outParams);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Sky] get skybox params crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

	bool TryGetSkyDomeGenParameters(void* sceneManager, Ogre::SkyDomeGenParameters& outParams)
	{
		__try
		{
			return Ogre::GetSkyDomeGenParameters(sceneManager, outParams);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Sky] get skydome params crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			return false;
		}
	}

		bool TryGetSkyPlaneGenParameters(void* sceneManager, Ogre::SkyPlaneGenParameters& outParams)
		{
			__try
			{
				return Ogre::GetSkyPlaneGenParameters(sceneManager, outParams);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Sky] get skyplane params crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
				return false;
			}
		}

		template<typename T>
		T ResolveOgreProc(const char* name)
		{
			HMODULE ogreMain = GetOgreMainModule();
			if (ogreMain == nullptr)
			{
				return nullptr;
			}

			return reinterpret_cast<T>(GetProcAddress(ogreMain, name));
		}

		struct OgreQuaternionValue
		{
			float w = 1.0f;
			float x = 0.0f;
			float y = 0.0f;
			float z = 0.0f;
		};

		struct OgrePlaneValue
		{
			BZR::VECTOR_3D normal{ 0.0f, -1.0f, 0.0f };
			float d = 1000.0f;
		};

		using IsSkyEnabledFn = bool(__thiscall*)(void*);
		using SetSkyEnabledFn = void(__thiscall*)(void*, bool);
		using SetSkyBoxFn = void(__thiscall*)(void*, bool, const std::string&, float, bool, const OgreQuaternionValue&, const std::string&);
		using SetSkyDomeFn = void(__thiscall*)(void*, bool, const std::string&, float, float, float, bool, const OgreQuaternionValue&, int, int, int, const std::string&);
		using SetSkyPlaneFn = void(__thiscall*)(void*, bool, const OgrePlaneValue&, const std::string&, float, float, bool, float, int, int, const std::string&);

		IsSkyEnabledFn ResolveIsSkyBoxEnabled()
		{
			static IsSkyEnabledFn fn = ResolveOgreProc<IsSkyEnabledFn>("?isSkyBoxEnabled@SceneManager@Ogre@@UBE_NXZ");
			return fn;
		}

		IsSkyEnabledFn ResolveIsSkyDomeEnabled()
		{
			static IsSkyEnabledFn fn = ResolveOgreProc<IsSkyEnabledFn>("?isSkyDomeEnabled@SceneManager@Ogre@@UBE_NXZ");
			return fn;
		}

		IsSkyEnabledFn ResolveIsSkyPlaneEnabled()
		{
			static IsSkyEnabledFn fn = ResolveOgreProc<IsSkyEnabledFn>("?isSkyPlaneEnabled@SceneManager@Ogre@@UBE_NXZ");
			return fn;
		}

		SetSkyEnabledFn ResolveSetSkyBoxEnabled()
		{
			static SetSkyEnabledFn fn = ResolveOgreProc<SetSkyEnabledFn>("?setSkyBoxEnabled@SceneManager@Ogre@@UAEX_N@Z");
			return fn;
		}

		SetSkyEnabledFn ResolveSetSkyDomeEnabled()
		{
			static SetSkyEnabledFn fn = ResolveOgreProc<SetSkyEnabledFn>("?setSkyDomeEnabled@SceneManager@Ogre@@UAEX_N@Z");
			return fn;
		}

		SetSkyEnabledFn ResolveSetSkyPlaneEnabled()
		{
			static SetSkyEnabledFn fn = ResolveOgreProc<SetSkyEnabledFn>("?setSkyPlaneEnabled@SceneManager@Ogre@@UAEX_N@Z");
			return fn;
		}

		SetSkyBoxFn ResolveSetSkyBox()
		{
			static SetSkyBoxFn fn = ResolveOgreProc<SetSkyBoxFn>("?setSkyBox@SceneManager@Ogre@@UAEX_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@M0ABVQuaternion@2@1@Z");
			return fn;
		}

		SetSkyDomeFn ResolveSetSkyDome()
		{
			static SetSkyDomeFn fn = ResolveOgreProc<SetSkyDomeFn>("?setSkyDome@SceneManager@Ogre@@UAEX_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@MMM0ABVQuaternion@2@HHH1@Z");
			return fn;
		}

		SetSkyPlaneFn ResolveSetSkyPlane()
		{
			static SetSkyPlaneFn fn = ResolveOgreProc<SetSkyPlaneFn>("?setSkyPlane@SceneManager@Ogre@@UAEX_NABVPlane@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@MM0MHH2@Z");
			return fn;
		}

		bool TryGetSkyEnabled(void* sceneManager, IsSkyEnabledFn fn, bool& outEnabled, const char* label)
		{
			outEnabled = false;
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				outEnabled = fn(sceneManager);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Sky] %s enabled probe crashed sceneManager=%p code=0x%08X", label, sceneManager, GetExceptionCode());
				return false;
			}
		}

		bool TrySetSkyEnabled(void* sceneManager, SetSkyEnabledFn fn, bool enabled, const char* label)
		{
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(sceneManager, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Sky] %s enabled setter crashed sceneManager=%p enabled=%d code=0x%08X", label, sceneManager, enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetSkyBox(void* sceneManager, const std::string& materialName, float distance, bool drawFirst, const std::string& resourceGroup)
		{
			const auto fn = ResolveSetSkyBox();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			const OgreQuaternionValue identity{};
			__try
			{
				fn(sceneManager, true, materialName, distance, drawFirst, identity, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug(
					"[EXU::Sky] setSkyBox crashed sceneManager=%p material=%s distance=%g drawFirst=%d group=%s code=0x%08X",
					sceneManager,
					materialName.c_str(),
					distance,
					drawFirst ? 1 : 0,
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetSkyDome(
			void* sceneManager,
			const std::string& materialName,
			float curvature,
			float tiling,
			float distance,
			bool drawFirst,
			int xsegments,
			int ysegments,
			int ysegmentsKeep,
			const std::string& resourceGroup)
		{
			const auto fn = ResolveSetSkyDome();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			const OgreQuaternionValue identity{};
			__try
			{
				fn(sceneManager, true, materialName, curvature, tiling, distance, drawFirst, identity, xsegments, ysegments, ysegmentsKeep, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug(
					"[EXU::Sky] setSkyDome crashed sceneManager=%p material=%s curvature=%g tiling=%g distance=%g drawFirst=%d group=%s code=0x%08X",
					sceneManager,
					materialName.c_str(),
					curvature,
					tiling,
					distance,
					drawFirst ? 1 : 0,
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetSkyPlane(
			void* sceneManager,
			const OgrePlaneValue& plane,
			const std::string& materialName,
			float scale,
			float tiling,
			bool drawFirst,
			float bow,
			int xsegments,
			int ysegments,
			const std::string& resourceGroup)
		{
			const auto fn = ResolveSetSkyPlane();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(sceneManager, true, plane, materialName, scale, tiling, drawFirst, bow, xsegments, ysegments, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug(
					"[EXU::Sky] setSkyPlane crashed sceneManager=%p material=%s scale=%g tiling=%g drawFirst=%d bow=%g group=%s code=0x%08X",
					sceneManager,
					materialName.c_str(),
					scale,
					tiling,
					drawFirst ? 1 : 0,
					bow,
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TryReadSkyPlane(lua_State* L, int idx, OgrePlaneValue& outPlane)
		{
			luaL_checktype(L, idx, LUA_TTABLE);

			BZR::VECTOR_3D normal{ 0.0f, -1.0f, 0.0f };
			lua_getfield(L, idx, "normal");
			if (!lua_isnil(L, -1))
			{
				normal = CheckVectorOrSingles(L, -1);
			}
			lua_pop(L, 1);

			bool hasDistance = false;
			float distance = 1000.0f;
			lua_getfield(L, idx, "d");
			if (!lua_isnil(L, -1))
			{
				hasDistance = true;
				distance = static_cast<float>(luaL_checknumber(L, -1));
			}
			lua_pop(L, 1);

			if (!hasDistance)
			{
				lua_getfield(L, idx, "distance");
				if (!lua_isnil(L, -1))
				{
					hasDistance = true;
					distance = static_cast<float>(luaL_checknumber(L, -1));
				}
				lua_pop(L, 1);
			}

			if (!hasDistance)
			{
				luaL_error(L, "sky plane table requires a numeric d or distance field");
				return false;
			}

			outPlane.normal = normal;
			outPlane.d = distance;
			return true;
		}

		std::string CheckOptionalSkyResourceGroup(lua_State* L, int idx)
		{
			if (lua_isnoneornil(L, idx))
			{
				return "General";
			}

			return luaL_checkstring(L, idx);
		}

		constexpr std::string_view kManagedParticleNodePrefix = "__exu_ps_node_";
		constexpr int kOgreTransformSpaceLocal = 0;

		using CreateParticleSystemFn = void*(__thiscall*)(void*, const std::string&, const std::string&);
		using DestroyParticleSystemFn = void(__thiscall*)(void*, const std::string&);
		using GetParticleSystemFn = void*(__thiscall*)(void*, const std::string&);
		using HasParticleSystemFn = bool(__thiscall*)(void*, const std::string&);
		using GetRootSceneNodeFn = void*(__thiscall*)(void*);
		using CreateChildSceneNodeFn = void*(__thiscall*)(void*, const std::string&, const BZR::VECTOR_3D&, const OgreQuaternionValue&);
		using GetSceneNodeFn = void*(__thiscall*)(void*, const std::string&);
		using HasSceneNodeFn = bool(__thiscall*)(void*, const std::string&);
		using DestroySceneNodeFn = void(__thiscall*)(void*, const std::string&);
		using AttachObjectFn = void(__thiscall*)(void*, void*);
		using SetNodePositionFn = void(__thiscall*)(void*, const BZR::VECTOR_3D&);
		using SetSceneNodeDirectionFn = void(__thiscall*)(void*, const BZR::VECTOR_3D&, int, const BZR::VECTOR_3D&);
		using SetParticleSystemEmittingFn = void(__thiscall*)(void*, bool);
		using SetParticleSystemSpeedFactorFn = void(__thiscall*)(void*, float);
		using SetParticleSystemKeepLocalSpaceFn = void(__thiscall*)(void*, bool);
		using SetParticleSystemMaterialFn = void(__thiscall*)(void*, const std::string&, const std::string&);
		using SetParticleSystemRenderQueueGroupFn = void(__thiscall*)(void*, uint8_t);
		using SetParticleSystemParticleQuotaFn = void(__thiscall*)(void*, uint32_t);
		using SetParticleSystemDefaultDimensionsFn = void(__thiscall*)(void*, float, float);
		using SetMovableObjectVisibleFn = void(__thiscall*)(void*, bool);

		CreateParticleSystemFn ResolveCreateParticleSystem()
		{
			static CreateParticleSystemFn fn = ResolveOgreProc<CreateParticleSystemFn>("?createParticleSystem@SceneManager@Ogre@@UAEPAVParticleSystem@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
			return fn;
		}

		DestroyParticleSystemFn ResolveDestroyParticleSystem()
		{
			static DestroyParticleSystemFn fn = ResolveOgreProc<DestroyParticleSystemFn>("?destroyParticleSystem@SceneManager@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		GetParticleSystemFn ResolveGetParticleSystem()
		{
			static GetParticleSystemFn fn = ResolveOgreProc<GetParticleSystemFn>("?getParticleSystem@SceneManager@Ogre@@UBEPAVParticleSystem@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		HasParticleSystemFn ResolveHasParticleSystem()
		{
			static HasParticleSystemFn fn = ResolveOgreProc<HasParticleSystemFn>("?hasParticleSystem@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		GetRootSceneNodeFn ResolveGetRootSceneNode()
		{
			static GetRootSceneNodeFn fn = ResolveOgreProc<GetRootSceneNodeFn>("?getRootSceneNode@SceneManager@Ogre@@UAEPAVSceneNode@2@XZ");
			return fn;
		}

		CreateChildSceneNodeFn ResolveCreateChildSceneNode()
		{
			static CreateChildSceneNodeFn fn = ResolveOgreProc<CreateChildSceneNodeFn>("?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@ABVVector3@2@ABVQuaternion@2@@Z");
			return fn;
		}

		GetSceneNodeFn ResolveGetSceneNode()
		{
			static GetSceneNodeFn fn = ResolveOgreProc<GetSceneNodeFn>("?getSceneNode@SceneManager@Ogre@@UBEPAVSceneNode@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		HasSceneNodeFn ResolveHasSceneNode()
		{
			static HasSceneNodeFn fn = ResolveOgreProc<HasSceneNodeFn>("?hasSceneNode@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		DestroySceneNodeFn ResolveDestroySceneNode()
		{
			static DestroySceneNodeFn fn = ResolveOgreProc<DestroySceneNodeFn>("?destroySceneNode@SceneManager@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		AttachObjectFn ResolveAttachObject()
		{
			static AttachObjectFn fn = ResolveOgreProc<AttachObjectFn>("?attachObject@SceneNode@Ogre@@UAEXPAVMovableObject@2@@Z");
			return fn;
		}

		SetNodePositionFn ResolveSetNodePosition()
		{
			static SetNodePositionFn fn = ResolveOgreProc<SetNodePositionFn>("?setPosition@Node@Ogre@@UAEXABVVector3@2@@Z");
			return fn;
		}

		SetSceneNodeDirectionFn ResolveSetSceneNodeDirection()
		{
			static SetSceneNodeDirectionFn fn = ResolveOgreProc<SetSceneNodeDirectionFn>("?setDirection@SceneNode@Ogre@@UAEXABVVector3@2@W4TransformSpace@Node@2@0@Z");
			return fn;
		}

		SetParticleSystemEmittingFn ResolveSetParticleSystemEmitting()
		{
			static SetParticleSystemEmittingFn fn = ResolveOgreProc<SetParticleSystemEmittingFn>("?setEmitting@ParticleSystem@Ogre@@QAEX_N@Z");
			return fn;
		}

		SetParticleSystemSpeedFactorFn ResolveSetParticleSystemSpeedFactor()
		{
			static SetParticleSystemSpeedFactorFn fn = ResolveOgreProc<SetParticleSystemSpeedFactorFn>("?setSpeedFactor@ParticleSystem@Ogre@@QAEXM@Z");
			return fn;
		}

		SetParticleSystemKeepLocalSpaceFn ResolveSetParticleSystemKeepLocalSpace()
		{
			static SetParticleSystemKeepLocalSpaceFn fn = ResolveOgreProc<SetParticleSystemKeepLocalSpaceFn>("?setKeepParticlesInLocalSpace@ParticleSystem@Ogre@@QAEX_N@Z");
			return fn;
		}

		SetParticleSystemMaterialFn ResolveSetParticleSystemMaterial()
		{
			static SetParticleSystemMaterialFn fn = ResolveOgreProc<SetParticleSystemMaterialFn>("?setMaterialName@ParticleSystem@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
			return fn;
		}

		SetParticleSystemRenderQueueGroupFn ResolveSetParticleSystemRenderQueueGroup()
		{
			static SetParticleSystemRenderQueueGroupFn fn = ResolveOgreProc<SetParticleSystemRenderQueueGroupFn>("?setRenderQueueGroup@ParticleSystem@Ogre@@UAEXE@Z");
			return fn;
		}

		SetParticleSystemParticleQuotaFn ResolveSetParticleSystemParticleQuota()
		{
			static SetParticleSystemParticleQuotaFn fn = ResolveOgreProc<SetParticleSystemParticleQuotaFn>("?setParticleQuota@ParticleSystem@Ogre@@QAEXI@Z");
			return fn;
		}

		SetParticleSystemDefaultDimensionsFn ResolveSetParticleSystemDefaultDimensions()
		{
			static SetParticleSystemDefaultDimensionsFn fn = ResolveOgreProc<SetParticleSystemDefaultDimensionsFn>("?setDefaultDimensions@ParticleSystem@Ogre@@UAEXMM@Z");
			return fn;
		}

		SetMovableObjectVisibleFn ResolveSetMovableObjectVisible()
		{
			static SetMovableObjectVisibleFn fn = ResolveOgreProc<SetMovableObjectVisibleFn>("?setVisible@MovableObject@Ogre@@UAEX_N@Z");
			return fn;
		}

		bool TryReturnManagedParticleToOwnNode(void* sceneManager, const std::string& name);
		void ForgetParticleCameraFollower(const std::string& name);

		std::string BuildManagedParticleNodeName(std::string_view particleName)
		{
			std::string result(kManagedParticleNodePrefix);
			result += particleName;
			return result;
		}

		std::string CheckOptionalParticleResourceGroup(lua_State* L, int idx)
		{
			if (lua_isnoneornil(L, idx))
			{
				return "General";
			}

			return luaL_checkstring(L, idx);
		}

		bool TryHasParticleSystem(void* sceneManager, const std::string& name, bool& outHasParticleSystem)
		{
			outHasParticleSystem = false;
			const auto fn = ResolveHasParticleSystem();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				outHasParticleSystem = fn(sceneManager, name);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] hasParticleSystem crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryHasSceneNode(void* sceneManager, const std::string& name, bool& outHasSceneNode)
		{
			outHasSceneNode = false;
			const auto fn = ResolveHasSceneNode();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				outHasSceneNode = fn(sceneManager, name);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] hasSceneNode crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryGetParticleSystem(void* sceneManager, const std::string& name, void*& outParticleSystem)
		{
			outParticleSystem = nullptr;
			bool hasParticleSystem = false;
			if (!TryHasParticleSystem(sceneManager, name, hasParticleSystem) || !hasParticleSystem)
			{
				return false;
			}

			const auto fn = ResolveGetParticleSystem();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outParticleSystem = fn(sceneManager, name);
				return outParticleSystem != nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] getParticleSystem crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryGetSceneNode(void* sceneManager, const std::string& nodeName, void*& outSceneNode)
		{
			outSceneNode = nullptr;
			bool hasSceneNode = false;
			if (!TryHasSceneNode(sceneManager, nodeName, hasSceneNode) || !hasSceneNode)
			{
				return false;
			}

			const auto fn = ResolveGetSceneNode();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outSceneNode = fn(sceneManager, nodeName);
				return outSceneNode != nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] getSceneNode crashed sceneManager=%p node=%s code=0x%08X", sceneManager, nodeName.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryGetManagedParticleSceneNode(void* sceneManager, const std::string& particleName, void*& outSceneNode)
		{
			return TryGetSceneNode(sceneManager, BuildManagedParticleNodeName(particleName), outSceneNode);
		}

		bool TryDestroyParticleSystemByName(void* sceneManager, const std::string& name)
		{
			const auto fn = ResolveDestroyParticleSystem();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			bool hasParticleSystem = false;
			if (!TryHasParticleSystem(sceneManager, name, hasParticleSystem) || !hasParticleSystem)
			{
				return false;
			}

			__try
			{
				fn(sceneManager, name);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] destroyParticleSystem crashed sceneManager=%p name=%s code=0x%08X", sceneManager, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryDestroySceneNodeByName(void* sceneManager, const std::string& nodeName)
		{
			const auto fn = ResolveDestroySceneNode();
			if (sceneManager == nullptr || fn == nullptr)
			{
				return false;
			}

			bool hasSceneNode = false;
			if (!TryHasSceneNode(sceneManager, nodeName, hasSceneNode) || !hasSceneNode)
			{
				return false;
			}

			__try
			{
				fn(sceneManager, nodeName);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] destroySceneNode crashed sceneManager=%p node=%s code=0x%08X", sceneManager, nodeName.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TryDestroyManagedParticleSystem(void* sceneManager, const std::string& name)
		{
			if (sceneManager == nullptr)
			{
				return false;
			}

			// A bone attachment leaves the system parented to a TagPoint rather
			// than to its own node. Break whatever link exists before the
			// destroy so nothing is left holding a freed movable object.
			TryReturnManagedParticleToOwnNode(sceneManager, name);

			bool removedAny = false;
			removedAny = TryDestroyParticleSystemByName(sceneManager, name) || removedAny;
			removedAny = TryDestroySceneNodeByName(sceneManager, BuildManagedParticleNodeName(name)) || removedAny;
			return removedAny;
		}

		bool TryCreateParticleSystemAttachment(void* sceneManager, const std::string& name, const std::string& templateName, const std::string& nodeName, const BZR::VECTOR_3D& position)
		{
			const auto createParticleFn = ResolveCreateParticleSystem();
			const auto getRootSceneNodeFn = ResolveGetRootSceneNode();
			const auto createChildSceneNodeFn = ResolveCreateChildSceneNode();
			const auto attachObjectFn = ResolveAttachObject();
			if (sceneManager == nullptr || createParticleFn == nullptr || getRootSceneNodeFn == nullptr ||
				createChildSceneNodeFn == nullptr || attachObjectFn == nullptr)
			{
				return false;
			}

			void* rootSceneNode = nullptr;
			void* particleSystem = nullptr;
			void* childSceneNode = nullptr;
			const OgreQuaternionValue identity{};
			__try
			{
				rootSceneNode = getRootSceneNodeFn(sceneManager);
				if (rootSceneNode == nullptr)
				{
					LogEnvironmentDebug("[EXU::Particle] create skipped sceneManager=%p name=%s reason=no_root_scene_node", sceneManager, name.c_str());
					return false;
				}

				particleSystem = createParticleFn(sceneManager, name, templateName);
				if (particleSystem == nullptr)
				{
					LogEnvironmentDebug("[EXU::Particle] create failed sceneManager=%p name=%s template=%s reason=null_particle", sceneManager, name.c_str(), templateName.c_str());
					return false;
				}

				childSceneNode = createChildSceneNodeFn(rootSceneNode, nodeName, position, identity);
				if (childSceneNode == nullptr)
				{
					LogEnvironmentDebug("[EXU::Particle] create failed sceneManager=%p name=%s node=%s reason=null_scene_node", sceneManager, name.c_str(), nodeName.c_str());
					return false;
				}

				attachObjectFn(childSceneNode, particleSystem);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug(
					"[EXU::Particle] create crashed sceneManager=%p name=%s template=%s node=%s code=0x%08X",
					sceneManager,
					name.c_str(),
					templateName.c_str(),
					nodeName.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TryCreateManagedParticleSystem(void* sceneManager, const std::string& name, const std::string& templateName, const BZR::VECTOR_3D& position)
		{
			if (sceneManager == nullptr)
			{
				return false;
			}

			bool hasParticleSystem = false;
			if (TryHasParticleSystem(sceneManager, name, hasParticleSystem) && hasParticleSystem)
			{
				LogEnvironmentDebug("[EXU::Particle] create skipped sceneManager=%p name=%s reason=particle_exists", sceneManager, name.c_str());
				return false;
			}

			const std::string nodeName = BuildManagedParticleNodeName(name);
			bool hasSceneNode = false;
			if (TryHasSceneNode(sceneManager, nodeName, hasSceneNode) && hasSceneNode)
			{
				LogEnvironmentDebug("[EXU::Particle] create skipped sceneManager=%p name=%s node=%s reason=node_exists", sceneManager, name.c_str(), nodeName.c_str());
				return false;
			}

			if (TryCreateParticleSystemAttachment(sceneManager, name, templateName, nodeName, position))
			{
				return true;
			}

			TryDestroyManagedParticleSystem(sceneManager, name);
			return false;
		}

		bool TrySetManagedParticleSceneNodePosition(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& position)
		{
			void* sceneNode = nullptr;
			const auto fn = ResolveSetNodePosition();
			if (!TryGetManagedParticleSceneNode(sceneManager, name, sceneNode) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(sceneNode, position);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setPosition crashed sceneNode=%p name=%s code=0x%08X", sceneNode, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TrySetManagedParticleSceneNodeDirection(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& direction)
		{
			void* sceneNode = nullptr;
			const auto fn = ResolveSetSceneNodeDirection();
			if (!TryGetManagedParticleSceneNode(sceneManager, name, sceneNode) || fn == nullptr)
			{
				return false;
			}

			const BZR::VECTOR_3D localDirectionVector{ 0.0f, 0.0f, -1.0f };
			__try
			{
				fn(sceneNode, direction, kOgreTransformSpaceLocal, localDirectionVector);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setDirection crashed sceneNode=%p name=%s code=0x%08X", sceneNode, name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemEmitting(void* sceneManager, const std::string& name, bool enabled)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemEmitting();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setEmitting crashed particleSystem=%p name=%s enabled=%d code=0x%08X", particleSystem, name.c_str(), enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemVisible(void* sceneManager, const std::string& name, bool enabled)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetMovableObjectVisible();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setVisible crashed particleSystem=%p name=%s enabled=%d code=0x%08X", particleSystem, name.c_str(), enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemSpeedFactor(void* sceneManager, const std::string& name, float speedFactor)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemSpeedFactor();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, speedFactor);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setSpeedFactor crashed particleSystem=%p name=%s speed=%g code=0x%08X", particleSystem, name.c_str(), speedFactor, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemKeepLocalSpace(void* sceneManager, const std::string& name, bool enabled)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemKeepLocalSpace();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setKeepParticlesInLocalSpace crashed particleSystem=%p name=%s enabled=%d code=0x%08X", particleSystem, name.c_str(), enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemMaterial(void* sceneManager, const std::string& name, const std::string& materialName, const std::string& resourceGroup)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemMaterial();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, materialName, resourceGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug(
					"[EXU::Particle] setMaterialName crashed particleSystem=%p name=%s material=%s group=%s code=0x%08X",
					particleSystem,
					name.c_str(),
					materialName.c_str(),
					resourceGroup.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemRenderQueueGroup(void* sceneManager, const std::string& name, uint8_t renderQueueGroup)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemRenderQueueGroup();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, renderQueueGroup);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setRenderQueueGroup crashed particleSystem=%p name=%s queue=%u code=0x%08X", particleSystem, name.c_str(), renderQueueGroup, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemParticleQuota(void* sceneManager, const std::string& name, uint32_t quota)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemParticleQuota();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, quota);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setParticleQuota crashed particleSystem=%p name=%s quota=%u code=0x%08X", particleSystem, name.c_str(), quota, GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemDefaultDimensions(void* sceneManager, const std::string& name, float width, float height)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetParticleSystemDefaultDimensions();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, width, height);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setDefaultDimensions crashed particleSystem=%p name=%s width=%g height=%g code=0x%08X", particleSystem, name.c_str(), width, height, GetExceptionCode());
				return false;
			}
		}

		// ---------------------------------------------------------------------
		// Particle attachment and emitter parameter control
		//
		// Weather and object VFX both need an emitter that travels with
		// something -- the camera for a camera-centred precipitation volume, a
		// craft's scene node for damage smoke, a bone for a weapon muzzle. Doing
		// that from Lua means a per-frame SetParticleSystemPosition for every
		// live system; doing it here means the Ogre scene graph carries the
		// transform for free.
		//
		// The camera is the one case that can fail: the engine is free to drive
		// its Ogre camera directly instead of through a scene node, and then
		// there is no node to parent to. That case degrades to a registered
		// follower that UpdateParticleFollowers snaps to the camera's derived
		// position -- still one native call per frame instead of one per system,
		// and still no Lua-side transform maths.
		// ---------------------------------------------------------------------

		struct ParticleCameraFollower
		{
			std::string particleName;
			BZR::VECTOR_3D offset{};
		};

		std::vector<ParticleCameraFollower> g_particleCameraFollowers;

		using GetParentSceneNodeFn = void*(__thiscall*)(void*);
		using NodeGetParentFn = void*(__thiscall*)(void*);
		using NodeAddChildFn = void(__thiscall*)(void*, void*);
		using NodeRemoveChildPtrFn = void*(__thiscall*)(void*, void*);
		using NodeSetInheritOrientationFn = void(__thiscall*)(void*, bool);
		using NodeSetInheritScaleFn = void(__thiscall*)(void*, bool);
		using CameraGetDerivedPositionFn = const BZR::VECTOR_3D*(__thiscall*)(void*);
		using MovableObjectDetachFromParentFn = void(__thiscall*)(void*);
		using EntityHasSkeletonFn = bool(__thiscall*)(void*);
		using EntityAttachObjectToBoneFn = void*(__thiscall*)(void*, const std::string&, void*, const OgreQuaternionValue&, const BZR::VECTOR_3D&);
		using EntityDetachObjectFromBoneFn = void(__thiscall*)(void*, void*);
		using GetNumEmittersFn = uint16_t(__thiscall*)(void*);
		using GetEmitterFn = void*(__thiscall*)(void*, uint16_t);
		using EmitterSetEnabledFn = void(__thiscall*)(void*, bool);
		using EmitterSetEmissionRateFn = void(__thiscall*)(void*, float);
		using EmitterGetEmissionRateFn = float(__thiscall*)(void*);
		using EmitterSetDirectionFn = void(__thiscall*)(void*, const BZR::VECTOR_3D&);
		using EmitterSetPositionFn = void(__thiscall*)(void*, const BZR::VECTOR_3D&);
		using EmitterSetVelocityRangeFn = void(__thiscall*)(void*, float, float);
		using EmitterSetAngleFn = void(__thiscall*)(void*, const float&);
		using EmitterSetTimeToLiveRangeFn = void(__thiscall*)(void*, float, float);
		using EmitterSetColourRangeFn = void(__thiscall*)(void*, const Ogre::Color&, const Ogre::Color&);
		using SetNonVisibleUpdateTimeoutFn = void(__thiscall*)(void*, float);

		GetParentSceneNodeFn ResolveGetParentSceneNode()
		{
			static GetParentSceneNodeFn fn = ResolveOgreProc<GetParentSceneNodeFn>("?getParentSceneNode@MovableObject@Ogre@@UBEPAVSceneNode@2@XZ");
			return fn;
		}

		NodeGetParentFn ResolveNodeGetParent()
		{
			static NodeGetParentFn fn = ResolveOgreProc<NodeGetParentFn>("?getParent@Node@Ogre@@UBEPAV12@XZ");
			return fn;
		}

		NodeAddChildFn ResolveNodeAddChild()
		{
			static NodeAddChildFn fn = ResolveOgreProc<NodeAddChildFn>("?addChild@Node@Ogre@@UAEXPAV12@@Z");
			return fn;
		}

		NodeRemoveChildPtrFn ResolveNodeRemoveChildPtr()
		{
			static NodeRemoveChildPtrFn fn = ResolveOgreProc<NodeRemoveChildPtrFn>("?removeChild@Node@Ogre@@UAEPAV12@PAV12@@Z");
			return fn;
		}

		NodeSetInheritOrientationFn ResolveNodeSetInheritOrientation()
		{
			static NodeSetInheritOrientationFn fn = ResolveOgreProc<NodeSetInheritOrientationFn>("?setInheritOrientation@Node@Ogre@@UAEX_N@Z");
			return fn;
		}

		NodeSetInheritScaleFn ResolveNodeSetInheritScale()
		{
			static NodeSetInheritScaleFn fn = ResolveOgreProc<NodeSetInheritScaleFn>("?setInheritScale@Node@Ogre@@UAEX_N@Z");
			return fn;
		}

		CameraGetDerivedPositionFn ResolveCameraGetDerivedPosition()
		{
			static CameraGetDerivedPositionFn fn = ResolveOgreProc<CameraGetDerivedPositionFn>("?getDerivedPosition@Camera@Ogre@@QBEABVVector3@2@XZ");
			return fn;
		}

		MovableObjectDetachFromParentFn ResolveMovableObjectDetachFromParent()
		{
			static MovableObjectDetachFromParentFn fn = ResolveOgreProc<MovableObjectDetachFromParentFn>("?detachFromParent@MovableObject@Ogre@@UAEXXZ");
			return fn;
		}

		EntityHasSkeletonFn ResolveEntityHasSkeleton()
		{
			static EntityHasSkeletonFn fn = ResolveOgreProc<EntityHasSkeletonFn>("?hasSkeleton@Entity@Ogre@@QBE_NXZ");
			return fn;
		}

		EntityAttachObjectToBoneFn ResolveEntityAttachObjectToBone()
		{
			static EntityAttachObjectToBoneFn fn = ResolveOgreProc<EntityAttachObjectToBoneFn>("?attachObjectToBone@Entity@Ogre@@QAEPAVTagPoint@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@PAVMovableObject@2@ABVQuaternion@2@ABVVector3@2@@Z");
			return fn;
		}

		EntityDetachObjectFromBoneFn ResolveEntityDetachObjectFromBone()
		{
			static EntityDetachObjectFromBoneFn fn = ResolveOgreProc<EntityDetachObjectFromBoneFn>("?detachObjectFromBone@Entity@Ogre@@QAEXPAVMovableObject@2@@Z");
			return fn;
		}

		GetNumEmittersFn ResolveGetNumEmitters()
		{
			static GetNumEmittersFn fn = ResolveOgreProc<GetNumEmittersFn>("?getNumEmitters@ParticleSystem@Ogre@@QBEGXZ");
			return fn;
		}

		GetEmitterFn ResolveGetEmitter()
		{
			static GetEmitterFn fn = ResolveOgreProc<GetEmitterFn>("?getEmitter@ParticleSystem@Ogre@@QBEPAVParticleEmitter@2@G@Z");
			return fn;
		}

		EmitterSetEnabledFn ResolveEmitterSetEnabled()
		{
			static EmitterSetEnabledFn fn = ResolveOgreProc<EmitterSetEnabledFn>("?setEnabled@ParticleEmitter@Ogre@@UAEX_N@Z");
			return fn;
		}

		EmitterSetEmissionRateFn ResolveEmitterSetEmissionRate()
		{
			static EmitterSetEmissionRateFn fn = ResolveOgreProc<EmitterSetEmissionRateFn>("?setEmissionRate@ParticleEmitter@Ogre@@UAEXM@Z");
			return fn;
		}

		EmitterGetEmissionRateFn ResolveEmitterGetEmissionRate()
		{
			static EmitterGetEmissionRateFn fn = ResolveOgreProc<EmitterGetEmissionRateFn>("?getEmissionRate@ParticleEmitter@Ogre@@UBEMXZ");
			return fn;
		}

		EmitterSetDirectionFn ResolveEmitterSetDirection()
		{
			static EmitterSetDirectionFn fn = ResolveOgreProc<EmitterSetDirectionFn>("?setDirection@ParticleEmitter@Ogre@@UAEXABVVector3@2@@Z");
			return fn;
		}

		EmitterSetPositionFn ResolveEmitterSetPosition()
		{
			static EmitterSetPositionFn fn = ResolveOgreProc<EmitterSetPositionFn>("?setPosition@ParticleEmitter@Ogre@@UAEXABVVector3@2@@Z");
			return fn;
		}

		EmitterSetVelocityRangeFn ResolveEmitterSetVelocityRange()
		{
			static EmitterSetVelocityRangeFn fn = ResolveOgreProc<EmitterSetVelocityRangeFn>("?setParticleVelocity@ParticleEmitter@Ogre@@UAEXMM@Z");
			return fn;
		}

		EmitterSetAngleFn ResolveEmitterSetAngle()
		{
			static EmitterSetAngleFn fn = ResolveOgreProc<EmitterSetAngleFn>("?setAngle@ParticleEmitter@Ogre@@UAEXABVRadian@2@@Z");
			return fn;
		}

		EmitterSetTimeToLiveRangeFn ResolveEmitterSetTimeToLiveRange()
		{
			static EmitterSetTimeToLiveRangeFn fn = ResolveOgreProc<EmitterSetTimeToLiveRangeFn>("?setTimeToLive@ParticleEmitter@Ogre@@UAEXMM@Z");
			return fn;
		}

		EmitterSetColourRangeFn ResolveEmitterSetColourRange()
		{
			static EmitterSetColourRangeFn fn = ResolveOgreProc<EmitterSetColourRangeFn>("?setColour@ParticleEmitter@Ogre@@UAEXABVColourValue@2@0@Z");
			return fn;
		}

		SetNonVisibleUpdateTimeoutFn ResolveSetNonVisibleUpdateTimeout()
		{
			static SetNonVisibleUpdateTimeoutFn fn = ResolveOgreProc<SetNonVisibleUpdateTimeoutFn>("?setNonVisibleUpdateTimeout@ParticleSystem@Ogre@@QAEXM@Z");
			return fn;
		}

		void ForgetParticleCameraFollower(const std::string& name)
		{
			for (auto it = g_particleCameraFollowers.begin(); it != g_particleCameraFollowers.end(); ++it)
			{
				if (it->particleName == name)
				{
					g_particleCameraFollowers.erase(it);
					return;
				}
			}
		}

		void RememberParticleCameraFollower(const std::string& name, const BZR::VECTOR_3D& offset)
		{
			for (auto& follower : g_particleCameraFollowers)
			{
				if (follower.particleName == name)
				{
					follower.offset = offset;
					return;
				}
			}

			g_particleCameraFollowers.push_back(ParticleCameraFollower{ name, offset });
		}

		// The active viewport's Ogre camera, or null when no viewport is live.
		void* GetActiveOgreCamera()
		{
			const ActiveViewportSet activeViewports = GetActiveViewports();
			if (activeViewports.count == 0 || activeViewports.viewports[0] == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return Ogre::GetViewportCamera(activeViewports.viewports[0]);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] Viewport::getCamera crashed code=0x%08X", GetExceptionCode());
				return nullptr;
			}
		}

		bool TryGetCameraDerivedPosition(void* camera, BZR::VECTOR_3D& outPosition)
		{
			const auto fn = ResolveCameraGetDerivedPosition();
			if (camera == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				const BZR::VECTOR_3D* position = fn(camera);
				if (position == nullptr)
				{
					return false;
				}

				outPosition = *position;
				return IsFiniteVector(outPosition);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] Camera::getDerivedPosition crashed camera=%p code=0x%08X", camera, GetExceptionCode());
				return false;
			}
		}

		void* GetMovableObjectParentSceneNode(void* movableObject)
		{
			const auto fn = ResolveGetParentSceneNode();
			if (movableObject == nullptr || fn == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return fn(movableObject);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] getParentSceneNode crashed movableObject=%p code=0x%08X", movableObject, GetExceptionCode());
				return nullptr;
			}
		}

		// Moves an EXU-owned particle node under a new parent. Ogre asserts if a
		// node is added to a second parent while it still has one, so the
		// current parent link is always broken first.
		bool TryReparentNode(void* node, void* newParent)
		{
			const auto getParentFn = ResolveNodeGetParent();
			const auto removeChildFn = ResolveNodeRemoveChildPtr();
			const auto addChildFn = ResolveNodeAddChild();
			if (node == nullptr || newParent == nullptr || getParentFn == nullptr ||
				removeChildFn == nullptr || addChildFn == nullptr)
			{
				return false;
			}

			__try
			{
				void* currentParent = getParentFn(node);
				if (currentParent == newParent)
				{
					return true;
				}

				if (currentParent != nullptr)
				{
					removeChildFn(currentParent, node);
				}

				addChildFn(newParent, node);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] reparent crashed node=%p newParent=%p code=0x%08X", node, newParent, GetExceptionCode());
				return false;
			}
		}

		bool TrySetNodeInheritOrientation(void* node, bool inherit)
		{
			const auto fn = ResolveNodeSetInheritOrientation();
			if (node == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(node, inherit);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setInheritOrientation crashed node=%p code=0x%08X", node, GetExceptionCode());
				return false;
			}
		}

		bool TrySetNodeInheritScale(void* node, bool inherit)
		{
			const auto fn = ResolveNodeSetInheritScale();
			if (node == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(node, inherit);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setInheritScale crashed node=%p code=0x%08X", node, GetExceptionCode());
				return false;
			}
		}

		bool TrySetNodePositionDirect(void* node, const BZR::VECTOR_3D& position)
		{
			const auto fn = ResolveSetNodePosition();
			if (node == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(node, position);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setPosition crashed node=%p code=0x%08X", node, GetExceptionCode());
				return false;
			}
		}

		bool TryAttachManagedParticleToRoot(void* sceneManager, const std::string& name)
		{
			const auto getRootFn = ResolveGetRootSceneNode();
			void* node = nullptr;
			if (sceneManager == nullptr || getRootFn == nullptr || !TryGetManagedParticleSceneNode(sceneManager, name, node))
			{
				return false;
			}

			void* rootSceneNode = nullptr;
			__try
			{
				rootSceneNode = getRootFn(sceneManager);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] getRootSceneNode crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
				return false;
			}

			if (!TryReparentNode(node, rootSceneNode))
			{
				return false;
			}

			TrySetNodeInheritOrientation(node, true);
			TrySetNodeInheritScale(node, true);
			return true;
		}

		// Returns true when the particle system's node now hangs off the
		// camera's own scene node. Returns false when the engine drives its
		// camera without a node -- the caller falls back to follower mode.
		bool TryAttachManagedParticleToCameraNode(void* sceneManager, const std::string& name, const BZR::VECTOR_3D& offset)
		{
			void* camera = GetActiveOgreCamera();
			if (camera == nullptr)
			{
				return false;
			}

			void* cameraNode = GetMovableObjectParentSceneNode(camera);
			if (cameraNode == nullptr)
			{
				LogEnvironmentDebug("[EXU::Particle] camera attach unavailable name=%s reason=camera_has_no_scene_node", name.c_str());
				return false;
			}

			void* node = nullptr;
			if (!TryGetManagedParticleSceneNode(sceneManager, name, node))
			{
				return false;
			}

			if (!TryReparentNode(node, cameraNode))
			{
				return false;
			}

			// A precipitation volume must not roll or yaw with the view; only
			// its position should track the camera.
			TrySetNodeInheritOrientation(node, false);
			TrySetNodeInheritScale(node, false);
			TrySetNodePositionDirect(node, offset);
			return true;
		}

		bool TryUpdateParticleCameraFollower(void* sceneManager, const ParticleCameraFollower& follower, const BZR::VECTOR_3D& cameraPosition)
		{
			void* node = nullptr;
			if (!TryGetManagedParticleSceneNode(sceneManager, follower.particleName, node))
			{
				return false;
			}

			const BZR::VECTOR_3D position{
				cameraPosition.x + follower.offset.x,
				cameraPosition.y + follower.offset.y,
				cameraPosition.z + follower.offset.z,
			};
			return TrySetNodePositionDirect(node, position);
		}

		bool TryAttachManagedParticleToObjectNode(void* sceneManager, const std::string& name, void* entity, const BZR::VECTOR_3D& offset)
		{
			void* objectNode = GetMovableObjectParentSceneNode(entity);
			if (objectNode == nullptr)
			{
				LogEnvironmentDebug("[EXU::Particle] object attach failed name=%s reason=entity_has_no_scene_node", name.c_str());
				return false;
			}

			void* node = nullptr;
			if (!TryGetManagedParticleSceneNode(sceneManager, name, node))
			{
				return false;
			}

			if (!TryReparentNode(node, objectNode))
			{
				return false;
			}

			// Damage smoke and engine glow are body-relative, so orientation is
			// inherited here even though the camera volume does not inherit it.
			TrySetNodeInheritOrientation(node, true);
			TrySetNodeInheritScale(node, false);
			TrySetNodePositionDirect(node, offset);
			return true;
		}

		bool TryAttachManagedParticleToBone(void* sceneManager, const std::string& name, void* entity, const std::string& boneName, const BZR::VECTOR_3D& offset)
		{
			const auto hasSkeletonFn = ResolveEntityHasSkeleton();
			const auto attachFn = ResolveEntityAttachObjectToBone();
			const auto detachFromParentFn = ResolveMovableObjectDetachFromParent();
			if (entity == nullptr || hasSkeletonFn == nullptr || attachFn == nullptr || detachFromParentFn == nullptr)
			{
				return false;
			}

			void* particleSystem = nullptr;
			if (!TryGetParticleSystem(sceneManager, name, particleSystem))
			{
				return false;
			}

			const OgreQuaternionValue identity{};
			__try
			{
				if (!hasSkeletonFn(entity))
				{
					LogEnvironmentDebug("[EXU::Particle] bone attach failed name=%s bone=%s reason=entity_has_no_skeleton", name.c_str(), boneName.c_str());
					return false;
				}

				// A movable object can only live on one attachment point, and
				// the managed system starts life on its own scene node.
				detachFromParentFn(particleSystem);
				return attachFn(entity, boneName, particleSystem, identity, offset) != nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug(
					"[EXU::Particle] bone attach crashed name=%s bone=%s entity=%p code=0x%08X",
					name.c_str(),
					boneName.c_str(),
					entity,
					GetExceptionCode());
				return false;
			}
		}

		void* GetParticleEmitter(void* sceneManager, const std::string& name, int emitterIndex)
		{
			const auto getNumFn = ResolveGetNumEmitters();
			const auto getEmitterFn = ResolveGetEmitter();
			void* particleSystem = nullptr;
			if (emitterIndex < 0 || getNumFn == nullptr || getEmitterFn == nullptr ||
				!TryGetParticleSystem(sceneManager, name, particleSystem))
			{
				return nullptr;
			}

			__try
			{
				const uint16_t count = getNumFn(particleSystem);
				if (emitterIndex >= static_cast<int>(count))
				{
					return nullptr;
				}

				return getEmitterFn(particleSystem, static_cast<uint16_t>(emitterIndex));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] getEmitter crashed name=%s index=%d code=0x%08X", name.c_str(), emitterIndex, GetExceptionCode());
				return nullptr;
			}
		}

		bool TryGetParticleEmitterCount(void* sceneManager, const std::string& name, int& outCount)
		{
			outCount = 0;
			const auto fn = ResolveGetNumEmitters();
			void* particleSystem = nullptr;
			if (fn == nullptr || !TryGetParticleSystem(sceneManager, name, particleSystem))
			{
				return false;
			}

			__try
			{
				outCount = static_cast<int>(fn(particleSystem));
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] getNumEmitters crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterEnabled(void* emitter, bool enabled)
		{
			const auto fn = ResolveEmitterSetEnabled();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter setEnabled crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterEmissionRate(void* emitter, float rate)
		{
			const auto fn = ResolveEmitterSetEmissionRate();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, rate);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter setEmissionRate crashed emitter=%p rate=%g code=0x%08X", emitter, rate, GetExceptionCode());
				return false;
			}
		}

		bool TryGetEmitterEmissionRate(void* emitter, float& outRate)
		{
			outRate = 0.0f;
			const auto fn = ResolveEmitterGetEmissionRate();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				outRate = fn(emitter);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter getEmissionRate crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterDirection(void* emitter, const BZR::VECTOR_3D& direction)
		{
			const auto fn = ResolveEmitterSetDirection();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, direction);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter setDirection crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterPosition(void* emitter, const BZR::VECTOR_3D& position)
		{
			const auto fn = ResolveEmitterSetPosition();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, position);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter setPosition crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterVelocityRange(void* emitter, float minVelocity, float maxVelocity)
		{
			const auto fn = ResolveEmitterSetVelocityRange();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, minVelocity, maxVelocity);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter setParticleVelocity crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterAngle(void* emitter, float radians)
		{
			const auto fn = ResolveEmitterSetAngle();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, radians);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter setAngle crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterTimeToLiveRange(void* emitter, float minTimeToLive, float maxTimeToLive)
		{
			const auto fn = ResolveEmitterSetTimeToLiveRange();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, minTimeToLive, maxTimeToLive);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter setTimeToLive crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		bool TrySetEmitterColourRange(void* emitter, const Ogre::Color& startColor, const Ogre::Color& endColor)
		{
			const auto fn = ResolveEmitterSetColourRange();
			if (emitter == nullptr || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(emitter, startColor, endColor);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] emitter setColour crashed emitter=%p code=0x%08X", emitter, GetExceptionCode());
				return false;
			}
		}

		// Undoes any of the three attachments: the system goes back onto its own
		// EXU-owned node, and that node goes back under the scene root.
		bool TryReturnManagedParticleToOwnNode(void* sceneManager, const std::string& name)
		{
			const auto detachFromParentFn = ResolveMovableObjectDetachFromParent();
			const auto attachObjectFn = ResolveAttachObject();
			void* particleSystem = nullptr;
			void* node = nullptr;
			if (detachFromParentFn == nullptr || attachObjectFn == nullptr ||
				!TryGetParticleSystem(sceneManager, name, particleSystem) ||
				!TryGetManagedParticleSceneNode(sceneManager, name, node))
			{
				return false;
			}

			if (!TryAttachManagedParticleToRoot(sceneManager, name))
			{
				return false;
			}

			__try
			{
				// A bone attachment moved the system off its own node; a camera or
				// object attachment only moved the node, so this is a no-op there.
				detachFromParentFn(particleSystem);
				attachObjectFn(node, particleSystem);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] detach crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		bool TrySetParticleSystemNonVisibleUpdateTimeout(void* sceneManager, const std::string& name, float timeout)
		{
			void* particleSystem = nullptr;
			const auto fn = ResolveSetNonVisibleUpdateTimeout();
			if (!TryGetParticleSystem(sceneManager, name, particleSystem) || fn == nullptr)
			{
				return false;
			}

			__try
			{
				fn(particleSystem, timeout);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				LogEnvironmentDebug("[EXU::Particle] setNonVisibleUpdateTimeout crashed name=%s code=0x%08X", name.c_str(), GetExceptionCode());
				return false;
			}
		}

		// Mission teardown: the scene these followers point into is gone, so the
		// list must not survive into the next mission's scene manager.
		void ForgetAllParticleCameraFollowers()
		{
			g_particleCameraFollowers.clear();
		}


		using GetViewportOverlaysEnabledFn = bool(__thiscall*)(void*);
		using SetViewportOverlaysEnabledFn = void(__thiscall*)(void*, bool);

	GetViewportOverlaysEnabledFn ResolveGetViewportOverlaysEnabled()
	{
		static GetViewportOverlaysEnabledFn fn = []()
		{
			HMODULE ogreMain = GetModuleHandleA("OgreMain.dll");
			if (ogreMain == nullptr)
			{
				return static_cast<GetViewportOverlaysEnabledFn>(nullptr);
			}

			return reinterpret_cast<GetViewportOverlaysEnabledFn>(
				GetProcAddress(ogreMain, "?getOverlaysEnabled@Viewport@Ogre@@QBE_NXZ"));
		}();

		return fn;
	}

	SetViewportOverlaysEnabledFn ResolveSetViewportOverlaysEnabled()
	{
		static SetViewportOverlaysEnabledFn fn = []()
		{
			HMODULE ogreMain = GetModuleHandleA("OgreMain.dll");
			if (ogreMain == nullptr)
			{
				return static_cast<SetViewportOverlaysEnabledFn>(nullptr);
			}

			return reinterpret_cast<SetViewportOverlaysEnabledFn>(
				GetProcAddress(ogreMain, "?setOverlaysEnabled@Viewport@Ogre@@QAEX_N@Z"));
		}();

		return fn;
	}

	bool TryGetViewportOverlaysEnabled(void* viewport, bool& outEnabled)
	{
		outEnabled = false;
		if (viewport == nullptr)
		{
			return false;
		}

		const auto fn = ResolveGetViewportOverlaysEnabled();
		if (fn == nullptr)
		{
			LogEnvironmentDebug("[EXU::Viewport] getOverlaysEnabled unavailable viewport=%p", viewport);
			return false;
		}

		__try
		{
			outEnabled = fn(viewport);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Viewport] getOverlaysEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
			return false;
		}
	}

	bool TrySetViewportOverlaysEnabled(void* viewport, bool enabled)
	{
		if (viewport == nullptr)
		{
			return false;
		}

		const auto fn = ResolveSetViewportOverlaysEnabled();
		if (fn == nullptr)
		{
			LogEnvironmentDebug("[EXU::Viewport] setOverlaysEnabled unavailable viewport=%p enabled=%d", viewport, enabled ? 1 : 0);
			return false;
		}

		__try
		{
			fn(viewport, enabled);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Viewport] setOverlaysEnabled crashed viewport=%p enabled=%d code=0x%08X", viewport, enabled ? 1 : 0, GetExceptionCode());
			return false;
		}
	}

	bool TryRefreshViewport(void* viewport)
	{
		if (viewport == nullptr)
		{
			return false;
		}

		bool overlaysEnabled = false;
		if (!TryGetViewportOverlaysEnabled(viewport, overlaysEnabled))
		{
			return false;
		}

		// Nudge the active viewport through a harmless state flip so Ogre reapplies the new material
		// scheme on the live viewport instead of waiting for a later startup/viewport rebuild path.
		if (!TrySetViewportOverlaysEnabled(viewport, !overlaysEnabled))
		{
			return false;
		}
		return TrySetViewportOverlaysEnabled(viewport, overlaysEnabled);
	}

	void* GetSceneManager()
	{
		return Ogre::sceneManager.Read();
	}

	void* GetCurrentViewport()
	{
		const ActiveViewportSet activeViewports = GetActiveViewports();
		return activeViewports.count > 0 ? activeViewports.viewports[0] : nullptr;
	}

	void* GetTerrainMasterLight()
	{
		return Ogre::terrain_masterlight.Read();
	}

	Ogre::Color DefaultSunColor()
	{
		return {};
	}

	int GetGravity(lua_State* L)
	{
		BZR::VECTOR_3D g = gravity.Read();

		PushVector(L, g);

		return 1;
	}

	int SetGravity(lua_State* L)
	{
		auto newGravity = CheckVectorOrSingles(L, 1);
		gravity.Write(newGravity);

		return 0;
	}

	int GetFog(lua_State* L)
	{
		Patch::TryInitializeOgre();

		if (GetSceneManager() == nullptr)
		{
			PushFog(L, {});
			return 1;
		}

		Ogre::Fog f = fog.Read();

		PushFog(L, f);

		return 1;
	}

	int SetFog(lua_State* L)
	{
		Patch::TryInitializeOgre();

		if (GetSceneManager() == nullptr)
		{
			return 0;
		}

		auto f = CheckFogOrSingles(L, 1);

		fog.Write(f);

		return 0;
	}

	int GetSunAmbient(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			PushColor(L, DefaultSunColor());
			return 1;
		}

		Ogre::Color sunColor = DefaultSunColor();
		TryGetSunAmbientColor(sceneManager, sunColor);
		PushColor(L, sunColor);

		return 1;
	}

	int SetSunAmbient(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunAmbient] enter caller=%s argType=%s sceneManager=%p",
			caller.c_str(),
			luaL_typename(L, 1),
			sceneManager);
		if (sceneManager == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunAmbient] skipped: scene manager unavailable");
			return 0;
		}

		auto color = CheckColorOrSingles(L, 1);
		LogEnvironmentDebug(
			"[EXU::SetSunAmbient] parsed r=%.9g g=%.9g b=%.9g finite=%s inRange=%s",
			color.r,
			color.g,
			color.b,
			IsFiniteColor(color) ? "true" : "false",
			IsExpectedColorRange(color) ? "true" : "false");
		if (!IsFiniteColor(color))
		{
			LogEnvironmentDebug("[EXU::SetSunAmbient] rejecting non-finite color");
			return luaL_argerror(L, 1, "SetSunAmbient requires finite numeric color values");
		}

		LogEnvironmentDebug("[EXU::SetSunAmbient] calling Ogre::SetAmbientLight");
		if (!TrySetSunAmbientColor(sceneManager, color))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunAmbient] completed");

		return 0;
	}

	int GetAmbientLight(lua_State* L)
	{
		return GetSunAmbient(L);
	}

	int SetAmbientLight(lua_State* L)
	{
		return SetSunAmbient(L);
	}

	int GetSunDiffuse(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		if (terrainMasterLight == nullptr)
		{
			PushColor(L, DefaultSunColor());
			return 1;
		}

		Ogre::Color diffuseColor = DefaultSunColor();
		TryGetSunDiffuseColor(terrainMasterLight, diffuseColor);
		PushColor(L, diffuseColor);

		return 1;
	}

	int SetSunDiffuse(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunDiffuse] enter caller=%s argType=%s terrainMasterLight=%p",
			caller.c_str(),
			luaL_typename(L, 1),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunDiffuse] skipped: terrain master light unavailable");
			return 0;
		}

		auto color = CheckColorOrSingles(L, 1);
		LogEnvironmentDebug(
			"[EXU::SetSunDiffuse] parsed r=%.9g g=%.9g b=%.9g finite=%s inRange=%s",
			color.r,
			color.g,
			color.b,
			IsFiniteColor(color) ? "true" : "false",
			IsExpectedColorRange(color) ? "true" : "false");
		if (!IsFiniteColor(color))
		{
			LogEnvironmentDebug("[EXU::SetSunDiffuse] rejecting non-finite color");
			return luaL_argerror(L, 1, "SetSunDiffuse requires finite numeric color values");
		}

		LogEnvironmentDebug("[EXU::SetSunDiffuse] calling Ogre::SetDiffuseColor");
		if (!TrySetSunDiffuseColor(terrainMasterLight, color))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunDiffuse] completed");

		return 0;
	}

	int GetSunSpecular(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		if (terrainMasterLight == nullptr)
		{
			PushColor(L, DefaultSunColor());
			return 1;
		}

		Ogre::Color specularColor = DefaultSunColor();
		TryGetSunSpecularColor(terrainMasterLight, specularColor);
		PushColor(L, specularColor);

		return 1;
	}

	int SetSunSpecular(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunSpecular] enter caller=%s argType=%s terrainMasterLight=%p",
			caller.c_str(),
			luaL_typename(L, 1),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunSpecular] skipped: terrain master light unavailable");
			return 0;
		}

		auto color = CheckColorOrSingles(L, 1);
		LogEnvironmentDebug(
			"[EXU::SetSunSpecular] parsed r=%.9g g=%.9g b=%.9g finite=%s inRange=%s",
			color.r,
			color.g,
			color.b,
			IsFiniteColor(color) ? "true" : "false",
			IsExpectedColorRange(color) ? "true" : "false");
		if (!IsFiniteColor(color))
		{
			LogEnvironmentDebug("[EXU::SetSunSpecular] rejecting non-finite color");
			return luaL_argerror(L, 1, "SetSunSpecular requires finite numeric color values");
		}

		LogEnvironmentDebug("[EXU::SetSunSpecular] calling Ogre::SetSpecularColor");
		if (!TrySetSunSpecularColor(terrainMasterLight, color))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunSpecular] completed");

		return 0;
	}

	int GetSunDirection(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		if (terrainMasterLight == nullptr)
		{
			PushVector(L, {});
			return 1;
		}

		BZR::VECTOR_3D direction{};
		TryGetSunDirection(terrainMasterLight, direction);
		PushVector(L, direction);
		return 1;
	}

	int SetSunDirection(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunDirection] enter caller=%s argType=%s terrainMasterLight=%p",
			caller.c_str(),
			luaL_typename(L, 1),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunDirection] skipped: terrain master light unavailable");
			return 0;
		}

		auto direction = CheckVectorOrSingles(L, 1);
		LogEnvironmentDebug(
			"[EXU::SetSunDirection] parsed x=%.9g y=%.9g z=%.9g finite=%s",
			direction.x,
			direction.y,
			direction.z,
			IsFiniteVector(direction) ? "true" : "false");
		if (!IsFiniteVector(direction))
		{
			LogEnvironmentDebug("[EXU::SetSunDirection] rejecting non-finite direction");
			return luaL_argerror(L, 1, "SetSunDirection requires finite numeric vector values");
		}

		LogEnvironmentDebug("[EXU::SetSunDirection] calling Ogre::SetDirection");
		if (!TrySetSunDirection(terrainMasterLight, direction))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunDirection] completed");
		return 0;
	}

	int SetOgreSunDirection(lua_State* L)
	{
		return SetSunDirection(L);
	}

	int SetTimeOfDay(lua_State* L)
	{
		Patch::TryInitializeOgre();

		const int timeOfDay = static_cast<int>(luaL_checkinteger(L, 1));
		const bool refreshSun = lua_gettop(L) < 2 || lua_toboolean(L, 2) != 0;
		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetTimeOfDay] enter caller=%s timeOfDay=%d refreshSun=%s terrainMasterLight=%p",
			caller.c_str(),
			timeOfDay,
			refreshSun ? "true" : "false",
			terrainMasterLight);
		if (!IsValidTimeOfDay(timeOfDay))
		{
			LogEnvironmentDebug("[EXU::SetTimeOfDay] rejecting invalid HHMM value");
			return luaL_argerror(L, 1, "SetTimeOfDay requires a TRN-style HHMM integer between 0000 and 2359");
		}

		LogEnvironmentDebug("[EXU::SetTimeOfDay] calling native light model hour=%d", timeOfDay / 100);
		if (!TrySetNativeTimeOfDay(timeOfDay))
		{
			return 0;
		}

		if (!refreshSun)
		{
			LogEnvironmentDebug("[EXU::SetTimeOfDay] completed without Ogre refresh");
			return 0;
		}

		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetTimeOfDay] completed without Ogre refresh: terrain master light unavailable");
			return 0;
		}

		LogEnvironmentDebug("[EXU::SetTimeOfDay] refreshing terrain master light");
		if (!TryRefreshTerrainMasterLight())
		{
			return 0;
		}

		LogEnvironmentDebug("[EXU::SetTimeOfDay] completed");
		return 0;
	}

	int GetSunPowerScale(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		if (terrainMasterLight == nullptr)
		{
			lua_pushnumber(L, 0.0);
			return 1;
		}

		float powerScale = 0.0f;
		TryGetSunPowerScale(terrainMasterLight, powerScale);
		lua_pushnumber(L, powerScale);
		return 1;
	}

	int SetSunPowerScale(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunPowerScale] enter caller=%s terrainMasterLight=%p",
			caller.c_str(),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunPowerScale] skipped: terrain master light unavailable");
			return 0;
		}

		float powerScale = static_cast<float>(luaL_checknumber(L, 1));
		LogEnvironmentDebug(
			"[EXU::SetSunPowerScale] parsed value=%.9g finite=%s",
			powerScale,
			IsFiniteScalar(powerScale) ? "true" : "false");
		if (!IsFiniteScalar(powerScale))
		{
			LogEnvironmentDebug("[EXU::SetSunPowerScale] rejecting non-finite value");
			return luaL_argerror(L, 1, "SetSunPowerScale requires a finite numeric value");
		}

		LogEnvironmentDebug("[EXU::SetSunPowerScale] calling Ogre::SetPowerScale");
		if (!TrySetSunPowerScale(terrainMasterLight, powerScale))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunPowerScale] completed");
		return 0;
	}

	int GetSunShadowFarDistance(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		if (terrainMasterLight == nullptr)
		{
			lua_pushnumber(L, 0.0);
			return 1;
		}

		float shadowFarDistance = 0.0f;
		TryGetSunShadowFarDistance(terrainMasterLight, shadowFarDistance);
		lua_pushnumber(L, shadowFarDistance);
		return 1;
	}

	int SetSunShadowFarDistance(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* terrainMasterLight = GetTerrainMasterLight();
		auto caller = DescribeLuaCaller(L);
		LogEnvironmentDebug(
			"[EXU::SetSunShadowFarDistance] enter caller=%s terrainMasterLight=%p",
			caller.c_str(),
			terrainMasterLight);
		if (terrainMasterLight == nullptr)
		{
			LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] skipped: terrain master light unavailable");
			return 0;
		}

		float shadowFarDistance = static_cast<float>(luaL_checknumber(L, 1));
		LogEnvironmentDebug(
			"[EXU::SetSunShadowFarDistance] parsed value=%.9g finite=%s",
			shadowFarDistance,
			IsFiniteScalar(shadowFarDistance) ? "true" : "false");
		if (!IsFiniteScalar(shadowFarDistance))
		{
			LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] rejecting non-finite value");
			return luaL_argerror(L, 1, "SetSunShadowFarDistance requires a finite numeric value");
		}

		LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] calling Ogre::SetShadowFarDistance");
		if (!TrySetSunShadowFarDistance(terrainMasterLight, shadowFarDistance))
		{
			return 0;
		}
		LogEnvironmentDebug("[EXU::SetSunShadowFarDistance] completed");
		return 0;
	}

	int GetSkyBoxParams(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool hasNode = false;
		Ogre::SkyBoxGenParameters params{};
		if (!TryHasSkyNode(sceneManager, Ogre::GetSkyBoxNode, hasNode, "skybox") || !hasNode ||
			!TryGetSkyBoxGenParameters(sceneManager, params))
		{
			lua_pushnil(L);
			return 1;
		}

		PushSkyBoxParams(L, params);
		return 1;
	}

	int GetSkyDomeParams(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool hasNode = false;
		Ogre::SkyDomeGenParameters params{};
		if (!TryHasSkyNode(sceneManager, Ogre::GetSkyDomeNode, hasNode, "skydome") || !hasNode ||
			!TryGetSkyDomeGenParameters(sceneManager, params))
		{
			lua_pushnil(L);
			return 1;
		}

		PushSkyDomeParams(L, params);
		return 1;
	}

	int GetSkyPlaneParams(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		bool hasNode = false;
		Ogre::SkyPlaneGenParameters params{};
		if (!TryHasSkyNode(sceneManager, Ogre::GetSkyPlaneNode, hasNode, "skyplane") || !hasNode ||
			!TryGetSkyPlaneGenParameters(sceneManager, params))
		{
			lua_pushnil(L);
			return 1;
		}

		PushSkyPlaneParams(L, params);
		return 1;
	}

	int GetSkyBoxEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		bool enabled = false;
		if (sceneManager != nullptr)
		{
			TryGetSkyEnabled(sceneManager, ResolveIsSkyBoxEnabled(), enabled, "skybox");
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetSkyBoxEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		const bool enabled = CheckBool(L, 1);
		TrySetSkyEnabled(sceneManager, ResolveSetSkyBoxEnabled(), enabled, "skybox");
		return 0;
	}

	int SetSkyBox(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string materialName = luaL_checkstring(L, 1);
		const float distance = static_cast<float>(luaL_optnumber(L, 2, 5000.0));
		const bool drawFirst = lua_gettop(L) < 3 || lua_toboolean(L, 3) != 0;
		const std::string resourceGroup = CheckOptionalSkyResourceGroup(L, 4);

		lua_pushboolean(L, TrySetSkyBox(sceneManager, materialName, distance, drawFirst, resourceGroup) ? 1 : 0);
		return 1;
	}

	int GetSkyDomeEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		bool enabled = false;
		if (sceneManager != nullptr)
		{
			TryGetSkyEnabled(sceneManager, ResolveIsSkyDomeEnabled(), enabled, "skydome");
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetSkyDomeEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		const bool enabled = CheckBool(L, 1);
		TrySetSkyEnabled(sceneManager, ResolveSetSkyDomeEnabled(), enabled, "skydome");
		return 0;
	}

	int SetSkyDome(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string materialName = luaL_checkstring(L, 1);
		const float curvature = static_cast<float>(luaL_optnumber(L, 2, 10.0));
		const float tiling = static_cast<float>(luaL_optnumber(L, 3, 8.0));
		const float distance = static_cast<float>(luaL_optnumber(L, 4, 4000.0));
		const bool drawFirst = lua_gettop(L) < 5 || lua_toboolean(L, 5) != 0;
		const int xsegments = luaL_optint(L, 6, 16);
		const int ysegments = luaL_optint(L, 7, 16);
		const int ysegmentsKeep = luaL_optint(L, 8, -1);
		const std::string resourceGroup = CheckOptionalSkyResourceGroup(L, 9);

		lua_pushboolean(L, TrySetSkyDome(
			sceneManager,
			materialName,
			curvature,
			tiling,
			distance,
			drawFirst,
			xsegments,
			ysegments,
			ysegmentsKeep,
			resourceGroup) ? 1 : 0);
		return 1;
	}

	int GetSkyPlaneEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		bool enabled = false;
		if (sceneManager != nullptr)
		{
			TryGetSkyEnabled(sceneManager, ResolveIsSkyPlaneEnabled(), enabled, "skyplane");
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetSkyPlaneEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		const bool enabled = CheckBool(L, 1);
		TrySetSkyEnabled(sceneManager, ResolveSetSkyPlaneEnabled(), enabled, "skyplane");
		return 0;
	}

	int SetSkyPlane(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string materialName = luaL_checkstring(L, 1);
		OgrePlaneValue plane{};
		TryReadSkyPlane(L, 2, plane);
		const float scale = static_cast<float>(luaL_optnumber(L, 3, 1000.0));
		const float tiling = static_cast<float>(luaL_optnumber(L, 4, 10.0));
		const bool drawFirst = lua_gettop(L) < 5 || lua_toboolean(L, 5) != 0;
		const float bow = static_cast<float>(luaL_optnumber(L, 6, 0.0));
		const int xsegments = luaL_optint(L, 7, 1);
		const int ysegments = luaL_optint(L, 8, 1);
		const std::string resourceGroup = CheckOptionalSkyResourceGroup(L, 9);

		lua_pushboolean(L, TrySetSkyPlane(
			sceneManager,
			plane,
			materialName,
			scale,
			tiling,
			drawFirst,
			bow,
			xsegments,
			ysegments,
			resourceGroup) ? 1 : 0);
		return 1;
	}

	int HasParticleSystem(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		bool hasParticleSystem = false;
		if (sceneManager != nullptr)
		{
			const std::string name = luaL_checkstring(L, 1);
			TryHasParticleSystem(sceneManager, name, hasParticleSystem);
		}

		lua_pushboolean(L, hasParticleSystem ? 1 : 0);
		return 1;
	}

	int CreateParticleSystem(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const std::string templateName = luaL_checkstring(L, 2);
		BZR::VECTOR_3D position{ 0.0f, 0.0f, 0.0f };
		if (!lua_isnoneornil(L, 3))
		{
			position = CheckVectorOrSingles(L, 3);
			if (!IsFiniteVector(position))
			{
				return luaL_argerror(L, 3, "CreateParticleSystem requires a finite position vector");
			}
		}

		lua_pushboolean(L, TryCreateManagedParticleSystem(sceneManager, name, templateName, position) ? 1 : 0);
		return 1;
	}

	int DestroyParticleSystem(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		ForgetParticleCameraFollower(name);
		lua_pushboolean(L, TryDestroyManagedParticleSystem(sceneManager, name) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemPosition(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const BZR::VECTOR_3D position = CheckVectorOrSingles(L, 2);
		if (!IsFiniteVector(position))
		{
			return luaL_argerror(L, 2, "SetParticleSystemPosition requires a finite position vector");
		}

		lua_pushboolean(L, TrySetManagedParticleSceneNodePosition(sceneManager, name, position) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemDirection(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const BZR::VECTOR_3D direction = CheckVectorOrSingles(L, 2);
		if (!IsFiniteVector(direction))
		{
			return luaL_argerror(L, 2, "SetParticleSystemDirection requires a finite direction vector");
		}

		lua_pushboolean(L, TrySetManagedParticleSceneNodeDirection(sceneManager, name, direction) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemEmitting(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const bool enabled = CheckBool(L, 2);
		lua_pushboolean(L, TrySetParticleSystemEmitting(sceneManager, name, enabled) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemVisible(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const bool enabled = CheckBool(L, 2);
		lua_pushboolean(L, TrySetParticleSystemVisible(sceneManager, name, enabled) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemSpeedFactor(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const float speedFactor = static_cast<float>(luaL_checknumber(L, 2));
		if (!IsFiniteScalar(speedFactor))
		{
			return luaL_argerror(L, 2, "SetParticleSystemSpeedFactor requires a finite numeric value");
		}

		lua_pushboolean(L, TrySetParticleSystemSpeedFactor(sceneManager, name, speedFactor) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemKeepLocalSpace(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const bool enabled = CheckBool(L, 2);
		lua_pushboolean(L, TrySetParticleSystemKeepLocalSpace(sceneManager, name, enabled) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemMaterial(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const std::string materialName = luaL_checkstring(L, 2);
		const std::string resourceGroup = CheckOptionalParticleResourceGroup(L, 3);
		lua_pushboolean(L, TrySetParticleSystemMaterial(sceneManager, name, materialName, resourceGroup) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemRenderQueueGroup(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const lua_Integer queueGroup = luaL_checkinteger(L, 2);
		if (queueGroup < 0 || queueGroup > 255)
		{
			return luaL_argerror(L, 2, "SetParticleSystemRenderQueueGroup requires an integer in the range 0-255");
		}

		lua_pushboolean(L, TrySetParticleSystemRenderQueueGroup(sceneManager, name, static_cast<uint8_t>(queueGroup)) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemParticleQuota(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const lua_Integer quota = luaL_checkinteger(L, 2);
		if (quota < 0)
		{
			return luaL_argerror(L, 2, "SetParticleSystemParticleQuota requires a non-negative integer");
		}

		lua_pushboolean(L, TrySetParticleSystemParticleQuota(sceneManager, name, static_cast<uint32_t>(quota)) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemDefaultDimensions(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const float width = static_cast<float>(luaL_checknumber(L, 2));
		const float height = static_cast<float>(luaL_checknumber(L, 3));
		if (!IsFiniteScalar(width))
		{
			return luaL_argerror(L, 2, "SetParticleSystemDefaultDimensions requires a finite width");
		}
		if (!IsFiniteScalar(height))
		{
			return luaL_argerror(L, 3, "SetParticleSystemDefaultDimensions requires a finite height");
		}

		lua_pushboolean(L, TrySetParticleSystemDefaultDimensions(sceneManager, name, width, height) ? 1 : 0);
		return 1;
	}


	int AttachParticleSystemToCamera(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		BZR::VECTOR_3D offset{ 0.0f, 0.0f, 0.0f };
		if (!lua_isnoneornil(L, 2))
		{
			offset = CheckVectorOrSingles(L, 2);
			if (!IsFiniteVector(offset))
			{
				return luaL_argerror(L, 2, "AttachParticleSystemToCamera requires a finite offset vector");
			}
		}

		if (TryAttachManagedParticleToCameraNode(sceneManager, name, offset))
		{
			// A real scene-graph attachment needs no per-frame help.
			ForgetParticleCameraFollower(name);
			lua_pushboolean(L, 1);
			return 1;
		}

		// No camera scene node in this build: fall back to a native follower
		// that UpdateParticleFollowers drives, and place it once now so the
		// first frame is already correct.
		BZR::VECTOR_3D cameraPosition{ 0.0f, 0.0f, 0.0f };
		if (!TryGetCameraDerivedPosition(GetActiveOgreCamera(), cameraPosition))
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		RememberParticleCameraFollower(name, offset);
		const ParticleCameraFollower follower{ name, offset };
		lua_pushboolean(L, TryUpdateParticleCameraFollower(sceneManager, follower, cameraPosition) ? 1 : 0);
		return 1;
	}

	int AttachParticleSystemToObject(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const BZR::handle h = CheckHandle(L, 2);
		BZR::VECTOR_3D offset{ 0.0f, 0.0f, 0.0f };
		if (!lua_isnoneornil(L, 3))
		{
			offset = CheckVectorOrSingles(L, 3);
			if (!IsFiniteVector(offset))
			{
				return luaL_argerror(L, 3, "AttachParticleSystemToObject requires a finite offset vector");
			}
		}

		void* entity = GameObject::ResolveAnimationEntity(h);
		if (entity == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		ForgetParticleCameraFollower(name);
		lua_pushboolean(L, TryAttachManagedParticleToObjectNode(sceneManager, name, entity, offset) ? 1 : 0);
		return 1;
	}

	int AttachParticleSystemToBone(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const BZR::handle h = CheckHandle(L, 2);
		const std::string boneName = luaL_checkstring(L, 3);
		BZR::VECTOR_3D offset{ 0.0f, 0.0f, 0.0f };
		if (!lua_isnoneornil(L, 4))
		{
			offset = CheckVectorOrSingles(L, 4);
			if (!IsFiniteVector(offset))
			{
				return luaL_argerror(L, 4, "AttachParticleSystemToBone requires a finite offset vector");
			}
		}

		void* entity = GameObject::ResolveAnimationEntity(h);
		if (entity == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		ForgetParticleCameraFollower(name);
		lua_pushboolean(L, TryAttachManagedParticleToBone(sceneManager, name, entity, boneName, offset) ? 1 : 0);
		return 1;
	}

	int DetachParticleSystem(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		ForgetParticleCameraFollower(name);
		lua_pushboolean(L, TryReturnManagedParticleToOwnNode(sceneManager, name) ? 1 : 0);
		return 1;
	}

	int UpdateParticleFollowers(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr || g_particleCameraFollowers.empty())
		{
			lua_pushinteger(L, 0);
			return 1;
		}

		BZR::VECTOR_3D cameraPosition{ 0.0f, 0.0f, 0.0f };
		if (!TryGetCameraDerivedPosition(GetActiveOgreCamera(), cameraPosition))
		{
			lua_pushinteger(L, 0);
			return 1;
		}

		int updated = 0;
		for (const auto& follower : g_particleCameraFollowers)
		{
			if (TryUpdateParticleCameraFollower(sceneManager, follower, cameraPosition))
			{
				++updated;
			}
		}

		lua_pushinteger(L, updated);
		return 1;
	}

	int GetParticleSystemEmitterCount(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		int count = 0;
		if (!TryGetParticleEmitterCount(sceneManager, name, count))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, count);
		return 1;
	}

	int GetParticleEmitterEmissionRate(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		float rate = 0.0f;
		if (!TryGetEmitterEmissionRate(GetParticleEmitter(sceneManager, name, emitterIndex), rate))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushnumber(L, rate);
		return 1;
	}

	int SetParticleEmitterEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const bool enabled = CheckBool(L, 3);
		lua_pushboolean(L, TrySetEmitterEnabled(GetParticleEmitter(sceneManager, name, emitterIndex), enabled) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterEmissionRate(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const float rate = static_cast<float>(luaL_checknumber(L, 3));
		if (!IsFiniteScalar(rate) || rate < 0.0f)
		{
			return luaL_argerror(L, 3, "SetParticleEmitterEmissionRate requires a finite non-negative rate");
		}

		lua_pushboolean(L, TrySetEmitterEmissionRate(GetParticleEmitter(sceneManager, name, emitterIndex), rate) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterDirection(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const BZR::VECTOR_3D direction = CheckVectorOrSingles(L, 3);
		if (!IsFiniteVector(direction))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterDirection requires a finite direction vector");
		}

		lua_pushboolean(L, TrySetEmitterDirection(GetParticleEmitter(sceneManager, name, emitterIndex), direction) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterPosition(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const BZR::VECTOR_3D position = CheckVectorOrSingles(L, 3);
		if (!IsFiniteVector(position))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterPosition requires a finite position vector");
		}

		lua_pushboolean(L, TrySetEmitterPosition(GetParticleEmitter(sceneManager, name, emitterIndex), position) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterVelocity(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const float minVelocity = static_cast<float>(luaL_checknumber(L, 3));
		const float maxVelocity = lua_isnoneornil(L, 4) ? minVelocity : static_cast<float>(luaL_checknumber(L, 4));
		if (!IsFiniteScalar(minVelocity))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterVelocity requires a finite minimum velocity");
		}
		if (!IsFiniteScalar(maxVelocity) || maxVelocity < minVelocity)
		{
			return luaL_argerror(L, 4, "SetParticleEmitterVelocity requires a finite maximum velocity that is not below the minimum");
		}

		lua_pushboolean(L, TrySetEmitterVelocityRange(GetParticleEmitter(sceneManager, name, emitterIndex), minVelocity, maxVelocity) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterAngle(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const float degrees = static_cast<float>(luaL_checknumber(L, 3));
		if (!IsFiniteScalar(degrees))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterAngle requires a finite angle in degrees");
		}

		// .particle scripts spell this in degrees, so the Lua surface does too.
		const float radians = degrees * 0.0174532925f;
		lua_pushboolean(L, TrySetEmitterAngle(GetParticleEmitter(sceneManager, name, emitterIndex), radians) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterTimeToLive(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const float minTimeToLive = static_cast<float>(luaL_checknumber(L, 3));
		const float maxTimeToLive = lua_isnoneornil(L, 4) ? minTimeToLive : static_cast<float>(luaL_checknumber(L, 4));
		if (!IsFiniteScalar(minTimeToLive) || minTimeToLive < 0.0f)
		{
			return luaL_argerror(L, 3, "SetParticleEmitterTimeToLive requires a finite non-negative minimum");
		}
		if (!IsFiniteScalar(maxTimeToLive) || maxTimeToLive < minTimeToLive)
		{
			return luaL_argerror(L, 4, "SetParticleEmitterTimeToLive requires a finite maximum that is not below the minimum");
		}

		lua_pushboolean(L, TrySetEmitterTimeToLiveRange(GetParticleEmitter(sceneManager, name, emitterIndex), minTimeToLive, maxTimeToLive) ? 1 : 0);
		return 1;
	}

	int SetParticleEmitterColor(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const int emitterIndex = static_cast<int>(luaL_checkinteger(L, 2));
		const Ogre::Color startColor = CheckColorOrSingles(L, 3);
		const Ogre::Color endColor = (lua_istable(L, 3) && !lua_isnoneornil(L, 4)) ? CheckColorOrSingles(L, 4) : startColor;
		if (!IsFiniteColor(startColor))
		{
			return luaL_argerror(L, 3, "SetParticleEmitterColor requires a finite start color");
		}
		if (!IsFiniteColor(endColor))
		{
			return luaL_argerror(L, 4, "SetParticleEmitterColor requires a finite end color");
		}

		lua_pushboolean(L, TrySetEmitterColourRange(GetParticleEmitter(sceneManager, name, emitterIndex), startColor, endColor) ? 1 : 0);
		return 1;
	}

	int SetParticleSystemNonVisibleUpdateTimeout(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		const std::string name = luaL_checkstring(L, 1);
		const float timeout = static_cast<float>(luaL_checknumber(L, 2));
		if (!IsFiniteScalar(timeout) || timeout < 0.0f)
		{
			return luaL_argerror(L, 2, "SetParticleSystemNonVisibleUpdateTimeout requires a finite non-negative timeout");
		}

		lua_pushboolean(L, TrySetParticleSystemNonVisibleUpdateTimeout(sceneManager, name, timeout) ? 1 : 0);
		return 1;
	}

	int GetShowBoundingBoxes(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool enabled = false;
		__try
		{
			enabled = Ogre::GetShowBoundingBoxes(sceneManager);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Scene] getShowBoundingBoxes crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetShowBoundingBoxes(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		bool enabled = CheckBool(L, 1);
		__try
		{
			Ogre::ShowBoundingBoxes(sceneManager, enabled);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Scene] showBoundingBoxes crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		return 0;
	}

	int GetShowDebugShadows(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool enabled = false;
		__try
		{
			enabled = Ogre::GetShowDebugShadows(sceneManager);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Scene] getShowDebugShadows crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetShowDebugShadows(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		bool enabled = CheckBool(L, 1);
		__try
		{
			Ogre::SetShowDebugShadows(sceneManager, enabled);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Scene] setShowDebugShadows crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		return 0;
	}

	int GetViewportShadowsEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* viewport = GetCurrentViewport();
		if (viewport == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool enabled = false;
		__try
		{
			enabled = Ogre::GetViewportShadowsEnabled(viewport);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Viewport] getShadowsEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
		}

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetViewportShadowsEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* viewport = GetCurrentViewport();
		if (viewport == nullptr)
		{
			return 0;
		}

		bool enabled = CheckBool(L, 1);
		__try
		{
			Ogre::SetViewportShadowsEnabled(viewport, enabled);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Viewport] setShadowsEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
		}

		return 0;
	}

	int GetViewportOverlaysEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* viewport = GetCurrentViewport();
		if (viewport == nullptr)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		bool enabled = false;
		TryGetViewportOverlaysEnabled(viewport, enabled);

		lua_pushboolean(L, enabled ? 1 : 0);
		return 1;
	}

	int SetViewportOverlaysEnabled(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* viewport = GetCurrentViewport();
		if (viewport == nullptr)
		{
			return 0;
		}

		bool enabled = CheckBool(L, 1);
		TrySetViewportOverlaysEnabled(viewport, enabled);

		return 0;
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

	bool CallSetCompositorEnabledGuarded(
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
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug(
				"[EXU::Viewport] setCompositorEnabled crashed viewport=%p enabled=%d code=0x%08X",
				viewport,
				enabled ? 1 : 0,
				GetExceptionCode());
			return false;
		}
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

	int GetSceneVisibilityMask(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		uint32_t mask = 0;
		__try
		{
			mask = Ogre::GetSceneVisibilityMask(sceneManager);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Scene] getVisibilityMask crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
			lua_pushnil(L);
			return 1;
		}

		lua_pushinteger(L, mask);
		return 1;
	}

	int SetSceneVisibilityMask(lua_State* L)
	{
		Patch::TryInitializeOgre();

		auto* sceneManager = GetSceneManager();
		if (sceneManager == nullptr)
		{
			return 0;
		}

		uint32_t mask = static_cast<uint32_t>(luaL_checkinteger(L, 1));
		__try
		{
			Ogre::SetSceneVisibilityMask(sceneManager, mask);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			LogEnvironmentDebug("[EXU::Scene] setVisibilityMask crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
		}

		return 0;
	}

	int HasSkyBoxNode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		bool hasNode = false;
		auto* sceneManager = GetSceneManager();
		if (sceneManager != nullptr)
		{
			TryHasSkyNode(sceneManager, Ogre::GetSkyBoxNode, hasNode, "skybox");
		}

		lua_pushboolean(L, hasNode);
		return 1;
	}

	int HasSkyDomeNode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		bool hasNode = false;
		auto* sceneManager = GetSceneManager();
		if (sceneManager != nullptr)
		{
			TryHasSkyNode(sceneManager, Ogre::GetSkyDomeNode, hasNode, "skydome");
		}

		lua_pushboolean(L, hasNode);
		return 1;
	}

	int HasSkyPlaneNode(lua_State* L)
	{
		Patch::TryInitializeOgre();

		bool hasNode = false;
		auto* sceneManager = GetSceneManager();
		if (sceneManager != nullptr)
		{
			TryHasSkyNode(sceneManager, Ogre::GetSkyPlaneNode, hasNode, "skyplane");
		}

		lua_pushboolean(L, hasNode);
		return 1;
	}
}

namespace ExtraUtilities::Patch
{
	// Prevents fog reset function from running.
	InlinePatch fogResetPatch(fogReset, BasicPatch::RET, BasicPatch::Status::INACTIVE);

	/*
	* This waits to initialize the ogre patch until you call an ogre function
	* in order to prevent the game crashing when alt tabbed in the loading screen.
	*
	* The old EXU sun reset hooks were unstable on BZR 2.2.301 and could cause
	* access violations during repeated lighting updates. Callers that need
	* dynamic sunlight can safely reapply SetSun* on their own update loop.
	*/
	namespace
	{
		// Which scene the current binding belongs to. A plain `done` latch was
		// process-lifetime, so after the first mission the fog reset patch was
		// never reloaded again and every later sun/ambient write in
		// Environment.lua's Update went at whatever the first mission had left
		// behind. Redux changes mission in-process and never calls
		// SceneManager::clearScene, so there is no teardown notification --
		// track the identity instead and re-bind whenever it moves.
		void* g_initializedSceneManager = nullptr;
		void* g_initializedTerrainMasterLight = nullptr;
		bool g_ogreInitialized = false;
	}

	void ResetOgreInitialization()
	{
		if (g_ogreInitialized)
		{
			Lua::Environment::LogEnvironmentDebug(
				"[EXU::ResetOgreInitialization] dropping binding sceneManager=%p terrainMasterLight=%p",
				g_initializedSceneManager,
				g_initializedTerrainMasterLight);
		}
		Lua::Environment::ForgetAllParticleCameraFollowers();
		g_initializedSceneManager = nullptr;
		g_initializedTerrainMasterLight = nullptr;
		g_ogreInitialized = false;
	}

	void TryInitializeOgre()
	{
		auto* sceneManager = Lua::Environment::GetSceneManager();
		auto* terrainMasterLight = Lua::Environment::GetTerrainMasterLight();

		if (g_ogreInitialized &&
			sceneManager == g_initializedSceneManager &&
			terrainMasterLight == g_initializedTerrainMasterLight)
		{
			return;
		}

		if (sceneManager == nullptr || terrainMasterLight == nullptr)
		{
			Lua::Environment::LogEnvironmentDebug(
				"[EXU::TryInitializeOgre] waiting sceneManager=%p terrainMasterLight=%p",
				sceneManager,
				terrainMasterLight);
			return;
		}

		fogResetPatch.Reload();
		Lua::Environment::LogEnvironmentDebug(
			"[EXU::TryInitializeOgre] initialized sceneManager=%p (was %p) terrainMasterLight=%p (was %p) rebind=%d",
			sceneManager,
			g_initializedSceneManager,
			terrainMasterLight,
			g_initializedTerrainMasterLight,
			g_ogreInitialized ? 1 : 0);
		g_initializedSceneManager = sceneManager;
		g_initializedTerrainMasterLight = terrainMasterLight;
		g_ogreInitialized = true;
	}
}
