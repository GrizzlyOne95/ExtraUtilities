/* Copyright (C) 2026 GrizzlyOne95
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

// The per-unit turbo data gate must decide exactly like the code patches it
// replaced: stock when a unit is not forced, the old "0.9 tolerance, gate
// NOP'd" rule when it is.

#include "Patches/TurboGate.h"
#include "HostTest.h"

#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace ExtraUtilities::Patch::TurboGate;

namespace
{
	// The stock engine compare, with the stock .rdata operands.
	bool StockDecision(float throttle, float gateInput)
	{
		if (!(throttle >= 1.0f))
		{
			return false;
		}
		return 0.8f > gateInput;
	}

	std::vector<float> SampleValues()
	{
		std::vector<float> values;
		for (int i = -40; i <= 40; ++i)
		{
			values.push_back(static_cast<float>(i) * 0.05f);
		}
		const float extra[] = {
			0.9f, 1.0f, 0.8f,
			std::nextafter(0.9f, 0.0f), std::nextafter(0.9f, 2.0f),
			std::nextafter(1.0f, 0.0f), std::nextafter(0.8f, 2.0f),
			std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
			-std::numeric_limits<float>::infinity(),
			std::numeric_limits<float>::denorm_min(),
		};
		values.insert(values.end(), std::begin(extra), std::end(extra));
		return values;
	}
}

int main()
{
	using HostTest::Expect;

	Expect(SelectOperands(false).tolerance == 1.0f && SelectOperands(false).gateLimit == 0.8f,
		"unforced operands are the stock .rdata values");
	Expect(SelectOperands(true).tolerance == 0.9f, "forced tolerance is EXU's 0.9");
	Expect(SelectOperands(true).gateLimit == std::numeric_limits<float>::infinity(), "forced gate limit is +inf");

	// Override resolution.
	Expect(!IsForced(false, false, false), "no global, no override: stock");
	Expect(IsForced(true, false, false), "global on, no override: forced");
	Expect(IsForced(false, true, true), "override true beats global off");
	Expect(!IsForced(true, true, false), "override false beats global on");

	const std::vector<float> values = SampleValues();
	int mismatches = 0;
	for (float throttle : values)
	{
		for (float gateInput : values)
		{
			if (EngineDecision(throttle, gateInput, SelectOperands(false)) != StockDecision(throttle, gateInput))
			{
				++mismatches;
			}
			if (EngineDecision(throttle, gateInput, SelectOperands(true)) != NopGateDecision(throttle))
			{
				++mismatches;
			}
		}
	}
	Expect(mismatches == 0, "data gate matches stock and the old NOP patch for finite gate inputs (" + std::to_string(mismatches) + " mismatches)");

	// The documented divergence from the NOP patch: a NaN or +inf gate input
	// keeps the stock "no turbo" result instead of forcing turbo.
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	Expect(!EngineDecision(1.0f, nan, SelectOperands(true)), "NaN gate input does not force turbo");
	Expect(!EngineDecision(1.0f, inf, SelectOperands(true)), "+inf gate input does not force turbo");
	Expect(!EngineDecision(nan, 0.0f, SelectOperands(true)), "NaN throttle never turbos");
	Expect(!EngineDecision(nan, 0.0f, SelectOperands(false)), "NaN throttle never turbos (stock)");

	return HostTest::Finish("turbo gate");
}
