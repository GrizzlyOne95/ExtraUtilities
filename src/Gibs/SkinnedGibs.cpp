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

#include "Gibs/SkinnedGibs.h"

#include "BasicPatch.h"
#include "EntryDetour32.h"
#include "Gibs/GibPayloads.h"
#include "Gibs/SkinnedGibCore.h"
#include "LuaCppBarrier.h"
#include "LuaHelpers.h"
#include "OpenShimBridge.h"
#include "Ogre/OgreGibRuntime.h"
#include "Ogre/OgreRenderOrigin.h"
#include "Ogre/OgreRenderSpace.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/MsvcRtti.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "Util/SignatureResolver.h"
#include "bzr.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace ExtraUtilities::Lua::SkinnedGibs
{
	namespace
	{
		namespace Core = ExtraUtilities::SkinnedGibCore;
		namespace Rt = ExtraUtilities::OgreGibRuntime;
		namespace Payloads = ExtraUtilities::Gibs::Payloads;

		// ----- Engine layout (GOG 2.2.301) -----------------------------------------

		constexpr const char* kPersonRtti = ".?AVPerson@@";
		constexpr uintptr_t kGameObjectRenderBridge = 0x0F0;   // GameObject: render bridge
		constexpr uintptr_t kGameObjectObj76 = 0x0F4;          // GameObject: OBJ76*
		constexpr uintptr_t kObj76GameObject = 0x08C;          // OBJ76: back-link to the GameObject
		constexpr uintptr_t kBridgeEntity = 0x094;             // render bridge: Ogre::Entity*
		constexpr uintptr_t kBridgeNode = 0x098;               // render bridge: Ogre::SceneNode* fallback
		constexpr uint16_t kMaxCapturedBones = 256;
		constexpr const char* kParentNodePrefix = "__exu_gib_root_";

		// ChunkEffect::FullFragmentObject, __thiscall(ChunkEffect*, OBJ76* root,
		// const float* velocity, uint8_t preserve), callee-clean. The first 16
		// bytes are the identity OpenShim guards (scripts/patches.json
		// ChunkEffectFullFragment); the detour steals the first 6 (push ebp;
		// mov ebp,esp; sub esp,28h), all complete reloc-free instructions.
		constexpr std::size_t kDetourLength = 6;
		constexpr std::uint8_t kExpectedEntry[16] = {
			0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x28, 0xA1, 0x00, 0x70, 0x8E, 0x00, 0x33, 0xC5, 0x89, 0x45, 0xFC
		};
		using FullFragmentFn = void(__thiscall*)(void* chunkEffect, void* root, const float* velocity, std::uint8_t preserve);

		// Ogre::FrameEvent (Real = float in BZR's Ogre).
		struct FrameEventAbi
		{
			float timeSinceLastEvent;
			float timeSinceLastFrame;
		};

		// ----- Pool ----------------------------------------------------------------

		struct Slot
		{
			void* node = nullptr;
			void* entity = nullptr;
			std::string mesh;
			bool active = false;
			uint64_t serial = 0;
			Core::Body body;
		};

		struct State
		{
			// Configure
			size_t max = Core::kDefaultMax;
			float linger = Core::kDefaultLinger;
			float force = Core::kDefaultForce;

			bool enabled = false;
			std::string reason = "not enabled";

			std::vector<Slot> slots;
			size_t activeCount = 0;
			uint64_t spawnSerial = 0;
			void* sceneManager = nullptr;
			void* parentNode = nullptr;
			std::string parentName;
			std::vector<std::string> failedMeshes;
			std::shared_ptr<const Payloads::ModelInfo> model;   // the death being spawned

			bool listenerRegistered = false;
			void* listenerRoot = nullptr;
			bool haveSimTime = false;
			float lastSimTime = 0.0f;
			DWORD ownerThread = 0;      // the thread that enabled gibs (Lua, the sim)
			DWORD listenerThread = 0;   // the thread the frame listener first ran on
			bool crossThread = false;
			bool openShimLogged = false;
			bool faulted = false;

			uint64_t deaths = 0;
			uint64_t spawned = 0;
			uint64_t recycled = 0;
			uint64_t failed = 0;
			uint64_t forgotten = 0;
		};

		State& S()
		{
			static State gibState;
			return gibState;
		}

		// Budgeted: a battle can kill dozens of pilots. EXU_DEBUG_LOG=1 lifts it.
		std::atomic<long> g_logBudget{ 96 };

		bool AcquireLogSlot() noexcept
		{
			return Logging::IsDebugLoggingEnabled() || g_logBudget.fetch_sub(1, std::memory_order_relaxed) > 0;
		}

		// ----- Engine reads ---------------------------------------------------------

		bool ReadPointer(const void* base, uintptr_t offset, void*& out) noexcept
		{
			out = nullptr;
			if (base == nullptr)
			{
				return false;
			}
			__try
			{
				out = *reinterpret_cast<void* const*>(reinterpret_cast<const uint8_t*>(base) + offset);
				return out != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out = nullptr;
				return false;
			}
		}

		bool ReadVelocity(const float* velocity, Core::V3& out) noexcept
		{
			out = {};
			if (velocity == nullptr)
			{
				return false;
			}
			__try
			{
				out = { velocity[0], velocity[1], velocity[2] };
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out = {};
				return false;
			}
			out = Core::SanitizeBodyVelocity(out);
			return true;
		}

		// RTTI: vftable[-1] is the CompleteObjectLocator; its type descriptor
		// carries the decorated name.
		bool IsPerson(const void* object) noexcept
		{
			if (object == nullptr)
			{
				return false;
			}
			__try
			{
				void** const vftable = *reinterpret_cast<void***>(const_cast<void*>(object));
				if (vftable == nullptr)
				{
					return false;
				}
				const auto* locator = *(reinterpret_cast<MsvcRtti::CompleteObjectLocator* const*>(vftable) - 1);
				if (locator == nullptr || locator->pTypeDescriptor == nullptr)
				{
					return false;
				}
				return std::strcmp(locator->pTypeDescriptor->name, kPersonRtti) == 0;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		using TerrainHeightAtFn = double(__cdecl*)(double, double);

		// Simulation-space terrain height (what Lua GetPosition reports), or NaN.
		float TerrainHeight(double simX, double simZ) noexcept
		{
			double height = 0.0;
			__try
			{
				height = reinterpret_cast<TerrainHeightAtFn>(EngineAddresses::Terrain::HeightAt)(simX, simZ);
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

		// ----- Death capture -----------------------------------------------------------

		struct BoneSample
		{
			char name[Rt::kBoneNameCapacity];
			Core::BoneFrame frame;
			bool valid;
		};

		// Plain data only: the entity is never touched after the hook returns.
		struct DeathCapture
		{
			bool active;
			void* entity;
			void* chunkEffect;
			char meshName[260];
			char group[128];
			Core::NodePose node;
			Core::V3 velocity;
			uint16_t boneCount;
			BoneSample bones[kMaxCapturedBones];
		};

		DeathCapture g_capture = {};

		// The original is re-entrant (fragmenting a tree can fragment its
		// children): only the outermost call captures. The hook runs on the sim
		// thread, so this needs no atomics.
		int g_depth = 0;
		bool g_removePending = false;

		EntryDetour32* g_detour = nullptr;
		FullFragmentFn g_original = nullptr;

		// ----- Scene ------------------------------------------------------------------------

		// Drops every gib WITHOUT calling Ogre: their scene is gone.
		void ForgetAll(const char* reason)
		{
			State& s = S();
			size_t n = 0;
			for (const Slot& slot : s.slots)
			{
				if (slot.node != nullptr || slot.entity != nullptr)
				{
					++n;
				}
			}
			s.slots.clear();
			s.activeCount = 0;
			s.parentNode = nullptr;
			s.sceneManager = nullptr;
			s.forgotten += n;
			if (n > 0)
			{
				Logging::LogMessage("[EXU::Gibs] forgot %u gib object(s) without touching Ogre (%s)", static_cast<unsigned>(n), reason);
			}
		}

		// Creates (or finds) the parent node. If the scene held a different node
		// of that name, or the manager changed, everything below is stale and is
		// forgotten first.
		void* EnsureParentNode(void* sceneManager)
		{
			State& s = S();
			if (s.parentNode != nullptr && s.sceneManager == sceneManager &&
				Rt::NamedChildAlive(sceneManager, s.parentNode, s.parentName))
			{
				return s.parentNode;
			}
			if (!s.slots.empty() || s.parentNode != nullptr)
			{
				ForgetAll(s.sceneManager != sceneManager ? "scene changed" : "parent node gone");
			}
			if (s.parentName.empty())
			{
				char name[64];
				std::snprintf(name, sizeof(name), "%s%08X", kParentNodePrefix, static_cast<unsigned>(GetTickCount()));
				s.parentName = name;
			}
			void* parent = Rt::EnsureNamedChild(sceneManager, s.parentName);
			s.parentNode = parent;
			s.sceneManager = parent != nullptr ? sceneManager : nullptr;
			return parent;
		}

		// Whether the pool's scene is still the live one.
		bool SceneIntact()
		{
			State& s = S();
			void* current = Rt::ReadSceneManager();
			return s.sceneManager != nullptr && current == s.sceneManager && s.parentNode != nullptr &&
				Rt::NamedChildAlive(current, s.parentNode, s.parentName);
		}

		void ReleaseSlot(Slot& slot)
		{
			// Hidden, not destroyed: the entity is reused by the next gib of the
			// same mesh, or replaced between frames when recycled.
			State& s = S();
			if (slot.active && s.activeCount > 0)
			{
				--s.activeCount;
			}
			slot.active = false;
			Rt::HideObject(slot.node, slot.entity);
		}

		void DestroyAll(bool destroyParent)
		{
			State& s = S();
			if (!SceneIntact())
			{
				ForgetAll(s.sceneManager == nullptr ? "no scene" : "scene changed or cleared");
				return;
			}
			void* sceneManager = s.sceneManager;
			for (Slot& slot : s.slots)
			{
				if (slot.entity != nullptr)
				{
					Rt::DestroyEntity(sceneManager, slot.entity);
				}
				if (slot.node != nullptr)
				{
					Rt::DestroySceneNode(sceneManager, slot.node);
				}
			}
			s.slots.clear();
			s.activeCount = 0;
			if (destroyParent)
			{
				Rt::DestroySceneNode(sceneManager, s.parentNode);
				s.parentNode = nullptr;
				s.sceneManager = nullptr;
			}
		}

		// ----- Spawning ---------------------------------------------------------------------------

		int FindBone(const DeathCapture& capture, const Payloads::PieceInfo& piece)
		{
			if (piece.bone < capture.boneCount && capture.bones[piece.bone].valid &&
				(piece.boneName.empty() || _stricmp(capture.bones[piece.bone].name, piece.boneName.c_str()) == 0))
			{
				return piece.bone;
			}
			if (piece.boneName.empty())
			{
				return -1;
			}
			for (uint16_t i = 0; i < capture.boneCount; ++i)
			{
				if (capture.bones[i].valid && _stricmp(capture.bones[i].name, piece.boneName.c_str()) == 0)
				{
					return i;
				}
			}
			return -1;
		}

		bool IsFailedMesh(const std::string& mesh)
		{
			const State& s = S();
			return std::find(s.failedMeshes.begin(), s.failedMeshes.end(), mesh) != s.failedMeshes.end();
		}

		// Index of a slot to build the next gib in, or -1 when the pool cannot
		// take one. Releases a recycled slot's previous gib.
		long AcquireSlot(const std::string& mesh)
		{
			State& s = S();
			static std::vector<Core::SlotView> views;
			views.resize(s.slots.size());
			for (size_t i = 0; i < s.slots.size(); ++i)
			{
				const Slot& slot = s.slots[i];
				Core::SlotView& view = views[i];
				view.active = slot.active;
				view.sceneMatches = slot.node != nullptr;   // every live slot belongs to the pool's one scene
				view.hasEntityForMesh = slot.entity != nullptr && slot.mesh == mesh;
				view.settled = slot.body.settled;
				view.serial = slot.serial;
			}
			const Core::SlotChoice choice = Core::ChooseSlot(views.data(), views.size(), s.max);
			switch (choice.kind)
			{
			case Core::SlotChoiceKind::ReuseMesh:
			case Core::SlotChoiceKind::ReuseFree:
				return static_cast<long>(choice.index);
			case Core::SlotChoiceKind::Append:
				s.slots.emplace_back();
				return static_cast<long>(s.slots.size() - 1);
			case Core::SlotChoiceKind::Recycle:
				ReleaseSlot(s.slots[choice.index]);
				++s.recycled;
				return static_cast<long>(choice.index);
			default:
				return -1;
			}
		}

		// Makes sure the slot holds an entity of `mesh`.
		bool PrepareSlotEntity(size_t index, const std::string& mesh, void* parent)
		{
			State& s = S();
			Slot& slot = s.slots[index];
			if (slot.entity != nullptr && slot.mesh == mesh)
			{
				return true;
			}
			if (slot.node != nullptr)
			{
				void* entity = slot.entity;
				if (Rt::ReplaceMeshEntity(s.sceneManager, slot.node, entity, mesh))
				{
					slot.entity = entity;
					slot.mesh = mesh;
					return true;
				}
				// The node is attached and empty; give it back to the scene.
				Rt::DestroySceneNode(s.sceneManager, slot.node);
				slot = Slot{};
			}
			void* node = nullptr;
			void* entity = nullptr;
			if (!Rt::CreateMeshObject(s.sceneManager, parent, mesh, node, entity))
			{
				return false;
			}
			slot.node = node;
			slot.entity = entity;
			slot.mesh = mesh;
			return true;
		}

		void DropSlotIfEmpty(size_t index)
		{
			State& s = S();
			if (index < s.slots.size() && s.slots[index].node == nullptr && !s.slots[index].active)
			{
				s.slots.erase(s.slots.begin() + static_cast<std::ptrdiff_t>(index));
			}
		}

		size_t SpawnFromCapture(size_t& outFailed)
		{
			State& s = S();
			const DeathCapture& capture = g_capture;
			outFailed = 0;
			if (!s.model || s.model->pieces.empty())
			{
				return 0;
			}
			void* const sceneManager = Rt::ReadSceneManager();
			if (sceneManager == nullptr)
			{
				return 0;
			}
			void* const parent = EnsureParentNode(sceneManager);
			if (parent == nullptr)
			{
				return 0;
			}

			Core::GibRandom random;
			random.state = static_cast<uint32_t>(GetTickCount()) ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(capture.entity)) ^
				static_cast<uint32_t>(s.spawnSerial * 2654435761ull);
			const Core::V3 origin = capture.node.position;
			size_t spawned = 0;
			for (const Payloads::PieceInfo& piece : s.model->pieces)
			{
				const int boneIndex = FindBone(capture, piece);
				if (boneIndex < 0)
				{
					++outFailed;
					continue;
				}
				const BoneSample& bone = capture.bones[boneIndex];
				Core::PiecePose pose;
				if (!Core::ComposePiecePose(capture.node, bone.frame, { piece.offset[0], piece.offset[1], piece.offset[2] }, pose))
				{
					++outFailed;
					continue;
				}
				if (IsFailedMesh(piece.resource))
				{
					++outFailed;
					continue;
				}

				const long acquired = AcquireSlot(piece.resource);
				if (acquired < 0)
				{
					++outFailed;
					continue;
				}
				const size_t index = static_cast<size_t>(acquired);
				if (!PrepareSlotEntity(index, piece.resource, parent))
				{
					DropSlotIfEmpty(index);
					if (s.failedMeshes.size() < 256)
					{
						s.failedMeshes.push_back(piece.resource);
					}
					Logging::LogMessage("[EXU::Gibs] entity creation failed mesh=%s (not retried this mission)", piece.resource.c_str());
					++outFailed;
					continue;
				}

				Slot& slot = s.slots[index];
				const float radius = Core::PieceRadius(piece.radius);
				const Core::LaunchResult launch = Core::ComputeLaunch(random, pose.position, origin, capture.velocity, radius, s.force);
				slot.body = Core::Body{};
				slot.body.position = pose.position;
				slot.body.orientation = pose.orientation;
				slot.body.velocity = launch.velocity;
				slot.body.omega = launch.omega;
				slot.body.scale = capture.node.scale;
				slot.body.radius = radius;
				slot.serial = ++s.spawnSerial;
				const float position[3] = { pose.position.x, pose.position.y, pose.position.z };
				const float orientation[4] = { pose.orientation.w, pose.orientation.x, pose.orientation.y, pose.orientation.z };
				const float scale[3] = { slot.body.scale.x, slot.body.scale.y, slot.body.scale.z };
				if (!Rt::SetNodeScale(slot.node, scale) || !Rt::ShowObject(slot.node, slot.entity, position, orientation))
				{
					// The objects faulted: never touch them again.
					slot = Slot{};
					++outFailed;
					continue;
				}
				slot.active = true;
				++s.activeCount;
				++spawned;
				if (Logging::IsDebugLoggingEnabled())
				{
					Logging::LogMessage("[EXU::Gibs]   gib mesh=%s bone=%s pos=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f) r=%.3f",
						piece.resource.c_str(), bone.name, pose.position.x, pose.position.y, pose.position.z,
						launch.velocity.x, launch.velocity.y, launch.velocity.z, radius);
				}
			}
			return spawned;
		}

		// ----- The hook ----------------------------------------------------------------------------

		// Before the original: gate, capture the pose, prepare the payload.
		bool BeginCaptureImpl(void* chunkEffect, void* root, const float* velocity)
		{
			State& s = S();
			g_capture.active = false;
			if (!s.enabled || s.faulted || chunkEffect == nullptr || root == nullptr)
			{
				return false;
			}
			if (s.crossThread)
			{
				return false;
			}
			if (GetCurrentThreadId() != s.ownerThread)
			{
				// The pool is unlocked by design: never spawn from another thread.
				s.crossThread = true;
				s.reason = "a person died on a different thread than the one that enabled gibs";
				Logging::LogMessage("[EXU::Gibs] death hook thread %lu differs from the enabling (sim) thread %lu; death capture stands down",
					static_cast<unsigned long>(GetCurrentThreadId()), static_cast<unsigned long>(s.ownerThread));
				return false;
			}

			// Persons only, and only their whole body: the root handed to
			// FullFragmentObject must be the person's own object tree.
			void* gameObject = nullptr;
			void* liveObj76 = nullptr;
			if (!ReadPointer(root, kObj76GameObject, gameObject) || !IsPerson(gameObject) ||
				!ReadPointer(gameObject, kGameObjectObj76, liveObj76) || liveObj76 != root)
			{
				return false;
			}

			void* bridge = nullptr;
			void* entity = nullptr;
			if (!ReadPointer(gameObject, kGameObjectRenderBridge, bridge) || !ReadPointer(bridge, kBridgeEntity, entity) ||
				!Rt::LooksLikeOgreObject(entity))
			{
				return false;
			}

			DeathCapture& capture = g_capture;
			capture.entity = entity;
			capture.chunkEffect = chunkEffect;
			if (!Rt::ReadEntityMeshIdentity(entity, capture.meshName, sizeof(capture.meshName), capture.group, sizeof(capture.group)))
			{
				return false;
			}

			void* node = Rt::GetParentSceneNode(entity);
			if (!Rt::LooksLikeOgreObject(node))
			{
				node = nullptr;
				ReadPointer(bridge, kBridgeNode, node);
			}
			float position[3] = {};
			float orientation[4] = {};
			float scale[3] = {};
			if (!Rt::LooksLikeOgreObject(node) || !Rt::ReadNodePose(node, position, orientation, scale))
			{
				return false;
			}
			capture.node.position = { position[0], position[1], position[2] };
			capture.node.orientation = Core::QNormalize({ orientation[0], orientation[1], orientation[2], orientation[3] });
			capture.node.scale = { scale[0], scale[1], scale[2] };
			if (!Core::Finite(capture.node.position) || !Core::Finite(capture.node.scale))
			{
				return false;
			}

			void* skeleton = nullptr;
			uint16_t boneCount = 0;
			if (!Rt::GetSkeleton(entity, skeleton, boneCount))
			{
				return false;
			}
			capture.boneCount = std::min<uint16_t>(boneCount, kMaxCapturedBones);
			uint16_t valid = 0;
			for (uint16_t index = 0; index < capture.boneCount; ++index)
			{
				BoneSample& sample = capture.bones[index];
				Rt::BoneRead read;
				sample.valid = false;
				sample.name[0] = '\0';
				if (!Rt::ReadBone(skeleton, index, read))
				{
					continue;
				}
				std::memcpy(sample.name, read.name, sizeof(sample.name));
				sample.frame.position = { read.position[0], read.position[1], read.position[2] };
				sample.frame.orientation = Core::QNormalize({ read.orientation[0], read.orientation[1], read.orientation[2], read.orientation[3] });
				sample.valid = Core::Finite(sample.frame.position);
				valid += sample.valid ? 1 : 0;
			}
			if (valid == 0)
			{
				return false;
			}
			ReadVelocity(velocity, capture.velocity);

			// The split is cached per model; the first death of a new model
			// pays for it here, once.
			s.model = Payloads::Prepare(capture.meshName, capture.group);
			if (!s.model || s.model->pieces.empty())
			{
				s.model.reset();
				return false;
			}
			capture.active = true;
			++s.deaths;
			return true;
		}

		bool BeginCapture(void* chunkEffect, void* root, const float* velocity) noexcept
		{
			bool captured = false;
			const bool completed = Seh::Guard("Gibs::BeginCapture", [&] { captured = BeginCaptureImpl(chunkEffect, root, velocity); },
				[](unsigned long code) { Logging::LogMessage("[EXU::Gibs] capture faulted code=0x%08lX", code); });
			if (!completed)
			{
				g_capture.active = false;
				return false;
			}
			return captured;
		}

		// After the original: spawn the pieces and hide the body.
		void EndCaptureImpl(void* chunkEffect)
		{
			State& s = S();
			DeathCapture& capture = g_capture;
			if (!capture.active || chunkEffect != capture.chunkEffect)
			{
				capture.active = false;
				return;
			}
			capture.active = false;

			size_t failed = 0;
			const size_t spawned = SpawnFromCapture(failed);
			s.spawned += spawned;
			s.failed += failed;
			if (AcquireLogSlot())
			{
				Logging::LogMessage(
					"[EXU::Gibs] person mesh=%s spawned=%u failed=%u bones=%u active=%u pool=%u at (%.1f, %.1f, %.1f)",
					capture.meshName, static_cast<unsigned>(spawned), static_cast<unsigned>(failed),
					static_cast<unsigned>(capture.boneCount), static_cast<unsigned>(s.activeCount),
					static_cast<unsigned>(s.slots.size()), capture.node.position.x, capture.node.position.y,
					capture.node.position.z);
			}
			if (spawned > 0 && Rt::LooksLikeOgreObject(capture.entity))
			{
				Rt::SetVisible(capture.entity, false);
			}
			capture.entity = nullptr;
			s.model.reset();
		}

		void EndCapture(void* chunkEffect) noexcept
		{
			const bool completed = Seh::Guard("Gibs::EndCapture", [&] { EndCaptureImpl(chunkEffect); },
				[](unsigned long code) { Logging::LogMessage("[EXU::Gibs] spawn faulted code=0x%08lX", code); });
			if (!completed)
			{
				// What state the pool is in is unknown: stop, and let go of it.
				g_capture.active = false;
				S().faulted = true;
				try
				{
					ForgetAll("spawn failed");
				}
				catch (...)
				{
				}
			}
		}

		void RemoveDetour() noexcept
		{
			if (g_detour != nullptr)
			{
				delete g_detour;   // restores the entry before freeing the trampoline
				g_detour = nullptr;
			}
			g_original = nullptr;
			g_removePending = false;
		}

		struct DepthScope
		{
			DepthScope() noexcept { ++g_depth; }
			~DepthScope() noexcept { --g_depth; }
		};

		void __fastcall FullFragmentHook(void* chunkEffect, void* /*edx*/, void* root, const float* velocity, std::uint8_t preserve)
		{
			const FullFragmentFn original = g_original;
			if (original == nullptr)
			{
				return;
			}
			const bool outermost = g_depth == 0;
			// Captures the person's pose now, before the engine turns its node
			// tree into chunks; false (and no cost beyond the gate) for anything
			// that is not a whole person.
			const bool capturing = outermost && BeginCapture(chunkEffect, root, velocity);
			{
				DepthScope scope;
				original(chunkEffect, root, velocity, preserve);
			}
			if (outermost)
			{
				if (capturing)
				{
					EndCapture(chunkEffect);
				}
				g_capture.active = false;
				if (g_removePending)
				{
					RemoveDetour();
				}
			}
		}

		bool InstallDetour(std::string& reason)
		{
			if (g_detour != nullptr)
			{
				return g_detour->IsActive();
			}
			const uintptr_t address = EngineAddresses::ChunkEffect::FullFragmentObject;
			if (!SignatureResolver::MatchBytes(address, kExpectedEntry, sizeof(kExpectedEntry)))
			{
				reason = "ChunkEffect::FullFragmentObject entry does not match (already detoured or a different build)";
				return false;
			}
			auto* raw = new (std::nothrow) EntryDetour32(address, reinterpret_cast<const void*>(&FullFragmentHook), kDetourLength,
				BasicPatch::Status::INACTIVE, std::vector<std::uint8_t>(kExpectedEntry, kExpectedEntry + kDetourLength));
			if (raw == nullptr)
			{
				reason = "could not allocate the death hook";
				return false;
			}
			if (!raw->PrepareTrampoline() || raw->GetTrampoline() == nullptr)
			{
				delete raw;
				reason = "death hook trampoline unavailable";
				return false;
			}
			g_detour = raw;
			g_original = reinterpret_cast<FullFragmentFn>(raw->GetTrampoline());
			g_detour->SetStatus(true);
			if (!g_detour->IsActive())
			{
				RemoveDetour();
				reason = "death hook could not activate (native patching refused)";
				return false;
			}
			return true;
		}

		// ----- Frame listener -------------------------------------------------------------------------

		void OnFrame(float realDt);

		// Same vtable shape as Ogre::FrameListener (1.10, no base class):
		// frameStarted, frameRenderingQueued, frameEnded, virtual destructor.
		// Every entry returns true: a false from frameStarted makes Ogre stop
		// rendering the frame.
		class GibFrameListener
		{
		public:
			virtual bool frameStarted(const FrameEventAbi& evt)
			{
				if (!S().faulted)
				{
					const bool completed = Seh::Guard("Gibs::frameStarted", [&] { OnFrame(evt.timeSinceLastFrame); },
						[](unsigned long code) {
							Logging::LogMessage("[EXU::Gibs] frame update faulted code=0x%08lX; gibs disabled for this mission", code);
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
			virtual ~GibFrameListener() = default;
		};

		GibFrameListener g_listener;

		bool RegisterListener()
		{
			State& s = S();
			if (s.listenerRegistered)
			{
				return true;
			}
			void* root = Rt::AddFrameListener(&g_listener);
			s.listenerRegistered = root != nullptr;
			s.listenerRoot = root;
			s.haveSimTime = false;
			s.listenerThread = 0;
			Logging::LogMessage("[EXU::Gibs] frame listener %s root=%p", s.listenerRegistered ? "registered" : "FAILED", root);
			return s.listenerRegistered;
		}

		void UnregisterListener()
		{
			State& s = S();
			if (!s.listenerRegistered)
			{
				return;
			}
			Rt::RemoveFrameListener(s.listenerRoot, &g_listener);
			s.listenerRegistered = false;
			s.listenerRoot = nullptr;
			Logging::LogMessage("[EXU::Gibs] frame listener removed");
		}

		void OnFrame(float realDt)
		{
			State& s = S();
			if (!s.listenerRegistered || s.faulted)
			{
				return;
			}

			// The hook and the listener share the pool, which is unlocked by
			// design (the engine's sim and render loop are one thread: see the
			// Spawn section of the design doc). Verify it rather than assume.
			if (s.listenerThread == 0)
			{
				s.listenerThread = GetCurrentThreadId();
				if (s.ownerThread != 0 && s.listenerThread != s.ownerThread)
				{
					s.crossThread = true;
					s.reason = "the frame listener runs on a different thread than the simulation";
					Logging::LogMessage("[EXU::Gibs] frame listener thread %lu differs from the enabling (sim) thread %lu; death capture stands down",
						static_cast<unsigned long>(s.listenerThread), static_cast<unsigned long>(s.ownerThread));
				}
				else
				{
					Logging::LogMessage("[EXU::Gibs] frame listener and hook share thread %lu", static_cast<unsigned long>(s.listenerThread));
				}
			}

			// Step by real frame time while the simulation clock advances
			// (zero while paused or stalled), so the gibs freeze with the game.
			const TimeSample time = ReadTime();
			const float step = Core::FrameStep(realDt, time.valid, time.paused, s.haveSimTime, s.lastSimTime, time.simTime);
			if (time.valid)
			{
				s.lastSimTime = time.simTime;
				s.haveSimTime = true;
			}
			if (s.activeCount == 0 || step <= 0.0f)
			{
				return;
			}

			// The scene the gibs were made in must still be the current one.
			if (!SceneIntact())
			{
				ForgetAll("scene changed or cleared");
				return;
			}

			BZR::VECTOR_3D origin{};
			if (!OgreRenderSpace::TryReadWorldRenderOrigin(origin) || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
				!std::isfinite(origin.z))
			{
				return;
			}
			// Gibs live in render space; terrain is sampled in sim space (the
			// inverse of OgreRenderSpace::SimPositionToRender: x + ox, -(z + oz)).
			const auto ground = [&origin](float renderX, float renderZ, float& out) {
				const float height = TerrainHeight(static_cast<double>(renderX) + origin.x, -(static_cast<double>(renderZ) + origin.z));
				if (!std::isfinite(height))
				{
					return false;
				}
				out = height - origin.y;
				return true;
			};

			for (Slot& slot : s.slots)
			{
				if (!slot.active)
				{
					continue;
				}
				const bool wasResting = slot.body.settled && !slot.body.sinking;
				if (!Core::StepGib(slot.body, step, s.linger, ground))
				{
					ReleaseSlot(slot);
					continue;
				}
				if (wasResting && slot.body.settled && !slot.body.sinking)
				{
					continue;   // nothing moved
				}
				const float position[3] = { slot.body.position.x, slot.body.position.y, slot.body.position.z };
				const float orientation[4] = { slot.body.orientation.w, slot.body.orientation.x, slot.body.orientation.y,
					slot.body.orientation.z };
				if (!Rt::PlaceNode(slot.node, position, orientation))
				{
					// The node is not ours any more; forget everything.
					ForgetAll("node update faulted");
					return;
				}
			}
		}

		// ----- Enable / disable ------------------------------------------------------------------------

		// Why gibs cannot run here, or empty when they can.
		std::string UnavailableReason()
		{
			if (!RuntimeGate::IsSupported())
			{
				return "unsupported or unqualified Battlezone build";
			}
			if (OpenShimBridge::GetModule() != nullptr)
			{
				return "OpenShim is loaded and provides skinned gibs";
			}
			if (const char* missing = Rt::FirstMissingExport())
			{
				return std::string("missing Ogre export: ") + missing;
			}
			return {};
		}

		void StopCapturing() noexcept
		{
			S().enabled = false;
			g_capture.active = false;
			if (g_depth > 0)
			{
				// Called from inside the original (a Lua callback during the
				// fragmentation): the hook removes itself when it unwinds.
				g_removePending = true;
				return;
			}
			RemoveDetour();
		}

		bool EnableGibs()
		{
			State& s = S();
			if (s.enabled && g_detour != nullptr && g_detour->IsActive())
			{
				return true;
			}
			const std::string unavailable = UnavailableReason();
			if (!unavailable.empty())
			{
				s.reason = unavailable;
				if (OpenShimBridge::GetModule() != nullptr)
				{
					if (!s.openShimLogged)
					{
						s.openShimLogged = true;
						Logging::LogMessage("[EXU::Gibs] standing down: OpenShim is live and provides skinned gibs");
					}
				}
				else
				{
					Logging::LogMessage("[EXU::Gibs] unavailable: %s", unavailable.c_str());
				}
				return false;
			}
			if (s.faulted)
			{
				s.reason = "gibs were disabled after a fault this mission (see exu.log)";
				return false;
			}
			s.ownerThread = GetCurrentThreadId();
			s.crossThread = false;
			if (!Payloads::EnsureResourceGroup())
			{
				s.reason = "payload resource group unavailable";
				return false;
			}
			if (!RegisterListener())
			{
				s.reason = "could not register the Ogre frame listener";
				return false;
			}
			std::string reason;
			if (!InstallDetour(reason))
			{
				s.reason = reason;
				Logging::LogMessage("[EXU::Gibs] unavailable: %s", reason.c_str());
				return false;
			}
			s.enabled = true;
			s.reason.clear();
			g_removePending = false;
			Logging::LogMessage("[EXU::Gibs] enabled: max=%u linger=%.1f force=%.2f", static_cast<unsigned>(s.max),
				static_cast<double>(s.linger), static_cast<double>(s.force));
			return true;
		}

		// ----- Lua ---------------------------------------------------------------------------------------

		int Enable(lua_State* L)
		{
			luaL_checkany(L, 1);
			const bool on = lua_toboolean(L, 1) != 0;
			bool active = false;
			if (on)
			{
				active = EnableGibs();
			}
			else
			{
				StopCapturing();
				S().reason = "not enabled";
			}
			lua_pushboolean(L, active ? 1 : 0);
			return 1;
		}

		int IsEnabled(lua_State* L)
		{
			const State& s = S();
			lua_pushboolean(L, (s.enabled && g_detour != nullptr && g_detour->IsActive()) ? 1 : 0);
			return 1;
		}

		bool OptionalNumber(lua_State* L, int table, const char* name, double& out)
		{
			lua_getfield(L, table, name);
			const bool present = !lua_isnil(L, -1);
			if (present)
			{
				out = luaL_checknumber(L, -1);
			}
			lua_pop(L, 1);
			if (present && !std::isfinite(out))
			{
				luaL_error(L, "exu.gibs.Configure: %s must be finite", name);
			}
			return present;
		}

		// exu.gibs.Configure{ max=, linger=, force= }
		int Configure(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			// Parse everything first: a Lua error longjmps over this frame, so
			// nothing with a destructor exists until parsing is done.
			double max = 0.0, linger = 0.0, force = 0.0;
			const bool haveMax = OptionalNumber(L, 1, "max", max);
			const bool haveLinger = OptionalNumber(L, 1, "linger", linger);
			const bool haveForce = OptionalNumber(L, 1, "force", force);

			State& s = S();
			if (haveMax)
			{
				s.max = static_cast<size_t>(std::clamp(std::floor(max), static_cast<double>(Core::kMinMax), static_cast<double>(Core::kMaxMax)));
			}
			if (haveLinger)
			{
				s.linger = static_cast<float>(std::clamp(linger, 0.0, static_cast<double>(Core::kMaxLinger)));
			}
			if (haveForce)
			{
				s.force = static_cast<float>(std::clamp(force, 0.0, static_cast<double>(Core::kMaxForce)));
			}
			return 0;
		}

		// Hides every live gib now (the objects are kept for reuse).
		int Clear(lua_State*)
		{
			State& s = S();
			if (s.activeCount > 0)
			{
				if (!SceneIntact())
				{
					ForgetAll("scene changed or cleared");
				}
				else
				{
					for (Slot& slot : s.slots)
					{
						if (slot.active)
						{
							ReleaseSlot(slot);
						}
					}
				}
			}
			return 0;
		}

		int GetStats(lua_State* L)
		{
			const State& s = S();
			size_t live = 0, settled = 0, sinking = 0;
			for (const Slot& slot : s.slots)
			{
				if (!slot.active)
				{
					continue;
				}
				++live;
				settled += slot.body.settled && !slot.body.sinking ? 1 : 0;
				sinking += slot.body.sinking ? 1 : 0;
			}
			lua_createtable(L, 0, 10);
			auto set = [L](const char* key, double value) {
				lua_pushnumber(L, value);
				lua_setfield(L, -2, key);
			};
			set("live", static_cast<double>(live));
			set("settled", static_cast<double>(settled));
			set("sinking", static_cast<double>(sinking));
			set("max", static_cast<double>(s.max));
			set("deaths", static_cast<double>(s.deaths));
			set("spawned", static_cast<double>(s.spawned));
			set("recycled", static_cast<double>(s.recycled));
			set("failed", static_cast<double>(s.failed));
			set("models", static_cast<double>(Payloads::ModelCount()));
			set("forgotten", static_cast<double>(s.forgotten));
			return 1;
		}

		// The capability `reason` text, in a plain buffer so GetCapabilities keeps
		// no C++ object alive across Lua calls that can raise.
		void ReasonText(char* out, size_t capacity, bool& outUnavailable)
		{
			const State& s = S();
			const std::string unavailable = UnavailableReason();
			outUnavailable = !unavailable.empty();
			const std::string& why = !unavailable.empty() ? unavailable : s.reason;
			std::snprintf(out, capacity, "%s", why.empty() ? "not enabled" : why.c_str());
		}

		// { gibs, openShim, hook, reason }. reason is present only while gibs are
		// not running, and says why.
		int GetCapabilities(lua_State* L)
		{
			char reason[256];
			bool unavailable = false;
			ReasonText(reason, sizeof(reason), unavailable);
			const bool openShim = OpenShimBridge::GetModule() != nullptr;
			const bool hook = g_detour != nullptr && g_detour->IsActive();
			const bool running = S().enabled && hook && !S().crossThread;
			const bool gibs = !unavailable && !S().faulted && !S().crossThread;
			lua_createtable(L, 0, 4);
			lua_pushboolean(L, gibs ? 1 : 0);
			lua_setfield(L, -2, "gibs");
			lua_pushboolean(L, openShim ? 1 : 0);
			lua_setfield(L, -2, "openShim");
			lua_pushboolean(L, hook ? 1 : 0);
			lua_setfield(L, -2, "hook");
			if (!running)
			{
				lua_pushstring(L, reason);
				lua_setfield(L, -2, "reason");
			}
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
			{ "Enable", &Enable },
			{ "Configure", &Configure },
			{ "IsEnabled", &IsEnabled },
			{ "Clear", &Clear },
			{ "GetStats", &GetStats },
			{ "GetCapabilities", &GetCapabilities },
			{ nullptr, nullptr },
		};
		RegisterFunctions(L, nullptr, functions);
		lua_setfield(L, -2, "gibs");
		lua_pop(L, 1);
	}

	void Shutdown() noexcept
	{
		try
		{
			State& s = S();
			// No new captures first, then the listener, then the objects.
			g_removePending = false;
			g_capture.active = false;
			RemoveDetour();
			const size_t live = s.activeCount;
			if (s.listenerRegistered || !s.slots.empty() || s.parentNode != nullptr)
			{
				DestroyAll(true);
				UnregisterListener();
				Logging::LogMessage(
					"[EXU::Gibs] shutdown: destroyed %u live; totals deaths=%llu spawned=%llu recycled=%llu failed=%llu models=%u",
					static_cast<unsigned>(live), static_cast<unsigned long long>(s.deaths), static_cast<unsigned long long>(s.spawned),
					static_cast<unsigned long long>(s.recycled), static_cast<unsigned long long>(s.failed),
					static_cast<unsigned>(Payloads::ModelCount()));
			}
			Payloads::ResetModels();
			s = State{};
		}
		catch (...)
		{
			OutputDebugStringA("ExtraUtilities: exception during skinned gibs shutdown\n");
		}
	}
}
