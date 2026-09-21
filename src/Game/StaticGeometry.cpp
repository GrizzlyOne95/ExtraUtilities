/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "StaticGeometry.h"

#include "LuaHelpers.h"
#include "Ogre/Ogre.h"
#include "Ogre/OgreRenderSpace.h"
#include "Util/Logging.h"
#include "bzr.h"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace ExtraUtilities::Lua::StaticGeometry
{
	namespace
	{
		constexpr size_t kMaxInstances = 100000;
		constexpr size_t kMaxNameLength = 128;

		// Keep Ogre ABI values local to this bridge. EXU's existing Ogre layer
		// intentionally exposes only a narrow SceneManager declaration, while
		// these exported methods need the binary layout of Vector3/Quaternion.
		struct OgreVector3Value
		{
			float x;
			float y;
			float z;
		};

		struct OgreQuaternionValue
		{
			float w;
			float x;
			float y;
			float z;
		};

		struct Instance
		{
			OgreVector3Value position{ 0.0f, 0.0f, 0.0f };
			OgreQuaternionValue orientation{ 1.0f, 0.0f, 0.0f, 0.0f };
			OgreVector3Value scale{ 1.0f, 1.0f, 1.0f };
		};

		struct Options
		{
			OgreVector3Value regionDimensions{ 1000.0f, 1000.0f, 1000.0f };
			OgreVector3Value origin{ 0.0f, 0.0f, 0.0f };
			float renderingDistance = 0.0f;
			bool castShadows = false;
			bool visible = true;
		};

		struct Record
		{
			void* owner = nullptr;
			void* geometry = nullptr;
			std::string mesh;
			std::string material;
			size_t instanceCount = 0;
			size_t regionCount = 0;
			uint64_t buildMilliseconds = 0;
			float renderingDistance = 0.0f;
			bool castShadows = false;
			bool visible = true;
		};

		std::map<std::string, Record> g_records;

		using SceneManagerCreateStaticGeometryFn = void*(__thiscall*)(void*, const std::string&);
		using SceneManagerHasStaticGeometryFn = bool(__thiscall*)(const void*, const std::string&);
		using SceneManagerDestroyStaticGeometryFn = void(__thiscall*)(void*, void*);
		using SceneManagerCreateEntityFn = void*(__thiscall*)(void*, const std::string&);
		using SceneManagerDestroyEntityFn = void(__thiscall*)(void*, void*);
		using StaticGeometrySetVectorFn = void(__thiscall*)(void*, const OgreVector3Value&);
		using StaticGeometrySetFloatFn = void(__thiscall*)(void*, float);
		using StaticGeometrySetBoolFn = void(__thiscall*)(void*, bool);
		using StaticGeometryAddEntityFn = void(__thiscall*)(
			void*, void*, const OgreVector3Value&, const OgreQuaternionValue&, const OgreVector3Value&);
		using StaticGeometryBuildFn = void(__thiscall*)(void*);
		using EntitySetMaterialNameFn = void(__thiscall*)(void*, const std::string&, const std::string&);

		template<typename T>
		T ResolveOgreProc(const char* name)
		{
			const HMODULE module = GetModuleHandleA("OgreMain.dll");
			return module == nullptr ? nullptr : reinterpret_cast<T>(GetProcAddress(module, name));
		}

		SceneManagerCreateStaticGeometryFn ResolveCreateStaticGeometry()
		{
			static auto fn = ResolveOgreProc<SceneManagerCreateStaticGeometryFn>(
				"?createStaticGeometry@SceneManager@Ogre@@UAEPAVStaticGeometry@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		SceneManagerHasStaticGeometryFn ResolveHasStaticGeometry()
		{
			static auto fn = ResolveOgreProc<SceneManagerHasStaticGeometryFn>(
				"?hasStaticGeometry@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		SceneManagerDestroyStaticGeometryFn ResolveDestroyStaticGeometry()
		{
			static auto fn = ResolveOgreProc<SceneManagerDestroyStaticGeometryFn>(
				"?destroyStaticGeometry@SceneManager@Ogre@@UAEXPAVStaticGeometry@2@@Z");
			return fn;
		}

		SceneManagerCreateEntityFn ResolveCreateEntity()
		{
			static auto fn = ResolveOgreProc<SceneManagerCreateEntityFn>(
				"?createEntity@SceneManager@Ogre@@UAEPAVEntity@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z");
			return fn;
		}

		SceneManagerDestroyEntityFn ResolveDestroyEntity()
		{
			static auto fn = ResolveOgreProc<SceneManagerDestroyEntityFn>(
				"?destroyEntity@SceneManager@Ogre@@UAEXPAVEntity@2@@Z");
			return fn;
		}

		StaticGeometrySetVectorFn ResolveSetRegionDimensions()
		{
			static auto fn = ResolveOgreProc<StaticGeometrySetVectorFn>(
				"?setRegionDimensions@StaticGeometry@Ogre@@UAEXABVVector3@2@@Z");
			return fn;
		}

		StaticGeometrySetVectorFn ResolveSetOrigin()
		{
			static auto fn = ResolveOgreProc<StaticGeometrySetVectorFn>(
				"?setOrigin@StaticGeometry@Ogre@@UAEXABVVector3@2@@Z");
			return fn;
		}

		StaticGeometrySetFloatFn ResolveSetRenderingDistance()
		{
			static auto fn = ResolveOgreProc<StaticGeometrySetFloatFn>(
				"?setRenderingDistance@StaticGeometry@Ogre@@UAEXM@Z");
			return fn;
		}

		StaticGeometrySetBoolFn ResolveSetCastShadows()
		{
			static auto fn = ResolveOgreProc<StaticGeometrySetBoolFn>(
				"?setCastShadows@StaticGeometry@Ogre@@UAEX_N@Z");
			return fn;
		}

		StaticGeometrySetBoolFn ResolveSetVisible()
		{
			static auto fn = ResolveOgreProc<StaticGeometrySetBoolFn>(
				"?setVisible@StaticGeometry@Ogre@@UAEX_N@Z");
			return fn;
		}

		StaticGeometryAddEntityFn ResolveAddEntity()
		{
			static auto fn = ResolveOgreProc<StaticGeometryAddEntityFn>(
				"?addEntity@StaticGeometry@Ogre@@UAEXPAVEntity@2@ABVVector3@2@ABVQuaternion@2@1@Z");
			return fn;
		}

		StaticGeometryBuildFn ResolveBuild()
		{
			static auto fn = ResolveOgreProc<StaticGeometryBuildFn>(
				"?build@StaticGeometry@Ogre@@UAEXXZ");
			return fn;
		}

		EntitySetMaterialNameFn ResolveEntitySetMaterialName()
		{
			static auto fn = ResolveOgreProc<EntitySetMaterialNameFn>(
				"?setMaterialName@Entity@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z");
			return fn;
		}

		bool IsFinite(float value)
		{
			return std::isfinite(value);
		}

		// Redux recentres the Ogre render world around a per-map origin and
		// mirrors Z, so Lua's simulation coordinates are NOT render coordinates.
		// Everything the exe draws goes through this conversion; geometry built
		// from raw sim positions lands far outside the render world and is never
		// drawn, identically on every backend.
		//
		// Its own SEH frame because Create() holds objects that require
		// unwinding, which __try may not share a function with.
		bool TryReadWorldRenderOrigin(OgreVector3Value& outOrigin) noexcept
		{
			__try
			{
				const float* origin =
					reinterpret_cast<const float*>(BZR::Ogre::worldRenderOriginAddress);
				outOrigin.x = origin[0];
				outOrigin.y = origin[1];
				outOrigin.z = origin[2];
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		float ReadNumberField(lua_State* L, int tableIndex, const char* name, float fallback)
		{
			tableIndex = AbsoluteStackIndex(L, tableIndex);
			lua_getfield(L, tableIndex, name);
			const float value = lua_isnil(L, -1)
				? fallback
				: static_cast<float>(luaL_checknumber(L, -1));
			lua_pop(L, 1);
			return value;
		}

		bool ReadBoolField(lua_State* L, int tableIndex, const char* name, bool fallback)
		{
			tableIndex = AbsoluteStackIndex(L, tableIndex);
			lua_getfield(L, tableIndex, name);
			const bool value = lua_isnil(L, -1) ? fallback : CheckBool(L, -1);
			lua_pop(L, 1);
			return value;
		}

		OgreVector3Value ReadVectorTable(lua_State* L, int tableIndex, const OgreVector3Value& fallback)
		{
			tableIndex = AbsoluteStackIndex(L, tableIndex);
			return OgreVector3Value{
				ReadNumberField(L, tableIndex, "x", fallback.x),
				ReadNumberField(L, tableIndex, "y", fallback.y),
				ReadNumberField(L, tableIndex, "z", fallback.z) };
		}

		OgreVector3Value ReadOptionalVectorField(
			lua_State* L,
			int tableIndex,
			const char* name,
			const OgreVector3Value& fallback)
		{
			tableIndex = AbsoluteStackIndex(L, tableIndex);
			lua_getfield(L, tableIndex, name);
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				return fallback;
			}
			if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
			{
				luaL_error(L, "%s must be a vector-like table or userdata", name);
			}
			const auto result = ReadVectorTable(L, -1, fallback);
			lua_pop(L, 1);
			return result;
		}

		OgreQuaternionValue QuaternionFromYaw(float yaw)
		{
			const float half = yaw * 0.5f;
			return OgreQuaternionValue{ std::cos(half), 0.0f, std::sin(half), 0.0f };
		}

		OgreQuaternionValue ReadOrientation(lua_State* L, int tableIndex)
		{
			tableIndex = AbsoluteStackIndex(L, tableIndex);
			lua_getfield(L, tableIndex, "orientation");
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				return QuaternionFromYaw(ReadNumberField(L, tableIndex, "yaw", 0.0f));
			}
			if (!lua_istable(L, -1))
			{
				luaL_error(L, "orientation must be a quaternion table");
			}

			const int orientationIndex = AbsoluteStackIndex(L, -1);
			const OgreQuaternionValue result{
				ReadNumberField(L, orientationIndex, "w", 1.0f),
				ReadNumberField(L, orientationIndex, "x", 0.0f),
				ReadNumberField(L, orientationIndex, "y", 0.0f),
				ReadNumberField(L, orientationIndex, "z", 0.0f) };
			lua_pop(L, 1);
			return result;
		}

		OgreVector3Value ReadScale(lua_State* L, int tableIndex)
		{
			tableIndex = AbsoluteStackIndex(L, tableIndex);
			lua_getfield(L, tableIndex, "scale");
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				return OgreVector3Value{ 1.0f, 1.0f, 1.0f };
			}
			if (lua_isnumber(L, -1))
			{
				const float scale = static_cast<float>(lua_tonumber(L, -1));
				lua_pop(L, 1);
				return OgreVector3Value{ scale, scale, scale };
			}
			if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
			{
				luaL_error(L, "scale must be a number or vector-like value");
			}
			const auto result = ReadVectorTable(L, -1, OgreVector3Value{ 1.0f, 1.0f, 1.0f });
			lua_pop(L, 1);
			return result;
		}

		Instance ReadInstance(lua_State* L, int tableIndex)
		{
			tableIndex = AbsoluteStackIndex(L, tableIndex);
			Instance instance;
			instance.position = ReadOptionalVectorField(
				L,
				tableIndex,
				"position",
				OgreVector3Value{
					ReadNumberField(L, tableIndex, "x", 0.0f),
					ReadNumberField(L, tableIndex, "y", 0.0f),
					ReadNumberField(L, tableIndex, "z", 0.0f) });
			instance.orientation = ReadOrientation(L, tableIndex);
			instance.scale = ReadScale(L, tableIndex);

			const float values[] = {
				instance.position.x, instance.position.y, instance.position.z,
				instance.orientation.w, instance.orientation.x,
				instance.orientation.y, instance.orientation.z,
				instance.scale.x, instance.scale.y, instance.scale.z,
			};
			for (const float value : values)
			{
				if (!IsFinite(value))
				{
					luaL_error(L, "StaticGeometry instance values must be finite");
				}
			}
			if (instance.scale.x <= 0.0f || instance.scale.y <= 0.0f || instance.scale.z <= 0.0f)
			{
				luaL_error(L, "StaticGeometry instance scale must be positive");
			}
			return instance;
		}

		Options ReadOptions(lua_State* L, int tableIndex)
		{
			Options options;
			if (lua_isnoneornil(L, tableIndex))
			{
				return options;
			}
			luaL_checktype(L, tableIndex, LUA_TTABLE);
			options.regionDimensions = ReadOptionalVectorField(
				L, tableIndex, "regionDimensions", options.regionDimensions);
			options.origin = ReadOptionalVectorField(L, tableIndex, "origin", options.origin);
			options.renderingDistance = ReadNumberField(L, tableIndex, "renderingDistance", 0.0f);
			options.castShadows = ReadBoolField(L, tableIndex, "castShadows", false);
			options.visible = ReadBoolField(L, tableIndex, "visible", true);

			if (!IsFinite(options.regionDimensions.x) ||
				!IsFinite(options.regionDimensions.y) ||
				!IsFinite(options.regionDimensions.z) ||
				options.regionDimensions.x <= 0.0f ||
				options.regionDimensions.y <= 0.0f ||
				options.regionDimensions.z <= 0.0f)
			{
				luaL_argerror(L, tableIndex, "regionDimensions must contain finite positive values");
			}
			if (!IsFinite(options.renderingDistance) || options.renderingDistance < 0.0f)
			{
				luaL_argerror(L, tableIndex, "renderingDistance must be finite and non-negative");
			}
			return options;
		}

		void* CurrentSceneManager()
		{
			return ExtraUtilities::Ogre::sceneManager.Read();
		}

		bool DestroyRecord(Record& record) noexcept
		{
			if (record.owner == nullptr || record.geometry == nullptr)
			{
				return true;
			}
			if (CurrentSceneManager() != record.owner)
			{
				// The owning manager has already left with its mission. Ogre owned
				// and destroyed the object; only EXU's stale bookkeeping remains.
				return true;
			}

			try
			{
				const auto destroy = ResolveDestroyStaticGeometry();
				if (destroy == nullptr)
				{
					return false;
				}
				destroy(record.owner, record.geometry);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		void PushInfo(lua_State* L, const std::string& name, const Record& record)
		{
			lua_createtable(L, 0, 10);
			lua_pushlstring(L, name.data(), name.size());
			lua_setfield(L, -2, "name");
			lua_pushlstring(L, record.mesh.data(), record.mesh.size());
			lua_setfield(L, -2, "mesh");
			lua_pushlstring(L, record.material.data(), record.material.size());
			lua_setfield(L, -2, "material");
			lua_pushinteger(L, static_cast<lua_Integer>(record.instanceCount));
			lua_setfield(L, -2, "instanceCount");
			lua_pushinteger(L, static_cast<lua_Integer>(record.regionCount));
			lua_setfield(L, -2, "regionCount");
			lua_pushnumber(L, static_cast<lua_Number>(record.buildMilliseconds));
			lua_setfield(L, -2, "buildMilliseconds");
			lua_pushnumber(L, record.renderingDistance);
			lua_setfield(L, -2, "renderingDistance");
			lua_pushboolean(L, record.castShadows ? 1 : 0);
			lua_setfield(L, -2, "castShadows");
			lua_pushboolean(L, record.visible ? 1 : 0);
			lua_setfield(L, -2, "visible");
		}

		size_t CountRegions(const std::vector<Instance>& instances, const Options& options)
		{
			std::set<std::tuple<int64_t, int64_t, int64_t>> regions;
			for (const auto& instance : instances)
			{
				regions.emplace(
					static_cast<int64_t>(std::floor((instance.position.x - options.origin.x) / options.regionDimensions.x)),
					static_cast<int64_t>(std::floor((instance.position.y - options.origin.y) / options.regionDimensions.y)),
					static_cast<int64_t>(std::floor((instance.position.z - options.origin.z) / options.regionDimensions.z)));
			}
			return regions.size();
		}

		int PushFailure(lua_State* L, const std::string& message)
		{
			lua_pushnil(L);
			lua_pushlstring(L, message.data(), message.size());
			return 2;
		}
	}

	int Create(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const std::string mesh = luaL_checkstring(L, 2);
		const std::string material = lua_isnoneornil(L, 3) ? "" : luaL_checkstring(L, 3);
		luaL_checktype(L, 4, LUA_TTABLE);

		if (name.empty() || name.size() > kMaxNameLength)
		{
			return luaL_argerror(L, 1, "name must contain 1-128 characters");
		}
		if (mesh.empty())
		{
			return luaL_argerror(L, 2, "mesh must not be empty");
		}

		const size_t instanceCount = lua_objlen(L, 4);
		if (instanceCount == 0 || instanceCount > kMaxInstances)
		{
			return luaL_argerror(L, 4, "instances must contain 1-100000 entries");
		}

		std::vector<Instance> instances;
		instances.reserve(instanceCount);
		for (size_t index = 1; index <= instanceCount; ++index)
		{
			lua_rawgeti(L, 4, static_cast<int>(index));
			if (!lua_istable(L, -1))
			{
				return luaL_argerror(L, 4, "each instance must be a table");
			}
			instances.push_back(ReadInstance(L, -1));
			lua_pop(L, 1);
		}
		const Options options = ReadOptions(L, 5);

		auto* sceneManager = CurrentSceneManager();
		if (sceneManager == nullptr)
		{
			return PushFailure(L, "Ogre SceneManager is unavailable");
		}

		// Lua hands us simulation coordinates -- the ones GetPosition and
		// GetTerrainHeightAndNormal return -- so convert every transform into
		// render space before Ogre sees it, exactly as Environment.cpp does for
		// particles. Fail the call rather than silently building geometry that
		// cannot be drawn.
		OgreVector3Value renderOrigin{};
		if (!TryReadWorldRenderOrigin(renderOrigin) ||
			!IsFinite(renderOrigin.x) || !IsFinite(renderOrigin.y) || !IsFinite(renderOrigin.z))
		{
			return PushFailure(L, "Redux render origin is unavailable");
		}

		Options renderOptions = options;
		renderOptions.origin =
			OgreRenderSpace::SimPositionToRender(options.origin, renderOrigin);
		for (auto& instance : instances)
		{
			instance.position =
				OgreRenderSpace::SimPositionToRender(instance.position, renderOrigin);
			instance.orientation =
				OgreRenderSpace::SimOrientationToRender(instance.orientation);
		}

		auto existing = g_records.find(name);
		if (existing != g_records.end())
		{
			if (!DestroyRecord(existing->second))
			{
				return PushFailure(L, "existing StaticGeometry could not be destroyed");
			}
			g_records.erase(existing);
		}
		else if (ResolveHasStaticGeometry() == nullptr)
		{
			return PushFailure(L, "required Ogre StaticGeometry exports are unavailable");
		}
		else if (ResolveHasStaticGeometry()(sceneManager, name))
		{
			return PushFailure(L, "StaticGeometry name is already owned outside EXU");
		}

		void* geometry = nullptr;
		void* entity = nullptr;
		try
		{
			const auto createGeometry = ResolveCreateStaticGeometry();
			const auto setRegionDimensions = ResolveSetRegionDimensions();
			const auto setOrigin = ResolveSetOrigin();
			const auto setRenderingDistance = ResolveSetRenderingDistance();
			const auto setCastShadows = ResolveSetCastShadows();
			const auto setVisible = ResolveSetVisible();
			const auto createEntity = ResolveCreateEntity();
			const auto destroyEntity = ResolveDestroyEntity();
			const auto addEntity = ResolveAddEntity();
			const auto build = ResolveBuild();
			if (createGeometry == nullptr || setRegionDimensions == nullptr || setOrigin == nullptr ||
				setRenderingDistance == nullptr || setCastShadows == nullptr || setVisible == nullptr ||
				createEntity == nullptr || destroyEntity == nullptr || addEntity == nullptr || build == nullptr ||
				ResolveDestroyStaticGeometry() == nullptr)
			{
				throw std::runtime_error("required Ogre StaticGeometry exports are unavailable");
			}

			geometry = createGeometry(sceneManager, name);
			setRegionDimensions(geometry, renderOptions.regionDimensions);
			setOrigin(geometry, renderOptions.origin);
			setRenderingDistance(geometry, renderOptions.renderingDistance);
			setCastShadows(geometry, renderOptions.castShadows);
			setVisible(geometry, renderOptions.visible);

			entity = createEntity(sceneManager, mesh);
			if (!material.empty())
			{
				const auto setMaterialName = ResolveEntitySetMaterialName();
				if (setMaterialName == nullptr)
				{
					throw std::runtime_error("Ogre Entity::setMaterialName export is unavailable");
				}
				setMaterialName(entity, material, "Autodetect");
			}

			for (const auto& instance : instances)
			{
				addEntity(geometry, entity, instance.position, instance.orientation, instance.scale);
			}
			destroyEntity(sceneManager, entity);
			entity = nullptr;

			const uint64_t startedAt = GetTickCount64();
			build(geometry);
			const uint64_t buildMilliseconds = GetTickCount64() - startedAt;

			Record record;
			record.owner = sceneManager;
			record.geometry = geometry;
			record.mesh = mesh;
			record.material = material;
			record.instanceCount = instances.size();
			record.regionCount = CountRegions(instances, renderOptions);
			record.buildMilliseconds = buildMilliseconds;
			record.renderingDistance = options.renderingDistance;
			record.castShadows = options.castShadows;
			record.visible = options.visible;

			const auto inserted = g_records.emplace(name, std::move(record));
			// The render origin is logged because a zero or stale one is the
			// difference between geometry that draws and geometry that is built
			// perfectly and sits outside the render world.
			Logging::LogMessage(
				"[EXU::StaticGeometry] built name=%s mesh=%s material=%s instances=%u regions=%u "
				"buildMs=%llu renderOrigin=(%.2f,%.2f,%.2f) firstRenderPos=(%.2f,%.2f,%.2f)",
				name.c_str(),
				mesh.c_str(),
				material.empty() ? "<mesh-default>" : material.c_str(),
				static_cast<unsigned int>(instances.size()),
				static_cast<unsigned int>(inserted.first->second.regionCount),
				static_cast<unsigned long long>(buildMilliseconds),
				static_cast<double>(renderOrigin.x),
				static_cast<double>(renderOrigin.y),
				static_cast<double>(renderOrigin.z),
				static_cast<double>(instances.front().position.x),
				static_cast<double>(instances.front().position.y),
				static_cast<double>(instances.front().position.z));
			PushInfo(L, name, inserted.first->second);
			return 1;
		}
		catch (const std::exception& error)
		{
			if (entity != nullptr)
			{
				try { ResolveDestroyEntity()(sceneManager, entity); } catch (...) {}
			}
			if (geometry != nullptr)
			{
				try { ResolveDestroyStaticGeometry()(sceneManager, geometry); } catch (...) {}
			}
			return PushFailure(L, error.what());
		}
		catch (...)
		{
			if (entity != nullptr)
			{
				try { ResolveDestroyEntity()(sceneManager, entity); } catch (...) {}
			}
			if (geometry != nullptr)
			{
				try { ResolveDestroyStaticGeometry()(sceneManager, geometry); } catch (...) {}
			}
			return PushFailure(L, "Ogre rejected the StaticGeometry request");
		}
	}

	int Destroy(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const auto found = g_records.find(name);
		if (found == g_records.end())
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		const bool destroyed = DestroyRecord(found->second);
		if (destroyed)
		{
			Logging::LogMessage("[EXU::StaticGeometry] destroyed name=%s", name.c_str());
			g_records.erase(found);
		}
		lua_pushboolean(L, destroyed ? 1 : 0);
		return 1;
	}

	int DestroyAll(lua_State* L)
	{
		const size_t count = g_records.size();
		Shutdown();
		lua_pushinteger(L, static_cast<lua_Integer>(count));
		return 1;
	}

	int GetInfo(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const auto found = g_records.find(name);
		if (found == g_records.end())
		{
			lua_pushnil(L);
			return 1;
		}
		PushInfo(L, name, found->second);
		return 1;
	}

	int SetVisible(lua_State* L)
	{
		const std::string name = luaL_checkstring(L, 1);
		const bool visible = CheckBool(L, 2);
		const auto found = g_records.find(name);
		if (found == g_records.end() || CurrentSceneManager() != found->second.owner)
		{
			lua_pushboolean(L, 0);
			return 1;
		}
		try
		{
			const auto setVisible = ResolveSetVisible();
			if (setVisible == nullptr)
			{
				lua_pushboolean(L, 0);
				return 1;
			}
			setVisible(found->second.geometry, visible);
			found->second.visible = visible;
			lua_pushboolean(L, 1);
			return 1;
		}
		catch (...)
		{
			lua_pushboolean(L, 0);
			return 1;
		}
	}

	void Shutdown() noexcept
	{
		const size_t count = g_records.size();
		for (auto& entry : g_records)
		{
			DestroyRecord(entry.second);
		}
		g_records.clear();
		if (count > 0)
		{
			Logging::LogMessage(
				"[EXU::StaticGeometry] shutdown cleared %u tracked object(s)",
				static_cast<unsigned int>(count));
		}
	}
}
