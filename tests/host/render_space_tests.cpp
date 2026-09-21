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

#include "Ogre/OgreRenderSpace.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ExtraUtilities::OgreRenderSpace;

namespace
{
	struct Vector
	{
		float x;
		float y;
		float z;
	};

	// Field order matches the Ogre ABI struct the StaticGeometry bridge uses.
	struct Quaternion
	{
		float w;
		float x;
		float y;
		float z;
	};

	Quaternion QuaternionFromYaw(float yaw)
	{
		const float half = yaw * 0.5f;
		return Quaternion{ std::cos(half), 0.0f, std::sin(half), 0.0f };
	}

	int g_failures = 0;

	void ExpectEqual(float actual, float expected, const std::string& what)
	{
		if (actual != expected)
		{
			std::cerr << "FAIL: " << what << " -- expected " << expected
			          << ", got " << actual << '\n';
			++g_failures;
		}
	}

	void TestMapCentreBecomesRenderOrigin()
	{
		const Vector origin{ 2560.0f, 0.0f, -2560.0f };
		const Vector render = SimPositionToRender(Vector{ 2560.0f, 0.0f, 2560.0f }, origin);

		ExpectEqual(render.x, 0.0f, "map-centre X is recentred");
		ExpectEqual(render.y, 0.0f, "map-centre Y keeps its height");
		ExpectEqual(render.z, 0.0f, "map-centre Z is mirrored and recentred");
	}

	void TestPositionUsesAllOriginComponents()
	{
		const Vector render = SimPositionToRender(
			Vector{ 2600.0f, 15.0f, 2500.0f },
			Vector{ 2560.0f, 5.0f, -2560.0f });

		ExpectEqual(render.x, 40.0f, "position X subtracts the render origin");
		ExpectEqual(render.y, 10.0f, "position Y subtracts the render origin");
		ExpectEqual(render.z, 60.0f, "position Z mirrors before subtracting the render origin");
	}

	void TestDirectionMirrorsZWithoutTranslation()
	{
		const Vector render = SimDirectionToRender(Vector{ 3.0f, -2.0f, 7.0f });

		ExpectEqual(render.x, 3.0f, "direction X is unchanged");
		ExpectEqual(render.y, -2.0f, "direction Y is unchanged");
		ExpectEqual(render.z, -7.0f, "direction Z is mirrored");
	}

	// StaticGeometry instances and particle emitters are handed the same kind of
	// simulation coordinate by Lua, so they must land on the same render point.
	// This is the regression that kept EXU's StaticGeometry invisible: the
	// bridge passed sim positions straight to Ogre::StaticGeometry::addEntity.
	void TestStaticGeometryInstanceMatchesParticleConversion()
	{
		const Vector origin{ 2560.0f, 0.0f, -2560.0f };

		// A clutter instance placed just in front of a player at the map centre.
		const Vector instance = SimPositionToRender(Vector{ 2595.0f, 12.5f, 2560.0f }, origin);
		ExpectEqual(instance.x, 35.0f, "instance X converts like a particle");
		ExpectEqual(instance.y, 12.5f, "instance Y converts like a particle");
		ExpectEqual(instance.z, 0.0f, "instance Z converts like a particle");

		// And the default StaticGeometry origin, which is the sim origin and NOT
		// the render origin -- leaving it at zero puts region bucketing in a
		// different space from the geometry it is bucketing.
		const Vector bucketOrigin = SimPositionToRender(Vector{ 0.0f, 0.0f, 0.0f }, origin);
		ExpectEqual(bucketOrigin.x, -2560.0f, "geometry origin X is converted too");
		ExpectEqual(bucketOrigin.y, 0.0f, "geometry origin Y is converted too");
		ExpectEqual(bucketOrigin.z, 2560.0f, "geometry origin Z is converted too");
	}

	void TestOrientationReversesYaw()
	{
		const float yaw = 0.75f;
		const Quaternion render = SimOrientationToRender(QuaternionFromYaw(yaw));
		const float half = yaw * 0.5f;

		ExpectEqual(render.w, std::cos(half), "yaw W is unchanged");
		ExpectEqual(render.x, 0.0f, "yaw X stays zero");
		ExpectEqual(render.y, -std::sin(half), "mirroring Z reverses yaw");
		ExpectEqual(render.z, 0.0f, "yaw Z stays zero");
	}

	void TestOrientationConjugatesTheZMirror()
	{
		const Quaternion render = SimOrientationToRender(Quaternion{ 0.5f, 0.5f, -0.5f, 0.5f });

		ExpectEqual(render.w, 0.5f, "orientation W is unchanged");
		ExpectEqual(render.x, -0.5f, "orientation X is negated");
		ExpectEqual(render.y, 0.5f, "orientation Y is negated");
		ExpectEqual(render.z, 0.5f, "orientation Z is unchanged");
	}

	// The conversion is its own inverse, so a mis-applied double conversion is
	// indistinguishable from none -- worth pinning so it stays deliberate.
	void TestOrientationConversionIsAnInvolution()
	{
		const Quaternion source{ 0.1f, 0.2f, -0.3f, 0.4f };
		const Quaternion roundTrip = SimOrientationToRender(SimOrientationToRender(source));

		ExpectEqual(roundTrip.w, source.w, "double conversion restores W");
		ExpectEqual(roundTrip.x, source.x, "double conversion restores X");
		ExpectEqual(roundTrip.y, source.y, "double conversion restores Y");
		ExpectEqual(roundTrip.z, source.z, "double conversion restores Z");
	}

	void TestIdentityOrientationSurvives()
	{
		const Quaternion render = SimOrientationToRender(Quaternion{ 1.0f, 0.0f, 0.0f, 0.0f });

		ExpectEqual(render.w, 1.0f, "identity W survives");
		ExpectEqual(render.x, 0.0f, "identity X survives");
		ExpectEqual(render.y, 0.0f, "identity Y survives");
		ExpectEqual(render.z, 0.0f, "identity Z survives");
	}
}

int main()
{
	TestMapCentreBecomesRenderOrigin();
	TestPositionUsesAllOriginComponents();
	TestDirectionMirrorsZWithoutTranslation();
	TestStaticGeometryInstanceMatchesParticleConversion();
	TestOrientationReversesYaw();
	TestOrientationConjugatesTheZMirror();
	TestOrientationConversionIsAnInvolution();
	TestIdentityOrientationSurvives();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " render-space check(s) failed\n";
		return EXIT_FAILURE;
	}

	std::cout << "All render-space checks passed.\n";
	return EXIT_SUCCESS;
}
