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

// The turbo decision as data. No Windows, engine or Lua dependency, so
// tests/host can check it.
//
// The hovercraft AI's turbo decision (BZR 2.2.301, 0x00601CA0-0x00601CC3) is
//
//   comiss xmm0 /*throttle*/, [tolerance]    ; jb  -> no turbo
//   movss  xmm0, [gateLimit]
//   comiss xmm0, [ebp-0x54]  /*gateInput*/   ; jbe -> no turbo
//   turbo = 1
//
// with tolerance = 1.0f and gateLimit = 0.8f in stock .rdata. GlobalTurbo
// points both memory operands at EXU floats once, at activation, and sets the
// floats for each unit just before the decision runs. No code is rewritten
// per tick.

#include <limits>

namespace ExtraUtilities::Patch::TurboGate
{
	// The stock .rdata values the two operands read (GOG 2.2.301). The patched
	// operands' expected bytes pin those addresses, and the build gate pins the
	// executable, so these are the values stock code compares against.
	inline constexpr float kStockTolerance = 1.0f;
	inline constexpr float kStockGateLimit = 0.8f;

	// Forced turbo. 0.9 is the tolerance EXU has always used. The old patch
	// NOP'd the gate branch; +infinity makes the gate pass for every gate input
	// except NaN and +infinity, which keep the stock "no turbo" result.
	inline constexpr float kForcedTolerance = 0.9f;
	inline constexpr float kForcedGateLimit = std::numeric_limits<float>::infinity();

	struct Operands
	{
		float tolerance;
		float gateLimit;
	};

	constexpr Operands SelectOperands(bool forced) noexcept
	{
		return forced ? Operands{ kForcedTolerance, kForcedGateLimit }
		              : Operands{ kStockTolerance, kStockGateLimit };
	}

	// A unit's own override wins; otherwise the global setting applies.
	constexpr bool IsForced(bool globalEnabled, bool hasUnitOverride, bool unitOverride) noexcept
	{
		return hasUnitOverride ? unitOverride : globalEnabled;
	}

	// Model of the engine's decision with the given operands. comiss sets CF
	// on "less than" and on unordered, and ZF on "equal" and unordered, so jb
	// and jbe are both taken for NaN.
	constexpr bool EngineDecision(float throttle, float gateInput, Operands operands) noexcept
	{
		if (!(throttle >= operands.tolerance))
		{
			return false;
		}
		return operands.gateLimit > gateInput;
	}

	// What the previous code-rewriting patch did when active: tolerance 0.9
	// and the gate branch NOP'd.
	constexpr bool NopGateDecision(float throttle) noexcept
	{
		return throttle >= kForcedTolerance;
	}
}
