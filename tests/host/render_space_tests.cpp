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
}

int main()
{
	TestMapCentreBecomesRenderOrigin();
	TestPositionUsesAllOriginComponents();
	TestDirectionMirrorsZWithoutTranslation();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " render-space check(s) failed\n";
		return EXIT_FAILURE;
	}

	std::cout << "All render-space checks passed.\n";
	return EXIT_SUCCESS;
}
