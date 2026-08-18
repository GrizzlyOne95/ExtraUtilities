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

	inline constexpr int kAnchor0_Camera_Set_View[] = { 0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x50, 0x8B, 0x4D, 0x0C, 0x51, 0xE8, -1, -1, -1, -1, 0x83, 0xC4, 0x08, 0x5D, 0xC3 };
	inline constexpr int kAnchor1_Camera_View_Record_MainCam_reference[] = { 0xA1, 0xE0, 0xAA, 0x8E, 0x00 };
	inline constexpr int kAnchor2_Camera_zoomFactorFPP_reference[] = { 0xD9, 0x05, 0x10, 0xAD, 0x8E, 0x00 };
	inline constexpr int kAnchor3_Overlay_pause_wrapper[] = { 0x55, 0x8B, 0xEC, 0x51, 0x83, 0x3D, 0x2C, 0x83, 0x91, 0x00, 0x00, 0x74, 0x05, 0xE9, -1, -1, -1, -1, 0x0F, 0xB6, 0x05, 0x2B, 0x81, 0x91, 0x00, 0x85, 0xC0, 0x0F, 0x85, -1, -1, -1, -1 };

	inline constexpr AnchorSpec kRuntimeAnchors[] =
	{
		{ "Camera.Set_View", kAnchor0_Camera_Set_View, sizeof(kAnchor0_Camera_Set_View) / sizeof(kAnchor0_Camera_Set_View[0]), AnchorMatchMode::ExpectedVa, 0x0061d120u, true },
		{ "Camera.View_Record_MainCam reference", kAnchor1_Camera_View_Record_MainCam_reference, sizeof(kAnchor1_Camera_View_Record_MainCam_reference) / sizeof(kAnchor1_Camera_View_Record_MainCam_reference[0]), AnchorMatchMode::ExecutableContains, 0x00000000u, true },
		{ "Camera.zoomFactorFPP reference", kAnchor2_Camera_zoomFactorFPP_reference, sizeof(kAnchor2_Camera_zoomFactorFPP_reference) / sizeof(kAnchor2_Camera_zoomFactorFPP_reference[0]), AnchorMatchMode::ExecutableContains, 0x00000000u, true },
		{ "Overlay pause wrapper", kAnchor3_Overlay_pause_wrapper, sizeof(kAnchor3_Overlay_pause_wrapper) / sizeof(kAnchor3_Overlay_pause_wrapper[0]), AnchorMatchMode::ExpectedVa, 0x005d4690u, true }
	};

	inline constexpr size_t kRuntimeAnchorCount =
		sizeof(kRuntimeAnchors) / sizeof(kRuntimeAnchors[0]);
}
