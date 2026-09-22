/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * Extra Utilities is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

// Coordinate-space math for weapon shot convergence.
//
// Deliberately free of Windows and of any game pointer chasing so the whole
// solution can be exercised on the host by tests/host/weapon_convergence_math_tests.cpp.
// ShotConvergence.cpp supplies the matrices it reads out of the live Weapon
// records and writes the solved matrices back.
//
// Engine facts this mirrors (Redux 2.2.301 addresses, from the shipped-exe
// decompile; cross-checked against OpenShim's include/weapon_convergence.h,
// which derived the same layout independently):
//
//   MAT_3D            9 floats of row-major basis (right/up/front), 4 bytes of
//                     padding, then three *doubles* of translation.
//   Matrix_Multiply   0x0081FE60. Row-vector convention: p * lhs * rhs, so the
//                     left operand is the inner (child) transform.
//   Matrix_Inverse    0x008203F0. Transpose plus negated rotated translation.
//   Weapon::Control   0x00611610. Writes the muzzle's world frame M to
//                     weapon+0x28 and Matrix_Inverse(M) to weapon+0x68 every
//                     frame.
//   Firing sites      0x005B1E10, 0x005B2010, 0x005B8FF0, 0x005D6330 and
//                     friends all spawn ordnance from
//                     Matrix_Multiply(weapon->obj->mat /*obj+0x20*/, weapon->M).
//
// So the matrix at weapon->obj+0x20 is the barrel's transform *in the mount
// frame*, never a world transform, and its translation is a mount-local offset
// that the stock aim updates explicitly preserve.
//
// The engine already ships convergence for one class: Walker::UpdateWeaponAim
// (0x0060F320) composes an extra rotation onto obj+0x20 built from the
// hardpoint's craft-space offset and the range to GameObject::GetTarget
// (0x00462610). That formulation only works because the stock aim step has
// already cancelled the hardpoint's own rotation, which is not true for every
// class. Everything below solves the same problem in world space instead, so it
// is independent of which branch a class's aim update took.

#include "Util/Vec3.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::WeaponConvergence
{
	using Vec3 = ExtraUtilities::Vec3;

	// Binary-compatible with the engine's MAT_3D (BZR::MAT_3D in BZR.h).
	struct Matrix
	{
		float rightX, rightY, rightZ;
		float upX, upY, upZ;
		float frontX, frontY, frontZ;
		unsigned char padding[4];
		double positionX, positionY, positionZ;
	};

	static_assert(sizeof(Matrix) == 64, "Unexpected weapon transform size");

	inline constexpr float kDirectionEpsilon = 0.001f;

	// Below this muzzle-to-aim-point distance the correction is numerically
	// meaningless and the geometry is degenerate anyway.
	inline constexpr float kMinConvergenceDistance = 5.0f;

	// Safety stop, not an aim correction: hardpoints are bolted to the hull, so
	// a solution that swings a barrel further than 45 degrees off the stock aim
	// is a bug or a degenerate target, never a shot anyone asked for. Even a
	// 5 m convergence range only needs about 20 degrees for a 2 m lateral
	// hardpoint, so this never engages during normal play.
	inline constexpr float kMaxDeviationCos = 0.70710678f;

	enum class SolveResult
	{
		Converged,
		AimPointTooClose,
		ExceedsDeviationLimit,
		DegenerateInput,
	};

	inline Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
	{
		return Vec3(
			lhs.y * rhs.z - lhs.z * rhs.y,
			lhs.z * rhs.x - lhs.x * rhs.z,
			lhs.x * rhs.y - lhs.y * rhs.x);
	}

	inline float Dot(const Vec3& lhs, const Vec3& rhs)
	{
		return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
	}

	inline bool Normalize(Vec3& value)
	{
		const float lengthSquared = Dot(value, value);
		if (!std::isfinite(lengthSquared) ||
			lengthSquared <= kDirectionEpsilon * kDirectionEpsilon)
		{
			return false;
		}

		const float inverseLength = 1.0f / std::sqrt(lengthSquared);
		value.x *= inverseLength;
		value.y *= inverseLength;
		value.z *= inverseLength;
		return true;
	}

	inline Matrix Identity()
	{
		return Matrix{
			1.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 1.0f,
			{ 0, 0, 0, 0 },
			0.0, 0.0, 0.0
		};
	}

	inline Vec3 Front(const Matrix& value)
	{
		return Vec3(value.frontX, value.frontY, value.frontZ);
	}

	inline Vec3 Up(const Matrix& value)
	{
		return Vec3(value.upX, value.upY, value.upZ);
	}

	inline Vec3 Position(const Matrix& value)
	{
		return Vec3(
			static_cast<float>(value.positionX),
			static_cast<float>(value.positionY),
			static_cast<float>(value.positionZ));
	}

	// Semantics of Matrix_Multiply (0x0081FE60): row-vector convention, `lhs`
	// is the inner/child transform and `rhs` the outer/parent one, so a point p
	// maps as p * lhs * rhs. Reimplemented rather than called so convergence
	// never re-enters the engine from inside an aim update.
	inline Matrix Multiply(const Matrix& lhs, const Matrix& rhs)
	{
		Matrix out = {};
		out.rightX = lhs.rightX * rhs.rightX + lhs.rightY * rhs.upX + lhs.rightZ * rhs.frontX;
		out.rightY = lhs.rightX * rhs.rightY + lhs.rightY * rhs.upY + lhs.rightZ * rhs.frontY;
		out.rightZ = lhs.rightX * rhs.rightZ + lhs.rightY * rhs.upZ + lhs.rightZ * rhs.frontZ;
		out.upX = lhs.upX * rhs.rightX + lhs.upY * rhs.upX + lhs.upZ * rhs.frontX;
		out.upY = lhs.upX * rhs.rightY + lhs.upY * rhs.upY + lhs.upZ * rhs.frontY;
		out.upZ = lhs.upX * rhs.rightZ + lhs.upY * rhs.upZ + lhs.upZ * rhs.frontZ;
		out.frontX = lhs.frontX * rhs.rightX + lhs.frontY * rhs.upX + lhs.frontZ * rhs.frontX;
		out.frontY = lhs.frontX * rhs.rightY + lhs.frontY * rhs.upY + lhs.frontZ * rhs.frontY;
		out.frontZ = lhs.frontX * rhs.rightZ + lhs.frontY * rhs.upZ + lhs.frontZ * rhs.frontZ;
		out.positionX =
			static_cast<double>(rhs.rightX) * lhs.positionX +
			static_cast<double>(rhs.upX) * lhs.positionY +
			static_cast<double>(rhs.frontX) * lhs.positionZ + rhs.positionX;
		out.positionY =
			static_cast<double>(rhs.rightY) * lhs.positionX +
			static_cast<double>(rhs.upY) * lhs.positionY +
			static_cast<double>(rhs.frontY) * lhs.positionZ + rhs.positionY;
		out.positionZ =
			static_cast<double>(rhs.rightZ) * lhs.positionX +
			static_cast<double>(rhs.upZ) * lhs.positionY +
			static_cast<double>(rhs.frontZ) * lhs.positionZ + rhs.positionZ;
		return out;
	}

	// Semantics of Matrix_Inverse (0x008203F0). Valid for the orthonormal
	// frames the object hierarchy stores, which is all this ever sees.
	inline Matrix Invert(const Matrix& value)
	{
		Matrix out = {};
		out.rightX = value.rightX;
		out.rightY = value.upX;
		out.rightZ = value.frontX;
		out.upX = value.rightY;
		out.upY = value.upY;
		out.upZ = value.frontY;
		out.frontX = value.rightZ;
		out.frontY = value.upZ;
		out.frontZ = value.frontZ;
		out.positionX = -(static_cast<double>(value.rightX) * value.positionX +
			static_cast<double>(value.rightY) * value.positionY +
			static_cast<double>(value.rightZ) * value.positionZ);
		out.positionY = -(static_cast<double>(value.upX) * value.positionX +
			static_cast<double>(value.upY) * value.positionY +
			static_cast<double>(value.upZ) * value.positionZ);
		out.positionZ = -(static_cast<double>(value.frontX) * value.positionX +
			static_cast<double>(value.frontY) * value.positionY +
			static_cast<double>(value.frontZ) * value.positionZ);
		return out;
	}

	inline bool IsFinite(const Matrix& value)
	{
		const float* rotation = &value.rightX;
		for (int i = 0; i < 9; ++i)
		{
			if (!std::isfinite(rotation[i]))
			{
				return false;
			}
		}
		return std::isfinite(value.positionX) &&
			std::isfinite(value.positionY) &&
			std::isfinite(value.positionZ);
	}

	// Rows of an object frame are expected to be unit length. A frame that
	// fails this is either a layout mismatch or uninitialised memory, and
	// convergence must not build an aim solution on top of it.
	inline bool IsRotationOrthonormal(const Matrix& value)
	{
		const float* rotation = &value.rightX;
		for (int row = 0; row < 3; ++row)
		{
			const float x = rotation[row * 3 + 0];
			const float y = rotation[row * 3 + 1];
			const float z = rotation[row * 3 + 2];
			const float lengthSquared = x * x + y * y + z * z;
			if (!std::isfinite(lengthSquared) ||
				lengthSquared < 0.98f || lengthSquared > 1.02f)
			{
				return false;
			}
		}
		return true;
	}

	// Minimal-arc rotation carrying `from` onto `to`, in the same row-vector
	// convention as the engine matrices: v * result == to when v == from.
	// Rotating the whole existing basis by this keeps the barrel's roll and up
	// vector as the stock aim left them instead of re-deriving them from world
	// up, which is what a gimballed mount physically does.
	inline bool BuildArcRotation(
		const Vec3& from,
		const Vec3& to,
		const Vec3& fallbackAxis,
		Matrix& outRotation)
	{
		outRotation = Identity();

		const float cosAngle = Dot(from, to);
		if (!std::isfinite(cosAngle))
		{
			return false;
		}

		Vec3 axis = Cross(from, to);
		const float sinAngle = std::sqrt(Dot(axis, axis));
		float s = sinAngle;
		if (sinAngle <= kDirectionEpsilon)
		{
			// Either already aligned (identity is correct) or exactly reversed,
			// which no reachable aim point produces; spin about the mount's own
			// up vector so the result stays a proper rotation instead of
			// collapsing.
			if (cosAngle > 0.0f)
			{
				return true;
			}
			axis = fallbackAxis;
			if (!Normalize(axis))
			{
				return false;
			}
			s = 0.0f;
		}
		else
		{
			axis.x /= sinAngle;
			axis.y /= sinAngle;
			axis.z /= sinAngle;
		}

		const float c = cosAngle;
		const float t = 1.0f - c;

		// Column-vector Rodrigues, transposed on write so the result composes
		// on the right of the engine's row vectors.
		outRotation.rightX = c + axis.x * axis.x * t;
		outRotation.upX = axis.x * axis.y * t - axis.z * s;
		outRotation.frontX = axis.x * axis.z * t + axis.y * s;
		outRotation.rightY = axis.y * axis.x * t + axis.z * s;
		outRotation.upY = c + axis.y * axis.y * t;
		outRotation.frontY = axis.y * axis.z * t - axis.x * s;
		outRotation.rightZ = axis.z * axis.x * t - axis.y * s;
		outRotation.upZ = axis.z * axis.y * t + axis.x * s;
		outRotation.frontZ = c + axis.z * axis.z * t;
		return IsFinite(outRotation);
	}

	struct Solution
	{
		// What to write back to weapon->obj+0x20.
		Matrix mountLocal = Identity();
		// Muzzle position in world space, i.e. where the shot actually starts.
		Vec3 muzzle = Vec3();
		// Angle between the resulting fire direction and the aim ray,
		// recomposed the same way the firing sites compose it. Zero for a
		// correct solution.
		float residualDegrees = 0.0f;
	};

	// `mountLocal` is weapon->obj+0x20 as the stock aim update just left it;
	// `mountWorld` is weapon->M at weapon+0x28. `aimPoint` is the world-space
	// point every barrel should converge on.
	inline SolveResult Solve(
		const Matrix& mountLocal,
		const Matrix& mountWorld,
		const Vec3& aimPoint,
		Solution& outSolution)
	{
		if (!IsFinite(mountWorld) || !IsRotationOrthonormal(mountWorld) ||
			!IsFinite(mountLocal) || !IsRotationOrthonormal(mountLocal))
		{
			return SolveResult::DegenerateInput;
		}

		// World frame of the barrel exactly as the firing sites build it:
		// fireMatrix = weaponObj->mat * weapon->M.
		const Matrix world = Multiply(mountLocal, mountWorld);
		const Vec3 muzzle = Position(world);

		Vec3 desired = aimPoint - muzzle;
		const float distanceSquared = Dot(desired, desired);
		if (!std::isfinite(distanceSquared) ||
			distanceSquared < kMinConvergenceDistance * kMinConvergenceDistance)
		{
			return SolveResult::AimPointTooClose;
		}
		if (!Normalize(desired))
		{
			return SolveResult::AimPointTooClose;
		}

		const Vec3 stockFront = Front(world);
		const float deviationCos = Dot(stockFront, desired);
		if (!std::isfinite(deviationCos) || deviationCos < kMaxDeviationCos)
		{
			return SolveResult::ExceedsDeviationLimit;
		}

		Matrix arc = {};
		if (!BuildArcRotation(stockFront, desired, Up(world), arc))
		{
			return SolveResult::DegenerateInput;
		}

		// Rotate the stock world basis onto the aim point, keep the muzzle
		// exactly where the mount put it, then push the result back through the
		// mount inverse so what lands in obj+0x20 is once again a mount-relative
		// transform.
		Matrix convergedWorld = Multiply(world, arc);
		convergedWorld.positionX = world.positionX;
		convergedWorld.positionY = world.positionY;
		convergedWorld.positionZ = world.positionZ;

		Matrix converged = Multiply(convergedWorld, Invert(mountWorld));
		if (!IsFinite(converged) || !IsRotationOrthonormal(converged))
		{
			return SolveResult::DegenerateInput;
		}

		// Stock never moves the hardpoint: every UpdateWeaponAim saves
		// obj+0x20's position and restores it around RefreshWeaponTransform. Do
		// the same so the barrel pivots in place instead of sliding along the
		// hull.
		converged.positionX = mountLocal.positionX;
		converged.positionY = mountLocal.positionY;
		converged.positionZ = mountLocal.positionZ;

		// Closed-loop check: recompose exactly what the firing sites will
		// compose and measure how far the resulting barrel direction still sits
		// off the aim ray.
		const Matrix verifyWorld = Multiply(converged, mountWorld);
		float verifyCos = Dot(Front(verifyWorld), desired);
		if (verifyCos > 1.0f)
		{
			verifyCos = 1.0f;
		}
		if (verifyCos < -1.0f)
		{
			verifyCos = -1.0f;
		}

		outSolution.mountLocal = converged;
		outSolution.muzzle = muzzle;
		outSolution.residualDegrees =
			std::acos(verifyCos) * (180.0f / 3.14159265358979f);
		return SolveResult::Converged;
	}

	// Accumulates the muzzle frames of one craft's weapon slots and produces the
	// single world-space point all of them should aim at.
	//
	// The convergence point is deliberately built from the *stock* aim rather
	// than from the target's position: `centroid + averageFront * range` is
	// where the barrel group as a whole is already pointing, at the target's
	// range. That preserves whatever lead the AI's aim already contained -
	// pointing each barrel straight at the target would throw the lead away -
	// and it makes the operation idempotent, so it is harmless to run on top of
	// a class that the engine (or another mod) already converges.
	class AimPointAccumulator
	{
	public:
		void Add(const Matrix& world)
		{
			const Vec3 position = Position(world);
			m_positionSum = m_positionSum + position;
			m_frontSum = m_frontSum + Front(world);
			++m_count;
		}

		int Count() const
		{
			return m_count;
		}

		// Convergence is meaningless with a single barrel: the muzzle would be
		// re-aimed at a point it already points at. Requiring two keeps
		// single-weapon craft bit-identical to stock.
		bool HasConvergeableGroup() const
		{
			return m_count >= 2;
		}

		Vec3 Centroid() const
		{
			if (m_count <= 0)
			{
				return Vec3();
			}
			const float inverse = 1.0f / static_cast<float>(m_count);
			return Vec3(
				m_positionSum.x * inverse,
				m_positionSum.y * inverse,
				m_positionSum.z * inverse);
		}

		// `range` is the distance from the barrel group to the target.
		bool Resolve(float range, Vec3& outAimPoint) const
		{
			if (!HasConvergeableGroup() || !std::isfinite(range) ||
				range < kMinConvergenceDistance)
			{
				return false;
			}

			Vec3 front = m_frontSum;
			if (!Normalize(front))
			{
				return false;
			}

			outAimPoint = Centroid() + front * range;
			return std::isfinite(outAimPoint.x) &&
				std::isfinite(outAimPoint.y) &&
				std::isfinite(outAimPoint.z);
		}

	private:
		Vec3 m_positionSum = Vec3();
		Vec3 m_frontSum = Vec3();
		int m_count = 0;
	};
}
