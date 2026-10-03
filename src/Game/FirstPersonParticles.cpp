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

#include "Game/FirstPersonParticles.h"

#include "EnvironmentInternal.h"

namespace ExtraUtilities::Lua::FirstPersonParticles
{
	namespace
	{
		BindingBook g_book;

		// Reads the system's live parent. False when the system does not exist
		// in the current scene (or Ogre could not be asked).
		bool Observe(void* sceneManager, const std::string& name, void*& outMovable, ObservedParent& outObserved)
		{
			outMovable = nullptr;
			outObserved = {};
			if (sceneManager == nullptr ||
				!Environment::TryGetParticleMovableObject(sceneManager, name, outMovable))
			{
				return false;
			}

			void* tagPoint = nullptr;
			void* entity = nullptr;
			if (!Environment::TryGetMovableObjectTagPointParent(outMovable, tagPoint, entity))
			{
				return false;
			}
			outObserved.tagPoint = tagPoint;
			outObserved.tagPointEntity = entity;
			return true;
		}

		void* CurrentSceneManager()
		{
			Patch::TryInitializeOgre();
			return Environment::GetSceneManager();
		}
	}

	bool IsSupported() noexcept
	{
		using namespace Environment;
		return ResolveEntityHasSkeleton() != nullptr &&
			ResolveEntityAttachObjectToBone() != nullptr &&
			ResolveMovableObjectDetachFromParent() != nullptr &&
			ResolveMovableObjectGetParentNode() != nullptr &&
			ResolveMovableObjectIsParentTagPoint() != nullptr &&
			ResolveTagPointGetParentEntity() != nullptr &&
			ResolveEntityGetSkeleton() != nullptr &&
			ResolveSkeletonHasBone() != nullptr &&
			ResolveAttachObject() != nullptr;
	}

	AttachResult Attach(void* firstPersonEntity, const std::string& name, const std::string& bone,
		const Offset& offset) noexcept
	{
		AttachResult result{};
		try
		{
			if (firstPersonEntity == nullptr || !IsSupported())
			{
				return result;
			}

			void* const sceneManager = CurrentSceneManager();
			void* movable = nullptr;
			ObservedParent observed{};
			if (!Observe(sceneManager, name, movable, observed))
			{
				// No such system (destroyed, or a previous mission's name).
				g_book.Forget(name);
				return result;
			}

			if (g_book.Decide(name, firstPersonEntity, bone, offset, observed) == Decision::AlreadyAttached)
			{
				result.attached = true;
				return result;
			}

			if (!g_book.CanRecord(name))
			{
				Environment::LogEnvironmentDebug(
					"[EXU::FpParticle] attach refused name=%s reason=binding_limit(%u)",
					name.c_str(), static_cast<unsigned>(BindingBook::kMaxBindings));
				return result;
			}

			if (!Environment::TryEntityHasBone(firstPersonEntity, bone))
			{
				Environment::LogEnvironmentDebug(
					"[EXU::FpParticle] attach failed name=%s bone=%s reason=no_such_bone_on_fp_entity",
					name.c_str(), bone.c_str());
				return result;
			}

			// The camera-follower list must not also drive this system.
			Environment::ForgetParticleCameraFollower(name);

			const BZR::VECTOR_3D position{ offset.x, offset.y, offset.z };
			if (!Environment::TryAttachManagedParticleToBone(sceneManager, name, firstPersonEntity, bone, position))
			{
				// TryAttachManagedParticleToBone detaches before attaching, so a
				// failure can leave the system unparented: put it back on its own
				// node rather than leave it invisible and un-updated.
				g_book.Forget(name);
				Environment::TryReturnManagedParticleToOwnNode(sceneManager, name);
				return result;
			}

			// Record the TagPoint Ogre actually used, as an identity token.
			ObservedParent after{};
			if (!Observe(sceneManager, name, movable, after) || after.tagPointEntity != firstPersonEntity)
			{
				g_book.Forget(name);
				return result;
			}

			const std::uint32_t generationBefore = g_book.TargetGeneration();
			g_book.Record(name, firstPersonEntity, bone, offset, after.tagPoint);
			if (g_book.TargetGeneration() != generationBefore)
			{
				Environment::LogEnvironmentDebug(
					"[EXU::FpParticle] first-person target changed entity=%p generation=%u",
					firstPersonEntity, static_cast<unsigned>(g_book.TargetGeneration()));
			}
			result.attached = true;
			result.reattached = true;
			return result;
		}
		catch (...)
		{
			return AttachResult{};
		}
	}

	bool IsAttached(void* firstPersonEntity, const std::string& name) noexcept
	{
		try
		{
			if (firstPersonEntity == nullptr || !g_book.Has(name) || !IsSupported())
			{
				return false;
			}

			void* movable = nullptr;
			ObservedParent observed{};
			if (!Observe(CurrentSceneManager(), name, movable, observed))
			{
				return false;
			}
			return g_book.IsAttached(name, firstPersonEntity, observed);
		}
		catch (...)
		{
			return false;
		}
	}

	bool Detach(const std::string& name) noexcept
	{
		try
		{
			if (!g_book.Forget(name))
			{
				return false;
			}

			void* const sceneManager = CurrentSceneManager();
			void* movable = nullptr;
			ObservedParent observed{};
			if (Observe(sceneManager, name, movable, observed) && observed.tagPoint != nullptr)
			{
				// detachFromParent goes through the system's LIVE parent, which is
				// a valid TagPoint (Ogre nulls it when the owning entity dies).
				Environment::TryReturnManagedParticleToOwnNode(sceneManager, name);
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	void Forget(const std::string& name) noexcept
	{
		try
		{
			g_book.Forget(name);
		}
		catch (...)
		{
		}
	}

	std::uint32_t TargetGeneration() noexcept
	{
		return g_book.TargetGeneration();
	}

	void ResetMissionState() noexcept
	{
		g_book.Clear();
	}
}
