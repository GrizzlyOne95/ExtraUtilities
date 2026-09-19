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

// Host-side checks for the friendly-name to ABI-id mapping used by
// exu.SetRenderEffectEnabled / SetRenderEffectFloat / GetRenderEffectStatus.
//
// This is the part of the bridge where a mistake is both easy and invisible:
// a name wired to the wrong integer would silently drive a different renderer
// effect, with nothing anywhere reporting an error. Pinning the exact numbers
// is the point of the file - these values cross a DLL boundary into an
// independently built OpenShim, so "whatever the enum happens to be" is not
// good enough.
//
// Needs no Windows, no OpenShim and no Lua.

#include "Game/RenderEffectNames.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace ExtraUtilities::RenderEffects;

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

	void ExpectEffect(const char* name, std::uint32_t expected)
	{
		std::uint32_t actual = 0;
		if (!EffectIdFromName(name, actual))
		{
			std::cerr << "FAIL: \"" << name << "\" should be a known effect\n";
			++g_failures;
			return;
		}
		if (actual != expected)
		{
			std::cerr << "FAIL: \"" << name << "\" should map to " << expected
			          << ", got " << actual << '\n';
			++g_failures;
		}
	}

	void ExpectParameter(const char* name, std::uint32_t expected)
	{
		std::uint32_t actual = 0;
		if (!ParameterIdFromName(name, actual))
		{
			std::cerr << "FAIL: \"" << name << "\" should be a known parameter\n";
			++g_failures;
			return;
		}
		if (actual != expected)
		{
			std::cerr << "FAIL: \"" << name << "\" should map to " << expected
			          << ", got " << actual << '\n';
			++g_failures;
		}
	}

	void ExpectReason(std::uint32_t code, const char* expected)
	{
		const std::string actual(ReasonName(code));
		if (actual != expected)
		{
			std::cerr << "FAIL: reason " << code << " should be \"" << expected
			          << "\", got \"" << actual << "\"\n";
			++g_failures;
		}
	}

	// The literal numbers below are the contract with OpenShim's
	// include/render_effect_intent.h. Changing one without changing the other
	// is exactly the failure this pins.
	void TestEffectNames()
	{
		ExpectEffect("ssao", 1u);
		ExpectEffect("depth_haze", 2u);
		ExpectEffect("soft_particles", 3u);
	}

	void TestParameterNames()
	{
		ExpectParameter("strength", 1u);
		ExpectParameter("radius", 2u);
		ExpectParameter("fade_start", 3u);
		ExpectParameter("fade_end", 4u);
		ExpectParameter("quality", 5u);
	}

	// The name set is closed on purpose: a mission must not be able to reach a
	// renderer internal by naming it.
	void TestUnknownNamesAreRejected()
	{
		std::uint32_t id = 0;
		Expect(!EffectIdFromName("", id), "an empty effect name is rejected");
		Expect(!EffectIdFromName("SSAO", id), "effect names are case sensitive");
		Expect(!EffectIdFromName("ssao ", id), "a trailing space is not silently trimmed");
		Expect(!EffectIdFromName("bloom", id), "an unimplemented effect name is rejected");
		Expect(!EffectIdFromName("global_gbuffer", id), "a renderer resource name is not an effect");

		Expect(!ParameterIdFromName("", id), "an empty parameter name is rejected");
		Expect(!ParameterIdFromName("Strength", id), "parameter names are case sensitive");
		Expect(!ParameterIdFromName("kernel", id), "a shader-internal name is not a parameter");
		Expect(!ParameterIdFromName("sampler0", id), "a sampler slot is not a parameter");
	}

	// These strings are EXU's public surface - a mission may branch on them -
	// so they are pinned independently of OpenShim's numbering.
	void TestReasonStrings()
	{
		ExpectReason(0u, "effective");
		ExpectReason(1u, "not-requested");
		ExpectReason(2u, "unsupported-renderer");
		ExpectReason(3u, "scene-depth-unavailable");
		ExpectReason(4u, "unsupported-build");
		ExpectReason(5u, "not-implemented");
		ExpectReason(6u, "unknown-effect");
		ExpectReason(7u, "pending");
	}

	// A newer OpenShim can report a code this build has never heard of. Saying
	// so beats inventing a meaning for it.
	void TestUnknownReasonDegradesHonestly()
	{
		ExpectReason(8u, "unknown");
		ExpectReason(0xDEADBEEFu, "unknown");
	}

	void TestOpenShimUnavailableReasonIsDistinct()
	{
		// Only EXU can observe OpenShim's absence, so this reason has no
		// OpenShim code and must not collide with one that does.
		const std::string unavailable(kReasonOpenShimUnavailable);
		Expect(unavailable == "openshim-unavailable", "the absent-OpenShim reason is stable");

		for (std::uint32_t code = 0; code <= 8; ++code)
		{
			Expect(std::string(ReasonName(code)) != unavailable,
				"no OpenShim reason code collides with openshim-unavailable");
		}
	}

	void TestResultCodes()
	{
		// Only kResultAccepted means the request was recorded; every other
		// value is a refusal the Lua layer reports as false.
		Expect(Abi::kResultAccepted == 0u, "accepted is 0");
		Expect(Abi::kResultRejectedEffect == 1u, "rejected-effect is 1");
		Expect(Abi::kResultRejectedParam == 2u, "rejected-param is 2");
		Expect(Abi::kResultRejectedValue == 3u, "rejected-value is 3");
	}
}

int main()
{
	TestEffectNames();
	TestParameterNames();
	TestUnknownNamesAreRejected();
	TestReasonStrings();
	TestUnknownReasonDegradesHonestly();
	TestOpenShimUnavailableReasonIsDistinct();
	TestResultCodes();

	if (g_failures != 0)
	{
		std::cerr << g_failures << " render-effect name check(s) failed\n";
		return EXIT_FAILURE;
	}

	std::cout << "All render-effect name checks passed.\n";
	return EXIT_SUCCESS;
}
