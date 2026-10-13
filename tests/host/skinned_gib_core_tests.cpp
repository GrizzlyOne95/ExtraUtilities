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

#include "Gibs/SkinnedGibCore.h"
#include "HostTest.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace ExtraUtilities::SkinnedGibCore;

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

	void ExpectVec(V3 actual, V3 expected, float tolerance, const char* what)
	{
		ExpectNear(actual.x, expected.x, tolerance, what);
		ExpectNear(actual.y, expected.y, tolerance, what);
		ExpectNear(actual.z, expected.z, tolerance, what);
	}

	constexpr float kPi = 3.14159265f;

	Quat AxisAngle(V3 axis, float angle)
	{
		const float s = std::sin(angle * 0.5f);
		return { std::cos(angle * 0.5f), axis.x * s, axis.y * s, axis.z * s };
	}

	struct FlatGround
	{
		float height = 0.0f;
		bool operator()(float, float, float& out) const
		{
			out = height;
			return true;
		}
	};

	// A 30-degree slope rising toward +x.
	struct Slope
	{
		bool operator()(float x, float, float& out) const
		{
			out = std::tan(kPi / 6.0f) * x;
			return true;
		}
	};

	struct NoGround
	{
		bool operator()(float, float, float&) const { return false; }
	};

	struct NanGround
	{
		bool operator()(float, float, float& out) const
		{
			out = std::numeric_limits<float>::quiet_NaN();
			return true;
		}
	};

	Body Dropped(float height, float radius = 0.1f)
	{
		Body body;
		body.position = { 0.0f, height, 0.0f };
		body.radius = radius;
		return body;
	}

	void TestLaunchIsDeterministic()
	{
		const V3 origin = { 10.0f, 2.0f, -4.0f };
		const V3 world = { 10.5f, 3.0f, -4.25f };
		const V3 bodyVelocity = { 1.0f, 0.0f, -2.0f };

		GibRandom a;
		a.state = 0xC0FFEEu;
		GibRandom b;
		b.state = 0xC0FFEEu;
		const LaunchResult first = ComputeLaunch(a, world, origin, bodyVelocity, 0.1f, 1.0f);
		const LaunchResult second = ComputeLaunch(b, world, origin, bodyVelocity, 0.1f, 1.0f);
		HostTest::Expect(first.velocity.x == second.velocity.x && first.velocity.y == second.velocity.y &&
				first.velocity.z == second.velocity.z && first.omega.x == second.omega.x &&
				first.omega.y == second.omega.y && first.omega.z == second.omega.z,
			"the same seed gives the same launch");

		// The draw order is velocity x, y, z then omega x, y, z, one Legacy() each.
		GibRandom replay;
		replay.state = 0xC0FFEEu;
		const float vx = replay.Legacy() * 10.0f;
		const float vy = replay.Legacy() * 10.0f + 5.0f;
		const float vz = replay.Legacy() * 10.0f;
		const V3 kick = { (world.x - origin.x) * 2.0f, (world.y - origin.y) * 1.0f, (world.z - origin.z) * 2.0f };
		ExpectNear(first.velocity.x, vx + kick.x + bodyVelocity.x, 1e-5f, "launch x = random + kick + body");
		ExpectNear(first.velocity.y, vy + kick.y + bodyVelocity.y, 1e-5f, "launch y = random + 5 + kick + body");
		ExpectNear(first.velocity.z, vz + kick.z + bodyVelocity.z, 1e-5f, "launch z = random + kick + body");
		ExpectNear(first.omega.x, replay.Legacy() * 5.0f, 1e-5f, "spin x is the fourth draw");
		ExpectNear(first.omega.y, replay.Legacy() * 5.0f, 1e-5f, "spin y is the fifth draw");

		GibRandom other;
		other.state = 0xC0FFEFu;
		const LaunchResult third = ComputeLaunch(other, world, origin, bodyVelocity, 0.1f, 1.0f);
		HostTest::Expect(third.velocity.x != first.velocity.x || third.velocity.y != first.velocity.y,
			"a different seed gives a different launch");

		// Heavy pieces spin at half rate; force 0 removes the random part.
		GibRandom light;
		light.state = 7;
		GibRandom heavy;
		heavy.state = 7;
		const LaunchResult l = ComputeLaunch(light, origin, origin, V3{}, 0.1f, 1.0f);
		const LaunchResult h = ComputeLaunch(heavy, origin, origin, V3{}, 0.5f, 1.0f);
		ExpectNear(h.omega.x, l.omega.x * 0.5f, 1e-5f, "pieces above radius 0.3 spin at half rate");
		GibRandom calm;
		calm.state = 7;
		const LaunchResult c = ComputeLaunch(calm, origin, origin, V3{}, 0.1f, 0.0f);
		ExpectVec(c.velocity, { 0.0f, 5.0f, 0.0f }, 1e-6f, "force 0 leaves only the +5 up");

		// The random part stays inside +-10 * force.
		GibRandom bounds;
		bounds.state = 99;
		for (int i = 0; i < 2000; ++i)
		{
			const LaunchResult r = ComputeLaunch(bounds, origin, origin, V3{}, 0.1f, 1.0f);
			if (std::fabs(r.velocity.x) > 10.0f || r.velocity.y < -5.0f || r.velocity.y > 15.0f ||
				std::fabs(r.omega.z) > 5.0f)
			{
				HostTest::Expect(false, "launch values stay within the legacy ranges");
				break;
			}
		}
	}

	void TestPoseComposition()
	{
		NodePose node;
		node.position = { 10.0f, 0.0f, 5.0f };
		node.orientation = AxisAngle({ 0.0f, 1.0f, 0.0f }, kPi * 0.5f);
		node.scale = { 2.0f, 2.0f, 2.0f };

		// Identity bone orientation: world = node * (bone + pivot).
		BoneFrame bone;
		bone.position = { 1.0f, 2.0f, 0.0f };
		PiecePose pose;
		HostTest::Expect(ComposePiecePose(node, bone, V3{ 0.5f, 0.0f, 0.0f }, pose), "pose composes");
		// local (1.5, 2, 0) * 2 = (3, 4, 0); +90 deg about y sends +x to -z.
		ExpectVec(pose.position, { 10.0f, 4.0f, 2.0f }, 1e-4f, "node * bone * pivot position");
		ExpectVec(Rotate(pose.orientation, V3{ 1.0f, 0.0f, 0.0f }), { 0.0f, 0.0f, -1.0f }, 1e-5f,
			"piece orientation is the node orientation when the bone is identity");

		// A rotated bone: 90 deg about z sends +x to +y and rotates the pivot offset with it.
		bone.orientation = AxisAngle({ 0.0f, 0.0f, 1.0f }, kPi * 0.5f);
		HostTest::Expect(ComposePiecePose(node, bone, V3{ 0.5f, 0.0f, 0.0f }, pose), "rotated bone composes");
		// pivot -> (0, 0.5, 0); local (1, 2.5, 0) * 2 = (2, 5, 0) -> node rotation -> (0, 5, -2); + node.
		ExpectVec(pose.position, { 10.0f, 5.0f, 3.0f }, 1e-4f, "bone rotation turns the pivot offset");
		// bone then node: +x -> +y -> +y (a y rotation leaves +y alone).
		ExpectVec(Rotate(pose.orientation, V3{ 1.0f, 0.0f, 0.0f }), { 0.0f, 1.0f, 0.0f }, 1e-5f,
			"piece orientation is node * bone");
		ExpectNear(std::sqrt(pose.orientation.w * pose.orientation.w + pose.orientation.x * pose.orientation.x +
					   pose.orientation.y * pose.orientation.y + pose.orientation.z * pose.orientation.z),
			1.0f, 1e-5f, "piece orientation is unit length");

		// Unnormalised input (a derived pose carries rounding) is normalised.
		node.orientation = { 2.0f, 0.0f, 0.0f, 0.0f };
		HostTest::Expect(ComposePiecePose(node, bone, V3{}, pose), "unnormalised node composes");
		ExpectNear(std::sqrt(pose.orientation.w * pose.orientation.w + pose.orientation.x * pose.orientation.x +
					   pose.orientation.y * pose.orientation.y + pose.orientation.z * pose.orientation.z),
			1.0f, 1e-5f, "result orientation is normalised");
	}

	void TestFallsThenSettles()
	{
		Body body = Dropped(10.0f);
		FlatGround ground;
		bool impacted = false;
		bool settled = false;
		float time = 0.0f;
		const float dt = 1.0f / 60.0f;
		for (int i = 0; i < 60 * 8 && !settled; ++i)
		{
			const bool alive = StepGib(body, dt, 8.0f, ground);
			HostTest::Expect(alive, "a falling gib is not retired");
			time += dt;
			if (!body.impacted)
			{
				HostTest::Expect(body.velocity.y < 0.0f, "falling downwards until the first contact");
			}
			impacted = impacted || body.impacted;
			settled = body.settled;
			HostTest::Expect(body.position.y >= 0.1f * 0.35f - 1e-4f, "never below the contact lift");
		}
		HostTest::Expect(impacted, "the fall ends in a contact");
		HostTest::Expect(settled, "the gib settles within a few seconds");
		HostTest::Expect(time > 1.2f && time < 4.0f, "free fall from 10 m plus bounces takes about 2-3 s");
		ExpectNear(body.position.y, 0.1f * 0.35f, 1e-4f, "settled at ground + radius * 0.35");
		ExpectVec(body.velocity, {}, 1e-6f, "settled velocity is zero");
		ExpectVec(body.omega, {}, 1e-6f, "settled spin is zero");
	}

	void TestSettleSinkRetireTimeline()
	{
		Body body = Dropped(0.5f);
		FlatGround ground;
		const float dt = 1.0f / 60.0f;
		const float linger = 1.0f;
		float time = 0.0f;
		float settledAt = -1.0f;
		float sinkStart = -1.0f;
		float retiredAt = -1.0f;
		float restY = 0.0f;
		for (int i = 0; i < 60 * 30; ++i)
		{
			const bool alive = StepGib(body, dt, linger, ground);
			time += dt;
			if (settledAt < 0.0f && body.settled)
			{
				settledAt = time;
				restY = body.position.y;
			}
			if (sinkStart < 0.0f && body.sinking)
			{
				sinkStart = time;
			}
			if (!alive)
			{
				retiredAt = time;
				break;
			}
		}
		HostTest::Expect(settledAt > 0.0f, "timeline: settles");
		HostTest::Expect(sinkStart > settledAt, "timeline: sinks after settling");
		HostTest::Expect(retiredAt > sinkStart, "timeline: retires after sinking");
		ExpectNear(sinkStart - settledAt, linger, 0.05f, "timeline: rests for the linger time");
		ExpectNear(retiredAt - sinkStart, 2.0f, 0.05f, "timeline: sinks for two seconds");
		ExpectNear(restY - body.position.y, 0.4f, 0.02f, "timeline: sinks 0.4 m");

		// A long linger is cut short so the sink ends by maxLifetime.
		Body lingerer = Dropped(0.5f);
		float t = 0.0f;
		float end = -1.0f;
		for (int i = 0; i < 60 * 40; ++i)
		{
			const bool alive = StepGib(lingerer, dt, 60.0f, ground);
			t += dt;
			if (!alive)
			{
				end = t;
				break;
			}
		}
		HostTest::Expect(end > 0.0f, "an over-long linger still retires");
		ExpectNear(end, 20.0f, 0.2f, "an over-long linger retires at the 20 s hard limit");
	}

	void TestSlopeBounceKeepsSpeedBelowIncoming()
	{
		Body body;
		body.position = { 0.0f, 0.10f, 0.0f };
		body.velocity = { 0.0f, -10.0f, 0.0f };
		body.radius = 0.1f;
		Slope ground;
		const float dt = 1.0f / 60.0f;
		const float incoming = Length(V3{ 0.0f, -10.0f - 9.8f * dt, 0.0f });
		HostTest::Expect(StepGib(body, dt, 8.0f, ground), "the slope contact step keeps the gib");
		HostTest::Expect(body.impacted, "the gib reaches the slope");
		const float outgoing = Length(body.velocity);
		HostTest::Expect(outgoing < incoming, "a bounce keeps less speed than it arrived with");
		HostTest::Expect(outgoing < incoming * 0.75f, "friction and restitution remove most of the speed");
		HostTest::Expect(!body.settled, "a 10 m/s impact does not settle at once");

		// The contact normal tilts the rebound away from vertical (down-slope is -x).
		HostTest::Expect(body.velocity.x < 0.0f, "the rebound has a component along the slope normal");

		// It keeps bouncing down to rest on the slope, finite throughout.
		bool finite = true;
		for (int i = 0; i < 60 * 10 && !body.settled; ++i)
		{
			StepGib(body, dt, 8.0f, ground);
			finite = finite && Finite(body.position) && Finite(body.velocity);
		}
		HostTest::Expect(finite, "slope motion stays finite");
		HostTest::Expect(body.settled, "the gib comes to rest on the slope");
	}

	void TestNoGroundRetiresAtMaxLifetime()
	{
		Body body = Dropped(100.0f);
		NoGround ground;
		const float dt = 0.1f;
		float time = 0.0f;
		float retiredAt = -1.0f;
		for (int i = 0; i < 400; ++i)
		{
			const bool alive = StepGib(body, dt, 8.0f, ground);
			time += dt;
			if (!alive)
			{
				retiredAt = time;
				break;
			}
		}
		HostTest::Expect(!body.hasGround, "no terrain marks the gib groundless");
		HostTest::Expect(!body.settled, "a groundless gib never settles");
		HostTest::Expect(retiredAt > 0.0f, "a groundless gib retires");
		ExpectNear(retiredAt, 20.0f, 0.2f, "a groundless gib retires at maxLifetime");

		// A ground that reports NaN is the same as no ground, not a crash.
		Body odd = Dropped(5.0f);
		NanGround nan;
		HostTest::Expect(StepGib(odd, dt, 8.0f, nan), "NaN terrain height keeps the state finite");
		HostTest::Expect(!odd.hasGround, "NaN terrain height counts as no ground");
	}

	void TestNanGuards()
	{
		FlatGround ground;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();

		Body badPosition = Dropped(1.0f);
		badPosition.position.x = nan;
		HostTest::Expect(!StepGib(badPosition, 0.016f, 8.0f, ground), "NaN position retires the gib");

		Body badVelocity = Dropped(1.0f);
		badVelocity.velocity.y = inf;
		HostTest::Expect(!StepGib(badVelocity, 0.016f, 8.0f, ground), "infinite velocity retires the gib");

		Body badSpin = Dropped(1.0f);
		badSpin.omega.z = nan;
		HostTest::Expect(!StepGib(badSpin, 0.016f, 8.0f, ground), "NaN spin retires the gib");

		Body badOrientation = Dropped(1.0f);
		badOrientation.orientation.w = nan;
		HostTest::Expect(!StepGib(badOrientation, 0.016f, 8.0f, ground), "NaN orientation retires the gib");

		Body steady = Dropped(1.0f);
		const V3 before = steady.position;
		HostTest::Expect(StepGib(steady, nan, 8.0f, ground), "a NaN step is ignored");
		HostTest::Expect(StepGib(steady, -1.0f, 8.0f, ground), "a negative step is ignored");
		HostTest::Expect(StepGib(steady, 0.0f, 8.0f, ground), "a zero step is ignored");
		ExpectVec(steady.position, before, 0.0f, "ignored steps do not move the gib");
		ExpectNear(steady.age, 0.0f, 0.0f, "ignored steps do not age the gib");

		// Launch inputs.
		GibRandom random;
		random.state = 5;
		const LaunchResult launch = ComputeLaunch(random, V3{}, V3{}, V3{ nan, 0.0f, 0.0f }, 0.1f, 1.0f);
		ExpectVec(launch.velocity, { 0.0f, 5.0f, 0.0f }, 0.0f, "a NaN body velocity falls back to straight up");
		const LaunchResult launchB = ComputeLaunch(random, V3{ nan, 0.0f, 0.0f }, V3{}, V3{}, 0.1f, 1.0f);
		ExpectVec(launchB.velocity, { 0.0f, 5.0f, 0.0f }, 0.0f, "a NaN spawn position falls back to straight up");
		ExpectVec(SanitizeBodyVelocity({ nan, 0.0f, 0.0f }), {}, 0.0f, "NaN death velocity is dropped");
		ExpectVec(SanitizeBodyVelocity({ 600.0f, 0.0f, 0.0f }), {}, 0.0f, "an implausible death velocity is dropped");
		ExpectVec(SanitizeBodyVelocity({ 3.0f, 0.0f, 4.0f }), { 3.0f, 0.0f, 4.0f }, 0.0f, "a sane death velocity is kept");

		NodePose node;
		BoneFrame bone;
		PiecePose pose;
		HostTest::Expect(!ComposePiecePose(node, bone, V3{ nan, 0.0f, 0.0f }, pose), "a NaN pivot fails to compose");
		node.position.y = inf;
		HostTest::Expect(!ComposePiecePose(node, bone, V3{}, pose), "an infinite node fails to compose");

		ExpectNear(PieceRadius(nan), 0.02f, 0.0f, "NaN radius uses the minimum");
		ExpectNear(PieceRadius(0.0f), 0.02f, 0.0f, "zero radius uses the minimum");
		ExpectNear(PieceRadius(0.3f), 0.3f, 0.0f, "a real radius is kept");
	}

	void TestFrameStep()
	{
		ExpectNear(FrameStep(0.016f, true, false, true, 10.0f, 10.017f), 0.016f, 1e-6f, "a normal frame steps by the real frame time");
		ExpectNear(FrameStep(0.5f, true, false, true, 10.0f, 10.5f), 0.1f, 0.0f, "a long frame is clamped to maxStep");
		ExpectNear(FrameStep(0.016f, true, true, true, 10.0f, 10.016f), 0.0f, 0.0f, "paused steps by zero");
		ExpectNear(FrameStep(0.016f, true, false, true, 10.0f, 10.0f), 0.0f, 0.0f, "a stalled clock steps by zero");
		ExpectNear(FrameStep(0.016f, true, false, true, 10.0f, 9.0f), 0.0f, 0.0f, "a clock that went back steps by zero");
		ExpectNear(FrameStep(0.016f, true, false, true, 10.0f, 30.0f), 0.0f, 0.0f, "a load jump steps by zero");
		ExpectNear(FrameStep(0.016f, true, false, false, 0.0f, 10.0f), 0.0f, 0.0f, "the first sample steps by zero");
		ExpectNear(FrameStep(0.016f, false, false, true, 10.0f, 10.016f), 0.0f, 0.0f, "an unreadable clock steps by zero");
	}

	SlotView View(bool active, bool match, bool mesh, bool settled, uint64_t serial)
	{
		SlotView view;
		view.active = active;
		view.sceneMatches = match;
		view.hasEntityForMesh = mesh;
		view.settled = settled;
		view.serial = serial;
		return view;
	}

	void TestSlotChoice()
	{
		{
			const SlotChoice c = ChooseSlot(nullptr, 0, 160);
			HostTest::Expect(c.kind == SlotChoiceKind::Append, "an empty pool appends");
		}
		{
			const SlotView slots[] = { View(false, true, false, false, 1), View(false, true, true, false, 2) };
			const SlotChoice c = ChooseSlot(slots, 2, 2);
			HostTest::Expect(c.kind == SlotChoiceKind::ReuseMesh && c.index == 1, "a free slot with the same mesh wins");
		}
		{
			const SlotView slots[] = { View(true, true, true, false, 1), View(false, true, false, false, 2) };
			const SlotChoice c = ChooseSlot(slots, 2, 2);
			HostTest::Expect(c.kind == SlotChoiceKind::ReuseFree && c.index == 1, "a full pool reuses a free slot of another mesh");
		}
		{
			const SlotView slots[] = { View(false, true, false, false, 1) };
			const SlotChoice c = ChooseSlot(slots, 1, 5);
			HostTest::Expect(c.kind == SlotChoiceKind::Append, "below the limit a new slot is preferred to replacing a mesh");
		}
		{
			const SlotView slots[] = { View(true, true, true, false, 7), View(true, true, true, true, 9),
				View(true, true, true, true, 4), View(true, true, true, false, 2) };
			const SlotChoice c = ChooseSlot(slots, 4, 4);
			HostTest::Expect(c.kind == SlotChoiceKind::Recycle && c.index == 2, "full: the oldest settled gib is recycled");
		}
		{
			const SlotView slots[] = { View(true, true, true, false, 7), View(true, true, true, false, 3),
				View(true, true, true, false, 5) };
			const SlotChoice c = ChooseSlot(slots, 3, 3);
			HostTest::Expect(c.kind == SlotChoiceKind::Recycle && c.index == 1, "full, none settled: the oldest gib is recycled");
		}
		{
			// An inactive slot from another scene is neither reused nor recycled.
			const SlotView slots[] = { View(false, false, true, false, 1) };
			const SlotChoice c = ChooseSlot(slots, 1, 1);
			HostTest::Expect(c.kind == SlotChoiceKind::None, "a full pool of stale-scene slots yields nothing");
		}
		{
			const SlotChoice c = ChooseSlot(nullptr, 0, 0);
			HostTest::Expect(c.kind == SlotChoiceKind::Append, "a zero limit still allows one slot");
		}
	}
}

int main()
{
	TestLaunchIsDeterministic();
	TestPoseComposition();
	TestFallsThenSettles();
	TestSettleSinkRetireTimeline();
	TestSlopeBounceKeepsSpeedBelowIncoming();
	TestNoGroundRetiresAtMaxLifetime();
	TestNanGuards();
	TestFrameStep();
	TestSlotChoice();
	return HostTest::Finish("skinned gib core");
}
