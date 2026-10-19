/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

// Free-standing world point lights (exu.CreatePointLight and friends).
//
// Each light is an Ogre::Light of type LT_POINT attached to its own scene node
// under the root node, created through SceneManager exports resolved by
// mangled name from the shipped OgreMain.dll, the way the particle runtime
// reaches Ogre. Nothing dispatches through the pinned headers' vtables.
//
// Lifetime: lights belong to the mission Lua state. The table below keeps no
// Ogre pointer that is ever dereferenced without first checking, by name, that
// the published SceneManager is the one the light was created in and still
// holds that light and node. Shutdown() (Lua-state close) destroys every light
// it still can, light first and then its node, and forgets the rest.

#include "PointLights.h"

#include "PointLightCore.h"

#include "LuaHelpers.h"
#include "Ogre/Ogre.h"
#include "Ogre/OgreGibRuntime.h"
#include "Ogre/OgreParticleAbi.h"
#include "Ogre/OgreProc.h"
#include "Ogre/OgreRenderOrigin.h"
#include "Ogre/OgreRenderSpace.h"
#include "Util/FiniteCheck.h"
#include "Util/Logging.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace ExtraUtilities::Lua::PointLights
{
	namespace
	{
		namespace Core = ExtraUtilities::PointLightCore;
		namespace Env = ExtraUtilities::Lua::Environment;

		constexpr int kOgreLightTypePoint = 0; // Ogre::Light::LT_POINT

		using CreateLightFn = void*(__thiscall*)(void*, const std::string&);
		using HasLightFn = bool(__thiscall*)(void*, const std::string&);
		using GetLightFn = void*(__thiscall*)(void*, const std::string&);
		using DestroyLightFn = void(__thiscall*)(void*, const std::string&);
		using LightSetTypeFn = void(__thiscall*)(void*, int);
		using LightSetVisibleFn = void(__thiscall*)(void*, bool);
		using GetLightsAffectingFrustumFn = const void*(__thiscall*)(void*);

		using ExtraUtilities::OgreDll::ResolveOgreProc;

		CreateLightFn ResolveCreateLight()
		{
			static auto fn = ResolveOgreProc<CreateLightFn>(
				"?createLight@SceneManager@Ogre@@UAEPAVLight@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		HasLightFn ResolveHasLight()
		{
			static auto fn = ResolveOgreProc<HasLightFn>(
				"?hasLight@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		GetLightFn ResolveGetLight()
		{
			static auto fn = ResolveOgreProc<GetLightFn>(
				"?getLight@SceneManager@Ogre@@UBEPAVLight@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		DestroyLightFn ResolveDestroyLight()
		{
			static auto fn = ResolveOgreProc<DestroyLightFn>(
				"?destroyLight@SceneManager@Ogre@@UAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		LightSetTypeFn ResolveLightSetType()
		{
			static auto fn = ResolveOgreProc<LightSetTypeFn>("?setType@Light@Ogre@@QAEXW4LightTypes@12@@Z");
			return fn;
		}

		// Light overrides MovableObject::setVisible.
		LightSetVisibleFn ResolveLightSetVisible()
		{
			static auto fn = ResolveOgreProc<LightSetVisibleFn>("?setVisible@Light@Ogre@@UAEX_N@Z");
			return fn;
		}

		GetLightsAffectingFrustumFn ResolveGetLightsAffectingFrustum()
		{
			static auto fn = ResolveOgreProc<GetLightsAffectingFrustumFn>(
				"?_getLightsAffectingFrustum@SceneManager@Ogre@@UBEABV?$HashedVector@PAVLight@Ogre@@@2@XZ");
			return fn;
		}

		const std::string* ResolveLightFactoryTypeName()
		{
			static const std::string* name = ResolveOgreProc<const std::string*>(
				"?FACTORY_TYPE_NAME@LightFactory@Ogre@@2V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@A");
			return name;
		}

		// Every export a light needs. Resolved together so a missing one fails
		// the whole feature closed rather than leaving a half-built light.
		struct Abi
		{
			CreateLightFn createLight = nullptr;
			HasLightFn hasLight = nullptr;
			GetLightFn getLight = nullptr;
			DestroyLightFn destroyLight = nullptr;
			LightSetTypeFn setType = nullptr;
			LightSetVisibleFn setVisible = nullptr;
			Env::GetRootSceneNodeFn getRootSceneNode = nullptr;
			Env::CreateChildSceneNodeFn createChildSceneNode = nullptr;
			Env::HasSceneNodeFn hasSceneNode = nullptr;
			Env::GetSceneNodeFn getSceneNode = nullptr;
			Env::DestroySceneNodeFn destroySceneNode = nullptr;
			Env::AttachObjectFn attachObject = nullptr;
			Env::GetMovableObjectFn getMovableObject = nullptr;
			Env::SetNodePositionFn setNodePosition = nullptr;
			const std::string* lightTypeName = nullptr;
		};

		const char* g_missingExport = nullptr;

		bool ResolveAbi(Abi& abi)
		{
			abi.createLight = ResolveCreateLight();
			abi.hasLight = ResolveHasLight();
			abi.getLight = ResolveGetLight();
			abi.destroyLight = ResolveDestroyLight();
			abi.setType = ResolveLightSetType();
			abi.setVisible = ResolveLightSetVisible();
			abi.getRootSceneNode = Env::ResolveGetRootSceneNode();
			abi.createChildSceneNode = Env::ResolveCreateChildSceneNode();
			abi.hasSceneNode = Env::ResolveHasSceneNode();
			abi.getSceneNode = Env::ResolveGetSceneNode();
			abi.destroySceneNode = Env::ResolveDestroySceneNode();
			abi.attachObject = Env::ResolveAttachObject();
			abi.getMovableObject = Env::ResolveGetMovableObject();
			abi.setNodePosition = Env::ResolveSetNodePosition();
			abi.lightTypeName = ResolveLightFactoryTypeName();

			const char* missing = nullptr;
			if (abi.createLight == nullptr) missing = "SceneManager::createLight";
			else if (abi.hasLight == nullptr) missing = "SceneManager::hasLight";
			else if (abi.getLight == nullptr) missing = "SceneManager::getLight";
			else if (abi.destroyLight == nullptr) missing = "SceneManager::destroyLight";
			else if (abi.setType == nullptr) missing = "Light::setType";
			else if (abi.setVisible == nullptr) missing = "Light::setVisible";
			else if (abi.getRootSceneNode == nullptr) missing = "SceneManager::getRootSceneNode";
			else if (abi.createChildSceneNode == nullptr) missing = "SceneNode::createChildSceneNode";
			else if (abi.hasSceneNode == nullptr) missing = "SceneManager::hasSceneNode";
			else if (abi.getSceneNode == nullptr) missing = "SceneManager::getSceneNode";
			else if (abi.destroySceneNode == nullptr) missing = "SceneManager::destroySceneNode";
			else if (abi.attachObject == nullptr) missing = "SceneNode::attachObject";
			else if (abi.getMovableObject == nullptr) missing = "SceneManager::getMovableObject";
			else if (abi.setNodePosition == nullptr) missing = "Node::setPosition";
			else if (abi.lightTypeName == nullptr) missing = "LightFactory::FACTORY_TYPE_NAME";
			else if (!ExtraUtilities::Ogre::SetDiffuseColor) missing = "Light::setDiffuseColour";
			else if (!ExtraUtilities::Ogre::SetSpecularColor) missing = "Light::setSpecularColour";
			else if (!ExtraUtilities::Ogre::SetAttenuation) missing = "Light::setAttenuation";
			else if (!ExtraUtilities::Ogre::SetCastShadows) missing = "MovableObject::setCastShadows";

			if (missing != nullptr && g_missingExport != missing)
			{
				g_missingExport = missing;
				Logging::LogMessage("[EXU::PointLights] disabled: OgreMain export %s not found", missing);
			}
			return missing == nullptr;
		}

		struct Record
		{
			void* owner = nullptr;
			// Compared, never dereferenced: Ogre is asked by name first.
			void* light = nullptr;
			std::string ogreName;
			std::string nodeName;
			BZR::VECTOR_3D simPosition{};
			Core::Colour diffuse{};
			Core::Colour specular{};
			float powerScale = 1.0f;
			Core::Attenuation attenuation{};
			bool customAttenuation = false;
			bool visible = true;
		};

		std::map<std::string, Record> g_records;

		// The published SceneManager, or null when it is absent or its vptr
		// does not point into OgreMain (a freed manager keeps its global).
		void* CurrentSceneManager() noexcept
		{
			void* sceneManager = nullptr;
			try
			{
				sceneManager = ExtraUtilities::Ogre::sceneManager.Read();
			}
			catch (...)
			{
				return nullptr;
			}
			if (sceneManager == nullptr || !OgreGibRuntime::LooksLikeOgreObject(sceneManager))
			{
				return nullptr;
			}
			return sceneManager;
		}

		bool TrySimToRender(const BZR::VECTOR_3D& sim, BZR::VECTOR_3D& outRender) noexcept
		{
			BZR::VECTOR_3D origin{};
			if (!OgreRenderSpace::TryReadWorldRenderOrigin(origin) || !FiniteCheck::IsFiniteVector(origin))
			{
				return false;
			}
			outRender = OgreRenderSpace::SimPositionToRender(sim, origin);
			return FiniteCheck::IsFiniteVector(outRender);
		}

		void LogFault(const char* site, const std::string& name, unsigned long code)
		{
			Logging::LogMessage("[EXU::PointLights] %s faulted (0x%08lX) name=%s", site, code, name.c_str());
		}

		// Looks the record's light and node up through Ogre by name. Fails when
		// the owning SceneManager is no longer the published one or no longer
		// holds the objects.
		bool Lookup(const Abi& abi, const Record& record, void*& outLight, void*& outNode)
		{
			outLight = nullptr;
			outNode = nullptr;
			void* const sceneManager = CurrentSceneManager();
			if (sceneManager == nullptr || sceneManager != record.owner)
			{
				return false;
			}

			bool found = false;
			Seh::Guard("PointLights::Lookup", [&] {
				if (!abi.hasLight(sceneManager, record.ogreName) || !abi.hasSceneNode(sceneManager, record.nodeName))
				{
					return;
				}
				void* const light = abi.getLight(sceneManager, record.ogreName);
				void* const node = abi.getSceneNode(sceneManager, record.nodeName);
				if (light == nullptr || node == nullptr || light != record.light)
				{
					return;
				}
				outLight = light;
				outNode = node;
				found = true;
			}, [&](unsigned long code) { LogFault("Lookup", record.ogreName, code); });
			return found;
		}

		bool ApplyLightState(const Record& record, void* light)
		{
			const Core::Colour diffuse = Core::ScaledColour(record.diffuse, record.powerScale);
			const Core::Colour specular = Core::ScaledColour(record.specular, record.powerScale);
			const Core::Attenuation& a = record.attenuation;
			const LightSetVisibleFn setVisible = ResolveLightSetVisible();
			return Seh::Guard("PointLights::Apply", [&] {
				ExtraUtilities::Ogre::SetDiffuseColor(light, diffuse.r, diffuse.g, diffuse.b);
				ExtraUtilities::Ogre::SetSpecularColor(light, specular.r, specular.g, specular.b);
				ExtraUtilities::Ogre::SetAttenuation(light, a.range, a.constant, a.linear, a.quadratic);
				setVisible(light, record.visible);
			}, [&](unsigned long code) { LogFault("Apply", record.ogreName, code); });
		}

		// Destroys the light and node by name when Ogre still has them in the
		// owning manager. Returns true when nothing of the light remains that
		// EXU could still reach.
		bool DestroyRecord(const Abi& abi, const Record& record)
		{
			void* const sceneManager = CurrentSceneManager();
			if (sceneManager == nullptr || sceneManager != record.owner)
			{
				// The manager went with its mission and took its lights along.
				return true;
			}

			return Seh::Guard("PointLights::Destroy", [&] {
				// destroyLight detaches the light from its node first.
				if (abi.hasLight(sceneManager, record.ogreName) &&
					abi.getLight(sceneManager, record.ogreName) == record.light)
				{
					abi.destroyLight(sceneManager, record.ogreName);
				}
				if (abi.hasSceneNode(sceneManager, record.nodeName))
				{
					abi.destroySceneNode(sceneManager, record.nodeName);
				}
			}, [&](unsigned long code) { LogFault("Destroy", record.ogreName, code); });
		}

		// Finds the record and its live Ogre objects. A record whose objects are
		// gone is forgotten, so Has/Create see the truth afterwards.
		Record* FindLive(const Abi& abi, const std::string& name, void*& outLight, void*& outNode)
		{
			const auto found = g_records.find(name);
			if (found == g_records.end())
			{
				return nullptr;
			}
			if (!Lookup(abi, found->second, outLight, outNode))
			{
				Logging::LogMessage("[EXU::PointLights] forgetting stale light name=%s", name.c_str());
				g_records.erase(found);
				return nullptr;
			}
			return &found->second;
		}

		std::string CheckName(lua_State* L, int index)
		{
			size_t length = 0;
			const char* text = luaL_checklstring(L, index, &length);
			const std::string_view name(text, length);
			if (!Core::IsValidName(name))
			{
				luaL_argerror(L, index, "light name must be 1-64 printable characters");
			}
			return std::string(name);
		}

		BZR::VECTOR_3D CheckPosition(lua_State* L, int index)
		{
			const BZR::VECTOR_3D position = CheckVectorOrSingles(L, index);
			if (!FiniteCheck::IsFiniteVector(position))
			{
				luaL_argerror(L, index, "position must be finite");
			}
			return position;
		}

		Core::Colour CheckColour(lua_State* L, int index)
		{
			if (!lua_istable(L, index))
			{
				luaL_argerror(L, index, "colour must be a table {r, g, b}");
			}
			const ExtraUtilities::Ogre::Color c = CheckColorOrSingles(L, index);
			const Core::Colour colour{ c.r, c.g, c.b };
			if (!Core::IsValidColour(colour))
			{
				luaL_argerror(L, index, "colour components must be finite and in [0, 100]");
			}
			return colour;
		}

		float CheckRange(lua_State* L, int index)
		{
			const float range = static_cast<float>(luaL_checknumber(L, index));
			if (!Core::IsValidRange(range))
			{
				luaL_argerror(L, index, "range must be finite, positive and at most 2000");
			}
			return range;
		}

		int PushFalse(lua_State* L)
		{
			lua_pushboolean(L, 0);
			return 1;
		}

		void PushColourTable(lua_State* L, const Core::Colour& colour)
		{
			lua_createtable(L, 0, 3);
			lua_pushnumber(L, colour.r);
			lua_setfield(L, -2, "r");
			lua_pushnumber(L, colour.g);
			lua_setfield(L, -2, "g");
			lua_pushnumber(L, colour.b);
			lua_setfield(L, -2, "b");
		}

		// Reads a HashedVector<Light*> (std::vector first: begin, end, capacity
		// end). Fails on anything that does not look like a sane vector.
		bool TryReadLightList(const void* list, const void* const*& outBegin, size_t& outCount) noexcept
		{
			const void* const* words[3] = {};
			if (list == nullptr || !Seh::TryCopy(words, list, sizeof(words)))
			{
				return false;
			}
			const uintptr_t begin = reinterpret_cast<uintptr_t>(words[0]);
			const uintptr_t end = reinterpret_cast<uintptr_t>(words[1]);
			const uintptr_t capacity = reinterpret_cast<uintptr_t>(words[2]);
			if (begin > end || end > capacity || (end - begin) % sizeof(void*) != 0)
			{
				return false;
			}
			const size_t count = (end - begin) / sizeof(void*);
			if (count > 4096 || (count > 0 && begin == 0))
			{
				return false;
			}
			outBegin = words[0];
			outCount = count;
			return true;
		}
	}

	int Create(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		if (!lua_istable(L, 2) && !lua_isuserdata(L, 2))
		{
			luaL_argerror(L, 2, "position must be a vector");
		}
		const BZR::VECTOR_3D simPosition = CheckPosition(L, 2);
		const Core::Colour colour = lua_isnoneornil(L, 3) ? Core::Colour{} : CheckColour(L, 3);
		const float range = lua_isnoneornil(L, 4) ? 50.0f : CheckRange(L, 4);

		Abi abi;
		void* const sceneManager = CurrentSceneManager();
		if (sceneManager == nullptr || !ResolveAbi(abi))
		{
			return PushFalse(L);
		}

		{
			void* light = nullptr;
			void* node = nullptr;
			if (FindLive(abi, name, light, node) != nullptr)
			{
				Logging::LogMessage("[EXU::PointLights] create refused: name=%s already exists", name.c_str());
				return PushFalse(L);
			}
		}
		if (!Core::HasCapacity(g_records.size()))
		{
			Logging::LogMessage("[EXU::PointLights] create refused: cap of %u lights reached (name=%s)",
				static_cast<unsigned>(Core::kMaxPointLights), name.c_str());
			return PushFalse(L);
		}

		BZR::VECTOR_3D renderPosition{};
		if (!TrySimToRender(simPosition, renderPosition))
		{
			Logging::LogMessage("[EXU::PointLights] create refused: render origin unavailable (name=%s)", name.c_str());
			return PushFalse(L);
		}

		Record record;
		record.owner = sceneManager;
		record.ogreName = Core::OgreLightName(name);
		record.nodeName = Core::OgreNodeName(name);
		record.simPosition = simPosition;
		record.diffuse = colour;
		record.specular = colour;
		record.attenuation = Core::AttenuationForRange(range);

		void* light = nullptr;
		void* node = nullptr;
		bool built = false;
		const bool completed = Seh::Guard("PointLights::Create", [&] {
			// A leftover from an earlier state that skipped shutdown; Ogre
			// throws on a duplicate name, so clear it out first.
			if (abi.hasLight(sceneManager, record.ogreName))
			{
				abi.destroyLight(sceneManager, record.ogreName);
			}
			if (abi.hasSceneNode(sceneManager, record.nodeName))
			{
				abi.destroySceneNode(sceneManager, record.nodeName);
			}

			void* const root = abi.getRootSceneNode(sceneManager);
			if (root == nullptr)
			{
				return;
			}
			light = abi.createLight(sceneManager, record.ogreName);
			if (light == nullptr)
			{
				return;
			}
			abi.setType(light, kOgreLightTypePoint);
			// The MovableObject the scene graph sees. Light inherits it singly,
			// so this is the same address; asking Ogre keeps that an observed
			// fact rather than an assumption about the build's layout.
			void* const movable = abi.getMovableObject(sceneManager, record.ogreName, *abi.lightTypeName);
			if (movable == nullptr)
			{
				return;
			}
			// Texture shadows here are three sun cascades with no slot to
			// spare; a point light must never claim one.
			ExtraUtilities::Ogre::SetCastShadows(movable, false);

			const Env::OgreQuaternionValue identity{};
			node = abi.createChildSceneNode(root, record.nodeName, renderPosition, identity);
			if (node == nullptr)
			{
				return;
			}
			abi.attachObject(node, movable);
			built = true;
		}, [&](unsigned long code) { LogFault("Create", record.ogreName, code); });

		record.light = light;
		if (!completed || !built || !ApplyLightState(record, light))
		{
			DestroyRecord(abi, record);
			Logging::LogMessage("[EXU::PointLights] create failed: name=%s", name.c_str());
			return PushFalse(L);
		}

		g_records.emplace(name, std::move(record));
		Logging::LogMessage("[EXU::PointLights] created name=%s sim=(%.1f, %.1f, %.1f) range=%.1f count=%u",
			name.c_str(), simPosition.x, simPosition.y, simPosition.z, range, static_cast<unsigned>(g_records.size()));
		lua_pushboolean(L, 1);
		return 1;
	}

	int SetPosition(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		const BZR::VECTOR_3D simPosition = CheckPosition(L, 2);

		Abi abi;
		void* light = nullptr;
		void* node = nullptr;
		Record* record = ResolveAbi(abi) ? FindLive(abi, name, light, node) : nullptr;
		BZR::VECTOR_3D renderPosition{};
		if (record == nullptr || !TrySimToRender(simPosition, renderPosition))
		{
			return PushFalse(L);
		}

		const bool ok = Seh::Guard("PointLights::SetPosition", [&] {
			abi.setNodePosition(node, renderPosition);
		}, [&](unsigned long code) { LogFault("SetPosition", record->ogreName, code); });
		if (ok)
		{
			record->simPosition = simPosition;
		}
		lua_pushboolean(L, ok ? 1 : 0);
		return 1;
	}

	namespace
	{
		// Applies `change` to a copy of the record and commits it only when
		// Ogre accepted the new state.
		template <typename Change>
		int UpdateLight(lua_State* L, const std::string& name, Change&& change)
		{
			Abi abi;
			void* light = nullptr;
			void* node = nullptr;
			Record* record = ResolveAbi(abi) ? FindLive(abi, name, light, node) : nullptr;
			if (record == nullptr)
			{
				return PushFalse(L);
			}
			Record updated = *record;
			change(updated);
			if (!ApplyLightState(updated, light))
			{
				return PushFalse(L);
			}
			*record = std::move(updated);
			lua_pushboolean(L, 1);
			return 1;
		}
	}

	int SetColour(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		const Core::Colour diffuse = CheckColour(L, 2);
		const Core::Colour specular = lua_isnoneornil(L, 3) ? diffuse : CheckColour(L, 3);
		return UpdateLight(L, name, [&](Record& r) {
			r.diffuse = diffuse;
			r.specular = specular;
		});
	}

	int SetRange(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		const float range = CheckRange(L, 2);
		return UpdateLight(L, name, [&](Record& r) {
			r.attenuation = Core::AttenuationForRange(range);
			r.customAttenuation = false;
		});
	}

	int SetAttenuation(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		Core::Attenuation attenuation;
		attenuation.range = CheckRange(L, 2);
		attenuation.constant = static_cast<float>(luaL_checknumber(L, 3));
		attenuation.linear = static_cast<float>(luaL_checknumber(L, 4));
		attenuation.quadratic = static_cast<float>(luaL_checknumber(L, 5));
		if (!Core::IsValidAttenuation(attenuation))
		{
			luaL_argerror(L, 3, "attenuation terms must be finite, non-negative and not all zero");
		}
		return UpdateLight(L, name, [&](Record& r) {
			r.attenuation = attenuation;
			r.customAttenuation = true;
		});
	}

	int SetPowerScale(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		const float scale = static_cast<float>(luaL_checknumber(L, 2));
		if (!Core::IsValidPowerScale(scale))
		{
			luaL_argerror(L, 2, "power scale must be finite and in [0, 100]");
		}
		return UpdateLight(L, name, [&](Record& r) { r.powerScale = scale; });
	}

	int SetVisible(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		const bool visible = CheckBool(L, 2);
		return UpdateLight(L, name, [&](Record& r) { r.visible = visible; });
	}

	int Destroy(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		const auto found = g_records.find(name);
		if (found == g_records.end())
		{
			return PushFalse(L);
		}
		Abi abi;
		const bool ok = ResolveAbi(abi) && DestroyRecord(abi, found->second);
		// Forget it either way: after a fault the objects must not be touched.
		g_records.erase(found);
		Logging::LogMessage("[EXU::PointLights] destroyed name=%s ok=%d", name.c_str(), ok ? 1 : 0);
		lua_pushboolean(L, ok ? 1 : 0);
		return 1;
	}

	int DestroyAll(lua_State* L)
	{
		Abi abi;
		const bool haveAbi = ResolveAbi(abi);
		lua_Integer destroyed = 0;
		for (const auto& [name, record] : g_records)
		{
			if (haveAbi && DestroyRecord(abi, record))
			{
				++destroyed;
			}
		}
		g_records.clear();
		lua_pushinteger(L, destroyed);
		return 1;
	}

	int Has(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		Abi abi;
		void* light = nullptr;
		void* node = nullptr;
		const bool live = ResolveAbi(abi) && FindLive(abi, name, light, node) != nullptr;
		lua_pushboolean(L, live ? 1 : 0);
		return 1;
	}

	int GetInfo(lua_State* L)
	{
		const std::string name = CheckName(L, 1);
		Abi abi;
		void* light = nullptr;
		void* node = nullptr;
		const Record* record = ResolveAbi(abi) ? FindLive(abi, name, light, node) : nullptr;
		if (record == nullptr)
		{
			lua_pushnil(L);
			return 1;
		}

		lua_createtable(L, 0, 12);
		lua_pushlstring(L, name.data(), name.size());
		lua_setfield(L, -2, "name");
		PushVector(L, record->simPosition);
		lua_setfield(L, -2, "position");
		PushColourTable(L, record->diffuse);
		lua_setfield(L, -2, "diffuse");
		PushColourTable(L, record->specular);
		lua_setfield(L, -2, "specular");
		lua_pushnumber(L, record->powerScale);
		lua_setfield(L, -2, "powerScale");
		lua_pushnumber(L, record->attenuation.range);
		lua_setfield(L, -2, "range");
		lua_pushnumber(L, record->attenuation.constant);
		lua_setfield(L, -2, "constant");
		lua_pushnumber(L, record->attenuation.linear);
		lua_setfield(L, -2, "linear");
		lua_pushnumber(L, record->attenuation.quadratic);
		lua_setfield(L, -2, "quadratic");
		lua_pushboolean(L, record->customAttenuation ? 1 : 0);
		lua_setfield(L, -2, "customAttenuation");
		lua_pushboolean(L, record->visible ? 1 : 0);
		lua_setfield(L, -2, "visible");
		return 1;
	}

	// The light-slot measurement aid: how many lights Ogre found touching the
	// camera frustum on the last render, and how many of those are EXU's.
	int GetStats(lua_State* L)
	{
		lua_createtable(L, 0, 5);
		lua_pushinteger(L, static_cast<lua_Integer>(g_records.size()));
		lua_setfield(L, -2, "count");
		lua_pushinteger(L, static_cast<lua_Integer>(Core::kMaxPointLights));
		lua_setfield(L, -2, "max");

		void* const sceneManager = CurrentSceneManager();
		const GetLightsAffectingFrustumFn getFrustumLights = ResolveGetLightsAffectingFrustum();
		lua_pushboolean(L, sceneManager != nullptr ? 1 : 0);
		lua_setfield(L, -2, "sceneManager");
		if (sceneManager == nullptr || getFrustumLights == nullptr)
		{
			return 1;
		}

		const void* list = nullptr;
		Seh::Guard("PointLights::GetStats", [&] { list = getFrustumLights(sceneManager); });

		const void* const* begin = nullptr;
		size_t count = 0;
		if (!TryReadLightList(list, begin, count))
		{
			return 1;
		}
		size_t owned = 0;
		for (size_t i = 0; i < count; ++i)
		{
			const void* entry = nullptr;
			if (!Seh::TryCopy(&entry, begin + i, sizeof(entry)))
			{
				return 1;
			}
			for (const auto& [name, record] : g_records)
			{
				if (record.owner == sceneManager && record.light == entry)
				{
					++owned;
					break;
				}
			}
		}
		lua_pushinteger(L, static_cast<lua_Integer>(count));
		lua_setfield(L, -2, "frustumLights");
		lua_pushinteger(L, static_cast<lua_Integer>(owned));
		lua_setfield(L, -2, "ownedInFrustum");
		return 1;
	}

	void Shutdown() noexcept
	{
		try
		{
			if (g_records.empty())
			{
				return;
			}
			Abi abi;
			const bool haveAbi = ResolveAbi(abi);
			size_t destroyed = 0;
			for (const auto& [name, record] : g_records)
			{
				if (haveAbi && DestroyRecord(abi, record))
				{
					++destroyed;
				}
			}
			const size_t total = g_records.size();
			g_records.clear();
			Logging::LogMessage("[EXU::PointLights] shutdown: released %u of %u lights",
				static_cast<unsigned>(destroyed), static_cast<unsigned>(total));
		}
		catch (...)
		{
			g_records.clear();
		}
	}
}
