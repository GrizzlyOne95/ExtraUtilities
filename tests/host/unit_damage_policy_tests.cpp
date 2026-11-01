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

// Per-receiver damage multipliers: identity by handle generation, healing and
// invalid input untouched.

#include "Game/UnitDamagePolicy.h"
#include "HostTest.h"

#include <limits>

#define CHECK(cond) HostTest::Expect((cond), #cond)

int main()
{
	ExtraUtilities::UnitDamage::Policy policy;
	CHECK(policy.Apply(0x1000, 123, 120) == 120);
	CHECK(policy.Set(0x1000, 123, .75f));
	CHECK(policy.Apply(0x1000, 123, 120) == 90);   // 100 HP survives at 10
	CHECK(policy.Apply(0x2000, 456, 120) == 120);  // shared blast unchanged
	CHECK(policy.Apply(0x1000, 124, 120) == 120);  // recycled pool address
	CHECK(policy.Apply(0x1000, 123, -20) == -20);  // healing unchanged
	CHECK(policy.Apply(0x1000, 123, 0) == 0);
	CHECK(!policy.Set(0x1000, 123, -1));
	CHECK(!policy.Set(0x1000, 123, 2));
	CHECK(!policy.Set(0x1000, 123, std::numeric_limits<float>::quiet_NaN()));
	CHECK(!policy.Set(0, 123, .5f));
	CHECK(!policy.Set(0x1000, 0, .5f));
	CHECK(policy.Get(0x1000, 123) == .75f);        // rejected updates leave it intact
	CHECK(policy.Set(0x1000, 124, 0));
	CHECK(policy.Apply(0x1000, 124, 120) == 0);
	policy.Clear(123);
	CHECK(policy.Get(0x1000, 124) == 0);           // stale handle cannot clear new unit
	policy.Clear(124);
	CHECK(policy.Empty());
	CHECK(policy.Set(0x1000, 123, .75f));
	CHECK(policy.Set(0x1000, 123, 1));
	CHECK(policy.Empty());
	CHECK(policy.Set(0x1000, 123, .5f));
	policy.Reset();
	CHECK(policy.Apply(0x1000, 123, 120) == 120);
	return HostTest::Finish("unit damage policy");
}
