/* Copyright (C) 2026 GrizzlyOne95
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

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Game/MuzzleFlash.h"
#include "Game/MuzzleFlashCore.h"

#include "LuaCppBarrier.h"
#include "LuaHelpers.h"
#include "Ogre/OgreProc.h"
#include "Ogre/OgreRenderOrigin.h"
#include "Ogre/OgreRenderSpace.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace ExtraUtilities::Lua::MuzzleFlash
{
	namespace
	{
		namespace C = ::MuzzleFlashCore;
		using ExtraUtilities::OgreDll::OgreProc;

		constexpr size_t kDefaultMax = 48;
		constexpr size_t kHardMax = 256;
		constexpr const char* kParentNodeName = "__exu_flash_root";

		// ----- Ogre ABI (same exports as exu.casing) -------------------------

		struct OgreV3 { float x, y, z; };
		struct OgreQ { float w, x, y, z; };

		struct FrameEventAbi
		{
			float timeSinceLastEvent;
			float timeSinceLastFrame;
		};

		using RootGetSingletonFn = void* (*)();
		using RootFrameListenerFn = void(__thiscall*)(void*, void*);
		using CreateEntityFn = void* (__thiscall*)(void*, const std::string&);
		using DestroyEntityFn = void(__thiscall*)(void*, void*);
		using EntitySetMaterialNameFn = void(__thiscall*)(void*, const std::string&, const std::string&);
		using GetRootSceneNodeFn = void* (__thiscall*)(void*);
		using CreateNamedChildFn = void* (__thiscall*)(void*, const std::string&, const OgreV3&, const OgreQ&);
		using CreateChildFn = void* (__thiscall*)(void*, const OgreV3&, const OgreQ&);
		using DestroySceneNodePtrFn = void(__thiscall*)(void*, void*);
		using HasSceneNodeFn = bool(__thiscall*)(void*, const std::string&);
		using GetSceneNodeFn = void* (__thiscall*)(void*, const std::string&);
		using AttachObjectFn = void(__thiscall*)(void*, void*);
		using NodeSetVectorFn = void(__thiscall*)(void*, const OgreV3&);
		using NodeSetQuaternionFn = void(__thiscall*)(void*, const OgreQ&);
		using MovableSetBoolFn = void(__thiscall*)(void*, bool);

		constinit OgreProc<RootGetSingletonFn> g_rootGetSingleton{ "?getSingletonPtr@Root@Ogre@@SAPAV12@XZ" };
		constinit OgreProc<RootFrameListenerFn> g_rootAddFrameListener{ "?addFrameListener@Root@Ogre@@QAEXPAVFrameListener@2@@Z" };
		constinit OgreProc<RootFrameListenerFn> g_rootRemoveFrameListener{ "?removeFrameListener@Root@Ogre@@QAEXPAVFrameListener@2@@Z" };
		constinit OgreProc<CreateEntityFn> g_createEntity{
			"?createEntity@SceneManager@Ogre@@UAEPAVEntity@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z" };
		constinit OgreProc<DestroyEntityFn> g_destroyEntity{ "?destroyEntity@SceneManager@Ogre@@UAEXPAVEntity@2@@Z" };
		constinit OgreProc<EntitySetMaterialNameFn> g_entitySetMaterialName{
			"?setMaterialName@Entity@Ogre@@QAEXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z" };
		constinit OgreProc<GetRootSceneNodeFn> g_getRootSceneNode{ "?getRootSceneNode@SceneManager@Ogre@@UAEPAVSceneNode@2@XZ" };
		constinit OgreProc<CreateNamedChildFn> g_createNamedChild{
			"?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@ABVVector3@2@ABVQuaternion@2@@Z" };
		constinit OgreProc<CreateChildFn> g_createChild{
			"?createChildSceneNode@SceneNode@Ogre@@UAEPAV12@ABVVector3@2@ABVQuaternion@2@@Z" };
		constinit OgreProc<DestroySceneNodePtrFn> g_destroySceneNode{ "?destroySceneNode@SceneManager@Ogre@@UAEXPAVSceneNode@2@@Z" };
		constinit OgreProc<HasSceneNodeFn> g_hasSceneNode{
			"?hasSceneNode@SceneManager@Ogre@@UBE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z" };
		constinit OgreProc<GetSceneNodeFn> g_getSceneNode{
			"?getSceneNode@SceneManager@Ogre@@UBEPAVSceneNode@2@ABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z" };
		constinit OgreProc<AttachObjectFn> g_attachObject{ "?attachObject@SceneNode@Ogre@@UAEXPAVMovableObject@2@@Z" };
		constinit OgreProc<NodeSetVectorFn> g_nodeSetPosition{ "?setPosition@Node@Ogre@@UAEXABVVector3@2@@Z" };
		constinit OgreProc<NodeSetQuaternionFn> g_nodeSetOrientation{ "?setOrientation@Node@Ogre@@UAEXABVQuaternion@2@@Z" };
		constinit OgreProc<NodeSetVectorFn> g_nodeSetScale{ "?setScale@Node@Ogre@@UAEXABVVector3@2@@Z" };
		constinit OgreProc<MovableSetBoolFn> g_setCastShadows{ "?setCastShadows@MovableObject@Ogre@@QAEX_N@Z" };

		bool OgreExportsAvailable() noexcept
		{
			return g_rootGetSingleton && g_rootAddFrameListener && g_rootRemoveFrameListener && g_createEntity &&
				g_destroyEntity && g_entitySetMaterialName && g_getRootSceneNode && g_createNamedChild && g_createChild &&
				g_destroySceneNode && g_hasSceneNode && g_getSceneNode && g_attachObject && g_nodeSetPosition &&
				g_nodeSetOrientation && g_nodeSetScale && g_setCastShadows;
		}

		// ----- Engine reads (qualified build only) -------------------------

		constexpr uintptr_t kGoObj = 0xF4;         // OBJ76*
		constexpr uintptr_t kObjMatrix = 0x20;     // right, up, front (floats)
		constexpr uintptr_t kObjPosit = 0x48;      // double[3]

		struct TimeSample
		{
			float simTime = 0.0f;
			bool paused = false;
			bool valid = false;
		};

		TimeSample ReadTime() noexcept
		{
			TimeSample sample;
			__try
			{
				sample.simTime = *reinterpret_cast<const float*>(EngineAddresses::Time::SimTimeSeconds);
				sample.paused = *reinterpret_cast<const int32_t*>(EngineAddresses::Time::Paused) != 0;
				sample.valid = std::isfinite(sample.simTime);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				sample.valid = false;
			}
			return sample;
		}

		void* ReadSceneManager() noexcept
		{
			__try
			{
				void* structure = *BZR::Ogre::sceneManagerStructure;
				if (structure == nullptr)
				{
					return nullptr;
				}
				return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(structure) + BZR::Ogre::sceneManagerOffset);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return nullptr;
			}
		}

		// The live simulation pose of a GameObject handle; false once the handle
		// is stale (its arena slot was recycled) or the object has no OBJ.
		bool ReadOwnerPose(uint32_t handle, C::Pose& out) noexcept
		{
			const uint32_t index = (handle >> 20) & 0xFFFu;
			const uint32_t serial = handle & 0xFFFFFu;
			if (handle == 0 || index >= BZR::GameObject::kArenaSlotCount)
			{
				return false;
			}
			float m[9] = {};
			double p[3] = {};
			__try
			{
				const uintptr_t go = BZR::GameObject::kArenaBase + static_cast<uintptr_t>(index) * BZR::GameObject::kArenaSlotSize;
				if (*reinterpret_cast<const uint32_t*>(go + BZR::GameObject::kSerialOffset) != serial)
				{
					return false;
				}
				const uint8_t* obj = *reinterpret_cast<uint8_t* const*>(go + kGoObj);
				if (obj == nullptr)
				{
					return false;
				}
				std::memcpy(m, obj + kObjMatrix, sizeof(m));
				std::memcpy(p, obj + kObjPosit, sizeof(p));
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
			out.position = { static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]) };
			out.orientation = C::FromAxes({ m[0], m[1], m[2] }, { m[3], m[4], m[5] }, { m[6], m[7], m[8] });
			return C::IsFinite(out) && C::Length(out.position) < 1.0e6f;
		}

		// ----- Pool --------------------------------------------------------

		struct Slot
		{
			bool live = false;
			uint32_t id = 0;
			C::Definition def;
			float age = 0.0f;
			uint32_t owner = 0;     // 0: world-fixed
			C::Pose local;          // relative to the owner, or the world pose
			C::Pose last;           // last world pose drawn
			void* entity = nullptr;
			void* node = nullptr;
		};

		struct State
		{
			std::vector<Slot> slots;
			size_t max = kDefaultMax;
			uint32_t nextId = 1;
			void* sceneManager = nullptr;
			void* parentNode = nullptr;
			std::string parentName;
			bool listenerRegistered = false;
			void* listenerRoot = nullptr;
			std::unordered_set<std::string> badMeshes;
			C::SimClock clock;
			uint64_t spawned = 0;
			uint64_t recycled = 0;
			uint64_t expired = 0;
			uint64_t failed = 0;
			uint64_t frames = 0;
			uint64_t forgotten = 0;
			uint64_t orphaned = 0;   // owner died mid-flash; finished world-fixed
			double frameMicrosTotal = 0.0;
			double frameMicrosMax = 0.0;
			bool faulted = false;
		};

		State& S()
		{
			static State flashState;
			return flashState;
		}

		size_t LiveCount()
		{
			size_t n = 0;
			for (const Slot& slot : S().slots)
			{
				n += slot.live ? 1 : 0;
			}
			return n;
		}

		OgreV3 ToRenderPosition(C::V3 sim, const BZR::VECTOR_3D& origin)
		{
			return OgreRenderSpace::SimPositionToRender(OgreV3{ sim.x, sim.y, sim.z }, OgreV3{ origin.x, origin.y, origin.z });
		}

		OgreQ ToRenderOrientation(const C::Quat& q)
		{
			return OgreRenderSpace::SimOrientationToRender(OgreQ{ q.w, q.x, q.y, q.z });
		}

		// ----- Ogre operations (all behind Seh::Guard) ---------------------

		bool ParentNodeAlive(void* sceneManager, void* parent, const std::string& name)
		{
			bool alive = false;
			Seh::Guard("MuzzleFlash::ParentNodeAlive", [&] {
				alive = g_hasSceneNode.Get()(sceneManager, name) && g_getSceneNode.Get()(sceneManager, name) == parent;
			});
			return alive;
		}

		void* EnsureParentNode(void* sceneManager)
		{
			State& s = S();
			if (s.parentNode != nullptr && s.sceneManager == sceneManager && ParentNodeAlive(sceneManager, s.parentNode, s.parentName))
			{
				return s.parentNode;
			}
			s.parentNode = nullptr;
			if (s.parentName.empty())
			{
				char name[64];
				std::snprintf(name, sizeof(name), "%s_%08X", kParentNodeName, static_cast<unsigned>(GetTickCount()));
				s.parentName = name;
			}
			void* parent = nullptr;
			Seh::Guard("MuzzleFlash::EnsureParentNode", [&] {
				if (g_hasSceneNode.Get()(sceneManager, s.parentName))
				{
					parent = g_getSceneNode.Get()(sceneManager, s.parentName);
					return;
				}
				void* root = g_getRootSceneNode.Get()(sceneManager);
				if (root != nullptr)
				{
					parent = g_createNamedChild.Get()(root, s.parentName, OgreV3{ 0.0f, 0.0f, 0.0f }, OgreQ{ 1.0f, 0.0f, 0.0f, 0.0f });
				}
			});
			s.parentNode = parent;
			s.sceneManager = parent != nullptr ? sceneManager : nullptr;
			return parent;
		}

		bool CreateVisual(void* sceneManager, void* parent, const std::string& mesh, const std::string& material,
			OgreV3 position, OgreQ orientation, float scale, void*& outEntity, void*& outNode)
		{
			outEntity = nullptr;
			outNode = nullptr;
			void* entity = nullptr;
			void* node = nullptr;
			const std::string group = "Autodetect";
			const bool ok = Seh::Guard("MuzzleFlash::CreateVisual", [&] {
				entity = g_createEntity.Get()(sceneManager, mesh);
				if (entity == nullptr)
				{
					return;
				}
				if (!material.empty())
				{
					g_entitySetMaterialName.Get()(entity, material, group);
				}
				g_setCastShadows.Get()(entity, false);
				node = g_createChild.Get()(parent, position, orientation);
				if (node == nullptr)
				{
					return;
				}
				g_nodeSetScale.Get()(node, OgreV3{ scale, scale, scale });
				g_attachObject.Get()(node, entity);
			});
			if (ok && entity != nullptr && node != nullptr)
			{
				outEntity = entity;
				outNode = node;
				return true;
			}
			Seh::Guard("MuzzleFlash::CreateVisualUndo", [&] {
				if (entity != nullptr)
				{
					g_destroyEntity.Get()(sceneManager, entity);
				}
				if (node != nullptr)
				{
					g_destroySceneNode.Get()(sceneManager, node);
				}
			});
			return false;
		}

		void DestroyVisual(void* sceneManager, Slot& slot)
		{
			if (slot.entity == nullptr && slot.node == nullptr)
			{
				return;
			}
			void* entity = slot.entity;
			void* node = slot.node;
			Seh::Guard("MuzzleFlash::DestroyVisual", [&] {
				if (entity != nullptr)
				{
					g_destroyEntity.Get()(sceneManager, entity);
				}
				if (node != nullptr)
				{
					g_destroySceneNode.Get()(sceneManager, node);
				}
			});
			slot.entity = nullptr;
			slot.node = nullptr;
		}

		bool PlaceNode(void* node, OgreV3 position, OgreQ orientation, float scale) noexcept
		{
			__try
			{
				g_nodeSetPosition.Get()(node, position);
				g_nodeSetOrientation.Get()(node, orientation);
				g_nodeSetScale.Get()(node, OgreV3{ scale, scale, scale });
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Drops every flash WITHOUT calling Ogre: their scene is gone.
		void ForgetAll(const char* reason)
		{
			State& s = S();
			size_t n = 0;
			for (Slot& slot : s.slots)
			{
				n += slot.live ? 1 : 0;
				slot = Slot{};
			}
			s.parentNode = nullptr;
			s.sceneManager = nullptr;
			s.forgotten += n;
			if (n > 0)
			{
				Logging::LogMessage("[EXU::Flash] forgot %u flash(es) without touching Ogre (%s)", static_cast<unsigned>(n), reason);
			}
		}

		void DestroyAll(bool destroyParent)
		{
			State& s = S();
			void* current = ReadSceneManager();
			if (s.sceneManager == nullptr || current != s.sceneManager || s.parentNode == nullptr ||
				!ParentNodeAlive(s.sceneManager, s.parentNode, s.parentName))
			{
				ForgetAll(s.sceneManager == nullptr ? "no scene" : "scene changed or cleared");
				return;
			}
			for (Slot& slot : s.slots)
			{
				if (slot.live)
				{
					DestroyVisual(s.sceneManager, slot);
				}
				slot = Slot{};
			}
			if (destroyParent)
			{
				void* sceneManager = s.sceneManager;
				void* parent = s.parentNode;
				Seh::Guard("MuzzleFlash::DestroyParent", [&] { g_destroySceneNode.Get()(sceneManager, parent); });
				s.parentNode = nullptr;
				s.sceneManager = nullptr;
			}
		}

		// World pose of a flash this frame. An owner that has gone leaves the
		// flash where it last was (world-fixed) for the rest of its life.
		C::Pose CurrentPose(Slot& slot)
		{
			if (slot.owner != 0)
			{
				C::Pose owner;
				if (ReadOwnerPose(slot.owner, owner))
				{
					return C::Compose(owner, slot.local);
				}
				slot.owner = 0;
				slot.local = slot.last;
				++S().orphaned;
			}
			return slot.local;
		}

		// ----- Frame listener ----------------------------------------------

		void OnFrame()
		{
			State& s = S();
			LARGE_INTEGER t0{}, t1{}, freq{};
			QueryPerformanceCounter(&t0);

			if (LiveCount() == 0)
			{
				s.clock.Reset();
				return;
			}
			if (s.sceneManager == nullptr || ReadSceneManager() != s.sceneManager || s.parentNode == nullptr)
			{
				ForgetAll("scene changed");
				return;
			}

			const TimeSample time = ReadTime();
			const float dt = s.clock.Advance(time.simTime, time.paused, time.valid);

			BZR::VECTOR_3D origin{};
			if (!OgreRenderSpace::TryReadWorldRenderOrigin(origin) || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
				!std::isfinite(origin.z))
			{
				return;
			}

			for (Slot& slot : s.slots)
			{
				if (!slot.live)
				{
					continue;
				}
				slot.age += dt;
				if (C::Expired(slot.def, slot.age))
				{
					DestroyVisual(s.sceneManager, slot);
					slot = Slot{};
					++s.expired;
					continue;
				}
				const C::Pose pose = CurrentPose(slot);
				slot.last = pose;
				if (!PlaceNode(slot.node, ToRenderPosition(pose.position, origin), ToRenderOrientation(pose.orientation),
					C::ScaleAt(slot.def, slot.age)))
				{
					ForgetAll("node update faulted");
					return;
				}
			}

			++s.frames;
			QueryPerformanceCounter(&t1);
			QueryPerformanceFrequency(&freq);
			const double micros = freq.QuadPart > 0 ? static_cast<double>(t1.QuadPart - t0.QuadPart) * 1.0e6 / static_cast<double>(freq.QuadPart) : 0.0;
			s.frameMicrosTotal += micros;
			s.frameMicrosMax = std::max(s.frameMicrosMax, micros);
		}

		// Same vtable shape as Ogre::FrameListener (1.10, no base class):
		// frameStarted, frameRenderingQueued, frameEnded, virtual destructor.
		// Every entry returns true: false from frameStarted stops rendering.
		class FlashFrameListener
		{
		public:
			virtual bool frameStarted(const FrameEventAbi&)
			{
				if (!S().faulted)
				{
					const bool completed = Seh::Guard("MuzzleFlash::frameStarted", [&] { OnFrame(); },
						[](unsigned long code) {
							Logging::LogMessage("[EXU::Flash] frame update faulted code=0x%08lX; flashes disabled for this mission", code);
						});
					if (!completed)
					{
						S().faulted = true;
						ForgetAll("frame update failed");
					}
				}
				return true;
			}
			virtual bool frameRenderingQueued(const FrameEventAbi&) { return true; }
			virtual bool frameEnded(const FrameEventAbi&) { return true; }
			virtual ~FlashFrameListener() = default;
		};

		FlashFrameListener g_listener;

		bool RegisterListener()
		{
			State& s = S();
			if (s.listenerRegistered)
			{
				return true;
			}
			void* root = nullptr;
			Seh::Guard("MuzzleFlash::RegisterListener", [&] {
				root = g_rootGetSingleton.Get()();
				if (root != nullptr)
				{
					g_rootAddFrameListener.Get()(root, &g_listener);
				}
			});
			s.listenerRegistered = root != nullptr;
			s.listenerRoot = root;
			s.clock.Reset();
			Logging::LogMessage("[EXU::Flash] frame listener %s root=%p", s.listenerRegistered ? "registered" : "FAILED", root);
			return s.listenerRegistered;
		}

		void UnregisterListener()
		{
			State& s = S();
			if (!s.listenerRegistered)
			{
				return;
			}
			void* root = s.listenerRoot;
			// Root::removeFrameListener only queues the pointer; Ogre drops it at
			// the start of the next frame without calling it.
			Seh::Guard("MuzzleFlash::UnregisterListener", [&] {
				void* current = g_rootGetSingleton.Get()();
				if (current != nullptr && current == root)
				{
					g_rootRemoveFrameListener.Get()(root, &g_listener);
				}
			});
			s.listenerRegistered = false;
			s.listenerRoot = nullptr;
			Logging::LogMessage("[EXU::Flash] frame listener removed");
		}

		// ----- Lua ---------------------------------------------------------

		bool Available()
		{
			return RuntimeGate::IsSupported() && OgreExportsAvailable();
		}

		float FieldNumber(lua_State* L, int table, const char* name, float fallback)
		{
			lua_getfield(L, table, name);
			const float value = lua_isnil(L, -1) ? fallback : static_cast<float>(luaL_checknumber(L, -1));
			lua_pop(L, 1);
			if (!std::isfinite(value))
			{
				luaL_error(L, "exu.flash.Spawn: %s must be finite", name);
			}
			return value;
		}

		const char* FieldString(lua_State* L, int table, const char* name)
		{
			lua_getfield(L, table, name);
			const char* value = lua_isnil(L, -1) ? nullptr : luaL_checkstring(L, -1);
			lua_pop(L, 1);   // the table keeps the string alive
			return value;
		}

		int PushFailure(lua_State* L, const char* message)
		{
			lua_pushnil(L);
			lua_pushstring(L, message);
			return 2;
		}

		Slot* AcquireSlot()
		{
			State& s = S();
			for (Slot& slot : s.slots)
			{
				if (!slot.live)
				{
					return &slot;
				}
			}
			if (s.slots.size() < s.max)
			{
				s.slots.emplace_back();
				return &s.slots.back();
			}
			// Full: recycle the flash nearest its end.
			Slot* best = nullptr;
			float bestLeft = 0.0f;
			for (Slot& slot : s.slots)
			{
				const float left = slot.def.duration - slot.age;
				if (slot.live && (best == nullptr || left < bestLeft))
				{
					best = &slot;
					bestLeft = left;
				}
			}
			if (best != nullptr)
			{
				DestroyVisual(s.sceneManager, *best);
				*best = Slot{};
				++s.recycled;
			}
			return best;
		}

		// exu.flash.Spawn{ transform=, mesh=, material=, owner=, duration=, startScale=, finishScale=, roll= }
		//   -> id | nil, err
		int Spawn(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			const int t = 1;

			// Parse everything first: a Lua error longjmps over this frame, so
			// nothing with a destructor exists until parsing is done.
			lua_getfield(L, t, "transform");
			if (lua_isnil(L, -1))
			{
				return luaL_argerror(L, 1, "transform is required");
			}
			const BZR::MAT_3D m = CheckMatrix(L, -1);
			lua_pop(L, 1);
			const char* meshArg = FieldString(L, t, "mesh");
			const char* materialArg = FieldString(L, t, "material");
			C::Definition def;
			def.duration = FieldNumber(L, t, "duration", 0.1f);
			def.startScale = FieldNumber(L, t, "startScale", 1.0f);
			def.finishScale = FieldNumber(L, t, "finishScale", def.startScale);
			const float roll = FieldNumber(L, t, "roll", 0.0f);
			uint32_t owner = 0;
			lua_getfield(L, t, "owner");
			if (!lua_isnil(L, -1))
			{
				owner = static_cast<uint32_t>(CheckHandle(L, -1));
			}
			lua_pop(L, 1);
			if (meshArg == nullptr || meshArg[0] == '\0')
			{
				return luaL_argerror(L, 1, "mesh is required");
			}

			// Nothing below raises a Lua error.
			State& s = S();
			if (!C::Sanitize(def))
			{
				return PushFailure(L, "flash has no visible size");
			}
			C::Pose world;
			world.position = { static_cast<float>(m.posit_x), static_cast<float>(m.posit_y), static_cast<float>(m.posit_z) };
			world.orientation = C::FromAxes({ m.right_x, m.right_y, m.right_z }, { m.up_x, m.up_y, m.up_z }, { m.front_x, m.front_y, m.front_z });
			world = C::RollAboutFront(world, roll);
			if (!C::IsFinite(world))
			{
				return PushFailure(L, "transform is not finite");
			}
			if (!Available())
			{
				return PushFailure(L, "exu.flash is unavailable (unqualified build or missing Ogre exports)");
			}
			if (s.faulted)
			{
				return PushFailure(L, "exu.flash was disabled after a fault this mission (see exu.log)");
			}
			const std::string mesh = meshArg;
			const std::string material = materialArg != nullptr ? materialArg : "";
			if (s.badMeshes.count(mesh) != 0)
			{
				++s.failed;
				return PushFailure(L, "mesh failed to load earlier this mission");
			}

			void* sceneManager = ReadSceneManager();
			if (sceneManager == nullptr)
			{
				++s.failed;
				return PushFailure(L, "Ogre SceneManager is unavailable");
			}
			if (s.sceneManager != nullptr && s.sceneManager != sceneManager)
			{
				ForgetAll("scene changed");
			}
			void* parent = EnsureParentNode(sceneManager);
			if (parent == nullptr)
			{
				++s.failed;
				return PushFailure(L, "could not create the flash parent node");
			}
			if (!RegisterListener())
			{
				++s.failed;
				return PushFailure(L, "could not register the Ogre frame listener");
			}
			BZR::VECTOR_3D origin{};
			if (!OgreRenderSpace::TryReadWorldRenderOrigin(origin) || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
				!std::isfinite(origin.z))
			{
				++s.failed;
				return PushFailure(L, "Redux render origin is unavailable");
			}

			// Carry the flash with its owner: store the muzzle relative to it.
			C::Pose local = world;
			C::Pose ownerPose;
			if (owner != 0 && ReadOwnerPose(owner, ownerPose))
			{
				local = C::Relative(ownerPose, world);
			}
			else
			{
				owner = 0;
			}

			Slot* slot = AcquireSlot();
			if (slot == nullptr)
			{
				++s.failed;
				return PushFailure(L, "flash pool is full");
			}
			void* entity = nullptr;
			void* node = nullptr;
			if (!CreateVisual(sceneManager, parent, mesh, material, ToRenderPosition(world.position, origin),
				ToRenderOrientation(world.orientation), C::ScaleAt(def, 0.0f), entity, node))
			{
				s.badMeshes.insert(mesh);
				++s.failed;
				Logging::LogMessage("[EXU::Flash] could not create mesh=%s material=%s; further spawns of it are refused this mission",
					mesh.c_str(), material.empty() ? "<mesh-default>" : material.c_str());
				return PushFailure(L, "Ogre could not create the flash entity (see exu.log)");
			}

			*slot = Slot{};
			slot->live = true;
			slot->id = s.nextId++;
			if (s.nextId == 0)
			{
				s.nextId = 1;
			}
			slot->def = def;
			slot->owner = owner;
			slot->local = local;
			slot->last = world;
			slot->entity = entity;
			slot->node = node;
			++s.spawned;
			if (s.spawned <= 3)
			{
				Logging::LogMessage(
					"[EXU::Flash] spawned id=%u mesh=%s material=%s pos=(%.2f,%.2f,%.2f) owner=0x%08X duration=%.3f scale=%.2f->%.2f live=%u/%u",
					slot->id, mesh.c_str(), material.empty() ? "<mesh-default>" : material.c_str(), world.position.x, world.position.y,
					world.position.z, owner, def.duration, def.startScale, def.finishScale, static_cast<unsigned>(LiveCount()),
					static_cast<unsigned>(s.max));
			}
			lua_pushinteger(L, static_cast<lua_Integer>(slot->id));
			return 1;
		}

		int SetMax(lua_State* L)
		{
			const lua_Integer requested = luaL_checkinteger(L, 1);
			State& s = S();
			if (LiveCount() > 0 && (ReadSceneManager() != s.sceneManager || s.parentNode == nullptr ||
				!ParentNodeAlive(s.sceneManager, s.parentNode, s.parentName)))
			{
				ForgetAll("scene changed or cleared");
			}
			s.max = static_cast<size_t>(std::min<lua_Integer>(std::max<lua_Integer>(requested, 0), static_cast<lua_Integer>(kHardMax)));
			while (LiveCount() > s.max)
			{
				Slot* victim = nullptr;
				for (Slot& slot : s.slots)
				{
					if (slot.live && (victim == nullptr || slot.age > victim->age))
					{
						victim = &slot;
					}
				}
				if (victim == nullptr)
				{
					break;
				}
				DestroyVisual(s.sceneManager, *victim);
				*victim = Slot{};
				++s.recycled;
			}
			std::stable_partition(s.slots.begin(), s.slots.end(), [](const Slot& slot) { return slot.live; });
			if (s.slots.size() > s.max)
			{
				s.slots.resize(s.max);
			}
			lua_pushinteger(L, static_cast<lua_Integer>(s.max));
			return 1;
		}

		int GetMax(lua_State* L)
		{
			lua_pushinteger(L, static_cast<lua_Integer>(S().max));
			return 1;
		}

		int Clear(lua_State* L)
		{
			const size_t n = LiveCount();
			DestroyAll(false);
			lua_pushinteger(L, static_cast<lua_Integer>(n));
			return 1;
		}

		int GetCount(lua_State* L)
		{
			lua_pushinteger(L, static_cast<lua_Integer>(LiveCount()));
			return 1;
		}

		int GetStats(lua_State* L)
		{
			const State& s = S();
			lua_createtable(L, 0, 11);
			auto set = [L](const char* key, double value) {
				lua_pushnumber(L, value);
				lua_setfield(L, -2, key);
			};
			set("live", static_cast<double>(LiveCount()));
			set("max", static_cast<double>(s.max));
			set("spawned", static_cast<double>(s.spawned));
			set("recycled", static_cast<double>(s.recycled));
			set("expired", static_cast<double>(s.expired));
			set("failed", static_cast<double>(s.failed));
			set("forgotten", static_cast<double>(s.forgotten));
			set("orphaned", static_cast<double>(s.orphaned));
			set("frames", static_cast<double>(s.frames));
			set("avgFrameMicros", s.frames > 0 ? s.frameMicrosTotal / static_cast<double>(s.frames) : 0.0);
			set("maxFrameMicros", s.frameMicrosMax);
			return 1;
		}

		int GetCapabilities(lua_State* L)
		{
			const bool available = Available();
			lua_createtable(L, 0, 5);
			lua_pushboolean(L, available ? 1 : 0);
			lua_setfield(L, -2, "flash");
			lua_pushboolean(L, available ? 1 : 0);
			lua_setfield(L, -2, "followsOwner");
			lua_pushboolean(L, available ? 1 : 0);
			lua_setfield(L, -2, "pauseAware");
			lua_pushinteger(L, static_cast<lua_Integer>(S().max));
			lua_setfield(L, -2, "max");
			lua_pushinteger(L, static_cast<lua_Integer>(kHardMax));
			lua_setfield(L, -2, "hardMax");
			return 1;
		}
	}

	void Install(lua_State* L)
	{
		lua_getglobal(L, "exu");
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			return;
		}

		lua_newtable(L);
		static const luaL_Reg functions[] = {
			{ "Spawn", &Spawn },
			{ "SetMax", &SetMax },
			{ "GetMax", &GetMax },
			{ "Clear", &Clear },
			{ "GetCount", &GetCount },
			{ "GetStats", &GetStats },
			{ "GetCapabilities", &GetCapabilities },
			{ nullptr, nullptr },
		};
		RegisterFunctions(L, nullptr, functions);
		lua_setfield(L, -2, "flash");
		lua_pop(L, 1);
	}

	void Shutdown() noexcept
	{
		try
		{
			State& s = S();
			const size_t live = LiveCount();
			if (s.listenerRegistered || live > 0 || s.parentNode != nullptr)
			{
				DestroyAll(true);
				UnregisterListener();
				Logging::LogMessage(
					"[EXU::Flash] shutdown: destroyed %u live; totals spawned=%llu recycled=%llu expired=%llu failed=%llu orphaned=%llu "
					"frames=%llu avgFrame=%.1fus maxFrame=%.1fus",
					static_cast<unsigned>(live), static_cast<unsigned long long>(s.spawned), static_cast<unsigned long long>(s.recycled),
					static_cast<unsigned long long>(s.expired), static_cast<unsigned long long>(s.failed),
					static_cast<unsigned long long>(s.orphaned), static_cast<unsigned long long>(s.frames),
					s.frames > 0 ? s.frameMicrosTotal / static_cast<double>(s.frames) : 0.0, s.frameMicrosMax);
			}
			s = State{};
		}
		catch (...)
		{
			OutputDebugStringA("ExtraUtilities: exception during muzzle flash shutdown\n");
		}
	}
}
