/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/PilotStateSemantics.h"

#include <cstring>

int main()
{
	using namespace ExtraUtilities::Lua::PilotState;

	HostTest::Expect(std::strcmp(SemanticStateName(0), "standing") == 0, "state 0 is standing");
	HostTest::Expect(std::strcmp(SemanticStateName(1), "enteringCrouch") == 0, "state 1 enters crouch");
	HostTest::Expect(std::strcmp(SemanticStateName(2), "crouched") == 0, "state 2 is crouched");
	HostTest::Expect(std::strcmp(SemanticStateName(3), "exitingCrouch") == 0, "state 3 exits crouch");
	HostTest::Expect(std::strcmp(SemanticStateName(99), "unknown") == 0, "unknown FSM state remains explicit");

	HostTest::Expect(!IsTransitionState(0), "standing is not a transition");
	HostTest::Expect(IsTransitionState(1), "enter crouch is a transition");
	HostTest::Expect(!IsTransitionState(2), "crouched is not a transition");
	HostTest::Expect(IsTransitionState(3), "exit crouch is a transition");

	HostTest::Expect(!IsFullyCrouchedState(1), "entering crouch is not fully crouched");
	HostTest::Expect(IsFullyCrouchedState(2), "state 2 is fully crouched");
	HostTest::Expect(!IsFullyCrouchedState(3), "exiting crouch is not fully crouched");

	HostTest::Expect(std::strcmp(KnownAnimationName(0), "stand2Kneel") == 0, "animation 0 mapping");
	HostTest::Expect(std::strcmp(KnownAnimationName(1), "kneel2stand") == 0, "animation 1 mapping");
	HostTest::Expect(std::strcmp(KnownAnimationName(2), "idle") == 0, "animation 2 mapping");
	HostTest::Expect(std::strcmp(KnownAnimationName(3), "fireRecoilSniper") == 0, "animation 3 mapping");
	HostTest::Expect(std::strcmp(KnownAnimationName(10), "landParachute") == 0, "animation 10 mapping");
	HostTest::Expect(std::strcmp(KnownAnimationName(11), "jump") == 0, "animation 11 mapping");
	HostTest::Expect(KnownAnimationName(4) == nullptr, "unproven locomotion index is not guessed");

	return HostTest::Finish("pilot state semantics");
}
