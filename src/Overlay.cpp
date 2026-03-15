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

#include "LuaHelpers.h"
#include "Logging.h"
#include "OgreNativeFontBridge.h"
#include "Ogre.h"
#include "OgreOverlayShim.h"

#include <Windows.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace ExtraUtilities::Lua::Overlay
{
	namespace
	{
		enum class ElementKind
		{
			Unknown,
			Panel,
			BorderPanel,
		};

		std::unordered_map<std::string, ElementKind> knownElements;
		std::unordered_set<void*> attachedOverlaySceneManagers;
		void* overlaySystemInstance = nullptr;
		constexpr size_t kOverlaySystemAllocSize = 256;
		constexpr const char* kOverlayRuntimeResourceGroup = "EXUOverlayRuntime";
		constexpr const char* kOverlayRuntimeFontName = "CRBZoneOverlayFont";
		constexpr const char* kOverlayRuntimeFontScript = "CRBZoneOverlay.fontdef";
		constexpr const char* kOverlayRuntimeTrueTypeSource = "BZONE.ttf";
		constexpr float kOverlayRuntimeTrueTypeSize = 32.0f;
		constexpr unsigned int kOverlayRuntimeTrueTypeResolution = 96u;
		constexpr unsigned int kOverlayRuntimeFirstCodePoint = 32u;
		constexpr unsigned int kOverlayRuntimeLastCodePoint = 126u;
		constexpr const char* kOverlayRuntimeFontSource = "bzfont.dds";
		constexpr const char* kOverlayRuntimeFontSpriteTable = "Edit\\stock\\bzfont.st";
		bool overlayRuntimeResourcesReady = false;
		bool overlayRuntimeResourcesAttempted = false;
		bool overlayRuntimeFontReady = false;
		bool overlayRuntimeFontAttempted = false;

		ElementKind GetElementKindByTypeName(const std::string& typeName)
		{
			if (typeName == "Panel")
			{
				return ElementKind::Panel;
			}
			if (typeName == "BorderPanel")
			{
				return ElementKind::BorderPanel;
			}
			return ElementKind::Unknown;
		}

		bool IsContainerKind(ElementKind kind)
		{
			return kind == ElementKind::Panel || kind == ElementKind::BorderPanel;
		}

		using AddRenderQueueListenerFn = void(__thiscall*)(void*, void*);
		using RemoveRenderQueueListenerFn = void(__thiscall*)(void*, void*);
		using OverlaySystemCtorFn = void(__thiscall*)(void*);
		using OverlaySystemDtorFn = void(__thiscall*)(void*);

		HMODULE GetOgreOverlayModule()
		{
			static HMODULE ogreOverlay = GetModuleHandleA("OgreOverlay.dll");
			return ogreOverlay;
		}

		::Ogre::OverlayManager* GetOverlayManagerRaw()
		{
			__try
			{
				return ::Ogre::OverlayManager::getSingletonPtr();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return nullptr;
			}
		}

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

		using GetViewportOverlaysEnabledFn = bool(__thiscall*)(void*);
		using SetViewportOverlaysEnabledFn = void(__thiscall*)(void*, bool);
		using GetRootSingletonFn = void*(*)();
		using GetRootRenderSystemFn = void*(__thiscall*)(void*);
		using GetRenderSystemSharedListenerFn = void*(*)();
		using GetRenderSystemViewportFn = void*(__thiscall*)(void*);
		HMODULE GetOgreMainModule()
		{
			static HMODULE ogreMain = GetModuleHandleA("OgreMain.dll");
			return ogreMain;
		}

		GetViewportOverlaysEnabledFn ResolveGetViewportOverlaysEnabled()
		{
			static GetViewportOverlaysEnabledFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
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
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<SetViewportOverlaysEnabledFn>(nullptr);
				}

				return reinterpret_cast<SetViewportOverlaysEnabledFn>(
					GetProcAddress(ogreMain, "?setOverlaysEnabled@Viewport@Ogre@@QAEX_N@Z"));
			}();

			return fn;
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

		GetRenderSystemSharedListenerFn ResolveGetRenderSystemSharedListener()
		{
			static GetRenderSystemSharedListenerFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<GetRenderSystemSharedListenerFn>(nullptr);
				}

				return reinterpret_cast<GetRenderSystemSharedListenerFn>(
					GetProcAddress(ogreMain, "?getSharedListener@RenderSystem@Ogre@@SAPAVListener@12@XZ"));
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
				Logging::LogMessage("[EXU::Overlay] Root::getSingletonPtr crashed code=0x%08X", GetExceptionCode());
				return false;
			}
		}

		std::string GetCurrentModuleDirectory()
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&GetCurrentModuleDirectory),
				&module) || module == nullptr)
			{
				return {};
			}

			std::array<char, MAX_PATH> path{};
			const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
			if (length == 0 || length >= path.size())
			{
				return {};
			}

			std::string result(path.data(), length);
			const auto slash = result.find_last_of("\\/");
			if (slash == std::string::npos)
			{
				return {};
			}

			result.resize(slash);
			return result;
		}

		std::string GetCurrentGameRootDirectory()
		{
			const std::string moduleDirectory = GetCurrentModuleDirectory();
			if (moduleDirectory.empty())
			{
				return {};
			}

			const auto addonSlash = moduleDirectory.find_last_of("\\/");
			if (addonSlash == std::string::npos)
			{
				return {};
			}

			const std::string addonDirectory = moduleDirectory.substr(0, addonSlash);
			const auto gameSlash = addonDirectory.find_last_of("\\/");
			if (gameSlash == std::string::npos)
			{
				return {};
			}

			return addonDirectory.substr(0, gameSlash);
		}

		AddRenderQueueListenerFn ResolveAddRenderQueueListener()
		{
			static AddRenderQueueListenerFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<AddRenderQueueListenerFn>(nullptr);
				}

				return reinterpret_cast<AddRenderQueueListenerFn>(
					GetProcAddress(ogreMain, "?addRenderQueueListener@SceneManager@Ogre@@UAEXPAVRenderQueueListener@2@@Z"));
			}();

			return fn;
		}

		RemoveRenderQueueListenerFn ResolveRemoveRenderQueueListener()
		{
			static RemoveRenderQueueListenerFn fn = []()
			{
				HMODULE ogreMain = GetOgreMainModule();
				if (ogreMain == nullptr)
				{
					return static_cast<RemoveRenderQueueListenerFn>(nullptr);
				}

				return reinterpret_cast<RemoveRenderQueueListenerFn>(
					GetProcAddress(ogreMain, "?removeRenderQueueListener@SceneManager@Ogre@@UAEXPAVRenderQueueListener@2@@Z"));
			}();

			return fn;
		}

		OverlaySystemCtorFn ResolveOverlaySystemCtor()
		{
			static OverlaySystemCtorFn fn = []()
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return static_cast<OverlaySystemCtorFn>(nullptr);
				}

				return reinterpret_cast<OverlaySystemCtorFn>(
					GetProcAddress(ogreOverlay, "??0OverlaySystem@Ogre@@QAE@XZ"));
			}();

			return fn;
		}

		OverlaySystemDtorFn ResolveOverlaySystemDtor()
		{
			static OverlaySystemDtorFn fn = []()
			{
				HMODULE ogreOverlay = GetOgreOverlayModule();
				if (ogreOverlay == nullptr)
				{
					return static_cast<OverlaySystemDtorFn>(nullptr);
				}

				return reinterpret_cast<OverlaySystemDtorFn>(
					GetProcAddress(ogreOverlay, "??1OverlaySystem@Ogre@@UAE@XZ"));
			}();

			return fn;
		}

		void* GetSceneManagerForOverlay()
		{
			void* sceneManager = nullptr;
			__try
			{
				sceneManager = ExtraUtilities::Ogre::sceneManager.Read();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] scene manager lookup crashed code=0x%08X", GetExceptionCode());
				return nullptr;
			}

			return sceneManager;
		}

		bool TryConstructOverlaySystem(void*& outOverlaySystem)
		{
			outOverlaySystem = nullptr;
			const auto ctor = ResolveOverlaySystemCtor();
			if (ctor == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem ctor unavailable");
				return false;
			}

			void* storage = std::malloc(kOverlaySystemAllocSize);
			if (storage == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem allocation failed size=%u", static_cast<unsigned>(kOverlaySystemAllocSize));
				return false;
			}

			std::memset(storage, 0, kOverlaySystemAllocSize);
			__try
			{
				ctor(storage);
				outOverlaySystem = storage;
				Logging::LogMessage("[EXU::Overlay] OverlaySystem constructed instance=%p", outOverlaySystem);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem ctor crashed storage=%p code=0x%08X", storage, GetExceptionCode());
				std::free(storage);
				return false;
			}
		}

		bool TryAttachOverlaySystemToSceneManager(void* sceneManager, void* overlaySystem)
		{
			if (sceneManager == nullptr || overlaySystem == nullptr)
			{
				return false;
			}

			if (attachedOverlaySceneManagers.find(sceneManager) != attachedOverlaySceneManagers.end())
			{
				return true;
			}

			const auto addListener = ResolveAddRenderQueueListener();
			if (addListener == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] addRenderQueueListener unavailable sceneManager=%p overlaySystem=%p", sceneManager, overlaySystem);
				return false;
			}

			__try
			{
				addListener(sceneManager, overlaySystem);
				attachedOverlaySceneManagers.insert(sceneManager);
				Logging::LogMessage("[EXU::Overlay] OverlaySystem attached sceneManager=%p overlaySystem=%p", sceneManager, overlaySystem);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] addRenderQueueListener crashed sceneManager=%p overlaySystem=%p code=0x%08X", sceneManager, overlaySystem, GetExceptionCode());
				return false;
			}
		}

		void EnsureOverlaySupport()
		{
			if (GetOverlayManagerRaw() == nullptr && overlaySystemInstance == nullptr)
			{
				TryConstructOverlaySystem(overlaySystemInstance);
			}

			void* sceneManager = GetSceneManagerForOverlay();
			if (sceneManager != nullptr && overlaySystemInstance != nullptr)
			{
				TryAttachOverlaySystemToSceneManager(sceneManager, overlaySystemInstance);
			}
		}

		void EnsureOverlayRuntimeResources()
		{
			if (overlayRuntimeResourcesReady || overlayRuntimeResourcesAttempted)
			{
				return;
			}

			overlayRuntimeResourcesAttempted = true;
			EnsureOverlaySupport();

			const std::string moduleDirectory = GetCurrentModuleDirectory();
			if (moduleDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime resources failed to resolve EXU module directory");
				return;
			}

			if (!Native::TryAddResourceLocation(moduleDirectory.c_str(), kOverlayRuntimeResourceGroup))
			{
				return;
			}

			const std::string gameRootDirectory = GetCurrentGameRootDirectory();
			if (gameRootDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime resources failed to resolve game root directory");
				return;
			}

			const std::string stockTextureDirectory = gameRootDirectory + "\\BZ_ASSETS\\pc\\textures\\MISC_DDS";
			if (!Native::TryAddResourceLocation(stockTextureDirectory.c_str(), kOverlayRuntimeResourceGroup))
			{
				return;
			}

			overlayRuntimeResourcesReady = true;
			Logging::LogMessage(
				"[EXU::Overlay] overlay runtime resources ready group=%s location=%s stockTextures=%s",
				kOverlayRuntimeResourceGroup,
				moduleDirectory.c_str(),
				stockTextureDirectory.c_str());
		}

		void EnsureOverlayRuntimeFont()
		{
			if (overlayRuntimeFontReady || overlayRuntimeFontAttempted)
			{
				return;
			}

			overlayRuntimeFontAttempted = true;
			EnsureOverlayRuntimeResources();
			if (!overlayRuntimeResourcesReady)
			{
				return;
			}

			const std::string gameRootDirectory = GetCurrentGameRootDirectory();
			if (gameRootDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime font failed to resolve game root directory");
				return;
			}

			if (Native::TryHasFontResource(kOverlayRuntimeFontName, kOverlayRuntimeResourceGroup))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s mode=existing",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup);
				return;
			}

			if (Native::TryParseFontScript(kOverlayRuntimeFontScript, kOverlayRuntimeResourceGroup)
				&& Native::TryHasFontResource(kOverlayRuntimeFontName, kOverlayRuntimeResourceGroup))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s script=%s mode=script",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup,
					kOverlayRuntimeFontScript);
				return;
			}

			if (Native::TryEnsureTrueTypeFont(
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeTrueTypeSource,
				kOverlayRuntimeTrueTypeSize,
				kOverlayRuntimeTrueTypeResolution,
				kOverlayRuntimeFirstCodePoint,
				kOverlayRuntimeLastCodePoint))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s source=%s mode=truetype size=%.1f resolution=%u range=%u-%u",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup,
					kOverlayRuntimeTrueTypeSource,
					kOverlayRuntimeTrueTypeSize,
					kOverlayRuntimeTrueTypeResolution,
					kOverlayRuntimeFirstCodePoint,
					kOverlayRuntimeLastCodePoint);
				return;
			}

			const std::string spriteTablePath = gameRootDirectory + "\\" + kOverlayRuntimeFontSpriteTable;
			if (Native::TryEnsureImageFontFromSpriteTable(
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeFontSource,
				spriteTablePath.c_str()))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s source=%s spriteTable=%s mode=image-fallback",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup,
					kOverlayRuntimeFontSource,
					spriteTablePath.c_str());
				return;
			}

			Logging::LogMessage(
				"[EXU::Overlay] overlay runtime font unavailable name=%s group=%s truetype=%s image=%s spriteTable=%s",
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeTrueTypeSource,
				kOverlayRuntimeFontSource,
				spriteTablePath.c_str());
		}

		void DetachOverlaySystemFromTrackedSceneManagers(void* overlaySystem)
		{
			if (overlaySystem == nullptr || attachedOverlaySceneManagers.empty())
			{
				return;
			}

			const auto removeListener = ResolveRemoveRenderQueueListener();
			if (removeListener == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] removeRenderQueueListener unavailable overlaySystem=%p", overlaySystem);
				return;
			}

			for (void* sceneManager : attachedOverlaySceneManagers)
			{
				if (sceneManager == nullptr)
				{
					continue;
				}

				__try
				{
					removeListener(sceneManager, overlaySystem);
					Logging::LogMessage("[EXU::Overlay] OverlaySystem detached sceneManager=%p overlaySystem=%p", sceneManager, overlaySystem);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					Logging::LogMessage("[EXU::Overlay] removeRenderQueueListener crashed sceneManager=%p overlaySystem=%p code=0x%08X", sceneManager, overlaySystem, GetExceptionCode());
				}
			}

			attachedOverlaySceneManagers.clear();
		}

		void DestroyOverlaySystemInstance()
		{
			if (overlaySystemInstance == nullptr)
			{
				return;
			}

			const auto dtor = ResolveOverlaySystemDtor();
			if (dtor == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem dtor unavailable instance=%p", overlaySystemInstance);
				return;
			}

			void* instance = overlaySystemInstance;
			overlaySystemInstance = nullptr;

			__try
			{
				dtor(instance);
				Logging::LogMessage("[EXU::Overlay] OverlaySystem destroyed instance=%p", instance);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem dtor crashed instance=%p code=0x%08X", instance, GetExceptionCode());
			}

			std::free(instance);
		}

		::Ogre::OverlayManager* GetOverlayManager()
		{
			::Ogre::OverlayManager* manager = GetOverlayManagerRaw();
			if (manager != nullptr)
			{
				return manager;
			}

			EnsureOverlaySupport();
			return GetOverlayManagerRaw();
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
				Logging::LogMessage("[EXU::Overlay] Root::getRenderSystem crashed root=%p code=0x%08X", root, GetExceptionCode());
				return false;
			}
		}

		bool TryGetRenderSystemSharedListener(void*& outSharedListener)
		{
			outSharedListener = nullptr;
			const auto fn = ResolveGetRenderSystemSharedListener();
			if (fn == nullptr)
			{
				return false;
			}

			__try
			{
				outSharedListener = fn();
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] RenderSystem::getSharedListener crashed code=0x%08X", GetExceptionCode());
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
				Logging::LogMessage("[EXU::Overlay] RenderSystem::_getViewport crashed renderSystem=%p code=0x%08X", renderSystem, GetExceptionCode());
				return false;
			}
		}

		void* GetCurrentViewportForOverlay()
		{
			void* sceneManager = GetSceneManagerForOverlay();
			if (sceneManager == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return ExtraUtilities::Ogre::GetCurrentViewport(sceneManager);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] current viewport lookup crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
				return nullptr;
			}
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
				Logging::LogMessage("[EXU::Overlay] getOverlaysEnabled unavailable viewport=%p", viewport);
				return false;
			}

			__try
			{
				outEnabled = fn(viewport);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] getOverlaysEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
				outEnabled = false;
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
				Logging::LogMessage("[EXU::Overlay] setOverlaysEnabled unavailable viewport=%p enabled=%d", viewport, enabled ? 1 : 0);
				return false;
			}

			__try
			{
				fn(viewport, enabled);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				Logging::LogMessage("[EXU::Overlay] setOverlaysEnabled crashed viewport=%p enabled=%d code=0x%08X", viewport, enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TryCallSetOverlayParameter(
			bool(__thiscall* setParameter)(void*, const std::string&, const std::string&),
			::Ogre::OverlayElement* element,
			const std::string& name,
			const std::string& value,
			bool& outSuccess,
			unsigned int& outExceptionCode)
		{
			outSuccess = false;
			outExceptionCode = 0;

			__try
			{
				outSuccess = setParameter(element, name, value);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool TryShowOverlay(::Ogre::Overlay* overlay, unsigned int& outExceptionCode)
		{
			outExceptionCode = 0;

			__try
			{
				overlay->show();
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				outExceptionCode = GetExceptionCode();
				return false;
			}
		}

		bool SetOverlayParameter(::Ogre::OverlayElement* element, const std::string& name, const std::string& value)
		{
			using SetParameterFn = bool(__thiscall*)(void*, const std::string&, const std::string&);
			static SetParameterFn setParameter = []()
			{
				HMODULE ogreMain = GetModuleHandleA("OgreMain.dll");
				if (ogreMain == nullptr)
				{
					return static_cast<SetParameterFn>(nullptr);
				}

				return reinterpret_cast<SetParameterFn>(
					GetProcAddress(ogreMain, "?setParameter@StringInterface@Ogre@@UAE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z"));
			}();

			if (setParameter == nullptr || element == nullptr)
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

			Logging::LogMessage(
				"[EXU::Overlay] setParameter element=%p name=%s value=%s success=%d",
				element,
				name.c_str(),
				value.c_str(),
				success ? 1 : 0);
			return success;
		}
	}

	void ShutdownOverlaySupport() noexcept
	{
		DetachOverlaySystemFromTrackedSceneManagers(overlaySystemInstance);
		DestroyOverlaySystemInstance();
		overlayRuntimeResourcesReady = false;
		overlayRuntimeResourcesAttempted = false;
		overlayRuntimeFontReady = false;
		overlayRuntimeFontAttempted = false;
		knownElements.clear();
	}

	int CreateOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
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
		Logging::LogMessage("[EXU::Overlay] CreateOverlay name=%s manager=%p overlay=%p", name.c_str(), manager, overlay);
		lua_pushboolean(L, overlay != nullptr);
		return 1;
	}

	int DestroyOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::OverlayManager* manager = GetOverlayManager();
		if (manager == nullptr)
		{
			return 0;
		}

		if (manager->getByName(name) != nullptr)
		{
			manager->destroy(name);
		}

		return 0;
	}

	int ShowOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			Logging::LogMessage("[EXU::Overlay] ShowOverlay missing name=%s", name.c_str());
			return 0;
		}

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
		const bool forceEnabledAttempted = viewport != nullptr;
		const bool forceEnabledSucceeded = forceEnabledAttempted ? TrySetViewportOverlaysEnabled(viewport, true) : false;
		bool overlaysEnabledAfter = false;
		const bool overlaysEnabledAfterAvailable = TryGetViewportOverlaysEnabled(viewport, overlaysEnabledAfter);

		Logging::LogMessage(
			"[EXU::Overlay] ShowOverlay pre name=%s overlay=%p viewport=%p root=%p renderSystem=%p sharedListener=%p sharedListenerAvailable=%d renderSystemViewport=%p renderSystemViewportAvailable=%d overlaysBefore=%s forceEnableAttempted=%d forceEnableSucceeded=%d overlaysAfter=%s",
			name.c_str(),
			overlay,
			viewport,
			root,
			renderSystem,
			sharedListener,
			sharedListenerAvailable ? 1 : 0,
			renderSystemViewport,
			renderSystemViewportAvailable ? 1 : 0,
			DescribeOptionalBool(overlaysEnabledBeforeAvailable, overlaysEnabledBefore),
			forceEnabledAttempted ? 1 : 0,
			forceEnabledSucceeded ? 1 : 0,
			DescribeOptionalBool(overlaysEnabledAfterAvailable, overlaysEnabledAfter));

		unsigned int exceptionCode = 0;
		if (!TryShowOverlay(overlay, exceptionCode))
		{
			Logging::LogMessage("[EXU::Overlay] ShowOverlay crashed name=%s overlay=%p code=0x%08X", name.c_str(), overlay, exceptionCode);
			return 0;
		}

		Logging::LogMessage("[EXU::Overlay] ShowOverlay done name=%s overlay=%p viewport=%p", name.c_str(), overlay, viewport);
		return 0;
	}

	int HideOverlay(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		::Ogre::Overlay* overlay = FindOverlay(name);
		if (overlay == nullptr)
		{
			return 0;
		}

		overlay->hide();
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

		::Ogre::OverlayElement* element = manager->createOverlayElement(typeName, instanceName, false);
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

		if (manager->hasOverlayElement(name, false))
		{
			manager->destroyOverlayElement(name, false);
		}
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

		parent->::Ogre::OverlayContainer::addChild(child);
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

		parent->::Ogre::OverlayContainer::removeChild(childName);
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

		element->::Ogre::OverlayElement::setMetricsMode(static_cast<::Ogre::GuiMetricsMode>(mode));
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

		element->::Ogre::OverlayElement::setMaterialName(materialName);
		Logging::LogMessage("[EXU::Overlay] SetOverlayMaterial name=%s element=%p material=%s", name.c_str(), element, materialName.c_str());
		return 0;
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
		element->::Ogre::OverlayElement::setColour(ogreColor);
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

		SetOverlayParameter(element, "caption", text);
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

		SetOverlayParameter(element, "char_height", std::to_string(charHeight));
		return 0;
	}
}
