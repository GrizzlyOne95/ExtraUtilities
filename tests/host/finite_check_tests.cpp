/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

// Host-side checks for the NaN/infinity guards in front of lighting, particle,
// overlay and static-geometry calls. Every component has to be checked: one
// NaN channel is enough to poison an Ogre colour or a render-space position.

#include "Util/FiniteCheck.h"
#include "HostTest.h"

#include <limits>

using namespace ExtraUtilities::FiniteCheck;
using HostTest::Expect;

namespace
{
	struct Vector
	{
		float x;
		float y;
		float z;
	};

	struct Color
	{
		float r;
		float g;
		float b;
		float a;
	};

	const float kNaN = std::numeric_limits<float>::quiet_NaN();
	const float kInf = std::numeric_limits<float>::infinity();

	void TestScalar()
	{
		Expect(IsFiniteScalar(0.0f), "zero is finite");
		Expect(IsFiniteScalar(-1.0e30f), "a large negative value is finite");
		Expect(IsFiniteScalar(std::numeric_limits<float>::denorm_min()), "a denormal is finite");
		Expect(!IsFiniteScalar(kNaN), "NaN is rejected");
		Expect(!IsFiniteScalar(kInf), "+inf is rejected");
		Expect(!IsFiniteScalar(-kInf), "-inf is rejected");
	}

	void TestVector()
	{
		Expect(IsFiniteVector(Vector{ 1.0f, -2.0f, 3.0f }), "a finite vector passes");
		Expect(!IsFiniteVector(Vector{ kNaN, 0.0f, 0.0f }), "NaN in x is rejected");
		Expect(!IsFiniteVector(Vector{ 0.0f, kInf, 0.0f }), "inf in y is rejected");
		Expect(!IsFiniteVector(Vector{ 0.0f, 0.0f, -kInf }), "-inf in z is rejected");
	}

	void TestColor()
	{
		Expect(IsFiniteColor(Color{ 0.25f, 0.5f, 0.75f, 1.0f }), "a finite colour passes");
		// Range is not this check's business; HDR and negative values pass.
		Expect(IsFiniteColor(Color{ 4.0f, -1.0f, 0.0f, 0.0f }), "out-of-range but finite passes");
		Expect(!IsFiniteColor(Color{ kNaN, 0.0f, 0.0f, 1.0f }), "NaN in r is rejected");
		Expect(!IsFiniteColor(Color{ 0.0f, kInf, 0.0f, 1.0f }), "inf in g is rejected");
		Expect(!IsFiniteColor(Color{ 0.0f, 0.0f, kNaN, 1.0f }), "NaN in b is rejected");
		Expect(!IsFiniteColor(Color{ 0.0f, 0.0f, 0.0f, kNaN }), "NaN in alpha is rejected");
	}
}

int main()
{
	TestScalar();
	TestVector();
	TestColor();
	return HostTest::Finish("finite-check");
}
