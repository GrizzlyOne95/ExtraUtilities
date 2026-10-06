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

// Physics of one shell casing (exu.casing), free of the engine, Ogre and Lua
// so tests/host can drive it. Everything is in SIMULATION space (the space of
// Lua GetPosition and Terrain::HeightAt): +y up, metres, seconds. The runtime
// (ShellCasings.cpp) converts to render space only when it places the node.
//
// A casing is a small rigid body approximated for contact as a sphere of
// `radius` at its centre. That is exactly right for a cylinder lying on its
// side, which is where every casing ends up: on contact the body is turned
// toward "axis perpendicular to the surface normal" (LayFlat), so it tumbles
// in the air, bounces, then rolls/slides to rest lying flat.
//
// Obstacles (the shooter's hull and nearby objects) are oriented boxes or
// spheres with a velocity; contact reflects the RELATIVE velocity, so a casing
// that lands on a moving deck is carried, not left behind.

#include <cmath>
#include <cstdint>

namespace ExtraUtilities::ShellCasingMath
{
	struct V3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	// Ogre's layout and convention: w first, unit length, rotates local to world.
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
	inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline float Length(V3 a) { return std::sqrt(Dot(a, a)); }
	inline bool IsFinite(float v) { return std::isfinite(v); }
	inline bool IsFinite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
	inline bool IsFinite(const Quat& q) { return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z); }

	inline V3 NormalizeOr(V3 a, V3 fallback)
	{
		const float length = Length(a);
		return (length > 1e-6f && std::isfinite(length)) ? Scale(a, 1.0f / length) : fallback;
	}

	inline Quat Mul(const Quat& a, const Quat& b)
	{
		return {
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		};
	}

	inline Quat Normalize(const Quat& q)
	{
		const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
		if (!(n > 1e-6f) || !std::isfinite(n))
		{
			return Quat{};
		}
		const float inv = 1.0f / n;
		return { q.w * inv, q.x * inv, q.y * inv, q.z * inv };
	}

	inline V3 Rotate(const Quat& q, V3 v)
	{
		// v' = v + 2w (u x v) + 2 u x (u x v), u = (x, y, z)
		const V3 u{ q.x, q.y, q.z };
		const V3 t = Scale(Cross(u, v), 2.0f);
		return Add(Add(v, Scale(t, q.w)), Cross(u, t));
	}

	inline Quat FromAxisAngle(V3 unitAxis, float angle)
	{
		const float s = std::sin(angle * 0.5f);
		return { std::cos(angle * 0.5f), unitAxis.x * s, unitAxis.y * s, unitAxis.z * s };
	}

	// A right-handed rotation matrix given as three unit world-space columns
	// (local x, y, z axes) to a quaternion.
	inline Quat FromAxes(V3 xAxis, V3 yAxis, V3 zAxis)
	{
		const float m00 = xAxis.x, m10 = xAxis.y, m20 = xAxis.z;
		const float m01 = yAxis.x, m11 = yAxis.y, m21 = yAxis.z;
		const float m02 = zAxis.x, m12 = zAxis.y, m22 = zAxis.z;
		const float trace = m00 + m11 + m22;
		Quat q;
		if (trace > 0.0f)
		{
			const float s = std::sqrt(trace + 1.0f) * 2.0f;
			q = { 0.25f * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s };
		}
		else if (m00 > m11 && m00 > m22)
		{
			const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
			q = { (m21 - m12) / s, 0.25f * s, (m01 + m10) / s, (m02 + m20) / s };
		}
		else if (m11 > m22)
		{
			const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
			q = { (m02 - m20) / s, (m01 + m10) / s, 0.25f * s, (m12 + m21) / s };
		}
		else
		{
			const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
			q = { (m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25f * s };
		}
		return Normalize(q);
	}

	// Integrates a WORLD-space angular velocity (rad/s) over dt.
	inline Quat IntegrateSpin(const Quat& q, V3 omega, float dt)
	{
		const float rate = Length(omega);
		if (!(rate * dt > 1e-7f))
		{
			return q;
		}
		return Normalize(Mul(FromAxisAngle(Scale(omega, 1.0f / rate), rate * dt), q));
	}

	// ----- Terrain ----------------------------------------------------------

	// Surface normal from central differences of four height samples taken at
	// x -/+ step and z -/+ step around a point.
	inline V3 TerrainNormal(float hxMinus, float hxPlus, float hzMinus, float hzPlus, float step)
	{
		return NormalizeOr({ hxMinus - hxPlus, 2.0f * step, hzMinus - hzPlus }, V3{ 0.0f, 1.0f, 0.0f });
	}

	// ----- Obstacles --------------------------------------------------------

	struct Obstacle
	{
		uint32_t handle = 0;
		V3 center;
		// Box: three unit world-space axes and half extents along them.
		bool box = false;
		V3 axis[3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
		V3 half;
		// Sphere (also the broad-phase bound of a box).
		float radius = 0.0f;
		// World velocity of the obstacle, for relative-velocity contact.
		V3 velocity;
	};

	struct Contact
	{
		bool hit = false;
		V3 normal{ 0.0f, 1.0f, 0.0f };   // out of the obstacle, toward the casing
		float depth = 0.0f;              // push along normal that clears it
	};

	inline Contact SphereContact(V3 point, float radius, V3 center, float obstacleRadius)
	{
		Contact c;
		const V3 d = Sub(point, center);
		const float reach = radius + obstacleRadius;
		const float distSq = Dot(d, d);
		if (distSq >= reach * reach)
		{
			return c;
		}
		const float dist = std::sqrt(distSq);
		c.hit = true;
		c.normal = dist > 1e-5f ? Scale(d, 1.0f / dist) : V3{ 0.0f, 1.0f, 0.0f };
		c.depth = reach - dist;
		return c;
	}

	// Sphere (point, radius) against an oriented box. A centre inside the box
	// leaves through the nearest face.
	inline Contact BoxContact(V3 point, float radius, const Obstacle& box)
	{
		Contact c;
		const V3 d = Sub(point, box.center);
		const float local[3] = { Dot(d, box.axis[0]), Dot(d, box.axis[1]), Dot(d, box.axis[2]) };
		const float half[3] = { box.half.x, box.half.y, box.half.z };
		float clamped[3];
		bool inside = true;
		for (int i = 0; i < 3; ++i)
		{
			clamped[i] = local[i] < -half[i] ? -half[i] : (local[i] > half[i] ? half[i] : local[i]);
			inside = inside && clamped[i] == local[i];
		}
		if (!inside)
		{
			V3 delta{};
			for (int i = 0; i < 3; ++i)
			{
				delta = Add(delta, Scale(box.axis[i], local[i] - clamped[i]));
			}
			const float dist = Length(delta);
			if (dist >= radius)
			{
				return c;
			}
			c.hit = true;
			c.normal = dist > 1e-6f ? Scale(delta, 1.0f / dist) : box.axis[1];
			c.depth = radius - dist;
			return c;
		}
		int best = 0;
		float bestGap = half[0] - std::fabs(local[0]);
		for (int i = 1; i < 3; ++i)
		{
			const float gap = half[i] - std::fabs(local[i]);
			if (gap < bestGap)
			{
				bestGap = gap;
				best = i;
			}
		}
		c.hit = true;
		c.normal = Scale(box.axis[best], local[best] < 0.0f ? -1.0f : 1.0f);
		c.depth = bestGap + radius;
		return c;
	}

	inline Contact ObstacleContact(V3 point, float radius, const Obstacle& obstacle)
	{
		// Broad phase on the bounding sphere first: cheap and rejects nearly all.
		if (!SphereContact(point, radius, obstacle.center, obstacle.radius).hit)
		{
			return {};
		}
		return obstacle.box ? BoxContact(point, radius, obstacle) : SphereContact(point, radius, obstacle.center, obstacle.radius);
	}

	// ----- Contact response -------------------------------------------------

	struct Surface
	{
		float restitution = 0.35f;
		float friction = 0.4f;
	};

	struct Tuning
	{
		float gravity = 9.8f;
		// Below this approach speed (m/s) a contact does not bounce at all, so
		// a resting casing cannot jitter.
		float bounceThreshold = 0.7f;
		// Fraction of spin kept through a bounce, and the gain that turns the
		// sliding (tangent) velocity into rolling spin about the contact.
		float spinKeep = 0.55f;
		float rollGain = 0.6f;
		// Extra tumble added on a hard impact, rad/s per m/s of approach.
		float impactTumble = 1.5f;
		// How fast a grounded casing is turned to lie flat (1/s).
		float layFlatRate = 10.0f;
		// Rest detection: relative speed and spin below these for restTime.
		float sleepSpeed = 0.25f;
		float sleepSpin = 1.5f;
		float restTime = 0.2f;
		// Hard caps: never more than this long airborne / alive.
		float maxAirTime = 12.0f;
		float maxAge = 45.0f;
		float sinkTime = 1.5f;
		float ownerIgnoreTime = 0.05f;
		// Physics substep (s); a frame is split into at most maxSubsteps.
		float maxStep = 1.0f / 90.0f;
		int maxSubsteps = 4;
	};

	struct BounceResult
	{
		bool contact = false;
		bool impact = false;     // bounced (approach speed above the threshold)
		float approachSpeed = 0.0f;
	};

	// Reflects the velocity relative to a surface moving at surfaceVelocity.
	// A bounce loses `friction` of the sliding speed at once; a resting
	// contact (approach below bounceThreshold) loses it as Coulomb friction,
	// friction * g * dt per step, so a casing slides and rolls a little before
	// it stops. `tumbleSign` (+1/-1) picks the direction of the impact tumble
	// so equal casings do not all spin the same way.
	inline BounceResult Bounce(V3& velocity, V3& omega, V3 normal, V3 surfaceVelocity, const Surface& surface,
		float radius, const Tuning& tuning, float dt, float tumbleSign = 1.0f)
	{
		BounceResult result;
		const V3 relative = Sub(velocity, surfaceVelocity);
		const float vn = Dot(relative, normal);
		if (vn >= 0.0f)
		{
			return result;
		}
		result.contact = true;
		result.approachSpeed = -vn;
		const V3 normalPart = Scale(normal, vn);
		const V3 tangentPart = Sub(relative, normalPart);
		const bool bounce = -vn > tuning.bounceThreshold;
		result.impact = bounce;
		const float e = bounce ? surface.restitution : 0.0f;
		const float friction = surface.friction < 0.0f ? 0.0f : (surface.friction > 1.0f ? 1.0f : surface.friction);
		float keepTangent = 1.0f;
		if (bounce)
		{
			keepTangent = 1.0f - friction;
		}
		else
		{
			const float slide = Length(tangentPart);
			const float loss = friction * tuning.gravity * dt;
			keepTangent = slide > loss ? (slide - loss) / slide : 0.0f;
		}
		const V3 newRelative = Sub(Scale(tangentPart, keepTangent), Scale(normalPart, e));
		velocity = Add(newRelative, surfaceVelocity);

		const float r = radius > 1e-3f ? radius : 1e-3f;
		V3 roll = Scale(Cross(normal, Scale(tangentPart, keepTangent)), tuning.rollGain / r);
		omega = Add(Scale(omega, bounce ? tuning.spinKeep : 0.85f), roll);
		if (bounce)
		{
			// A hard hit kicks the casing end over end about an axis in the
			// surface plane, perpendicular to how it slides.
			const V3 axis = NormalizeOr(Cross(normal, NormalizeOr(tangentPart, Cross(normal, V3{ 1.0f, 0.0f, 0.0f }))),
				V3{ 1.0f, 0.0f, 0.0f });
			omega = Add(omega, Scale(axis, tumbleSign * tuning.impactTumble * -vn));
		}
		return result;
	}

	// Turns the casing's long axis (local +Z) toward the surface plane by the
	// fraction 1 - exp(-rate dt), keeping its heading in that plane.
	inline Quat LayFlat(const Quat& orientation, V3 normal, float rate, float dt)
	{
		const V3 axis = Rotate(orientation, V3{ 0.0f, 0.0f, 1.0f });
		const float along = Dot(axis, normal);
		V3 target = Sub(axis, Scale(normal, along));
		if (Length(target) < 1e-4f)
		{
			// Standing on end: tip over toward any direction in the plane.
			target = Cross(normal, V3{ 1.0f, 0.0f, 0.0f });
			if (Length(target) < 1e-4f)
			{
				target = Cross(normal, V3{ 0.0f, 0.0f, 1.0f });
			}
		}
		target = NormalizeOr(target, V3{ 1.0f, 0.0f, 0.0f });
		float cosine = Dot(axis, target);
		cosine = cosine > 1.0f ? 1.0f : (cosine < -1.0f ? -1.0f : cosine);
		const float angle = std::acos(cosine);
		if (angle < 1e-4f)
		{
			return orientation;
		}
		const float fraction = 1.0f - std::exp(-rate * dt);
		const V3 turnAxis = NormalizeOr(Cross(axis, target), normal);
		return Normalize(Mul(FromAxisAngle(turnAxis, angle * fraction), orientation));
	}

	// ----- One casing -------------------------------------------------------

	enum class Phase : uint8_t
	{
		Flying,
		Resting,
		Sinking,
		Done,
	};

	struct Body
	{
		V3 position;
		V3 velocity;
		V3 omega;
		Quat orientation;
		float radius = 0.06f;
		Surface surface;
		float linger = 6.0f;
		float sinkDepth = 0.2f;

		Phase phase = Phase::Flying;
		float age = 0.0f;
		float airTime = 0.0f;
		float restTimer = 0.0f;
		float restAge = 0.0f;
		float sinkElapsed = 0.0f;
		uint32_t owner = 0;
		// The owner is ignored until ownerIgnoreTime has passed AND the casing
		// has been seen clear of it once, so a casing spawned inside the hull's
		// box is never shoved out through a face.
		bool ownerClear = false;
		uint32_t support = 0;        // obstacle handle it last rested on (0 = terrain)
		bool grounded = false;
		float tumbleSign = 1.0f;
		uint32_t impacts = 0;
	};

	// Terrain: a callable float(float x, float z) returning the height, or NaN
	// when there is no terrain (the casing then falls and expires).
	template <typename Terrain>
	inline V3 SampleNormal(Terrain& terrain, float x, float z, float step = 0.3f)
	{
		const float a = terrain(x - step, z), b = terrain(x + step, z);
		const float c = terrain(x, z - step), d = terrain(x, z + step);
		if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d))
		{
			return { 0.0f, 1.0f, 0.0f };
		}
		return TerrainNormal(a, b, c, d, step);
	}

	// One physics substep for a Flying casing. Returns whether it touched anything.
	template <typename Terrain>
	inline bool StepFlying(Body& body, float dt, Terrain& terrain, const Obstacle* obstacles, int obstacleCount, const Tuning& tuning)
	{
		body.velocity.y -= tuning.gravity * dt;
		body.position = Add(body.position, Scale(body.velocity, dt));
		body.orientation = IntegrateSpin(body.orientation, body.omega, dt);
		body.grounded = false;

		bool touched = false;
		V3 supportNormal{};
		V3 supportVelocity{};
		bool supported = false;
		uint32_t supportHandle = 0;

		// Objects first, so a casing on a deck is lifted before the terrain test.
		bool overlapsOwner = false;
		for (int i = 0; i < obstacleCount; ++i)
		{
			const Obstacle& obstacle = obstacles[i];
			const Contact contact = ObstacleContact(body.position, body.radius, obstacle);
			const bool isOwner = obstacle.handle != 0 && obstacle.handle == body.owner;
			if (isOwner)
			{
				overlapsOwner = contact.hit;
				if (!body.ownerClear)
				{
					continue;
				}
			}
			if (!contact.hit)
			{
				continue;
			}
			touched = true;
			body.position = Add(body.position, Scale(contact.normal, contact.depth));
			const BounceResult r = Bounce(body.velocity, body.omega, contact.normal, obstacle.velocity, body.surface,
				body.radius, tuning, dt, body.tumbleSign);
			if (r.impact)
			{
				++body.impacts;
				body.tumbleSign = -body.tumbleSign;
			}
			if (contact.normal.y > 0.5f)
			{
				supported = true;
				supportNormal = contact.normal;
				supportVelocity = obstacle.velocity;
				supportHandle = obstacle.handle;
			}
		}
		if (!body.ownerClear && body.age >= tuning.ownerIgnoreTime && !overlapsOwner)
		{
			body.ownerClear = true;
		}

		const float ground = terrain(body.position.x, body.position.z);
		if (std::isfinite(ground) && body.position.y - body.radius < ground)
		{
			touched = true;
			body.position.y = ground + body.radius;
			const V3 normal = SampleNormal(terrain, body.position.x, body.position.z);
			const BounceResult r = Bounce(body.velocity, body.omega, normal, V3{}, body.surface, body.radius, tuning, dt, body.tumbleSign);
			if (r.impact)
			{
				++body.impacts;
				body.tumbleSign = -body.tumbleSign;
			}
			supported = true;
			supportNormal = normal;
			supportVelocity = V3{};
			supportHandle = 0;
		}

		if (supported)
		{
			body.grounded = true;
			body.support = supportHandle;
			body.orientation = LayFlat(body.orientation, supportNormal, tuning.layFlatRate, dt);
			const float relSpeed = Length(Sub(body.velocity, supportVelocity));
			if (relSpeed < tuning.sleepSpeed && Length(body.omega) < tuning.sleepSpin && Length(supportVelocity) < tuning.sleepSpeed)
			{
				body.restTimer += dt;
			}
			else
			{
				body.restTimer = 0.0f;
			}
		}
		else
		{
			body.restTimer = 0.0f;
		}
		return touched;
	}

	// Advances a casing by one rendered frame of simulation time (dt may be 0
	// while the game is paused). Splits the frame into substeps.
	template <typename Terrain>
	inline void Step(Body& body, float dt, Terrain& terrain, const Obstacle* obstacles, int obstacleCount, const Tuning& tuning)
	{
		if (!(dt > 0.0f) || body.phase == Phase::Done)
		{
			return;
		}
		int steps = static_cast<int>(std::ceil(dt / tuning.maxStep));
		steps = steps < 1 ? 1 : (steps > tuning.maxSubsteps ? tuning.maxSubsteps : steps);
		const float h = dt / static_cast<float>(steps);
		for (int i = 0; i < steps && body.phase != Phase::Done; ++i)
		{
			body.age += h;
			switch (body.phase)
			{
			case Phase::Flying:
				StepFlying(body, h, terrain, obstacles, obstacleCount, tuning);
				body.airTime = body.grounded ? 0.0f : body.airTime + h;
				if (body.restTimer >= tuning.restTime)
				{
					body.phase = Phase::Resting;
					body.velocity = V3{};
					body.omega = V3{};
					body.restAge = 0.0f;
				}
				else if (body.airTime >= tuning.maxAirTime || body.age >= tuning.maxAge || !IsFinite(body.position))
				{
					body.phase = Phase::Done;   // fell off the world, or never settled
				}
				break;
			case Phase::Resting:
				body.restAge += h;
				if (body.restAge >= body.linger || body.age >= tuning.maxAge)
				{
					body.phase = Phase::Sinking;
					body.sinkElapsed = 0.0f;
				}
				break;
			case Phase::Sinking:
				body.sinkElapsed += h;
				body.position.y -= body.sinkDepth / tuning.sinkTime * h;
				if (body.sinkElapsed >= tuning.sinkTime)
				{
					body.phase = Phase::Done;
				}
				break;
			case Phase::Done:
				break;
			}
		}
	}

	// A Resting casing whose support moved away (the deck it lay on drove off,
	// or the obstacle died) goes back to Flying.
	inline void Wake(Body& body)
	{
		if (body.phase == Phase::Resting)
		{
			body.phase = Phase::Flying;
			body.restTimer = 0.0f;
			body.support = 0;
		}
	}

	// ----- Eject (used by the Lua side's documented recipe and by tests) ---

	struct EjectParams
	{
		float back = 0.6f;      // m toward the breech (against the shot)
		float side = 0.25f;     // m to the right
		float up = 0.15f;       // m up
		float speedSide = 3.0f; // m/s out to the right
		float speedUp = 2.5f;   // m/s up
		float speedBack = 0.8f; // m/s backward
	};

	// The spawn point and velocity for a casing ejected from a shot whose
	// frame is (right, up, front) at `muzzle`, fired by a shooter moving at
	// `shooterVelocity`. Randomness is the caller's (jitter in [-1, 1]^3).
	inline void Eject(V3 muzzle, V3 right, V3 up, V3 front, V3 shooterVelocity, const EjectParams& p, V3 jitter,
		V3& outPosition, V3& outVelocity)
	{
		outPosition = Add(Add(Add(muzzle, Scale(front, -p.back)), Scale(right, p.side)), Scale(up, p.up));
		outVelocity = Add(shooterVelocity,
			Add(Add(Scale(right, p.speedSide * (1.0f + 0.25f * jitter.x)), Scale(up, p.speedUp * (1.0f + 0.25f * jitter.y))),
				Scale(front, -p.speedBack * (1.0f + 0.5f * jitter.z))));
	}
}
