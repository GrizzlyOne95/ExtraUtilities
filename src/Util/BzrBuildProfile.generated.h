/*
 * AUTO-GENERATED FILE. DO NOT EDIT BY HAND.
 *
 * Source: profiles/bzr_2.2.301.json + exu.json
 * Regenerate with: python tools/generate_bzr_build_profile.py
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::BzrBuildProfile
{
	enum class AnchorMatchMode : uint8_t
	{
		ExpectedVa,
		ExecutableContains,
		UniqueExecutable,
	};

	struct AnchorSpec
	{
		const char* name;
		const int* pattern;
		size_t patternSize;
		AnchorMatchMode mode;
		uintptr_t expectedVa;
		bool required;
	};

	inline constexpr const char* kProfileId = "bzr-2.2.301";
	inline constexpr const char* kGameVersion = "2.2.301";
	inline constexpr uintptr_t kImageBase = 0x00400000u;

	inline constexpr int kAnchor0_Overlay_pause_wrapper[] = { 0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x2C, 0x83, 0x91, 0x00, 0x00, 0x74, 0x05, 0xE9, -1, -1, -1, -1, 0x0F, 0xB6, 0x05, 0x2B, 0x81, 0x91, 0x00, 0x85, 0xC0, 0x0F, 0x85, -1, -1, -1, -1 };
	inline constexpr int kAnchor1_Overlay_game_shell_wrapper[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x34, 0xA1, 0x00, 0x70, 0x8E, 0x00, 0x33, 0xC5, 0x89, 0x45, 0xFC, 0x68, 0x28, 0x79, 0x88, 0x00 };
	inline constexpr int kAnchor2_Wingman_Hunt_activation[] = { 0x83, 0x7D, 0x08, 0x0D, 0x75, 0x10, 0x6A, 0x14, 0x8B, 0x4D, 0xFC, 0xE8, -1, -1, -1, -1, 0xB0, 0x01, 0xEB, -1, 0xEB, -1, 0x83, 0x7D, 0x08, 0x10, 0x75, -1, 0xA1, -1, -1, -1, -1, 0x50, 0x6A, 0x06, 0x8B, 0x4D, 0xFC, 0xE8, -1, -1, -1, -1 };

	inline constexpr AnchorSpec kRuntimeAnchors[] =
	{
		{ "Overlay pause wrapper", kAnchor0_Overlay_pause_wrapper, sizeof(kAnchor0_Overlay_pause_wrapper) / sizeof(kAnchor0_Overlay_pause_wrapper[0]), AnchorMatchMode::ExpectedVa, 0x005d4690u, true },
		{ "Overlay game shell wrapper", kAnchor1_Overlay_game_shell_wrapper, sizeof(kAnchor1_Overlay_game_shell_wrapper) / sizeof(kAnchor1_Overlay_game_shell_wrapper[0]), AnchorMatchMode::ExpectedVa, 0x005d42e0u, true },
		{ "Wingman Hunt activation", kAnchor2_Wingman_Hunt_activation, sizeof(kAnchor2_Wingman_Hunt_activation) / sizeof(kAnchor2_Wingman_Hunt_activation[0]), AnchorMatchMode::UniqueExecutable, 0x00000000u, true }
	};

	inline constexpr size_t kRuntimeAnchorCount =
		sizeof(kRuntimeAnchors) / sizeof(kRuntimeAnchors[0]);
}
