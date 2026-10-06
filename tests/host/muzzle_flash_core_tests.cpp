// Host checks for src/Game/MuzzleFlashCore.h (the engine-free timing and pose
// policy shared by exu.flash and BZR-OpenShim's weapon presentation).

#include "Game/MuzzleFlashCore.h"
#include "HostTest.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace MuzzleFlashCore;

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

	void ExpectNear(V3 actual, V3 expected, float tolerance, const char* what)
	{
		ExpectNear(actual.x, expected.x, tolerance, what);
		ExpectNear(actual.y, expected.y, tolerance, what);
		ExpectNear(actual.z, expected.z, tolerance, what);
	}

	// A yawed owner: front = +X, right = -Z, up = +Y (BZ left-handed basis).
	Pose YawedOwner()
	{
		return { { 100.0f, 5.0f, -20.0f }, FromAxes({ 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }) };
	}

	void FromAxesReproducesTheBasis()
	{
		const Quat q = FromAxes({ 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f });
		ExpectNear(Rotate(q, { 1.0f, 0.0f, 0.0f }), { 0.0f, 0.0f, -1.0f }, 1e-5f, "right axis");
		ExpectNear(Rotate(q, { 0.0f, 1.0f, 0.0f }), { 0.0f, 1.0f, 0.0f }, 1e-5f, "up axis");
		ExpectNear(Rotate(q, { 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f }, 1e-5f, "front axis");

		// A sheared engine matrix is re-orthonormalized around front.
		const Quat s = FromAxes({ 1.0f, 0.0f, 0.1f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 2.0f });
		ExpectNear(Rotate(s, { 0.0f, 0.0f, 1.0f }), { 0.0f, 0.0f, 1.0f }, 1e-5f, "sheared front kept");
		ExpectNear(Rotate(s, { 1.0f, 0.0f, 0.0f }), { 1.0f, 0.0f, 0.0f }, 1e-5f, "sheared right orthogonalized");

		HostTest::Expect(IsFinite(FromAxes({}, {}, {})), "degenerate axes give a finite identity");
	}

	void RelativeComposeRoundTrip()
	{
		const Pose owner = YawedOwner();
		const Pose muzzle{ { 103.0f, 6.5f, -20.5f }, FromAxes({ 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }) };
		const Pose local = Relative(owner, muzzle);
		ExpectNear(local.position, { 0.5f, 1.5f, 3.0f }, 1e-4f, "muzzle in owner space (right, up, front)");
		const Pose back = Compose(owner, local);
		ExpectNear(back.position, muzzle.position, 1e-4f, "round trip position");
		ExpectNear(Rotate(back.orientation, { 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f }, 1e-5f, "round trip front");

		// The owner turns 90 degrees left (front -> -Z): the flash turns with it.
		const Pose turned{ owner.position, FromAxes({ -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }) };
		const Pose carried = Compose(turned, local);
		ExpectNear(carried.position, { 99.5f, 6.5f, -23.0f }, 1e-4f, "carried muzzle position");
		ExpectNear(Rotate(carried.orientation, { 0.0f, 0.0f, 1.0f }), { 0.0f, 0.0f, -1.0f }, 1e-5f, "carried front");
	}

	void RollKeepsTheBarrelAxis()
	{
		const Pose pose = YawedOwner();
		const Pose rolled = RollAboutFront(pose, 1.2f);
		ExpectNear(rolled.position, pose.position, 0.0f, "roll keeps position");
		ExpectNear(Rotate(rolled.orientation, { 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f }, 1e-5f, "roll keeps front");
		const V3 up = Rotate(rolled.orientation, { 0.0f, 1.0f, 0.0f });
		ExpectNear(up.y, std::cos(1.2f), 1e-5f, "roll turns up about front");
	}

	void DefinitionPolicy()
	{
		Definition d{ 0.1f, 3.0f, 1.0f };   // BZ2 gminigun_c: startRadius 3, finishRadius 1, animateTime 0.1
		HostTest::Expect(Sanitize(d), "BZ2 minigun flash is drawable");
		ExpectNear(ScaleAt(d, 0.0f), 3.0f, 1e-6f, "start scale");
		ExpectNear(ScaleAt(d, 0.05f), 2.0f, 1e-6f, "mid scale");
		ExpectNear(ScaleAt(d, 1.0f), 1.0f, 1e-6f, "scale clamps after the end");
		ExpectNear(ScaleAt(d, -1.0f), 3.0f, 1e-6f, "scale clamps before the start");
		HostTest::Expect(!Expired(d, 0.099f), "alive just before the end");
		HostTest::Expect(Expired(d, 0.1f), "expired at the end");

		Definition tiny{ 0.0f, 1.0f, 1.0f };
		HostTest::Expect(Sanitize(tiny) && tiny.duration == kMinDuration, "zero duration clamps to the minimum");
		Definition huge{ 100.0f, 1000.0f, -5.0f };
		HostTest::Expect(Sanitize(huge) && huge.duration == kMaxDuration && huge.startScale == kMaxScale && huge.finishScale == 0.0f,
			"duration and scales clamp");
		Definition invisible{ 0.1f, 0.0f, 0.0f };
		HostTest::Expect(!Sanitize(invisible), "zero size is not drawn");
		Definition nan{ std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f };
		HostTest::Expect(!Sanitize(nan), "NaN duration is not drawn");
	}

	void ClockFollowsSimulationTime()
	{
		SimClock clock;
		ExpectNear(clock.Advance(10.0f, false, true), 0.0f, 0.0f, "first sample has no step");
		ExpectNear(clock.Advance(10.02f, false, true), 0.02f, 1e-6f, "normal step");
		ExpectNear(clock.Advance(10.05f, true, true), 0.0f, 0.0f, "paused does not age");
		ExpectNear(clock.Advance(10.05f, false, true), 0.0f, 0.0f, "stalled sim does not age");
		ExpectNear(clock.Advance(13.0f, false, true), 0.0f, 0.0f, "a load jump does not age");
		ExpectNear(clock.Advance(13.01f, false, true), 0.01f, 1e-5f, "resumes after the jump");
		ExpectNear(clock.Advance(2.0f, false, true), 0.0f, 0.0f, "a restart does not age");
		ExpectNear(clock.Advance(0.0f, false, false), 0.0f, 0.0f, "unreadable time does not age");
		ExpectNear(clock.Advance(2.03f, false, true), 0.03f, 1e-5f, "unreadable sample keeps the last valid time");
	}
}

int main()
{
	FromAxesReproducesTheBasis();
	RelativeComposeRoundTrip();
	RollKeepsTheBarrelAxis();
	DefinitionPolicy();
	ClockFollowsSimulationTime();
	return HostTest::Finish("muzzle flash core");
}
