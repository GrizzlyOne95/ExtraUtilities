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

#include "Game/ShellCasings.h"
#include "Game/ShellCasingMath.h"

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

namespace ExtraUtilities::Lua::ShellCasings
{
	namespace
	{
		namespace M = ExtraUtilities::ShellCasingMath;
		using ExtraUtilities::OgreDll::OgreProc;

		constexpr size_t kDefaultMax = 32;
		constexpr size_t kHardMax = 256;
		constexpr const char* kDefaultMesh = "casing.mesh";
		constexpr const char* kParentNodeName = "__exu_casing_root";
		constexpr float kObstacleRefreshSeconds = 0.25f;
		constexpr float kObstacleReach = 40.0f;      // m beyond the casings' bound
		constexpr size_t kMaxObstacles = 24;
		constexpr float kMaxObstacleRadius = 80.0f;

		// ----- Ogre ABI ----------------------------------------------------

		struct OgreV3 { float x, y, z; };
		struct OgreQ { float w, x, y, z; };

		// Ogre::FrameEvent (Real = float in BZR's Ogre).
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

		// GameObject arena slot fields (GOG 2.2.301; see bzr.h and
		// Docs/Research/SHELL_CASINGS_RE_20261005.md).
		constexpr uintptr_t kGoEntity = 0xF0;      // tagENTITY*
		constexpr uintptr_t kGoObj = 0xF4;         // OBJ76*
		constexpr uintptr_t kGoClass = 0xF8;       // GameObjectClass*
		constexpr uintptr_t kGoPosition = 0x108;   // float VECTOR_3D
		constexpr uintptr_t kEntSphereRadius = 0x14;   // float, metres
		constexpr uintptr_t kClassBoxMin = 0x10C;      // float[3], object-local
		constexpr uintptr_t kClassBoxMax = 0x118;      // float[3]
		constexpr uintptr_t kClassSphereRadius = 0x130; // float; <= 0 until the box is built
		constexpr uintptr_t kObjMatrix = 0x20;         // right, up, front (floats)
		constexpr uintptr_t kObjPosit = 0x48;          // double[3]

		using TerrainHeightAtFn = double(__cdecl*)(double, double);

		float TerrainHeight(float x, float z) noexcept
		{
			double height = 0.0;
			__try
			{
				height = reinterpret_cast<TerrainHeightAtFn>(EngineAddresses::Terrain::HeightAt)(x, z);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return NAN;
			}
			return (std::isfinite(height) && std::fabs(height) < 1.0e6) ? static_cast<float>(height) : NAN;
		}

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

		uintptr_t SlotAddress(uint32_t index) noexcept
		{
			return BZR::GameObject::kArenaBase + static_cast<uintptr_t>(index) * BZR::GameObject::kArenaSlotSize;
		}

		uint32_t ReadSerial(uint32_t index) noexcept
		{
			return *reinterpret_cast<const uint32_t*>(SlotAddress(index) + BZR::GameObject::kSerialOffset);
		}

		struct Candidate
		{
			uint32_t index = 0;
			uint32_t serial = 0;
			float distSq = 0.0f;
			// Previous centre, for the obstacle velocity estimate.
			M::V3 lastCenter;
			bool hasLast = false;
		};

		uint32_t HandleOf(const Candidate& c) noexcept
		{
			return (c.index << 20) | c.serial;
		}

		// Collects live objects whose position lies within `reach` of `center`.
		// One pass over the static arena, raw reads only.
		size_t ScanArena(M::V3 center, float reach, Candidate* out, size_t capacity) noexcept
		{
			size_t count = 0;
			const float reachSq = reach * reach;
			__try
			{
				for (uint32_t index = 0; index < BZR::GameObject::kArenaSlotCount && count < capacity; ++index)
				{
					const uint32_t serial = ReadSerial(index);
					if (serial == 0)
					{
						continue;
					}
					const float* pos = reinterpret_cast<const float*>(SlotAddress(index) + kGoPosition);
					const float dx = pos[0] - center.x, dy = pos[1] - center.y, dz = pos[2] - center.z;
					const float distSq = dx * dx + dy * dy + dz * dz;
					if (!(distSq <= reachSq))
					{
						continue;
					}
					Candidate& c = out[count++];
					c = Candidate{};
					c.index = index;
					c.serial = serial;
					c.distSq = distSq;
				}
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
			}
			return count;
		}

		struct ObstacleRead
		{
			bool ok = false;
			bool box = false;
			M::V3 position;     // OBJ76 posit (sim)
			M::V3 axis[3];
			float boxMin[3] = {};
			float boxMax[3] = {};
			float sphereRadius = 0.0f;
		};

		// Reads one live object's collision shape. Fails for a recycled slot.
		ObstacleRead ReadObstacle(uint32_t index, uint32_t serial) noexcept
		{
			ObstacleRead r;
			__try
			{
				if (ReadSerial(index) != serial)
				{
					return r;
				}
				const uintptr_t go = SlotAddress(index);
				const uint8_t* obj = *reinterpret_cast<uint8_t* const*>(go + kGoObj);
				const uint8_t* ent = *reinterpret_cast<uint8_t* const*>(go + kGoEntity);
				const uint8_t* cls = *reinterpret_cast<uint8_t* const*>(go + kGoClass);
				if (obj == nullptr)
				{
					return r;
				}
				const float* m = reinterpret_cast<const float*>(obj + kObjMatrix);
				const double* p = reinterpret_cast<const double*>(obj + kObjPosit);
				r.axis[0] = { m[0], m[1], m[2] };
				r.axis[1] = { m[3], m[4], m[5] };
				r.axis[2] = { m[6], m[7], m[8] };
				r.position = { static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2]) };
				if (ent != nullptr)
				{
					r.sphereRadius = *reinterpret_cast<const float*>(ent + kEntSphereRadius);
				}
				if (cls != nullptr && *reinterpret_cast<const float*>(cls + kClassSphereRadius) > 0.0f)
				{
					std::memcpy(r.boxMin, cls + kClassBoxMin, sizeof(r.boxMin));
					std::memcpy(r.boxMax, cls + kClassBoxMax, sizeof(r.boxMax));
					r.box = true;
				}
				r.ok = true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				r.ok = false;
			}
			return r;
		}

		// Builds a world obstacle from what ReadObstacle found, rejecting
		// anything implausible (a box larger than kMaxObstacleRadius, a
		// non-orthonormal matrix, NaN). Prefers the class model box, falls back
		// to the entity bounding sphere around the position.
		bool BuildObstacle(const ObstacleRead& r, uint32_t handle, M::Obstacle& out) noexcept
		{
			if (!r.ok || !M::IsFinite(r.position))
			{
				return false;
			}
			out = M::Obstacle{};
			out.handle = handle;
			bool boxOk = r.box;
			M::V3 half{};
			M::V3 localCenter{};
			if (boxOk)
			{
				half = { (r.boxMax[0] - r.boxMin[0]) * 0.5f, (r.boxMax[1] - r.boxMin[1]) * 0.5f, (r.boxMax[2] - r.boxMin[2]) * 0.5f };
				localCenter = { (r.boxMax[0] + r.boxMin[0]) * 0.5f, (r.boxMax[1] + r.boxMin[1]) * 0.5f, (r.boxMax[2] + r.boxMin[2]) * 0.5f };
				boxOk = M::IsFinite(half) && M::IsFinite(localCenter) && half.x > 0.02f && half.y > 0.02f && half.z > 0.02f &&
					M::Length(half) < kMaxObstacleRadius && M::Length(localCenter) < kMaxObstacleRadius;
				for (int i = 0; i < 3 && boxOk; ++i)
				{
					const float length = M::Length(r.axis[i]);
					boxOk = std::isfinite(length) && std::fabs(length - 1.0f) < 0.05f;
				}
			}
			if (boxOk)
			{
				out.box = true;
				for (int i = 0; i < 3; ++i)
				{
					out.axis[i] = M::NormalizeOr(r.axis[i], out.axis[i]);
				}
				out.half = half;
				out.center = M::Add(r.position, M::Add(M::Add(M::Scale(out.axis[0], localCenter.x), M::Scale(out.axis[1], localCenter.y)),
					M::Scale(out.axis[2], localCenter.z)));
				out.radius = M::Length(half);
				return true;
			}
			if (std::isfinite(r.sphereRadius) && r.sphereRadius > 0.05f && r.sphereRadius < kMaxObstacleRadius)
			{
				out.center = r.position;
				out.radius = r.sphereRadius;
				return true;
			}
			return false;
		}

		// ----- Pool --------------------------------------------------------

		struct Slot
		{
			bool live = false;
			uint32_t id = 0;
			M::Body body;
			void* entity = nullptr;
			void* node = nullptr;
			float scale = 1.0f;
			bool moved = true;
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
			std::vector<Candidate> candidates;
			std::vector<Candidate> scratch;   // arena scan buffer, kept to avoid a per-refresh allocation
			std::vector<M::Obstacle> obstacles;
			float sinceRefresh = kObstacleRefreshSeconds;
			float lastSimTime = 0.0f;
			bool haveSimTime = false;
			// Stats.
			uint64_t spawned = 0;
			uint64_t recycled = 0;
			uint64_t expired = 0;
			uint64_t failed = 0;
			uint64_t frames = 0;
			uint64_t forgotten = 0;
			double frameMicrosTotal = 0.0;
			double frameMicrosMax = 0.0;
			uint32_t obstacleLogs = 0;
			bool loggedFirstFrame = false;
			bool faulted = false;
		};

		State& S()
		{
			static State casingState;
			return casingState;
		}

		M::Tuning& Tuning()
		{
			static M::Tuning tuning;
			return tuning;
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

		OgreV3 ToRenderPosition(M::V3 sim, const BZR::VECTOR_3D& origin)
		{
			const OgreV3 simValue{ sim.x, sim.y, sim.z };
			const OgreV3 originValue{ origin.x, origin.y, origin.z };
			return OgreRenderSpace::SimPositionToRender(simValue, originValue);
		}

		OgreQ ToRenderOrientation(const M::Quat& q)
		{
			return OgreRenderSpace::SimOrientationToRender(OgreQ{ q.w, q.x, q.y, q.z });
		}

		// ----- Ogre operations (all behind Seh::Guard) ---------------------

		// The parent node is how a cleared scene is detected: every casing node
		// hangs under one named node, and if that name has gone from the scene
		// manager (clearScene, a new scene) every pointer below it is stale and
		// is forgotten without being touched.
		bool ParentNodeAlive(void* sceneManager, void* parent, const std::string& name)
		{
			bool alive = false;
			Seh::Guard("ShellCasings::ParentNodeAlive", [&] {
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
			Seh::Guard("ShellCasings::EnsureParentNode", [&] {
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
			const bool ok = Seh::Guard("ShellCasings::CreateVisual", [&] {
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
			// Partial: undo whatever was made.
			Seh::Guard("ShellCasings::CreateVisualUndo", [&] {
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
			Seh::Guard("ShellCasings::DestroyVisual", [&] {
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

		bool PlaceNode(void* node, OgreV3 position, OgreQ orientation) noexcept
		{
			__try
			{
				g_nodeSetPosition.Get()(node, position);
				g_nodeSetOrientation.Get()(node, orientation);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Drops every casing WITHOUT calling Ogre: their scene is gone.
		void ForgetAll(const char* reason)
		{
			State& s = S();
			size_t n = 0;
			for (Slot& slot : s.slots)
			{
				if (slot.live)
				{
					++n;
				}
				slot = Slot{};
			}
			s.parentNode = nullptr;
			s.sceneManager = nullptr;
			s.candidates.clear();
			s.obstacles.clear();
			s.forgotten += n;
			if (n > 0)
			{
				Logging::LogMessage("[EXU::Casing] forgot %u casing(s) without touching Ogre (%s)", static_cast<unsigned>(n), reason);
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
				Seh::Guard("ShellCasings::DestroyParent", [&] { g_destroySceneNode.Get()(sceneManager, parent); });
				s.parentNode = nullptr;
				s.sceneManager = nullptr;
			}
		}

		// ----- Frame listener ----------------------------------------------

		void OnFrame(float realDt);

		// Same vtable shape as Ogre::FrameListener (1.10, no base class):
		// frameStarted, frameRenderingQueued, frameEnded, virtual destructor.
		// Every entry returns true: a false from frameStarted makes Ogre stop
		// rendering the frame.
		class CasingFrameListener
		{
		public:
			virtual bool frameStarted(const FrameEventAbi& evt)
			{
				const float dt = std::isfinite(evt.timeSinceLastFrame) ? evt.timeSinceLastFrame : 0.0f;
				if (!S().faulted)
				{
					const bool completed = Seh::Guard("ShellCasings::frameStarted", [&] { OnFrame(dt); },
						[](unsigned long code) {
							Logging::LogMessage("[EXU::Casing] frame update faulted code=0x%08lX; casings disabled for this mission", code);
						});
					if (!completed)
					{
						// Never fault every frame: stop simulating, and forget
						// (not destroy) what may be in a bad state.
						S().faulted = true;
						ForgetAll("frame update failed");
					}
				}
				return true;
			}
			virtual bool frameRenderingQueued(const FrameEventAbi&) { return true; }
			virtual bool frameEnded(const FrameEventAbi&) { return true; }
			virtual ~CasingFrameListener() = default;
		};

		CasingFrameListener g_listener;

		bool RegisterListener()
		{
			State& s = S();
			if (s.listenerRegistered)
			{
				return true;
			}
			void* root = nullptr;
			Seh::Guard("ShellCasings::RegisterListener", [&] {
				root = g_rootGetSingleton.Get()();
				if (root != nullptr)
				{
					g_rootAddFrameListener.Get()(root, &g_listener);
				}
			});
			s.listenerRegistered = root != nullptr;
			s.listenerRoot = root;
			s.haveSimTime = false;
			Logging::LogMessage("[EXU::Casing] frame listener %s root=%p", s.listenerRegistered ? "registered" : "FAILED", root);
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
			// Root::removeFrameListener only queues the pointer; Ogre drops it
			// at the start of the next frame without calling it, so the DLL may
			// unload right after this.
			Seh::Guard("ShellCasings::UnregisterListener", [&] {
				void* current = g_rootGetSingleton.Get()();
				if (current != nullptr && current == root)
				{
					g_rootRemoveFrameListener.Get()(root, &g_listener);
				}
			});
			s.listenerRegistered = false;
			s.listenerRoot = nullptr;
			Logging::LogMessage("[EXU::Casing] frame listener removed");
		}

		void RefreshObstacles(float realDt, float simDt)
		{
			State& s = S();
			s.sinceRefresh += realDt;

			// The bound of every casing that can still touch something: flying
			// ones, and resting ones that lie on an object (to wake them).
			M::V3 lo{ 1e30f, 1e30f, 1e30f };
			M::V3 hi{ -1e30f, -1e30f, -1e30f };
			bool any = false;
			std::vector<uint32_t> owners;
			for (const Slot& slot : s.slots)
			{
				if (!slot.live)
				{
					continue;
				}
				const M::Body& b = slot.body;
				if (b.phase != M::Phase::Flying && !(b.phase == M::Phase::Resting && b.support != 0))
				{
					continue;
				}
				any = true;
				lo = { std::min(lo.x, b.position.x), std::min(lo.y, b.position.y), std::min(lo.z, b.position.z) };
				hi = { std::max(hi.x, b.position.x), std::max(hi.y, b.position.y), std::max(hi.z, b.position.z) };
				if (b.owner != 0 && std::find(owners.begin(), owners.end(), b.owner) == owners.end())
				{
					owners.push_back(b.owner);
				}
			}
			if (!any)
			{
				s.candidates.clear();
				s.obstacles.clear();
				return;
			}

			if (s.sinceRefresh >= kObstacleRefreshSeconds)
			{
				s.sinceRefresh = 0.0f;
				const M::V3 center = M::Scale(M::Add(lo, hi), 0.5f);
				const float reach = M::Length(M::Sub(hi, lo)) * 0.5f + kObstacleReach;
				std::vector<Candidate>& found = s.scratch;
				found.resize(BZR::GameObject::kArenaSlotCount);
				const size_t count = ScanArena(center, reach, found.data(), found.size());
				found.resize(count);
				std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.distSq < b.distSq; });
				std::vector<Candidate> next;
				next.reserve(kMaxObstacles + owners.size());
				// Owners always, then the nearest others.
				for (uint32_t owner : owners)
				{
					const uint32_t index = (owner >> 20) & 0xFFFu;
					Candidate c;
					c.index = index;
					c.serial = owner & 0xFFFFFu;
					next.push_back(c);
				}
				for (const Candidate& c : found)
				{
					if (next.size() >= kMaxObstacles + owners.size())
					{
						break;
					}
					const uint32_t handle = HandleOf(c);
					if (std::find(owners.begin(), owners.end(), handle) == owners.end())
					{
						next.push_back(c);
					}
				}
				// Keep the velocity history of candidates that stay.
				for (Candidate& c : next)
				{
					for (const Candidate& old : s.candidates)
					{
						if (old.index == c.index && old.serial == c.serial)
						{
							c.lastCenter = old.lastCenter;
							c.hasLast = old.hasLast;
							break;
						}
					}
				}
				s.candidates.swap(next);
			}

			s.obstacles.clear();
			for (Candidate& c : s.candidates)
			{
				const ObstacleRead read = ReadObstacle(c.index, c.serial);
				M::Obstacle obstacle;
				if (!BuildObstacle(read, HandleOf(c), obstacle))
				{
					c.hasLast = false;
					continue;
				}
				if (c.hasLast && simDt > 1e-4f)
				{
					const M::V3 v = M::Scale(M::Sub(obstacle.center, c.lastCenter), 1.0f / simDt);
					obstacle.velocity = M::Length(v) < 150.0f ? v : M::V3{};
				}
				if (simDt > 1e-4f || !c.hasLast)
				{
					c.lastCenter = obstacle.center;
					c.hasLast = true;
				}
				if (s.obstacleLogs < 6 && s.frames > 0)
				{
					++s.obstacleLogs;
					Logging::LogMessage(
						"[EXU::Casing] obstacle handle=0x%08X shape=%s center=(%.2f,%.2f,%.2f) half=(%.2f,%.2f,%.2f) radius=%.2f entSphere=%.2f",
						obstacle.handle, obstacle.box ? "box" : "sphere", obstacle.center.x, obstacle.center.y, obstacle.center.z,
						obstacle.half.x, obstacle.half.y, obstacle.half.z, obstacle.radius, read.sphereRadius);
				}
				s.obstacles.push_back(obstacle);
			}
		}

		void OnFrame(float realDt)
		{
			State& s = S();
			if (!s.listenerRegistered || s.faulted)
			{
				return;
			}
			LARGE_INTEGER t0{}, t1{}, freq{};
			QueryPerformanceCounter(&t0);

			if (LiveCount() == 0)
			{
				return;
			}

			// The scene the casings were made in must still be the current one.
			void* sceneManager = ReadSceneManager();
			if (sceneManager == nullptr || sceneManager != s.sceneManager || !ParentNodeAlive(sceneManager, s.parentNode, s.parentName))
			{
				ForgetAll("scene changed or cleared");
				return;
			}

			// Step by real frame time, but only while the simulation advances:
			// a paused game (bPaused, or a menu loop that stops SetLoopTimes)
			// freezes the casings with everything else.
			const TimeSample time = ReadTime();
			float dt = std::min(std::max(realDt, 0.0f), 0.1f);
			if (!time.valid || time.paused)
			{
				dt = 0.0f;
			}
			else if (s.haveSimTime)
			{
				const float simAdvance = time.simTime - s.lastSimTime;
				if (!(simAdvance > 0.0f) || simAdvance > 1.0f)
				{
					dt = 0.0f;   // stalled, or a restart/load jump
				}
			}
			const float simDt = (time.valid && s.haveSimTime) ? std::max(0.0f, std::min(time.simTime - s.lastSimTime, 1.0f)) : 0.0f;
			if (time.valid)
			{
				s.lastSimTime = time.simTime;
				s.haveSimTime = true;
			}

			RefreshObstacles(realDt, simDt);

			BZR::VECTOR_3D origin{};
			if (!OgreRenderSpace::TryReadWorldRenderOrigin(origin) || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
				!std::isfinite(origin.z))
			{
				return;
			}

			auto terrain = [](float x, float z) { return TerrainHeight(x, z); };
			const M::Obstacle* obstacles = s.obstacles.empty() ? nullptr : s.obstacles.data();
			const int obstacleCount = static_cast<int>(s.obstacles.size());
			size_t awake = 0;
			for (Slot& slot : s.slots)
			{
				if (!slot.live)
				{
					continue;
				}
				M::Body& body = slot.body;
				if (body.phase == M::Phase::Resting && body.support != 0)
				{
					// Lying on an object: wake if that object moves or is gone.
					bool supported = false;
					for (const M::Obstacle& o : s.obstacles)
					{
						if (o.handle == body.support)
						{
							supported = M::Length(o.velocity) < Tuning().sleepSpeed;
							break;
						}
					}
					if (!supported)
					{
						M::Wake(body);
					}
				}
				const M::V3 before = body.position;
				const M::Quat beforeQ = body.orientation;
				M::Step(body, dt, terrain, obstacles, obstacleCount, Tuning());
				if (body.phase == M::Phase::Done)
				{
					DestroyVisual(s.sceneManager, slot);
					slot = Slot{};
					++s.expired;
					continue;
				}
				awake += body.phase == M::Phase::Flying ? 1 : 0;
				const bool moved = slot.moved || before.x != body.position.x || before.y != body.position.y ||
					before.z != body.position.z || beforeQ.w != body.orientation.w || beforeQ.x != body.orientation.x ||
					beforeQ.y != body.orientation.y || beforeQ.z != body.orientation.z;
				if (moved)
				{
					slot.moved = false;
					if (!PlaceNode(slot.node, ToRenderPosition(body.position, origin), ToRenderOrientation(body.orientation)))
					{
						// The node is not ours any more; forget everything.
						ForgetAll("node update faulted");
						return;
					}
				}
			}

			++s.frames;
			QueryPerformanceCounter(&t1);
			QueryPerformanceFrequency(&freq);
			const double micros = freq.QuadPart > 0 ? static_cast<double>(t1.QuadPart - t0.QuadPart) * 1.0e6 / static_cast<double>(freq.QuadPart) : 0.0;
			s.frameMicrosTotal += micros;
			s.frameMicrosMax = std::max(s.frameMicrosMax, micros);
			if (!s.loggedFirstFrame)
			{
				s.loggedFirstFrame = true;
				Logging::LogMessage(
					"[EXU::Casing] first frame: dt=%.4f simTime=%.3f paused=%d live=%u awake=%u obstacles=%u renderOrigin=(%.1f,%.1f,%.1f) %.1fus",
					dt, time.simTime, time.paused ? 1 : 0, static_cast<unsigned>(LiveCount()), static_cast<unsigned>(awake),
					static_cast<unsigned>(s.obstacles.size()), origin.x, origin.y, origin.z, micros);
			}
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
				luaL_error(L, "exu.casing.Spawn: %s must be finite", name);
			}
			return value;
		}

		bool FieldVector(lua_State* L, int table, const char* name, M::V3& out)
		{
			lua_getfield(L, table, name);
			if (lua_isnil(L, -1))
			{
				lua_pop(L, 1);
				return false;
			}
			if (!lua_istable(L, -1) && !lua_isuserdata(L, -1))
			{
				luaL_error(L, "exu.casing.Spawn: %s must be a vector", name);
			}
			const BZR::VECTOR_3D v = CheckVectorOrSingles(L, -1);
			lua_pop(L, 1);
			out = { v.x, v.y, v.z };
			if (!M::IsFinite(out))
			{
				luaL_error(L, "exu.casing.Spawn: %s must be finite", name);
			}
			return true;
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
			// Full: recycle the casing that matters least -- sinking first, then
			// the longest-resting, then the oldest in flight.
			Slot* best = nullptr;
			auto rank = [](const Slot& slot) {
				switch (slot.body.phase)
				{
				case M::Phase::Sinking: return 3000.0f + slot.body.sinkElapsed;
				case M::Phase::Resting: return 2000.0f + slot.body.restAge;
				default: return slot.body.age;
				}
			};
			for (Slot& slot : s.slots)
			{
				if (slot.live && (best == nullptr || rank(slot) > rank(*best)))
				{
					best = &slot;
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

		// exu.casing.Spawn{ pos=, vel=, spin=, transform=|orientation=, mesh=, material=, scale=, owner=,
		//                   linger=, restitution=, friction=, radius=, sinkDepth= } -> id | nil, err
		int Spawn(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			const int t = 1;

			// Parse everything first: a Lua error longjmps over this frame, so
			// nothing with a destructor exists until parsing is done.
			M::V3 position{}, velocity{}, spin{};
			if (!FieldVector(L, t, "pos", position))
			{
				return luaL_argerror(L, 1, "pos is required");
			}
			FieldVector(L, t, "vel", velocity);
			FieldVector(L, t, "spin", spin);
			const char* meshArg = FieldString(L, t, "mesh");
			const char* materialArg = FieldString(L, t, "material");
			const float scale = FieldNumber(L, t, "scale", 1.0f);
			const float linger = FieldNumber(L, t, "linger", 6.0f);
			const float restitution = FieldNumber(L, t, "restitution", 0.35f);
			const float friction = FieldNumber(L, t, "friction", 0.4f);
			const float radius = FieldNumber(L, t, "radius", 0.06f * scale);
			const float sinkDepth = FieldNumber(L, t, "sinkDepth", 3.0f * radius);
			if (!(scale > 0.01f && scale < 50.0f) || !(radius > 0.001f && radius < 5.0f))
			{
				return luaL_argerror(L, 1, "scale/radius out of range");
			}
			uint32_t owner = 0;
			lua_getfield(L, t, "owner");
			if (!lua_isnil(L, -1))
			{
				owner = static_cast<uint32_t>(CheckHandle(L, -1));
			}
			lua_pop(L, 1);

			M::Quat orientation{};
			lua_getfield(L, t, "transform");
			if (!lua_isnil(L, -1))
			{
				const BZR::MAT_3D m = CheckMatrix(L, -1);
				orientation = M::FromAxes(M::NormalizeOr({ m.right_x, m.right_y, m.right_z }, { 1.0f, 0.0f, 0.0f }),
					M::NormalizeOr({ m.up_x, m.up_y, m.up_z }, { 0.0f, 1.0f, 0.0f }),
					M::NormalizeOr({ m.front_x, m.front_y, m.front_z }, { 0.0f, 0.0f, 1.0f }));
			}
			lua_pop(L, 1);
			lua_getfield(L, t, "orientation");
			if (lua_istable(L, -1))
			{
				const int q = lua_gettop(L);
				orientation = M::Normalize({ FieldNumber(L, q, "w", 1.0f), FieldNumber(L, q, "x", 0.0f), FieldNumber(L, q, "y", 0.0f),
					FieldNumber(L, q, "z", 0.0f) });
			}
			lua_pop(L, 1);
			if (!M::IsFinite(orientation))
			{
				orientation = M::Quat{};
			}

			// Nothing below raises a Lua error.
			State& s = S();
			if (!Available())
			{
				return PushFailure(L, "exu.casing is unavailable (unqualified build or missing Ogre exports)");
			}
			if (s.faulted)
			{
				return PushFailure(L, "exu.casing was disabled after a fault this mission (see exu.log)");
			}
			const std::string mesh = meshArg != nullptr && meshArg[0] != '\0' ? meshArg : kDefaultMesh;
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
				return PushFailure(L, "could not create the casing parent node");
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

			Slot* slot = AcquireSlot();
			if (slot == nullptr)
			{
				++s.failed;
				return PushFailure(L, "casing pool is full");
			}

			void* entity = nullptr;
			void* node = nullptr;
			if (!CreateVisual(sceneManager, parent, mesh, material, ToRenderPosition(position, origin), ToRenderOrientation(orientation),
				scale, entity, node))
			{
				s.badMeshes.insert(mesh);
				++s.failed;
				Logging::LogMessage("[EXU::Casing] could not create mesh=%s material=%s; further spawns of it are refused this mission",
					mesh.c_str(), material.empty() ? "<mesh-default>" : material.c_str());
				return PushFailure(L, "Ogre could not create the casing entity (see exu.log)");
			}

			*slot = Slot{};
			slot->live = true;
			slot->id = s.nextId++;
			if (s.nextId == 0)
			{
				s.nextId = 1;
			}
			slot->entity = entity;
			slot->node = node;
			slot->scale = scale;
			M::Body& body = slot->body;
			body.position = position;
			body.velocity = velocity;
			body.omega = spin;
			body.orientation = orientation;
			body.radius = radius;
			body.surface = { std::min(std::max(restitution, 0.0f), 0.95f), std::min(std::max(friction, 0.0f), 1.0f) };
			body.linger = std::max(0.0f, linger);
			body.sinkDepth = std::max(0.0f, sinkDepth);
			body.owner = owner;
			body.tumbleSign = (slot->id & 1u) ? 1.0f : -1.0f;
			++s.spawned;
			if (s.spawned <= 3)
			{
				Logging::LogMessage(
					"[EXU::Casing] spawned id=%u mesh=%s pos=(%.2f,%.2f,%.2f) vel=(%.2f,%.2f,%.2f) scale=%.2f owner=0x%08X "
					"renderOrigin=(%.1f,%.1f,%.1f) live=%u/%u",
					slot->id, mesh.c_str(), position.x, position.y, position.z, velocity.x, velocity.y, velocity.z, scale, owner,
					origin.x, origin.y, origin.z, static_cast<unsigned>(LiveCount()), static_cast<unsigned>(s.max));
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
			// Shrinking: retire the excess now, least important first.
			while (LiveCount() > s.max)
			{
				Slot* victim = nullptr;
				for (Slot& slot : s.slots)
				{
					if (slot.live && (victim == nullptr || slot.body.age > victim->body.age))
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
			size_t flying = 0, resting = 0, sinking = 0;
			for (const Slot& slot : s.slots)
			{
				if (!slot.live)
				{
					continue;
				}
				flying += slot.body.phase == M::Phase::Flying ? 1 : 0;
				resting += slot.body.phase == M::Phase::Resting ? 1 : 0;
				sinking += slot.body.phase == M::Phase::Sinking ? 1 : 0;
			}
			lua_createtable(L, 0, 14);
			auto set = [L](const char* key, double value) {
				lua_pushnumber(L, value);
				lua_setfield(L, -2, key);
			};
			set("live", static_cast<double>(flying + resting + sinking));
			set("flying", static_cast<double>(flying));
			set("resting", static_cast<double>(resting));
			set("sinking", static_cast<double>(sinking));
			set("max", static_cast<double>(s.max));
			set("spawned", static_cast<double>(s.spawned));
			set("recycled", static_cast<double>(s.recycled));
			set("expired", static_cast<double>(s.expired));
			set("failed", static_cast<double>(s.failed));
			set("forgotten", static_cast<double>(s.forgotten));
			set("frames", static_cast<double>(s.frames));
			set("obstacles", static_cast<double>(s.obstacles.size()));
			set("avgFrameMicros", s.frames > 0 ? s.frameMicrosTotal / static_cast<double>(s.frames) : 0.0);
			set("maxFrameMicros", s.frameMicrosMax);
			return 1;
		}

		int GetCapabilities(lua_State* L)
		{
			const bool available = Available();
			lua_createtable(L, 0, 8);
			lua_pushboolean(L, available ? 1 : 0);
			lua_setfield(L, -2, "casings");
			lua_pushboolean(L, available ? 1 : 0);
			lua_setfield(L, -2, "frameDriven");
			lua_pushboolean(L, available ? 1 : 0);
			lua_setfield(L, -2, "terrainCollision");
			lua_pushboolean(L, available ? 1 : 0);
			lua_setfield(L, -2, "objectCollision");
			lua_pushstring(L, "box");   // class model box, sphere fallback
			lua_setfield(L, -2, "objectShape");
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
		lua_setfield(L, -2, "casing");
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
					"[EXU::Casing] shutdown: destroyed %u live; totals spawned=%llu recycled=%llu expired=%llu failed=%llu frames=%llu "
					"avgFrame=%.1fus maxFrame=%.1fus",
					static_cast<unsigned>(live), static_cast<unsigned long long>(s.spawned), static_cast<unsigned long long>(s.recycled),
					static_cast<unsigned long long>(s.expired), static_cast<unsigned long long>(s.failed),
					static_cast<unsigned long long>(s.frames), s.frames > 0 ? s.frameMicrosTotal / static_cast<double>(s.frames) : 0.0,
					s.frameMicrosMax);
			}
			s = State{};
		}
		catch (...)
		{
			OutputDebugStringA("ExtraUtilities: exception during shell casing shutdown\n");
		}
	}
}
