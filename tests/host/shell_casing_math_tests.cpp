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

#include "Game/ShellCasingMath.h"
#include "HostTest.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace ExtraUtilities::ShellCasingMath;

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

	struct FlatGround
	{
		float height = 0.0f;
		float operator()(float, float) const { return height; }
	};

	// A 30-degree slope rising toward +x.
	struct Slope
	{
		float operator()(float x, float) const { return std::tan(0.5235988f) * x; }
	};

	struct NoTerrain
	{
		float operator()(float, float) const { return std::numeric_limits<float>::quiet_NaN(); }
	};

	void TestBounceReflectsAndDamps()
	{
		Tuning tuning;
		Surface surface{ 0.4f, 0.3f };
		V3 velocity{ 2.0f, -5.0f, 0.0f };
		V3 omega{};
		const BounceResult r = Bounce(velocity, omega, V3{ 0.0f, 1.0f, 0.0f }, V3{}, surface, 0.06f, tuning, 0.01f);
		HostTest::Expect(r.contact && r.impact, "a 5 m/s landing is an impact");
		ExpectNear(velocity.y, 2.0f, 1e-4f, "normal speed reflected with restitution 0.4");
		ExpectNear(velocity.x, 1.4f, 1e-4f, "tangent speed keeps 1 - friction on a bounce");
		HostTest::Expect(Length(omega) > 0.1f, "an impact makes the casing tumble");

		// Leaving the surface is not a contact.
		V3 leaving{ 0.0f, 1.0f, 0.0f };
		V3 spin{};
		HostTest::Expect(!Bounce(leaving, spin, V3{ 0.0f, 1.0f, 0.0f }, V3{}, surface, 0.06f, tuning, 0.01f).contact,
			"a separating velocity is left alone");

		// A slow approach does not bounce: no jitter at rest.
		V3 resting{ 0.0f, -0.1f, 0.0f };
		const BounceResult rest = Bounce(resting, spin, V3{ 0.0f, 1.0f, 0.0f }, V3{}, surface, 0.06f, tuning, 0.01f);
		HostTest::Expect(rest.contact && !rest.impact, "a slow approach is a resting contact");
		ExpectNear(resting.y, 0.0f, 1e-6f, "a resting contact removes the normal speed");

		// Coulomb friction while resting: speed drops by mu * g * dt.
		V3 sliding{ 1.0f, -0.05f, 0.0f };
		Bounce(sliding, spin, V3{ 0.0f, 1.0f, 0.0f }, V3{}, surface, 0.06f, tuning, 0.1f);
		ExpectNear(sliding.x, 1.0f - 0.3f * 9.8f * 0.1f, 1e-4f, "resting friction is mu g dt");
	}

	void TestBounceOnMovingSurface()
	{
		// A casing sitting still on a deck that rises at 3 m/s is pushed up
		// with the deck, not left behind.
		Tuning tuning;
		Surface surface{ 0.3f, 0.5f };
		V3 velocity{};
		V3 omega{};
		Bounce(velocity, omega, V3{ 0.0f, 1.0f, 0.0f }, V3{ 0.0f, 3.0f, 0.0f }, surface, 0.06f, tuning, 0.01f);
		HostTest::Expect(velocity.y >= 3.0f - 1e-4f, "relative contact carries the casing with a moving surface");
	}

	void TestBoxContact()
	{
		Obstacle box;
		box.box = true;
		box.center = V3{ 0.0f, 1.0f, 0.0f };
		box.half = V3{ 2.0f, 1.0f, 3.0f };
		box.radius = Length(box.half);

		// Just above the top face.
		Contact top = BoxContact(V3{ 0.5f, 2.03f, 0.0f }, 0.06f, box);
		HostTest::Expect(top.hit, "a casing touching the top face is in contact");
		ExpectNear(top.normal.y, 1.0f, 1e-5f, "top face normal points up");
		ExpectNear(top.depth, 0.03f, 1e-4f, "top face depth");

		// Clear of the box.
		HostTest::Expect(!BoxContact(V3{ 0.0f, 2.2f, 0.0f }, 0.06f, box).hit, "a casing above the box is clear");
		HostTest::Expect(!ObstacleContact(V3{ 50.0f, 0.0f, 0.0f }, 0.06f, box).hit, "broad phase rejects far casings");

		// Inside: leaves through the nearest face (+x here).
		Contact inside = BoxContact(V3{ 1.8f, 1.0f, 0.0f }, 0.06f, box);
		HostTest::Expect(inside.hit, "a casing inside the box is in contact");
		ExpectNear(inside.normal.x, 1.0f, 1e-5f, "inside casing leaves through the nearest face");
		ExpectNear(inside.depth, 0.26f, 1e-4f, "inside depth clears the face plus the radius");

		// A rotated box: yaw 90 degrees swaps the x and z extents.
		Obstacle yawed = box;
		yawed.axis[0] = V3{ 0.0f, 0.0f, -1.0f };
		yawed.axis[2] = V3{ 1.0f, 0.0f, 0.0f };
		HostTest::Expect(BoxContact(V3{ 2.9f, 1.0f, 0.0f }, 0.06f, yawed).hit, "the rotated box reaches 3 m along world x");
		HostTest::Expect(!BoxContact(V3{ 0.0f, 1.0f, 2.9f }, 0.06f, yawed).hit, "and only 2 m along world z");
	}

	void TestSphereContact()
	{
		Contact c = SphereContact(V3{ 0.0f, 0.0f, 1.0f }, 0.1f, V3{}, 1.0f);
		HostTest::Expect(c.hit, "sphere overlap");
		ExpectNear(c.normal.z, 1.0f, 1e-5f, "sphere normal");
		ExpectNear(c.depth, 0.1f, 1e-5f, "sphere depth");
		HostTest::Expect(!SphereContact(V3{ 0.0f, 0.0f, 1.2f }, 0.1f, V3{}, 1.0f).hit, "sphere clear");
	}

	void TestTerrainNormal()
	{
		const V3 flat = TerrainNormal(0.0f, 0.0f, 0.0f, 0.0f, 0.3f);
		ExpectNear(flat.y, 1.0f, 1e-6f, "flat ground normal is up");
		Slope slope;
		const V3 n = SampleNormal(slope, 0.0f, 0.0f);
		ExpectNear(n.x, -0.5f, 1e-3f, "30-degree slope normal leans away from the rise");
		ExpectNear(n.y, 0.8660254f, 1e-3f, "30-degree slope normal y");
	}

	void TestSettlesFlatAndExpires()
	{
		Tuning tuning;
		FlatGround ground;
		Body body;
		body.position = V3{ 0.0f, 2.0f, 0.0f };
		body.velocity = V3{ 1.5f, 2.0f, 0.5f };
		body.omega = V3{ 6.0f, 0.0f, 9.0f };
		body.orientation = FromAxisAngle(V3{ 1.0f, 0.0f, 0.0f }, -1.2f);   // nose up
		body.linger = 6.0f;
		body.sinkDepth = 0.2f;

		float t = 0.0f;
		const float frame = 1.0f / 60.0f;
		while (body.phase == Phase::Flying && t < 10.0f)
		{
			Step(body, frame, ground, nullptr, 0, tuning);
			HostTest::Expect(body.position.y >= body.radius - 1e-4f, "never below the ground");
			t += frame;
		}
		HostTest::Expect(body.phase == Phase::Resting, "the casing comes to rest");
		HostTest::Expect(t < 4.0f, "and does so within a few seconds");
		HostTest::Expect(body.impacts >= 1, "it bounced at least once on the way");
		ExpectNear(body.position.y, body.radius, 1e-3f, "resting on the ground at its radius");
		const V3 axis = Rotate(body.orientation, V3{ 0.0f, 0.0f, 1.0f });
		HostTest::Expect(std::fabs(axis.y) < 0.1f, "resting casing lies flat");

		const float restedAt = t;
		while (body.phase != Phase::Done && t < restedAt + 20.0f)
		{
			Step(body, frame, ground, nullptr, 0, tuning);
			t += frame;
		}
		HostTest::Expect(body.phase == Phase::Done, "the casing expires");
		ExpectNear(t - restedAt, body.linger + tuning.sinkTime, 0.1f, "after lingering and sinking");
		ExpectNear(body.position.y, body.radius - body.sinkDepth, 1e-2f, "it sank by sinkDepth");
	}

	void TestPauseFreezes()
	{
		Tuning tuning;
		FlatGround ground;
		Body body;
		body.position = V3{ 0.0f, 5.0f, 0.0f };
		const V3 before = body.position;
		Step(body, 0.0f, ground, nullptr, 0, tuning);
		ExpectNear(body.position.y, before.y, 0.0f, "a zero step (paused game) does not move the casing");
		ExpectNear(body.age, 0.0f, 0.0f, "nor age it");
	}

	void TestFallsOffWorld()
	{
		Tuning tuning;
		NoTerrain none;
		Body body;
		float t = 0.0f;
		while (body.phase != Phase::Done && t < 30.0f)
		{
			Step(body, 0.05f, none, nullptr, 0, tuning);
			t += 0.05f;
		}
		HostTest::Expect(body.phase == Phase::Done, "with no terrain the casing retires");
		ExpectNear(t, tuning.maxAirTime, 0.2f, "after maxAirTime");
	}

	void TestOwnerIgnoredUntilClear()
	{
		Tuning tuning;
		FlatGround ground{ -100.0f };
		Obstacle hull;
		hull.handle = 0x00100001u;
		hull.box = true;
		hull.center = V3{};
		hull.half = V3{ 2.0f, 1.0f, 3.0f };
		hull.radius = Length(hull.half);

		// Spawned inside the owner's box with an upward speed: it must not be
		// shoved out sideways; it flies out the top and only then collides.
		Body body;
		body.owner = hull.handle;
		body.position = V3{ 0.0f, 0.5f, 0.0f };
		body.velocity = V3{ 0.0f, 6.0f, 0.0f };
		Step(body, 1.0f / 60.0f, ground, &hull, 1, tuning);
		ExpectNear(body.position.x, 0.0f, 1e-4f, "owner ignored while the casing starts inside its box");
		HostTest::Expect(!body.ownerClear, "not clear while still inside");

		float t = 0.0f;
		bool landed = false;
		while (t < 3.0f && !landed)
		{
			Step(body, 1.0f / 60.0f, ground, &hull, 1, tuning);
			t += 1.0f / 60.0f;
			landed = body.ownerClear && body.grounded && body.support == hull.handle;
		}
		HostTest::Expect(body.ownerClear, "the owner counts once the casing has left its box");
		HostTest::Expect(landed, "and the casing then lands on the owner's deck");
		HostTest::Expect(body.position.y >= hull.half.y + body.radius - 0.02f, "resting on top of the hull");
	}

	void TestEject()
	{
		V3 position, velocity;
		EjectParams params;
		Eject(V3{ 10.0f, 2.0f, 5.0f }, V3{ 1.0f, 0.0f, 0.0f }, V3{ 0.0f, 1.0f, 0.0f }, V3{ 0.0f, 0.0f, 1.0f },
			V3{ 0.0f, 0.0f, 10.0f }, params, V3{}, position, velocity);
		ExpectNear(position.z, 5.0f - params.back, 1e-5f, "eject starts toward the breech");
		ExpectNear(position.x, 10.0f + params.side, 1e-5f, "to the right");
		ExpectNear(position.y, 2.0f + params.up, 1e-5f, "and up");
		ExpectNear(velocity.x, params.speedSide, 1e-5f, "ejected out to the right");
		ExpectNear(velocity.y, params.speedUp, 1e-5f, "and up");
		ExpectNear(velocity.z, 10.0f - params.speedBack, 1e-5f, "inheriting the shooter's velocity");

		V3 p2, v2;
		Eject(V3{}, V3{ 1.0f, 0.0f, 0.0f }, V3{ 0.0f, 1.0f, 0.0f }, V3{ 0.0f, 0.0f, 1.0f }, V3{}, params,
			V3{ 1.0f, -1.0f, 1.0f }, p2, v2);
		HostTest::Expect(v2.x > params.speedSide && v2.y < params.speedUp, "jitter varies the speed within bounds");
	}

	void TestQuaternionHelpers()
	{
		const Quat q = FromAxes(V3{ 0.0f, 0.0f, -1.0f }, V3{ 0.0f, 1.0f, 0.0f }, V3{ 1.0f, 0.0f, 0.0f });
		const V3 z = Rotate(q, V3{ 0.0f, 0.0f, 1.0f });
		ExpectNear(z.x, 1.0f, 1e-5f, "FromAxes maps local z to the given z axis");
		const Quat spun = IntegrateSpin(Quat{}, V3{ 0.0f, 3.14159265f, 0.0f }, 0.5f);
		const V3 x = Rotate(spun, V3{ 1.0f, 0.0f, 0.0f });
		ExpectNear(x.z, -1.0f, 1e-4f, "half a second at pi rad/s about +y turns +x to -z");
	}
}

int main()
{
	TestBounceReflectsAndDamps();
	TestBounceOnMovingSurface();
	TestBoxContact();
	TestSphereContact();
	TestTerrainNormal();
	TestSettlesFlatAndExpires();
	TestPauseFreezes();
	TestFallsOffWorld();
	TestOwnerIgnoredUntilClear();
	TestEject();
	TestQuaternionHelpers();
	return HostTest::Finish("shell casing math");
}
