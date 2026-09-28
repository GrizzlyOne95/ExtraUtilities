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

#include "OgreOverlayRuntime.h"

#include "Ogre/Ogre.h"
#include "Util/Logging.h"
#include "Util/SehGuard.h"

#include <cstdlib>

namespace ExtraUtilities::Lua::Overlay
{
	namespace
	{
		constexpr size_t kOverlaySystemAllocSize = 256;

		using AddRenderQueueListenerFn = void(__thiscall*)(void*, void*);
		using RemoveRenderQueueListenerFn = void(__thiscall*)(void*, void*);
		using OverlaySystemCtorFn = void(__thiscall*)(void*);
		using OverlaySystemDtorFn = void(__thiscall*)(void*);

		using GetViewportOverlaysEnabledFn = bool(__thiscall*)(void*);
		using SetViewportOverlaysEnabledFn = void(__thiscall*)(void*, bool);
		using GetRootSingletonFn = void*(*)();
		using GetRootRenderSystemFn = void*(__thiscall*)(void*);
		using GetRenderSystemSharedListenerFn = void*(*)();
		using GetRenderSystemViewportFn = void*(__thiscall*)(void*);
		GetViewportOverlaysEnabledFn ResolveGetViewportOverlaysEnabled()
		{
			static constinit OgreProc<GetViewportOverlaysEnabledFn> fn{ "?getOverlaysEnabled@Viewport@Ogre@@QBE_NXZ" };
			return fn.Get();
		}

		SetViewportOverlaysEnabledFn ResolveSetViewportOverlaysEnabled()
		{
			static constinit OgreProc<SetViewportOverlaysEnabledFn> fn{ "?setOverlaysEnabled@Viewport@Ogre@@QAEX_N@Z" };
			return fn.Get();
		}

		GetRootSingletonFn ResolveGetRootSingleton()
		{
			static constinit OgreProc<GetRootSingletonFn> fn{ "?getSingletonPtr@Root@Ogre@@SAPAV12@XZ" };
			return fn.Get();
		}

		GetRootRenderSystemFn ResolveGetRootRenderSystem()
		{
			static constinit OgreProc<GetRootRenderSystemFn> fn{ "?getRenderSystem@Root@Ogre@@QAEPAVRenderSystem@2@XZ" };
			return fn.Get();
		}

		GetRenderSystemSharedListenerFn ResolveGetRenderSystemSharedListener()
		{
			static constinit OgreProc<GetRenderSystemSharedListenerFn> fn{ "?getSharedListener@RenderSystem@Ogre@@SAPAVListener@12@XZ" };
			return fn.Get();
		}

		GetRenderSystemViewportFn ResolveGetRenderSystemViewport()
		{
			static constinit OgreProc<GetRenderSystemViewportFn> fn{ "?_getViewport@RenderSystem@Ogre@@UAEPAVViewport@2@XZ" };
			return fn.Get();
		}

		AddRenderQueueListenerFn ResolveAddRenderQueueListener()
		{
			static constinit OgreProc<AddRenderQueueListenerFn> fn{ "?addRenderQueueListener@SceneManager@Ogre@@UAEXPAVRenderQueueListener@2@@Z" };
			return fn.Get();
		}

		bool TryAddRenderQueueListenerWithSeh(void* sceneManager, void* overlaySystem, AddRenderQueueListenerFn addListener)
		{
			return Seh::Guard(
				"addRenderQueueListener",
				[&] { addListener(sceneManager, overlaySystem); },
				[&](unsigned long exceptionCode) {
					Logging::LogMessage("[EXU::Overlay] addRenderQueueListener crashed sceneManager=%p overlaySystem=%p code=0x%08X", sceneManager, overlaySystem, exceptionCode);
				});
		}

		RemoveRenderQueueListenerFn ResolveRemoveRenderQueueListener()
		{
			static constinit OgreProc<RemoveRenderQueueListenerFn> fn{ "?removeRenderQueueListener@SceneManager@Ogre@@UAEXPAVRenderQueueListener@2@@Z" };
			return fn.Get();
		}

		OverlaySystemCtorFn ResolveOverlaySystemCtor()
		{
			static constinit OgreProc<OverlaySystemCtorFn> fn{ OgreModule::Overlay, "??0OverlaySystem@Ogre@@QAE@XZ" };
			return fn.Get();
		}

		OverlaySystemDtorFn ResolveOverlaySystemDtor()
		{
			static constinit OgreProc<OverlaySystemDtorFn> fn{ OgreModule::Overlay, "??1OverlaySystem@Ogre@@UAE@XZ" };
			return fn.Get();
		}

		void* GetSceneManagerForOverlay()
		{
			void* sceneManager = nullptr;
			__try
			{
				sceneManager = ExtraUtilities::Ogre::sceneManager.Read();
			}
			__except (Seh::Filter(GetExceptionCode()))
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
			const bool constructed = Seh::Guard(
				"OverlaySystem ctor",
				[&] { ctor(storage); },
				[&](unsigned long exceptionCode) {
					Logging::LogMessage("[EXU::Overlay] OverlaySystem ctor crashed storage=%p code=0x%08X", storage, exceptionCode);
				});
			if (!constructed)
			{
				std::free(storage);
				return false;
			}

			outOverlaySystem = storage;
			Logging::LogMessage("[EXU::Overlay] OverlaySystem constructed instance=%p", outOverlaySystem);
			return true;
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

			if (!TryAddRenderQueueListenerWithSeh(sceneManager, overlaySystem, addListener))
			{
				return false;
			}

			attachedOverlaySceneManagers.insert(sceneManager);
			Logging::LogMessage("[EXU::Overlay] OverlaySystem attached sceneManager=%p overlaySystem=%p", sceneManager, overlaySystem);
			return true;
		}

		void DestroyOverlaySystemInstance();

		// Returns false if the OverlaySystem may still be registered as a
		// render-queue listener on some scene manager; it must not be freed then.
		bool DetachOverlaySystemFromTrackedSceneManagers(void* overlaySystem)
		{
			if (overlaySystem == nullptr || attachedOverlaySceneManagers.empty())
			{
				return true;
			}

			const auto removeListener = ResolveRemoveRenderQueueListener();
			if (removeListener == nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] removeRenderQueueListener unavailable overlaySystem=%p", overlaySystem);
				return false;
			}

			bool allDetached = true;

			for (void* sceneManager : attachedOverlaySceneManagers)
			{
				if (sceneManager == nullptr)
				{
					continue;
				}

				const bool detached = Seh::Guard(
					"removeRenderQueueListener",
					[&] { removeListener(sceneManager, overlaySystem); },
					[&](unsigned long exceptionCode) {
						Logging::LogMessage("[EXU::Overlay] removeRenderQueueListener crashed sceneManager=%p overlaySystem=%p code=0x%08X", sceneManager, overlaySystem, exceptionCode);
					});
				if (detached)
				{
					Logging::LogMessage("[EXU::Overlay] OverlaySystem detached sceneManager=%p overlaySystem=%p", sceneManager, overlaySystem);
				}
				else
				{
					allDetached = false;
				}
			}

			attachedOverlaySceneManagers.clear();
			return allDetached;
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

			if (Seh::Guard(
					"OverlaySystem dtor",
					[&] { dtor(instance); },
					[&](unsigned long exceptionCode) {
						Logging::LogMessage("[EXU::Overlay] OverlaySystem dtor crashed instance=%p code=0x%08X", instance, exceptionCode);
					}))
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem destroyed instance=%p", instance);
			}

			std::free(instance);
		}

		bool TrySetViewportOverlaysEnabledSeh(void* viewport, bool enabled)
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
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::Overlay] setOverlaysEnabled crashed viewport=%p enabled=%d code=0x%08X", viewport, enabled ? 1 : 0, GetExceptionCode());
				return false;
			}
		}

		bool TrySetViewportOverlaysEnabled(void* viewport, bool enabled)
		{
			return Seh::CatchCpp("TrySetViewportOverlaysEnabled", [&] { return TrySetViewportOverlaysEnabledSeh(viewport, enabled); }, false);
		}
	}

	namespace Detail
	{
		std::unordered_set<void*> attachedOverlaySceneManagers;
		void* overlaySystemInstance = nullptr;

		::Ogre::OverlayManager* GetOverlayManagerRawSeh()
		{
			__try
			{
				return ::Ogre::OverlayManager::getSingletonPtr();
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return nullptr;
			}
		}

		::Ogre::OverlayManager* GetOverlayManagerRaw()
		{
			return Seh::CatchCpp("GetOverlayManagerRaw", [&] { return GetOverlayManagerRawSeh(); }, nullptr);
		}

		// Lookup that never creates overlay support; safe from hook context.
		::Ogre::Overlay* FindExistingOverlaySeh(const std::string& name)
		{
			::Ogre::OverlayManager* manager = GetOverlayManagerRaw();
			if (manager == nullptr)
			{
				return nullptr;
			}

			__try
			{
				return manager->getByName(name);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return nullptr;
			}
		}

		::Ogre::Overlay* FindExistingOverlay(const std::string& name)
		{
			return Seh::CatchCpp("FindExistingOverlay", [&] { return FindExistingOverlaySeh(name); }, nullptr);
		}

		bool TryGetRootSingletonSeh(void*& outRoot)
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
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::Overlay] Root::getSingletonPtr crashed code=0x%08X", GetExceptionCode());
				return false;
			}
		}

		bool TryGetRootSingleton(void*& outRoot)
		{
			return Seh::CatchCpp("TryGetRootSingleton", [&] { return TryGetRootSingletonSeh(outRoot); }, false);
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

		// Frees EXU's OverlaySystem only when nothing can still call into it.
		// Otherwise it is deliberately leaked: a dangling render-queue listener
		// crashes the next frame, a leaked 256-byte object does not.
		void DetachAndDestroyOverlaySystem()
		{
			if (DetachOverlaySystemFromTrackedSceneManagers(overlaySystemInstance))
			{
				DestroyOverlaySystemInstance();
			}
			else if (overlaySystemInstance != nullptr)
			{
				Logging::LogMessage("[EXU::Overlay] OverlaySystem left allocated; detach did not complete instance=%p", overlaySystemInstance);
				overlaySystemInstance = nullptr;
			}
		}

		bool TryGetRootRenderSystemSeh(void* root, void*& outRenderSystem)
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
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::Overlay] Root::getRenderSystem crashed root=%p code=0x%08X", root, GetExceptionCode());
				return false;
			}
		}

		bool TryGetRootRenderSystem(void* root, void*& outRenderSystem)
		{
			return Seh::CatchCpp("TryGetRootRenderSystem", [&] { return TryGetRootRenderSystemSeh(root, outRenderSystem); }, false);
		}

		bool TryGetRenderSystemSharedListenerSeh(void*& outSharedListener)
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
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::Overlay] RenderSystem::getSharedListener crashed code=0x%08X", GetExceptionCode());
				return false;
			}
		}

		bool TryGetRenderSystemSharedListener(void*& outSharedListener)
		{
			return Seh::CatchCpp("TryGetRenderSystemSharedListener", [&] { return TryGetRenderSystemSharedListenerSeh(outSharedListener); }, false);
		}

		bool TryGetRenderSystemViewportSeh(void* renderSystem, void*& outViewport)
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
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::Overlay] RenderSystem::_getViewport crashed renderSystem=%p code=0x%08X", renderSystem, GetExceptionCode());
				return false;
			}
		}

		bool TryGetRenderSystemViewport(void* renderSystem, void*& outViewport)
		{
			return Seh::CatchCpp("TryGetRenderSystemViewport", [&] { return TryGetRenderSystemViewportSeh(renderSystem, outViewport); }, false);
		}

		void* GetCurrentViewportForOverlaySeh()
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
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::Overlay] current viewport lookup crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
				return nullptr;
			}
		}

		void* GetCurrentViewportForOverlay()
		{
			return Seh::CatchCpp("GetCurrentViewportForOverlay", [&] { return GetCurrentViewportForOverlaySeh(); }, nullptr);
		}

		bool TryGetViewportOverlaysEnabledSeh(void* viewport, bool& outEnabled)
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
			__except (Seh::Filter(GetExceptionCode()))
			{
				Logging::LogMessage("[EXU::Overlay] getOverlaysEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
				outEnabled = false;
				return false;
			}
		}

		bool TryGetViewportOverlaysEnabled(void* viewport, bool& outEnabled)
		{
			return Seh::CatchCpp("TryGetViewportOverlaysEnabled", [&] { return TryGetViewportOverlaysEnabledSeh(viewport, outEnabled); }, [&] { outEnabled = false; return false; });
		}
	}
}
