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

#pragma once

// exu.fps.AttachParticleToBone: an EXU-managed particle system riding a bone
// of the local player's first-person (cockpit) entity.
//
// Where the FP entity lives (BZR 2.2.301, see Docs/FPS_API.md):
//   * It is an ordinary Ogre::Entity on its own SceneNode under SceneRoot, a
//     sibling of the world pilot's node with a near-identical transform. The
//     camera is the FP skeleton's *POV bone, so the FP bones are in WORLD space
//     and a TagPoint-attached particle system lands at the real muzzle.
//   * FUN_0067E6A8 creates it: setCastShadows(false), setRenderQueueGroup(
//     [0x008ED6A8] = 10) and infinite mesh bounds (never frustum culled). No
//     separate scene, camera or viewport.
//   * Group 10 draws before terrain (40). That is why FP passes without depth
//     write vanish except over the rifle. A particle system on a TagPoint is
//     queued by Entity::_updateRenderQueue only while the FP entity is
//     rendered, but into the particle renderer's OWN group (default 50), after
//     terrain, so ordinary depth_write-off particle materials work.
//
// Lifetime: Ogre's Entity::_deinitialise detaches every bone child
// (_notifyAttached(0)), so the FP entity being destroyed leaves the particle
// system alive and unparented, never pointing at a freed TagPoint. EXU keeps
// no Ogre pointer it dereferences later: the recorded entity and TagPoint are
// identity tokens, compared against the particle system's LIVE parent each
// call. A new entity at a recycled address is caught because the particle is
// no longer parented to the recorded TagPoint.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace ExtraUtilities::Lua::FirstPersonParticles
{
	struct Offset
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		bool operator==(const Offset& other) const noexcept
		{
			return x == other.x && y == other.y && z == other.z;
		}
		bool operator!=(const Offset& other) const noexcept { return !(*this == other); }
	};

	// What the particle system is parented to right now, read from Ogre for
	// this call. tagPoint is null when the system is not on a bone.
	struct ObservedParent
	{
		const void* tagPoint = nullptr;
		const void* tagPointEntity = nullptr;
	};

	struct Binding
	{
		std::string bone;
		Offset offset{};
		const void* entity = nullptr;   // identity token only, never dereferenced
		const void* tagPoint = nullptr; // identity token only, never dereferenced
	};

	enum class Decision
	{
		AlreadyAttached, // nothing to do
		Attach,          // (re)attach, then Record
	};

	// The bookkeeping, Ogre-free so tests/host can drive it.
	class BindingBook
	{
	public:
		// Generous for muzzle flash, smoke, casings and a few spares; a script
		// that leaks names hits the cap instead of growing the map forever.
		static constexpr std::size_t kMaxBindings = 64;

		Decision Decide(const std::string& name, const void* entity, const std::string& bone,
			const Offset& offset, const ObservedParent& observed) const
		{
			const auto it = m_bindings.find(name);
			if (it == m_bindings.end())
				return Decision::Attach;
			const Binding& binding = it->second;
			if (binding.entity != entity || binding.bone != bone || binding.offset != offset)
				return Decision::Attach;
			// Detached by the entity's destruction, moved by another API, or a
			// recycled entity address: the live parent is the authority.
			if (binding.tagPoint == nullptr || observed.tagPoint != binding.tagPoint ||
				observed.tagPointEntity != entity)
				return Decision::Attach;
			return Decision::AlreadyAttached;
		}

		// True when this binding is live on `entity` right now.
		bool IsAttached(const std::string& name, const void* entity, const ObservedParent& observed) const
		{
			const auto it = m_bindings.find(name);
			if (it == m_bindings.end() || entity == nullptr)
				return false;
			const Binding& binding = it->second;
			return binding.entity == entity && binding.tagPoint != nullptr &&
				observed.tagPoint == binding.tagPoint && observed.tagPointEntity == entity;
		}

		// False only when the book is full and `name` is new.
		bool CanRecord(const std::string& name) const
		{
			return m_bindings.size() < kMaxBindings || m_bindings.count(name) != 0;
		}

		// After a successful native attach. Bumps the target generation when
		// the FP entity differs from the last one any binding was made on.
		bool Record(const std::string& name, const void* entity, const std::string& bone,
			const Offset& offset, const void* tagPoint)
		{
			if (!CanRecord(name))
				return false;
			Binding& binding = m_bindings[name];
			binding.bone = bone;
			binding.offset = offset;
			binding.entity = entity;
			binding.tagPoint = tagPoint;
			if (entity != m_lastEntity)
			{
				m_lastEntity = entity;
				++m_targetGeneration;
			}
			return true;
		}

		bool Has(const std::string& name) const { return m_bindings.count(name) != 0; }

		bool Forget(const std::string& name) { return m_bindings.erase(name) != 0; }

		// Mission teardown: the scene behind every token is gone. The
		// generation keeps counting so a stale Lua copy never matches again.
		void Clear()
		{
			m_bindings.clear();
			m_lastEntity = nullptr;
		}

		std::size_t Count() const { return m_bindings.size(); }

		// Increments each time a binding is made on a different FP entity than
		// the previous binding (respawn, re-board and exit, new mission).
		std::uint32_t TargetGeneration() const { return m_targetGeneration; }

	private:
		std::map<std::string, Binding> m_bindings;
		const void* m_lastEntity = nullptr;
		std::uint32_t m_targetGeneration = 0;
	};

	// ----- Runtime (FirstPersonParticles.cpp) -------------------------------
	// Lua thread only. firstPersonEntity is the caller's one-operation
	// snapshot of the local FP entity (null when there is none).

	// The Ogre entry points this feature needs resolved from OgreMain.
	bool IsSupported() noexcept;

	struct AttachResult
	{
		bool attached = false;
		bool reattached = false; // this call made the Ogre attachment
	};

	AttachResult Attach(void* firstPersonEntity, const std::string& name, const std::string& bone,
		const Offset& offset) noexcept;

	bool IsAttached(void* firstPersonEntity, const std::string& name) noexcept;

	// Forgets the binding; when the system still sits on a bone, returns it to
	// its own EXU node (Ogre detaches through the live parent, so this is safe
	// whether or not the old FP entity still exists). True when a binding
	// existed.
	bool Detach(const std::string& name) noexcept;

	// Drops the binding without touching Ogre (the generic particle API took
	// the system over, or it is being destroyed).
	void Forget(const std::string& name) noexcept;

	std::uint32_t TargetGeneration() noexcept;

	// Mission teardown / Ogre rebinding: drops every binding without Ogre.
	void ResetMissionState() noexcept;
}
