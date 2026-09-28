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

#include "OgreSceneRuntime.h"
#include "Game/EnvironmentLog.h"

#include "Ogre/Ogre.h"

namespace ExtraUtilities::Lua::Environment
{
	namespace
	{
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

		using GetRootSingletonFn = void*(*)();
		using GetRootRenderSystemFn = void*(__thiscall*)(void*);
		using GetRenderSystemViewportFn = void*(__thiscall*)(void*);

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

		GetRenderSystemViewportFn ResolveGetRenderSystemViewport()
		{
			static constinit OgreProc<GetRenderSystemViewportFn> fn{ "?_getViewport@RenderSystem@Ogre@@UAEPAVViewport@2@XZ" };
			return fn.Get();
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
				LogEnvironmentFault("[EXU::Viewport] Root::getSingletonPtr crashed code=0x%08X", GetExceptionCode());
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
				LogEnvironmentFault("[EXU::Viewport] Root::getRenderSystem crashed root=%p code=0x%08X", root, GetExceptionCode());
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
				LogEnvironmentFault("[EXU::Viewport] RenderSystem::_getViewport crashed renderSystem=%p code=0x%08X", renderSystem, GetExceptionCode());
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
				LogEnvironmentFault("[EXU::Viewport] get current viewport crashed sceneManager=%p code=0x%08X", sceneManager, GetExceptionCode());
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
	}

	namespace Detail
	{
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
				LogEnvironmentFault(
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
				LogEnvironmentFault(
					"[EXU::Viewport] set material scheme crashed viewport=%p target=%s code=0x%08X",
					viewport,
					scheme.c_str(),
					GetExceptionCode());
				return false;
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


		using GetViewportOverlaysEnabledFn = bool(__thiscall*)(void*);
		using SetViewportOverlaysEnabledFn = void(__thiscall*)(void*, bool);

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
			LogEnvironmentFault("[EXU::Viewport] getOverlaysEnabled crashed viewport=%p code=0x%08X", viewport, GetExceptionCode());
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
			LogEnvironmentFault("[EXU::Viewport] setOverlaysEnabled crashed viewport=%p enabled=%d code=0x%08X", viewport, enabled ? 1 : 0, GetExceptionCode());
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
}
