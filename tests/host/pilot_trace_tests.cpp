/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/PilotTrace.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

namespace
{
	using namespace ExtraUtilities::Lua::PilotTrace;

	Frame At(std::uint32_t state, std::int32_t animation = 2, std::int32_t handle = -1)
	{
		Frame frame{};
		frame.nativeState = state;
		frame.animationIndex = animation;
		frame.animationHandle = handle;
		return frame;
	}

	bool Near(double a, double b)
	{
		// dt arrives as float, so compare at float precision.
		return std::fabs(a - b) < 1e-5;
	}

	// Snapshots are ~10 KB; keep them off the test's stack frames.
	std::unique_ptr<Snapshot> Read(const Recorder& recorder, bool* ok = nullptr)
	{
		auto snapshot = std::make_unique<Snapshot>();
		const bool read = recorder.Read(*snapshot);
		if (ok != nullptr)
		{
			*ok = read;
		}
		return snapshot;
	}
}

int main()
{
	// ---- Off by default: nothing is recorded -------------------------------
	{
		auto recorder = std::make_unique<Recorder>();
		HostTest::Expect(!recorder->IsEnabled(), "trace starts disabled");
		recorder->Record(0.1f, At(0), At(1));
		bool ok = false;
		auto snapshot = Read(*recorder, &ok);
		HostTest::Expect(ok, "read succeeds without a writer");
		HostTest::Expect(!snapshot->enabled, "snapshot reports disabled");
		HostTest::Expect(snapshot->localCalls == 0 && snapshot->sampleCount == 0,
			"disabled trace records nothing");
	}

	// ---- Started, but the hook has not run yet ------------------------------
	{
		auto recorder = std::make_unique<Recorder>();
		recorder->Start(false);
		auto snapshot = Read(*recorder);
		HostTest::Expect(snapshot->enabled, "started trace reports enabled");
		HostTest::Expect(snapshot->localCalls == 0 && snapshot->sampleCount == 0,
			"started trace is empty before the first call");
	}

	// ---- One crouch cycle: dwell of each transition state -------------------
	{
		auto recorder = std::make_unique<Recorder>();
		recorder->Start(false);

		// Standing (visit already in progress: not a complete visit).
		recorder->Record(0.1f, At(0), At(0));
		// Sniper selected: 0 -> 1, stand2Kneel starts.
		recorder->Record(0.1f, At(0), At(1, 0, 7));
		// Entering crouch for three calls; the third sees the clip finish.
		recorder->Record(0.25f, At(1, 0, 7), At(1, 0, 7));
		recorder->Record(0.25f, At(1, 0, 7), At(1, 0, 7));
		recorder->Record(0.5f, At(1, 0, 7), At(2, 3, -1));
		// Crouched for two calls, then sniper deselected: 2 -> 3.
		recorder->Record(0.1f, At(2, 3, -1), At(2, 3, -1));
		recorder->Record(0.1f, At(2, 3, -1), At(3, 1, 9));
		// Exiting crouch for two calls.
		recorder->Record(0.3f, At(3, 1, 9), At(3, 1, 9));
		recorder->Record(0.2f, At(3, 1, 9), At(0, 2, -1));

		auto snapshot = Read(*recorder);
		HostTest::Expect(snapshot->localCalls == 9, "every local call counted");
		HostTest::Expect(snapshot->recorded == 9 && snapshot->sampleCount == 9,
			"every call sampled when not changes-only");
		HostTest::Expect(Near(snapshot->time, 1.9), "trace clock is the sum of dt");

		HostTest::Expect(snapshot->samples[0].call == 1 && snapshot->samples[8].call == 9,
			"samples are oldest first and numbered from 1");
		HostTest::Expect(Near(snapshot->samples[4].time, 1.2),
			"sample time is the clock at the end of that call");
		HostTest::Expect(snapshot->samples[4].after.nativeState == 2 &&
			snapshot->samples[4].after.animationHandle == -1,
			"after-frame recorded as captured");

		const Dwell& standing = snapshot->dwell[0];
		HostTest::Expect(standing.count == 0,
			"a visit in progress when the trace started is not a complete dwell");

		const Dwell& entering = snapshot->dwell[1];
		HostTest::Expect(entering.count == 1, "one complete entering-crouch visit");
		HostTest::Expect(Near(entering.last, 1.0) && entering.lastCalls == 3,
			"entering-crouch dwell covers the calls that started in state 1");

		const Dwell& crouched = snapshot->dwell[2];
		HostTest::Expect(crouched.count == 1 && Near(crouched.last, 0.2) && crouched.lastCalls == 2,
			"crouched dwell measured");

		const Dwell& exiting = snapshot->dwell[3];
		HostTest::Expect(exiting.count == 1 && Near(exiting.last, 0.5) && exiting.lastCalls == 2,
			"exiting-crouch dwell measured");
	}

	// ---- Dwell statistics across repeated visits ----------------------------
	{
		auto recorder = std::make_unique<Recorder>();
		recorder->Start(true);
		const float waits[] = { 0.5f, 0.25f, 0.75f };
		for (const float wait : waits)
		{
			recorder->Record(0.1f, At(0), At(1));
			recorder->Record(wait, At(1), At(2));
			recorder->Record(0.1f, At(2), At(0));
		}

		auto snapshot = Read(*recorder);
		const Dwell& entering = snapshot->dwell[1];
		HostTest::Expect(entering.count == 3, "three entering visits");
		HostTest::Expect(Near(entering.min, 0.25) && Near(entering.max, 0.75),
			"min and max tracked");
		HostTest::Expect(Near(entering.total, 1.5) && Near(entering.last, 0.75),
			"total and last tracked");
		HostTest::Expect(snapshot->dwell[0].count == 2,
			"standing visits between cycles are complete after the first");
	}

	// ---- Changes-only sampling still keeps the clock and dwell --------------
	{
		auto recorder = std::make_unique<Recorder>();
		recorder->Start(true);
		recorder->Record(0.1f, At(0), At(0));
		recorder->Record(0.1f, At(0), At(1, 0, 7));
		recorder->Record(0.1f, At(1, 0, 7), At(1, 0, 7));
		recorder->Record(0.1f, At(1, 0, 7), At(1, 0, -1));
		recorder->Record(0.1f, At(1, 0, -1), At(2, 3, -1));

		auto snapshot = Read(*recorder);
		HostTest::Expect(snapshot->changesOnly, "changes-only flag reported");
		HostTest::Expect(snapshot->localCalls == 5, "unchanged calls still counted");
		HostTest::Expect(snapshot->recorded == 3 && snapshot->sampleCount == 3,
			"only calls whose state, index, or handle changed are sampled");
		HostTest::Expect(snapshot->samples[1].call == 4 &&
			snapshot->samples[1].after.animationHandle == -1,
			"a handle-only change is sampled with its call number");
		HostTest::Expect(Near(snapshot->samples[2].time, 0.5), "clock includes unsampled calls");
		HostTest::Expect(snapshot->dwell[1].count == 1 && snapshot->dwell[1].lastCalls == 3,
			"dwell counts unsampled calls too");
	}

	// ---- Discontinuity: a skipped stretch breaks the visit -------------------
	{
		auto recorder = std::make_unique<Recorder>();
		recorder->Start(false);
		recorder->Record(0.1f, At(0), At(1));
		recorder->Record(0.1f, At(1), At(1));
		// No local calls while in a vehicle; returns already in state 2.
		recorder->Record(0.1f, At(2), At(0));
		auto snapshot = Read(*recorder);
		HostTest::Expect(snapshot->dwell[1].count == 0,
			"an entering visit whose exit was not observed is not recorded");
		HostTest::Expect(snapshot->dwell[2].count == 0,
			"a visit whose entry was not observed is not recorded");
	}

	// ---- Invalid dt does not poison the clock --------------------------------
	{
		auto recorder = std::make_unique<Recorder>();
		recorder->Start(false);
		recorder->Record(std::numeric_limits<float>::quiet_NaN(), At(0), At(0));
		recorder->Record(-1.0f, At(0), At(0));
		recorder->Record(std::numeric_limits<float>::infinity(), At(0), At(0));
		recorder->Record(0.5f, At(0), At(0));
		auto snapshot = Read(*recorder);
		HostTest::Expect(Near(snapshot->time, 0.5), "non-finite and negative dt add nothing");
		HostTest::Expect(std::isnan(snapshot->samples[0].dt), "raw dt is still reported");
	}

	// ---- Ring wrap keeps the newest samples ----------------------------------
	{
		auto recorder = std::make_unique<Recorder>();
		recorder->Start(false);
		const std::uint32_t total = static_cast<std::uint32_t>(kCapacity) + 10u;
		for (std::uint32_t i = 0; i < total; ++i)
		{
			recorder->Record(0.01f, At(0), At(0));
		}
		auto snapshot = Read(*recorder);
		HostTest::Expect(snapshot->recorded == total, "recorded counts every sample");
		HostTest::Expect(snapshot->sampleCount == kCapacity, "ring holds capacity samples");
		HostTest::Expect(snapshot->samples[0].call == 11u &&
			snapshot->samples[kCapacity - 1].call == total,
			"oldest samples are overwritten first and order is preserved");
	}

	// ---- Stop keeps data; Start discards it; Reset hides it ------------------
	{
		auto recorder = std::make_unique<Recorder>();
		recorder->Start(false);
		recorder->Record(0.1f, At(0), At(1));
		recorder->Stop();
		recorder->Record(0.1f, At(1), At(2));

		auto stopped = Read(*recorder);
		HostTest::Expect(!stopped->enabled, "stop disables the trace");
		HostTest::Expect(stopped->localCalls == 1, "calls after stop are ignored");
		HostTest::Expect(stopped->sampleCount == 1, "stopped trace remains readable");

		recorder->Start(true);
		auto restarted = Read(*recorder);
		HostTest::Expect(restarted->enabled && restarted->changesOnly, "restart applies new options");
		HostTest::Expect(restarted->localCalls == 0 && restarted->sampleCount == 0,
			"restart discards earlier data even before the next call");
		recorder->Record(0.1f, At(0), At(1));
		auto fresh = Read(*recorder);
		HostTest::Expect(fresh->localCalls == 1 && fresh->samples[0].call == 1,
			"restarted trace numbers calls from 1 again");

		recorder->Reset();
		auto reset = Read(*recorder);
		HostTest::Expect(!reset->enabled, "reset disables the trace");
		HostTest::Expect(reset->localCalls == 0 && reset->sampleCount == 0,
			"reset hides data from the previous Lua state");
		recorder->Record(0.1f, At(0), At(1));
		HostTest::Expect(Read(*recorder)->localCalls == 0, "reset trace records nothing");
	}

	return HostTest::Finish("pilot trace");
}
