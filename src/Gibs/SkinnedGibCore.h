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

// Engine-free core of exu.gibs (ported from OpenShim's skinned_gibs.cpp,
// commit 72134279c): vector/quaternion math, the legacy chunk launch, the
// pose of a bone-frame piece, the flesh ground response and the pool's slot
// choice. Free of Windows, Ogre and Lua so tests/host can drive it.
//
// Gibs live in Ogre RENDER space (their start pose is read from the dead
// body's scene node), so the ground callback handed to StepGib converts to
// simulation space for the terrain query; see SkinnedGibs.cpp.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::SkinnedGibCore
{
	struct V3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	// Ogre's layout and convention: w first, rotates local to world.
	struct Quat
	{
		float w = 1.0f;
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	inline V3 Add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3 Scale(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline V3 Mul(V3 a, V3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
	inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline float Length(V3 a) { return std::sqrt(Dot(a, a)); }
	inline bool Finite(V3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
	inline bool Finite(const Quat& q) { return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z); }

	inline Quat QMul(Quat a, Quat b)
	{
		return {
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		};
	}

	inline Quat QNormalize(Quat q)
	{
		const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
		if (!(n > 1e-8f) || !std::isfinite(n))
		{
			return { 1.0f, 0.0f, 0.0f, 0.0f };
		}
		return { q.w / n, q.x / n, q.y / n, q.z / n };
	}

	inline V3 Rotate(Quat q, V3 v)
	{
		const Quat r = QMul(QMul(q, { 0.0f, v.x, v.y, v.z }), { q.w, -q.x, -q.y, -q.z });
		return { r.x, r.y, r.z };
	}

	// ----- Tuning ------------------------------------------------------------
	// Launch values are the legacy engine's own; the ground response is new
	// (legacy chunks explode on first terrain contact), tuned so limbs thud,
	// slide a little and lie still instead of bouncing.
	struct Tuning
	{
		// ChunkEffect::CreateChunk (BZ 1.5 0x004BE555): each axis gets
		// Pseudo_Rand() * 10 plus the fragment velocity, and +5 up;
		// omega = Pseudo_Rand() * 5 rad/s per axis.
		float launchRandom = 10.0f;
		float launchUp = 5.0f;
		float spin = 5.0f;
		// ChunkEffect::FullFragmentObject (0x004BF08B): every piece is kicked
		// away from the fragment root's world position by (p - origin) * (2, 1, 2).
		float kickHorizontal = 2.0f;
		float kickVertical = 1.0f;
		// ChunkEffect::Simulate: veloc.y -= dt * 9.8.
		float gravity = 9.8f;
		// Flesh ground response (not legacy).
		float restitution = 0.15f;    // normal speed kept per bounce
		float friction = 0.55f;       // tangential speed lost per bounce
		float spinDamp = 0.5f;        // omega kept per bounce
		float settleSpeed = 0.6f;     // below this after a bounce: at rest
		float contactRadius = 0.35f;  // fraction of radius kept above ground
		float heavyRadius = 0.3f;     // larger pieces spin at half rate
		float sinkDepth = 0.4f;
		float sinkSeconds = 2.0f;
		float maxLifetime = 20.0f;
		float maxStep = 0.1f;
		float minRadius = 0.02f;
		float maxBodySpeed = 500.0f;  // a death velocity above this is discarded
	};
	inline constexpr Tuning kTuning = {};

	// Lua-facing limits (OpenShim's env clamps).
	inline constexpr size_t kMinMax = 12;
	inline constexpr size_t kMaxMax = 1024;
	inline constexpr size_t kDefaultMax = 160;
	inline constexpr float kDefaultLinger = 8.0f;
	inline constexpr float kMaxLinger = 60.0f;
	inline constexpr float kDefaultForce = 1.0f;
	inline constexpr float kMaxForce = 5.0f;

	// Legacy Pseudo_Rand reads a 256-entry table of values in [-1, 1] clustered
	// around zero; the mean of two uniforms has that shape.
	struct GibRandom
	{
		uint32_t state = 1;

		float Uniform()
		{
			state = state * 1664525u + 1013904223u;
			return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
		}

		float Legacy()
		{
			// Separate statements: the two draws must happen in a fixed order.
			const float a = Uniform() * 2.0f - 1.0f;
			const float b = Uniform() * 2.0f - 1.0f;
			return (a + b) * 0.5f;
		}
	};

	// ----- Spawn -------------------------------------------------------------

	// The dead body's scene node (derived pose) and one bone's derived pose,
	// both as captured on the frame of death.
	struct NodePose
	{
		V3 position;
		Quat orientation;
		V3 scale = { 1.0f, 1.0f, 1.0f };
	};

	struct BoneFrame
	{
		V3 position;
		Quat orientation;
	};

	struct PiecePose
	{
		V3 position;
		Quat orientation;
	};

	// Bone-frame runtime piece: world = node pose * (bone derived pose * pivot).
	// False when the result is not finite.
	inline bool ComposePiecePose(const NodePose& node, const BoneFrame& bone, V3 pivot, PiecePose& out)
	{
		const V3 local = Add(bone.position, Rotate(bone.orientation, pivot));
		out.position = Add(node.position, Rotate(node.orientation, Mul(node.scale, local)));
		out.orientation = QNormalize(QMul(node.orientation, bone.orientation));
		return Finite(out.position) && Finite(out.orientation);
	}

	// A death velocity that is unreadable or implausible launches nothing.
	inline V3 SanitizeBodyVelocity(V3 velocity, const Tuning& tuning = kTuning)
	{
		if (!Finite(velocity) || Length(velocity) > tuning.maxBodySpeed)
		{
			return {};
		}
		return velocity;
	}

	inline float PieceRadius(float radius, const Tuning& tuning = kTuning)
	{
		return std::isfinite(radius) ? std::max(radius, tuning.minRadius) : tuning.minRadius;
	}

	struct LaunchResult
	{
		V3 velocity;
		V3 omega;
	};

	// Draw order is part of the contract (six Legacy() draws: velocity x, y, z,
	// then omega x, y, z), so a seed always produces the same launch.
	inline LaunchResult ComputeLaunch(GibRandom& random, V3 world, V3 origin, V3 bodyVelocity, float radius, float force,
		const Tuning& tuning = kTuning)
	{
		const float spinScale = radius > tuning.heavyRadius ? 0.5f : 1.0f;
		const V3 kick = Mul(Sub(world, origin), { tuning.kickHorizontal, tuning.kickVertical, tuning.kickHorizontal });
		const float vx = random.Legacy() * tuning.launchRandom * force;
		const float vy = random.Legacy() * tuning.launchRandom * force + tuning.launchUp;
		const float vz = random.Legacy() * tuning.launchRandom * force;
		LaunchResult result;
		result.velocity = Add(Add(V3{ vx, vy, vz }, kick), bodyVelocity);
		const float ox = random.Legacy() * tuning.spin * spinScale;
		const float oy = random.Legacy() * tuning.spin * spinScale;
		const float oz = random.Legacy() * tuning.spin * spinScale;
		result.omega = { ox, oy, oz };
		if (!Finite(result.velocity))
		{
			result.velocity = { 0.0f, tuning.launchUp, 0.0f };
		}
		if (!Finite(result.omega))
		{
			result.omega = {};
		}
		return result;
	}

	// ----- Simulation --------------------------------------------------------

	struct Body
	{
		V3 position;
		V3 velocity;
		V3 omega;
		Quat orientation;
		V3 scale = { 1.0f, 1.0f, 1.0f };
		float radius = 0.0f;
		float age = 0.0f;
		float settledAge = 0.0f;
		float sinkElapsed = 0.0f;
		bool settled = false;
		bool sinking = false;
		bool impacted = false;
		bool hasGround = true;
	};

	// Ground height under (x, z) in the SAME space as Body::position (render
	// space for the runtime): `bool(float x, float z, float& height)`; false
	// means there is no terrain there.
	template <typename Ground>
	V3 GroundNormal(Ground&& ground, float x, float z)
	{
		constexpr float kStep = 0.25f;
		float hx0 = 0.0f, hx1 = 0.0f, hz0 = 0.0f, hz1 = 0.0f;
		if (!ground(x - kStep, z, hx0) || !ground(x + kStep, z, hx1) || !ground(x, z - kStep, hz0) ||
			!ground(x, z + kStep, hz1))
		{
			return { 0.0f, 1.0f, 0.0f };
		}
		const V3 n = { hx0 - hx1, 2.0f * kStep, hz0 - hz1 };
		const float length = Length(n);
		return (length > 1e-6f && std::isfinite(length)) ? Scale(n, 1.0f / length) : V3{ 0.0f, 1.0f, 0.0f };
	}

	// Advances one gib by dt seconds. Returns false when it is finished (or its
	// state went non-finite) and the slot should be released.
	template <typename Ground>
	bool StepGib(Body& body, float dt, float linger, Ground&& ground, const Tuning& tuning = kTuning)
	{
		if (!Finite(body.position) || !Finite(body.velocity) || !Finite(body.omega) || !Finite(body.orientation))
		{
			return false;
		}
		if (!std::isfinite(dt) || dt <= 0.0f)
		{
			return true;
		}

		body.age += dt;
		if (body.age >= tuning.maxLifetime + tuning.sinkSeconds)
		{
			return false;
		}

		if (body.sinking)
		{
			body.sinkElapsed += dt;
			body.position.y -= tuning.sinkDepth / tuning.sinkSeconds * dt;
			return body.sinkElapsed < tuning.sinkSeconds;
		}
		if (body.settled)
		{
			body.settledAge += dt;
			if (body.settledAge >= linger || body.age >= tuning.maxLifetime - tuning.sinkSeconds)
			{
				body.sinking = true;
			}
			return true;
		}
		if (body.age >= tuning.maxLifetime)
		{
			return false;   // still airborne (no ground below): just retire
		}

		body.velocity.y -= tuning.gravity * dt;
		body.position = Add(body.position, Scale(body.velocity, dt));
		const Quat spin = { 0.0f, body.omega.x, body.omega.y, body.omega.z };
		const Quat delta = QMul(spin, body.orientation);
		body.orientation = QNormalize({ body.orientation.w + 0.5f * dt * delta.w, body.orientation.x + 0.5f * dt * delta.x,
			body.orientation.y + 0.5f * dt * delta.y, body.orientation.z + 0.5f * dt * delta.z });

		float groundHeight = 0.0f;
		if (!body.hasGround || !ground(body.position.x, body.position.z, groundHeight) || !std::isfinite(groundHeight))
		{
			// No terrain: the ground is -infinity; fall, then expire.
			body.hasGround = false;
			return Finite(body.position);
		}
		const float lift = body.radius * tuning.contactRadius;
		if (body.position.y - lift < groundHeight)
		{
			body.position.y = groundHeight + lift;
			const V3 normal = GroundNormal(ground, body.position.x, body.position.z);
			const float normalSpeed = Dot(body.velocity, normal);
			if (normalSpeed < 0.0f)
			{
				const V3 normalPart = Scale(normal, normalSpeed);
				const V3 tangentPart = Sub(body.velocity, normalPart);
				body.velocity = Sub(Scale(tangentPart, 1.0f - tuning.friction), Scale(normalPart, tuning.restitution));
				body.omega = Scale(body.omega, tuning.spinDamp);
				body.impacted = true;
				if (Length(body.velocity) < tuning.settleSpeed)
				{
					body.settled = true;
					body.velocity = {};
					body.omega = {};
				}
			}
		}
		return Finite(body.position);
	}

	// The step the runtime may take for one rendered frame: the real frame
	// time (smooth), clamped to maxStep, but only on frames where the
	// simulation clock advanced, as exu.casing. Zero while paused, before the
	// first sample, or on a stall/jump (a restart or load moves the clock by
	// more than a second).
	inline float FrameStep(float realDt, bool timeValid, bool paused, bool haveLast, float lastSimTime, float simTime,
		const Tuning& tuning = kTuning)
	{
		if (!timeValid || paused || !haveLast || !std::isfinite(realDt))
		{
			return 0.0f;
		}
		const float advance = simTime - lastSimTime;
		if (!(advance > 0.0f) || advance > 1.0f)
		{
			return 0.0f;
		}
		return std::min(std::max(realDt, 0.0f), tuning.maxStep);
	}

	// ----- Pool --------------------------------------------------------------

	// What the slot choice needs to know about one slot.
	struct SlotView
	{
		bool active = false;
		bool sceneMatches = false;
		bool hasEntityForMesh = false;   // slot.mesh == wanted mesh and it owns an entity
		bool settled = false;
		uint64_t serial = 0;
	};

	enum class SlotChoiceKind
	{
		None,        // nothing usable
		ReuseMesh,   // free slot already holding the wanted mesh: no new entity
		Append,      // a new slot (pool below its limit)
		ReuseFree,   // free slot of another mesh: its entity is replaced
		Recycle,     // pool full: the victim is released first, then reused
	};

	struct SlotChoice
	{
		SlotChoiceKind kind = SlotChoiceKind::None;
		size_t index = 0;   // slot index; unused for Append
	};

	inline SlotChoice ChooseSlot(const SlotView* slots, size_t count, size_t limit)
	{
		limit = std::max<size_t>(limit, 1);
		bool haveFree = false;
		size_t freeIndex = 0;
		for (size_t i = 0; i < count; ++i)
		{
			const SlotView& slot = slots[i];
			if (slot.active || !slot.sceneMatches)
			{
				continue;
			}
			if (slot.hasEntityForMesh)
			{
				return { SlotChoiceKind::ReuseMesh, i };
			}
			if (!haveFree)
			{
				haveFree = true;
				freeIndex = i;
			}
		}
		if (count < limit)
		{
			return { SlotChoiceKind::Append, count };
		}
		if (haveFree)
		{
			return { SlotChoiceKind::ReuseFree, freeIndex };
		}
		// Full: recycle the oldest gib already at rest, else the oldest.
		bool haveSettled = false, haveOldest = false;
		size_t settledIndex = 0, oldestIndex = 0;
		for (size_t i = 0; i < count; ++i)
		{
			const SlotView& slot = slots[i];
			if (!slot.active)
			{
				continue;
			}
			if (slot.settled && (!haveSettled || slot.serial < slots[settledIndex].serial))
			{
				haveSettled = true;
				settledIndex = i;
			}
			if (!haveOldest || slot.serial < slots[oldestIndex].serial)
			{
				haveOldest = true;
				oldestIndex = i;
			}
		}
		if (haveSettled)
		{
			return { SlotChoiceKind::Recycle, settledIndex };
		}
		if (haveOldest)
		{
			return { SlotChoiceKind::Recycle, oldestIndex };
		}
		return {};
	}
}
