/* Copyright (C) 2023-2026 VTrider
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

// Host-side checks for the Lua -> Ogre StringInterface value conversion used by
// exu.SetParticleEmitterParameter / exu.SetParticleAffectorParameter.
//
// These run on any host with a C++17 compiler: the header under test has no
// Windows, Ogre or Lua dependency, which is the whole reason the conversion
// rules live in their own translation unit instead of inside Environment.cpp.

#include "Ogre/OgreParameterValue.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

using namespace ExtraUtilities::OgreParams;

namespace
{
	int g_failures = 0;

	void Expect(bool condition, const std::string& what)
	{
		if (!condition)
		{
			std::cerr << "FAIL: " << what << '\n';
			++g_failures;
		}
	}

	void ExpectEqual(const std::string& actual, const std::string& expected, const std::string& what)
	{
		if (actual != expected)
		{
			std::cerr << "FAIL: " << what << " -- expected \"" << expected
			          << "\", got \"" << actual << "\"\n";
			++g_failures;
		}
	}

	void TestBoolConversion()
	{
		// Ogre::StringConverter::parseBool reads these spellings.
		ExpectEqual(BoolToParameterValue(true), "true", "true converts to \"true\"");
		ExpectEqual(BoolToParameterValue(false), "false", "false converts to \"false\"");
	}

	void TestIntegralNumbers()
	{
		// Mission code passes box dimensions and quotas as whole numbers. A
		// .particle script spells those without a fraction, so the round trip
		// through getParameter should read back the same way.
		ExpectEqual(NumberToParameterValue(120.0), "120", "120 stays integral");
		ExpectEqual(NumberToParameterValue(0.0), "0", "zero stays integral");
		ExpectEqual(NumberToParameterValue(-5.0), "-5", "negatives keep their sign");
		ExpectEqual(NumberToParameterValue(6400.0), "6400", "an emission rate stays integral");
	}

	void TestFractionalNumbers()
	{
		ExpectEqual(NumberToParameterValue(0.45), "0.45", "trailing zeros are trimmed");
		ExpectEqual(NumberToParameterValue(1.25), "1.25", "a quarter survives");
		ExpectEqual(NumberToParameterValue(-0.5), "-0.5", "a negative fraction survives");

		// std::to_string is fixed-format with six fraction digits, matching
		// Ogre's own StringConverter default, so this is a truncation the
		// engine would have applied anyway rather than a loss we introduce.
		ExpectEqual(NumberToParameterValue(0.123456789), "0.123457", "six fraction digits are kept");
	}

	void TestNonFiniteNumbersAreRejected()
	{
		// Ogre's parser has no spelling for these; an empty result is the
		// signal the Lua layer turns into an argument error.
		Expect(NumberToParameterValue(std::numeric_limits<double>::infinity()).empty(),
			"infinity is rejected");
		Expect(NumberToParameterValue(-std::numeric_limits<double>::infinity()).empty(),
			"negative infinity is rejected");
		Expect(NumberToParameterValue(std::numeric_limits<double>::quiet_NaN()).empty(),
			"NaN is rejected");
	}

	void TestParameterNameValidation()
	{
		Expect(IsValidParameterName("width"), "\"width\" is a valid name");
		Expect(IsValidParameterName("inner_width"), "underscores are allowed");
		Expect(IsValidParameterName("colour0"), "digits are allowed");
		Expect(IsValidParameterName("time1"), "\"time1\" is a valid name");

		Expect(!IsValidParameterName(""), "an empty name is rejected");
		Expect(!IsValidParameterName("force vector"), "a space in a name is rejected");
		Expect(!IsValidParameterName("width\n"), "a newline in a name is rejected");
		Expect(!IsValidParameterName("../../etc"), "punctuation in a name is rejected");
		Expect(!IsValidParameterName(std::string(kMaxParameterNameLength + 1, 'a')),
			"an over-long name is rejected");
		Expect(IsValidParameterName(std::string(kMaxParameterNameLength, 'a')),
			"a name exactly at the cap is accepted");
	}

	void TestParameterValueValidation()
	{
		// Vectors and colours are space-separated text, so spaces must pass.
		Expect(IsValidParameterValue("20 0 -5"), "a force vector is a valid value");
		Expect(IsValidParameterValue("1 1 1 0.75"), "a colour quadruple is a valid value");
		Expect(IsValidParameterValue(""), "an empty value is allowed");
		Expect(IsValidParameterValue("add"), "an enum word is a valid value");

		Expect(!IsValidParameterValue(std::string("a\0b", 3)), "an embedded NUL is rejected");
		Expect(!IsValidParameterValue("a\nb"), "a newline is rejected");
		Expect(!IsValidParameterValue("a\tb"), "a tab is rejected");
		Expect(!IsValidParameterValue(std::string(kMaxParameterValueLength + 1, 'x')),
			"an over-long value is rejected");
		Expect(IsValidParameterValue(std::string(kMaxParameterValueLength, 'x')),
			"a value exactly at the cap is accepted");
	}

	// The conversions exist to be fed straight back into an Ogre parameter, so
	// check the composed path a mission actually takes.
	void TestRoundTripShapes()
	{
		Expect(IsValidParameterValue(NumberToParameterValue(120.0)),
			"a converted number is a valid value");
		Expect(IsValidParameterValue(BoolToParameterValue(true)),
			"a converted boolean is a valid value");
	}
}

int main()
{
	TestBoolConversion();
	TestIntegralNumbers();
	TestFractionalNumbers();
	TestNonFiniteNumbersAreRejected();
	TestParameterNameValidation();
	TestParameterValueValidation();
	TestRoundTripShapes();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " parameter-value check(s) failed\n";
		return EXIT_FAILURE;
	}

	std::cout << "All parameter-value checks passed.\n";
	return EXIT_SUCCESS;
}
