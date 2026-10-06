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

#include "Game/TextureWindowMath.h"
#include "HostTest.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace ExtraUtilities::TextureWindowMath;

namespace
{
	void ExpectNear(float actual, float expected, float tolerance, const char* what)
	{
		if (!(std::fabs(actual - expected) <= tolerance))
		{
			std::cerr << "FAIL: " << what << " expected " << expected << ", got " << actual << '\n';
			HostTest::CountFailure();
		}
	}

	void TestIdentityWindow()
	{
		Transform t;
		HostTest::Expect(FromWindow(0.0f, 0.0f, 1.0f, 1.0f, t), "unit window accepted");
		ExpectNear(t.scaleU, 1.0f, 1e-6f, "unit window scale u");
		ExpectNear(t.scaleV, 1.0f, 1e-6f, "unit window scale v");
		ExpectNear(t.scrollU, 0.0f, 1e-6f, "unit window scroll u");
		ExpectNear(t.scrollV, 0.0f, 1e-6f, "unit window scroll v");
	}

	void TestCornersMapToWindow()
	{
		// A crack window like wetglass.lua's: star at (0.3, 0.4) on a 1.5-tall,
		// 1.5 * 1.5-wide window.
		const float dv = 1.5f, du = dv * 1.5f;
		const float u0 = 0.5f - 0.3f * du, v0 = 0.5f - 0.4f * dv;
		Transform t;
		HostTest::Expect(FromWindow(u0, v0, du, dv, t), "crack window accepted");
		float tu = 0.0f, tv = 0.0f;
		Apply(t, 0.0f, 0.0f, tu, tv);
		ExpectNear(tu, u0, 1e-5f, "uv 0 maps to window origin u");
		ExpectNear(tv, v0, 1e-5f, "uv 0 maps to window origin v");
		Apply(t, 1.0f, 1.0f, tu, tv);
		ExpectNear(tu, u0 + du, 1e-5f, "uv 1 maps to window end u");
		ExpectNear(tv, v0 + dv, 1e-5f, "uv 1 maps to window end v");
		Apply(t, 0.3f, 0.4f, tu, tv);
		ExpectNear(tu, 0.5f, 1e-5f, "the requested point lands on the texture centre u");
		ExpectNear(tv, 0.5f, 1e-5f, "the requested point lands on the texture centre v");
	}

	void TestMirroredWindow()
	{
		// wetglass mirrors a splat with (u0 + du, v0, -du, dv).
		Transform t;
		HostTest::Expect(FromWindow(2.0f, 0.0f, -1.5f, 1.0f, t), "mirrored window accepted");
		HostTest::Expect(t.scaleU < 0.0f, "mirrored window has a negative u scale");
		float tu = 0.0f, tv = 0.0f;
		Apply(t, 0.0f, 0.0f, tu, tv);
		ExpectNear(tu, 2.0f, 1e-5f, "mirrored uv 0");
		Apply(t, 1.0f, 0.0f, tu, tv);
		ExpectNear(tu, 0.5f, 1e-5f, "mirrored uv 1");
	}

	void TestRejectsBadWindows()
	{
		Transform t;
		t.scrollU = 7.0f;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		HostTest::Expect(!FromWindow(0.0f, 0.0f, 0.0f, 1.0f, t), "zero width rejected");
		HostTest::Expect(!FromWindow(0.0f, 0.0f, 1.0f, 1.0e-6f, t), "tiny height rejected");
		HostTest::Expect(!FromWindow(nan, 0.0f, 1.0f, 1.0f, t), "NaN origin rejected");
		HostTest::Expect(!FromWindow(0.0f, 0.0f, inf, 1.0f, t), "infinite width rejected");
		HostTest::Expect(!FromWindow(0.0f, 0.0f, 1.0f, 2.0e4f, t), "absurd height rejected");
		ExpectNear(t.scrollU, 7.0f, 0.0f, "a rejected window leaves the output alone");
	}
}

int main()
{
	TestIdentityWindow();
	TestCornersMapToWindow();
	TestMirroredWindow();
	TestRejectsBadWindows();
	return HostTest::Finish("texture window math");
}
