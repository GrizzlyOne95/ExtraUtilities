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

// Engine-free core of the BZ2-style muzzle flash: a short-lived mesh (BZ2's
// draw_geom flash, e.g. g_sflash's two crossed cards) placed on the muzzle,
// carried along with the firing object, and scaled from startScale to
// finishScale over its duration (BZ2 startRadius/finishRadius/animateTime).
//
// SHARED SOURCE: this header is meant to be compiled unchanged into both EXU
// (exu.flash, driven from Lua) and BZR-OpenShim (native fire seams). It
// depends on nothing but the C++ standard library; each DLL supplies its own
// Ogre backend, engine reads and producer. Keep the copies byte-identical.
//
// All poses are in Battlezone SIMULATION space (BZ matrices: right, up, front;
// left-handed, +Z = front). Converting to Ogre render space is the backend's
// job.

#include <algorithm>
#include <cmath>

namespace MuzzleFlashCore
{
	struct V3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;
	};

	struct Quat
	{
		float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;
	};

	struct Pose
	{
		V3 position;
		Quat orientation;
	};

	inline bool IsFinite(V3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
	inline bool IsFinite(const Quat& q) { return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z); }
	inline bool IsFinite(const Pose& p) { return IsFinite(p.position) && IsFinite(p.orientation); }

	inline V3 Add(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3 Sub(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3 Scale(V3 v, float s) { return { v.x * s, v.y * s, v.z * s }; }
	inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline V3 Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	inline float Length(V3 v) { return std::sqrt(Dot(v, v)); }

	inline Quat Normalize(const Quat& q)
	{
		const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
		if (!(n > 1.0e-8f) || !std::isfinite(n))
		{
			return Quat{};
		}
		return { q.w / n, q.x / n, q.y / n, q.z / n };
	}

	inline Quat Conjugate(const Quat& q) { return { q.w, -q.x, -q.y, -q.z }; }

	inline Quat Mul(const Quat& a, const Quat& b)
	{
		return {
			a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
			a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
			a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
			a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		};
	}

	inline V3 Rotate(const Quat& q, V3 v)
	{
		const V3 u{ q.x, q.y, q.z };
		const V3 t = Scale(Cross(u, v), 2.0f);
		return Add(Add(v, Scale(t, q.w)), Cross(u, t));
	}

	inline Quat FromAxisAngle(V3 axis, float angle)
	{
		const float len = Length(axis);
		if (!(len > 1.0e-8f))
		{
			return Quat{};
		}
		const float h = angle * 0.5f;
		const float s = std::sin(h) / len;
		return { std::cos(h), axis.x * s, axis.y * s, axis.z * s };
	}

	// Rotation whose columns are the given orthonormal axes (BZ right/up/front).
	// Re-orthonormalizes first, so a slightly sheared engine matrix still works.
	inline Quat FromAxes(V3 right, V3 up, V3 front)
	{
		const float fl = Length(front);
		if (!(fl > 1.0e-6f))
		{
			return Quat{};
		}
		V3 z = Scale(front, 1.0f / fl);
		V3 x = Sub(right, Scale(z, Dot(right, z)));
		float xl = Length(x);
		if (!(xl > 1.0e-6f))
		{
			x = Cross(up, z);
			xl = Length(x);
			if (!(xl > 1.0e-6f))
			{
				return Quat{};
			}
		}
		x = Scale(x, 1.0f / xl);
		// Left-handed BZ basis: up = front x right.
		const V3 y = Cross(z, x);
		const float m00 = x.x, m01 = y.x, m02 = z.x;
		const float m10 = x.y, m11 = y.y, m12 = z.y;
		const float m20 = x.z, m21 = y.z, m22 = z.z;
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

	// `world` expressed relative to `owner` (so Compose(owner, Relative(owner, world)) == world).
	inline Pose Relative(const Pose& owner, const Pose& world)
	{
		const Quat inv = Conjugate(owner.orientation);
		return { Rotate(inv, Sub(world.position, owner.position)), Normalize(Mul(inv, world.orientation)) };
	}

	inline Pose Compose(const Pose& owner, const Pose& local)
	{
		return { Add(owner.position, Rotate(owner.orientation, local.position)), Normalize(Mul(owner.orientation, local.orientation)) };
	}

	// Spins a pose about its own front (+Z) axis: BZ2-style flashes look the
	// same shot to shot unless each one gets a different roll.
	inline Pose RollAboutFront(const Pose& pose, float radians)
	{
		return { pose.position, Normalize(Mul(pose.orientation, FromAxisAngle({ 0.0f, 0.0f, 1.0f }, radians))) };
	}

	// What one flash does over its life (BZ2 draw_geom: startRadius,
	// finishRadius, animateTime; the weapon's flashDuration caps the life).
	struct Definition
	{
		float duration = 0.1f;
		float startScale = 1.0f;
		float finishScale = 1.0f;
	};

	constexpr float kMinDuration = 0.005f;
	constexpr float kMaxDuration = 5.0f;
	constexpr float kMaxScale = 100.0f;

	// Clamps a definition into the range the renderer accepts. Returns false
	// for one that should not be drawn at all (non-finite, or no visible size).
	inline bool Sanitize(Definition& d)
	{
		if (!std::isfinite(d.duration) || !std::isfinite(d.startScale) || !std::isfinite(d.finishScale))
		{
			return false;
		}
		d.duration = std::min(std::max(d.duration, kMinDuration), kMaxDuration);
		d.startScale = std::min(std::max(d.startScale, 0.0f), kMaxScale);
		d.finishScale = std::min(std::max(d.finishScale, 0.0f), kMaxScale);
		return d.startScale > 0.0f || d.finishScale > 0.0f;
	}

	inline bool Expired(const Definition& d, float age) { return !(age < d.duration); }

	// Linear, like BZ2's particle renders. Age is clamped to [0, duration].
	inline float ScaleAt(const Definition& d, float age)
	{
		const float t = d.duration > 0.0f ? std::min(std::max(age / d.duration, 0.0f), 1.0f) : 1.0f;
		return d.startScale + (d.finishScale - d.startScale) * t;
	}

	// Advances flash age by SIMULATION time, so a paused game or a menu loop
	// that stops the simulation freezes flashes along with everything else,
	// and a load / restart time jump never ages a flash by seconds.
	class SimClock
	{
	public:
		// Returns the seconds to age flashes by this frame.
		float Advance(float simTime, bool paused, bool valid)
		{
			float dt = 0.0f;
			if (valid && !paused && has_)
			{
				const float step = simTime - last_;
				dt = (step > 0.0f && step <= kMaxStep) ? step : 0.0f;
			}
			if (valid)
			{
				last_ = simTime;
				has_ = true;
			}
			return dt;
		}

		void Reset() { has_ = false; last_ = 0.0f; }

	private:
		static constexpr float kMaxStep = 0.25f;
		float last_ = 0.0f;
		bool has_ = false;
	};
}
