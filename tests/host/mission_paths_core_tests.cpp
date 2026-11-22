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

#include "Game/MissionPathsCore.h"
#include "HostTest.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace ExtraUtilities::MissionPathsCore;
using HostTest::Expect;

namespace
{
	void TestNames()
	{
		Expect(IsValidPathName("path_1", 6), "plain name is valid");
		Expect(!IsValidPathName("", 0), "empty name is rejected");
		Expect(!IsValidPathName(nullptr, 3), "null name is rejected");
		const char embedded[] = { 'a', '\0', 'b', '\0' };
		Expect(!IsValidPathName(embedded, 3), "embedded NUL is rejected");
		const std::string longest(kMaxNameLength, 'x');
		Expect(IsValidPathName(longest.c_str(), longest.size()), "255-byte name is valid");
		const std::string tooLong(kMaxNameLength + 1, 'x');
		Expect(!IsValidPathName(tooLong.c_str(), tooLong.size()), "256-byte name is rejected");
	}

	void TestFinite()
	{
		Expect(IsFinitePoint(1.0f, -2.0f), "finite point");
		Expect(!IsFinitePoint(std::numeric_limits<float>::quiet_NaN(), 0.0f), "NaN x");
		Expect(!IsFinitePoint(0.0f, std::numeric_limits<float>::infinity()), "infinite z");
	}

	void TestInsertSlot()
	{
		std::int32_t slot = -1;
		Expect(InsertSlot(3, false, 0, slot) && slot == 3, "no index appends");
		Expect(InsertSlot(3, true, 1, slot) && slot == 0, "index 1 inserts at the front");
		Expect(InsertSlot(3, true, 4, slot) && slot == 3, "count + 1 appends");
		Expect(!InsertSlot(3, true, 0, slot), "index 0 is out of range");
		Expect(!InsertSlot(3, true, 5, slot), "count + 2 is out of range");
		Expect(!InsertSlot(kMaxPoints, false, 0, slot), "full path refuses");
		Expect(!InsertSlot(-1, false, 0, slot), "negative count refuses");
	}

	void TestRemoveSlot()
	{
		std::int32_t slot = -1;
		Expect(RemoveSlot(3, false, 0, slot) && slot == 2, "no index removes the last");
		Expect(RemoveSlot(3, true, 1, slot) && slot == 0, "index 1 removes the first");
		Expect(RemoveSlot(3, true, 3, slot) && slot == 2, "index count removes the last");
		Expect(!RemoveSlot(3, true, 4, slot), "index past the end refuses");
		Expect(!RemoveSlot(3, true, 0, slot), "index 0 refuses");
		Expect(!RemoveSlot(1, false, 0, slot), "one-point path keeps its point");
		Expect(!RemoveSlot(kSaneCount + 1, false, 0, slot), "corrupt count refuses");
	}

	void TestPairs()
	{
		std::vector<float> xz = { 1, 10, 2, 20, 3, 30, 0, 0 };
		InsertPair(xz.data(), 3, 1, 9, 90);
		Expect(xz == std::vector<float>{ 1, 10, 9, 90, 2, 20, 3, 30 }, "insert in the middle shifts the tail");
		RemovePair(xz.data(), 4, 0);
		Expect(xz[0] == 9 && xz[1] == 90 && xz[4] == 3 && xz[5] == 30, "remove the first shifts down");
		RemovePair(xz.data(), 3, 2);
		Expect(xz[0] == 9 && xz[2] == 2 && xz[3] == 20, "remove the last leaves the rest");

		std::vector<float> one = { 5, 50, 0, 0 };
		InsertPair(one.data(), 1, 1, 6, 60);
		Expect(one == std::vector<float>{ 5, 50, 6, 60 }, "append to a one-point path");
	}
}

int main()
{
	TestNames();
	TestFinite();
	TestInsertSlot();
	TestRemoveSlot();
	TestPairs();
	return HostTest::Finish("mission paths core");
}
