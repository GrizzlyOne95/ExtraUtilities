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

#pragma once

#include "bzr.h"
#include "OgreSceneManagerShim.h"
#include "Scanner.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>

#include <Windows.h>

namespace ExtraUtilities::Ogre
{
	struct Fog
	{
		float r;
		float g;
		float b;
		uint8_t padding[4] = { 0 };
		float start;
		float ending;

		Fog() = default;
		Fog(float r, float g, float b, float start, float ending)
			: r(r), g(g), b(b), start(start), ending(ending) {}
	};

	struct Color
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 1.0f;
	};

	struct SkyBoxGenParameters
	{
		float distance;
	};

	struct SkyDomeGenParameters
	{
		float curvature;
		float tiling;
		float distance;
		int32_t xsegments;
		int32_t ysegments;
		int32_t ysegments_keep;
	};

	struct SkyPlaneGenParameters
	{
		float scale;
		float tiling;
		float bow;
		int32_t xsegments;
		int32_t ysegments;
	};

	inline Scanner terrain_masterlight(BZR::Ogre::terrain_masterlight, BasicScanner::Restore::DISABLED);
	inline Scanner sceneManager(BZR::Ogre::sceneManagerStructure, { BZR::Ogre::sceneManagerOffset }, BasicScanner::Restore::DISABLED);

	// Gets the fog structure from the scene manager
	inline Fog* GetFog()
	{
		void* sm = sceneManager.Read();
		Fog* fog;
		__asm
		{
			mov eax, [sm]
			add eax, 0x128
			mov [fog], eax
		}
		return fog;
	}

	// OgreMain entry points, resolved by mangled export name on first use.
	// They used to be fixed offsets from OgreMain's base with no check that the
	// DLL was the build they were taken from; a missing export now makes the
	// call a no-op returning a default value instead of a jump into arbitrary
	// code. The names were mapped from the offsets against the shipped
	// OgreMain.dll (identical on GOG and Steam, 2026-09-27).
	template <typename Fn>
	class OgreExport;

	template <typename R, typename... Args>
	class OgreExport<R(__thiscall*)(Args...)>
	{
	public:
		using Fn = R(__thiscall*)(Args...);

		constexpr explicit OgreExport(const char* mangledName) noexcept
			: m_name(mangledName)
		{
		}

		Fn Get() const noexcept
		{
			if (!m_resolved)
			{
				if (HMODULE ogreMain = GetModuleHandleA("OgreMain.dll"))
				{
					m_fn = reinterpret_cast<Fn>(GetProcAddress(ogreMain, m_name));
					m_resolved = true;
				}
			}
			return m_fn;
		}

		explicit operator bool() const noexcept
		{
			return Get() != nullptr;
		}

		R operator()(Args... args) const
		{
			if (const Fn fn = Get())
			{
				return fn(args...);
			}

			if constexpr (std::is_void_v<R>)
			{
				return;
			}
			else if constexpr (std::is_reference_v<R>)
			{
				static std::remove_cvref_t<R> fallback{};
				return fallback;
			}
			else
			{
				return R{};
			}
		}

	private:
		const char* m_name;
		mutable Fn m_fn = nullptr;
		mutable bool m_resolved = false;
	};

	using _GetAmbientLight = Color*(__thiscall*)(void*);
	inline constinit OgreExport<_GetAmbientLight> GetAmbientLight{ "?getAmbientLight@SceneManager@Ogre@@QBEABVColourValue@2@XZ" };

	using _SetAmbientLight = void(__thiscall*)(void*, Color*);
	inline constinit OgreExport<_SetAmbientLight> SetAmbientLight{ "?setAmbientLight@SceneManager@Ogre@@QAEXABVColourValue@2@@Z" };

	using _GetDiffuseColor = Color*(__thiscall*)(void*);
	inline constinit OgreExport<_GetDiffuseColor> GetDiffuseColor{ "?getDiffuseColour@Light@Ogre@@QBEABVColourValue@2@XZ" };

	using _SetDiffuseColor = void(__thiscall*)(void*, float, float, float);
	inline constinit OgreExport<_SetDiffuseColor> SetDiffuseColor{ "?setDiffuseColour@Light@Ogre@@QAEXMMM@Z" };

	using _GetSpecularColor = Color*(__thiscall*)(void*);
	inline constinit OgreExport<_GetSpecularColor> GetSpecularColor{ "?getSpecularColour@Light@Ogre@@QBEABVColourValue@2@XZ" };

	using _SetSpecularColor = void(__thiscall*)(void*, float, float, float);
	inline constinit OgreExport<_SetSpecularColor> SetSpecularColor{ "?setSpecularColour@Light@Ogre@@QAEXMMM@Z" };

	using _GetDirection = BZR::VECTOR_3D*(__thiscall*)(void*);
	inline constinit OgreExport<_GetDirection> GetDirection{ "?getDirection@Light@Ogre@@QBEABVVector3@2@XZ" };

	using _SetDirection = void(__thiscall*)(void*, float, float, float);
	inline constinit OgreExport<_SetDirection> SetDirection{ "?setDirection@Light@Ogre@@QAEXMMM@Z" };

	using _GetPowerScale = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetPowerScale> GetPowerScale{ "?getPowerScale@Light@Ogre@@QBEMXZ" };

	using _SetPowerScale = void(__thiscall*)(void*, float);
	inline constinit OgreExport<_SetPowerScale> SetPowerScale{ "?setPowerScale@Light@Ogre@@QAEXM@Z" };

	using _GetShadowFarDistance = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetShadowFarDistance> GetShadowFarDistance{ "?getShadowFarDistance@Light@Ogre@@QBEMXZ" };

	using _SetShadowFarDistance = void(__thiscall*)(void*, float);
	inline constinit OgreExport<_SetShadowFarDistance> SetShadowFarDistance{ "?setShadowFarDistance@Light@Ogre@@QAEXM@Z" };

	using _SetSpotlightRange = void(__thiscall*)(void*, float*, float*, float);
	inline constinit OgreExport<_SetSpotlightRange> SetSpotlightRange{ "?setSpotlightRange@Light@Ogre@@QAEXABVRadian@2@0M@Z" };

	using _SetVisible = void(__thiscall*)(void*, bool);
	inline constinit OgreExport<_SetVisible> SetVisible{ "?setVisible@MovableObject@Ogre@@UAEX_N@Z" };

	using _GetVisible = bool(__thiscall*)(void*);
	inline constinit OgreExport<_GetVisible> GetVisible{ "?getVisible@MovableObject@Ogre@@UBE_NXZ" };

	using _GetCastShadows = bool(__thiscall*)(void*);
	inline constinit OgreExport<_GetCastShadows> GetCastShadows{ "?getCastShadows@MovableObject@Ogre@@UBE_NXZ" };

	using _SetCastShadows = void(__thiscall*)(void*, bool);
	inline constinit OgreExport<_SetCastShadows> SetCastShadows{ "?setCastShadows@MovableObject@Ogre@@QAEX_N@Z" };

	using _GetRenderingDistance = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetRenderingDistance> GetRenderingDistance{ "?getRenderingDistance@MovableObject@Ogre@@UBEMXZ" };

	using _SetRenderingDistance = void(__thiscall*)(void*, float);
	inline constinit OgreExport<_SetRenderingDistance> SetRenderingDistance{ "?setRenderingDistance@MovableObject@Ogre@@UAEXM@Z" };

	using _GetVisibilityFlags = uint32_t(__thiscall*)(void*);
	inline constinit OgreExport<_GetVisibilityFlags> GetVisibilityFlags{ "?getVisibilityFlags@MovableObject@Ogre@@UBEIXZ" };

	using _SetVisibilityFlags = void(__thiscall*)(void*, uint32_t);
	inline constinit OgreExport<_SetVisibilityFlags> SetVisibilityFlags{ "?setVisibilityFlags@MovableObject@Ogre@@UAEXI@Z" };

	using _GetQueryFlags = uint32_t(__thiscall*)(void*);
	inline constinit OgreExport<_GetQueryFlags> GetQueryFlags{ "?getQueryFlags@MovableObject@Ogre@@UBEIXZ" };

	using _SetQueryFlags = void(__thiscall*)(void*, uint32_t);
	inline constinit OgreExport<_SetQueryFlags> SetQueryFlags{ "?setQueryFlags@MovableObject@Ogre@@UAEXI@Z" };

	using _GetRenderQueueGroup = uint8_t(__thiscall*)(void*);
	inline constinit OgreExport<_GetRenderQueueGroup> GetRenderQueueGroup{ "?getRenderQueueGroup@MovableObject@Ogre@@UBEEXZ" };

	using _SetRenderQueueGroupMovable = void(__thiscall*)(void*, uint8_t);
	inline constinit OgreExport<_SetRenderQueueGroupMovable> SetRenderQueueGroupMovable{ "?setRenderQueueGroup@MovableObject@Ogre@@UAEXE@Z" };

	using _SetRenderQueueGroupSubEntity = void(__thiscall*)(void*, uint8_t);
	inline constinit OgreExport<_SetRenderQueueGroupSubEntity> SetRenderQueueGroupSubEntity{ "?setRenderQueueGroup@SubEntity@Ogre@@UAEXE@Z" };

	using _GetNumSubEntities = uint32_t(__thiscall*)(void*);
	inline constinit OgreExport<_GetNumSubEntities> GetNumSubEntities{ "?getNumSubEntities@Entity@Ogre@@QBEIXZ" };

	using _GetSubEntityByIndex = void*(__thiscall*)(void*, uint32_t);
	inline constinit OgreExport<_GetSubEntityByIndex> GetSubEntityByIndex{ "?getSubEntity@Entity@Ogre@@QBEPAVSubEntity@2@I@Z" };

	using _GetMaterialNameSubEntity = const std::string&(__thiscall*)(void*);
	inline constinit OgreExport<_GetMaterialNameSubEntity> GetMaterialNameSubEntity{ "?getMaterialName@SubEntity@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ" };

	using _SetMaterialNameEntity = void(__thiscall*)(void*, const std::string&, const std::string&);
	inline constinit OgreExport<_SetMaterialNameEntity> SetMaterialNameEntity{ "?setMaterialName@Entity@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z" };

	using _SetMaterialNameSubEntity = void(__thiscall*)(void*, const std::string&, const std::string&);
	inline constinit OgreExport<_SetMaterialNameSubEntity> SetMaterialNameSubEntity{ "?setMaterialName@SubEntity@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z" };

	using _HasSkeleton = bool(__thiscall*)(void*);
	inline constinit OgreExport<_HasSkeleton> HasSkeleton{ "?hasSkeleton@Entity@Ogre@@QBE_NXZ" };

	using _GetAllAnimationStates = void*(__thiscall*)(void*);
	inline constinit OgreExport<_GetAllAnimationStates> GetAllAnimationStates{ "?getAllAnimationStates@Entity@Ogre@@QBEPAVAnimationStateSet@2@XZ" };

	using _HasAnimationState = bool(__thiscall*)(void*, const std::string&);
	inline constinit OgreExport<_HasAnimationState> HasAnimationState{ "?hasAnimationState@AnimationStateSet@Ogre@@QBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z" };

	using _GetAnimationState = void*(__thiscall*)(void*, const std::string&);
	inline constinit OgreExport<_GetAnimationState> GetAnimationState{ "?getAnimationState@AnimationStateSet@Ogre@@QBEPAVAnimationState@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z" };

	using _GetAnimationLength = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetAnimationLength> GetAnimationLength{ "?getLength@AnimationState@Ogre@@QBEMXZ" };

	using _GetAnimationTimePosition = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetAnimationTimePosition> GetAnimationTimePosition{ "?getTimePosition@AnimationState@Ogre@@QBEMXZ" };

	using _GetAnimationWeight = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetAnimationWeight> GetAnimationWeight{ "?getWeight@AnimationState@Ogre@@QBEMXZ" };

	using _GetAnimationLoop = bool(__thiscall*)(void*);
	inline constinit OgreExport<_GetAnimationLoop> GetAnimationLoop{ "?getLoop@AnimationState@Ogre@@QBE_NXZ" };

	using _GetAnimationEnabled = bool(__thiscall*)(void*);
	inline constinit OgreExport<_GetAnimationEnabled> GetAnimationEnabled{ "?getEnabled@AnimationState@Ogre@@QBE_NXZ" };

	using _SetAnimationTimePosition = void(__thiscall*)(void*, float);
	inline constinit OgreExport<_SetAnimationTimePosition> SetAnimationTimePosition{ "?setTimePosition@AnimationState@Ogre@@QAEXM@Z" };

	using _SetAnimationWeight = void(__thiscall*)(void*, float);
	inline constinit OgreExport<_SetAnimationWeight> SetAnimationWeight{ "?setWeight@AnimationState@Ogre@@QAEXM@Z" };

	using _SetAnimationEnabled = void(__thiscall*)(void*, bool);
	inline constinit OgreExport<_SetAnimationEnabled> SetAnimationEnabled{ "?setEnabled@AnimationState@Ogre@@QAEX_N@Z" };

	using _SetAnimationLoop = void(__thiscall*)(void*, bool);
	inline constinit OgreExport<_SetAnimationLoop> SetAnimationLoop{ "?setLoop@AnimationState@Ogre@@QAEX_N@Z" };

	using _GetLightPosition = BZR::VECTOR_3D * (__thiscall*)(void*);
	inline constinit OgreExport<_GetLightPosition> GetLightPosition{ "?getPosition@Light@Ogre@@QBEABVVector3@2@XZ" };

	using _SetLightPosition = void(__thiscall*)(void*, float, float, float);
	inline constinit OgreExport<_SetLightPosition> SetLightPosition{ "?setPosition@Light@Ogre@@QAEXMMM@Z" };

	using _SetAttenuation = void(__thiscall*)(void*, float, float, float, float);
	inline constinit OgreExport<_SetAttenuation> SetAttenuation{ "?setAttenuation@Light@Ogre@@QAEXMMMM@Z" };

	using _GetCameraNearClipDistance = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetCameraNearClipDistance> GetCameraNearClipDistance{ "?getNearClipDistance@Camera@Ogre@@UBEMXZ" };

	using _GetCameraFarClipDistance = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetCameraFarClipDistance> GetCameraFarClipDistance{ "?getFarClipDistance@Camera@Ogre@@UBEMXZ" };

	using _GetFrustumAspectRatio = float(__thiscall*)(void*);
	inline constinit OgreExport<_GetFrustumAspectRatio> GetFrustumAspectRatio{ "?getAspectRatio@Frustum@Ogre@@UBEMXZ" };

	using _SetFrustumAspectRatio = void(__thiscall*)(void*, float);
	inline constinit OgreExport<_SetFrustumAspectRatio> SetFrustumAspectRatio{ "?setAspectRatio@Frustum@Ogre@@UAEXM@Z" };

	using _SetFrustumNearClipDistance = void(__thiscall*)(void*, float);
	inline constinit OgreExport<_SetFrustumNearClipDistance> SetFrustumNearClipDistance{ "?setNearClipDistance@Frustum@Ogre@@UAEXM@Z" };

	using _SetFrustumFarClipDistance = void(__thiscall*)(void*, float);
	inline constinit OgreExport<_SetFrustumFarClipDistance> SetFrustumFarClipDistance{ "?setFarClipDistance@Frustum@Ogre@@UAEXM@Z" };

	using _GetFrustumProjectionType = int(__thiscall*)(void*);
	inline constinit OgreExport<_GetFrustumProjectionType> GetFrustumProjectionType{ "?getProjectionType@Frustum@Ogre@@UBE?AW4ProjectionType@2@XZ" };

	using _SetFrustumProjectionType = void(__thiscall*)(void*, int);
	inline constinit OgreExport<_SetFrustumProjectionType> SetFrustumProjectionType{ "?setProjectionType@Frustum@Ogre@@UAEXW4ProjectionType@2@@Z" };

	using _GetCameraPolygonMode = int(__thiscall*)(void*);
	inline constinit OgreExport<_GetCameraPolygonMode> GetCameraPolygonMode{ "?getPolygonMode@Camera@Ogre@@QBE?AW4PolygonMode@2@XZ" };

	using _SetCameraPolygonMode = void(__thiscall*)(void*, int);
	inline constinit OgreExport<_SetCameraPolygonMode> SetCameraPolygonMode{ "?setPolygonMode@Camera@Ogre@@QAEXW4PolygonMode@2@@Z" };

	using _GetShowBoundingBoxes = bool(__thiscall*)(void*);
	inline constinit OgreExport<_GetShowBoundingBoxes> GetShowBoundingBoxes{ "?getShowBoundingBoxes@SceneManager@Ogre@@UBE_NXZ" };

	using _ShowBoundingBoxes = void(__thiscall*)(void*, bool);
	inline constinit OgreExport<_ShowBoundingBoxes> ShowBoundingBoxes{ "?showBoundingBoxes@SceneManager@Ogre@@UAEX_N@Z" };

	using _GetShowDebugShadows = bool(__thiscall*)(void*);
	inline constinit OgreExport<_GetShowDebugShadows> GetShowDebugShadows{ "?getShowDebugShadows@SceneManager@Ogre@@UBE_NXZ" };

	using _SetShowDebugShadows = void(__thiscall*)(void*, bool);
	inline constinit OgreExport<_SetShowDebugShadows> SetShowDebugShadows{ "?setShowDebugShadows@SceneManager@Ogre@@UAEX_N@Z" };

	using _GetSceneVisibilityMask = uint32_t(__thiscall*)(void*);
	inline constinit OgreExport<_GetSceneVisibilityMask> GetSceneVisibilityMask{ "?getVisibilityMask@SceneManager@Ogre@@UAEIXZ" };

	using _SetSceneVisibilityMask = void(__thiscall*)(void*, uint32_t);
	inline constinit OgreExport<_SetSceneVisibilityMask> SetSceneVisibilityMask{ "?setVisibilityMask@SceneManager@Ogre@@UAEXI@Z" };

	using _GetViewportShadowsEnabled = bool(__thiscall*)(void*);
	inline constinit OgreExport<_GetViewportShadowsEnabled> GetViewportShadowsEnabled{ "?getShadowsEnabled@Viewport@Ogre@@QBE_NXZ" };

	using _SetViewportShadowsEnabled = void(__thiscall*)(void*, bool);
	inline constinit OgreExport<_SetViewportShadowsEnabled> SetViewportShadowsEnabled{ "?setShadowsEnabled@Viewport@Ogre@@QAEX_N@Z" };

	inline ::Ogre::SceneManager* AsSceneManager(void* sceneManagerPtr)
	{
		return reinterpret_cast<::Ogre::SceneManager*>(sceneManagerPtr);
	}

	inline ::Ogre::Viewport* AsViewport(void* viewportPtr)
	{
		return reinterpret_cast<::Ogre::Viewport*>(viewportPtr);
	}

	inline void* GetSkyBoxNode(void* sceneManagerPtr)
	{
		return AsSceneManager(sceneManagerPtr)->::Ogre::SceneManager::getSkyBoxNode();
	}

	inline void* GetCurrentViewport(void* sceneManagerPtr)
	{
		return AsSceneManager(sceneManagerPtr)->::Ogre::SceneManager::getCurrentViewport();
	}

	inline void* GetViewportCamera(void* viewportPtr)
	{
		return AsViewport(viewportPtr)->::Ogre::Viewport::getCamera();
	}

	inline bool GetSkyBoxGenParameters(void* sceneManagerPtr, SkyBoxGenParameters& outParams)
	{
		const auto& params = AsSceneManager(sceneManagerPtr)->::Ogre::SceneManager::getSkyBoxGenParameters();
		outParams.distance = params.skyBoxDistance;
		return true;
	}

	inline void* GetSkyDomeNode(void* sceneManagerPtr)
	{
		return AsSceneManager(sceneManagerPtr)->::Ogre::SceneManager::getSkyDomeNode();
	}

	inline bool GetSkyDomeGenParameters(void* sceneManagerPtr, SkyDomeGenParameters& outParams)
	{
		const auto& params = AsSceneManager(sceneManagerPtr)->::Ogre::SceneManager::getSkyDomeGenParameters();
		outParams.curvature = params.skyDomeCurvature;
		outParams.tiling = params.skyDomeTiling;
		outParams.distance = params.skyDomeDistance;
		outParams.xsegments = params.skyDomeXSegments;
		outParams.ysegments = params.skyDomeYSegments;
		outParams.ysegments_keep = params.skyDomeYSegments_keep;
		return true;
	}

	inline void* GetSkyPlaneNode(void* sceneManagerPtr)
	{
		return AsSceneManager(sceneManagerPtr)->::Ogre::SceneManager::getSkyPlaneNode();
	}

	inline bool GetSkyPlaneGenParameters(void* sceneManagerPtr, SkyPlaneGenParameters& outParams)
	{
		const auto& params = AsSceneManager(sceneManagerPtr)->::Ogre::SceneManager::getSkyPlaneGenParameters();
		outParams.scale = params.skyPlaneScale;
		outParams.tiling = params.skyPlaneTiling;
		outParams.bow = params.skyPlaneBow;
		outParams.xsegments = params.skyPlaneXSegments;
		outParams.ysegments = params.skyPlaneYSegments;
		return true;
	}
}
