/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Game/DeathCamera.h"

#include "Game/FirstPersonTarget.h"
#include "InlinePatch.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ExtraUtilities::Lua::DeathCamera
{
	namespace
	{
		namespace Addr = EngineAddresses::DeathCamera;

		using CameraPushFn = int(__cdecl*)();
		using CameraSetFreeEyeFn = void(__cdecl*)(void* renderBridge);

		// Person layout (Docs/Research/DEATH_CAMERA_RE_20261005.md).
		constexpr std::size_t kPersonRenderBridgeOffset = 0x0F0;
		constexpr std::size_t kPersonAnimIndexOffset = 0x2A8;
		constexpr std::int32_t kDeathIndex = 8;
		// Bridge fields read only by the probe.
		constexpr std::size_t kBridgeWorldEntity = 0x094;
		constexpr std::size_t kBridgeFirstPersonEntity = 0x0C0;
		constexpr std::size_t kBridgePovBone = 0x0E0;
		constexpr std::size_t kBridgeFirstPersonDone = 0x0DC;
		// Camera mode 0 = attached to a render bridge (cockpit or chase).
		constexpr std::int32_t kCameraAttached = 0;

		int __cdecl PushCameraStub();
		void __cdecl SetViewStub(void* renderBridge);

		// call rel32 from the site to the stub.
		std::vector<std::uint8_t> CallTo(std::uintptr_t site, const void* target)
		{
			const std::int32_t rel = static_cast<std::int32_t>(
				reinterpret_cast<std::uintptr_t>(target) - (site + 5u));
			std::vector<std::uint8_t> bytes(5);
			bytes[0] = 0xE8;
			std::memcpy(bytes.data() + 1, &rel, sizeof(rel));
			return bytes;
		}

		// Preimages: the stock call rel32 ties each patch to its own site.
		InlinePatch g_pushPatch(
			Addr::ExplodePilotPushCameraCall,
			CallTo(Addr::ExplodePilotPushCameraCall, reinterpret_cast<const void*>(&PushCameraStub)),
			BasicPatch::Status::INACTIVE,
			{ 0xE8, 0xCA, 0xC7, 0x16, 0x00 });

		InlinePatch g_setViewPatch(
			Addr::ExplodePilotSetViewCall,
			CallTo(Addr::ExplodePilotSetViewCall, reinterpret_cast<const void*>(&SetViewStub)),
			BasicPatch::Status::INACTIVE,
			{ 0xE8, 0xE8, 0xF6, 0x16, 0x00 });

		std::atomic<std::uint8_t> g_mode{ static_cast<std::uint8_t>(Mode::Stock) };
		std::atomic<bool> g_probe{ false };
		std::atomic<std::uint32_t> g_kept{ 0 };
		std::atomic<std::uint32_t> g_forced{ 0 };
		std::atomic<std::uint32_t> g_declined{ 0 };

		// Game thread only (the stubs and the Simulate hook run there).
		bool g_skipSetView = false;
		const void* g_armedPerson = nullptr;
		void* g_armedBridge = nullptr;
		std::atomic<bool> g_armed{ false };
		// death1 has been seen applied since arming (the guard's index check
		// waits for it: the kill lands before the next Simulate selects 8).
		bool g_seenDeath = false;
		std::uint32_t g_probeTick = 0;

		template <typename... Args>
		void LogNoThrow(const char* format, Args... args) noexcept
		{
			try
			{
				Logging::LogMessage(format, args...);
			}
			catch (...)
			{
				OutputDebugStringA("ExtraUtilities: death camera log line dropped\n");
			}
		}

		void Disarm() noexcept
		{
			g_armedPerson = nullptr;
			g_armedBridge = nullptr;
			g_seenDeath = false;
			g_armed.store(false, std::memory_order_release);
		}

		struct CameraState
		{
			std::int32_t mode = -1;
			void* attached = nullptr;
		};

		bool ReadCameraSeh(CameraState& out) noexcept
		{
			__try
			{
				out.mode = *reinterpret_cast<const std::int32_t*>(Addr::CameraMode);
				out.attached = *reinterpret_cast<void* const*>(Addr::CameraAttachedBridge);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool ReadUserSeh(void*& outUser, void*& outBridge) noexcept
		{
			outUser = nullptr;
			outBridge = nullptr;
			__try
			{
				outUser = *reinterpret_cast<void* const*>(BZR::GameObject::p_userObject);
				if (outUser != nullptr)
				{
					outBridge = *reinterpret_cast<void* const*>(
						reinterpret_cast<const std::uint8_t*>(outUser) + kPersonRenderBridgeOffset);
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool IsNetGameSeh() noexcept
		{
			__try
			{
				return *BZR::Multiplayer::isNetGame;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return true;
			}
		}

		// Every condition of the first-person death view, evaluated inside the
		// player branch of the pilot-kill path (the object being killed is the
		// user object there).
		bool ShouldKeepFirstPerson(void*& outPerson, void*& outBridge) noexcept
		{
			outPerson = nullptr;
			outBridge = nullptr;
			if (static_cast<Mode>(g_mode.load(std::memory_order_acquire)) != Mode::First ||
				!RuntimeGate::IsSupported() || IsNetGameSeh())
			{
				return false;
			}
			void* user = nullptr;
			void* bridge = nullptr;
			if (!ReadUserSeh(user, bridge) || user == nullptr || bridge == nullptr)
			{
				return false;
			}
			// A Person with a dedicated first-person entity (the resolver
			// checks the RTTI, the bridge and the stock clip vocabulary).
			void* firstPerson = nullptr;
			try
			{
				if (!FirstPersonTarget::ResolveNativeLocalFirstPersonEntity(firstPerson, nullptr) ||
					firstPerson == nullptr)
				{
					return false;
				}
			}
			catch (...)
			{
				return false;
			}
			CameraState camera{};
			if (!ReadCameraSeh(camera) || camera.mode != kCameraAttached || camera.attached != bridge)
			{
				return false;
			}
			outPerson = user;
			outBridge = bridge;
			return true;
		}

		int __cdecl PushCameraStub()
		{
			g_skipSetView = false;
			void* person = nullptr;
			void* bridge = nullptr;
			if (ShouldKeepFirstPerson(person, bridge))
			{
				g_skipSetView = true;
				g_armedPerson = person;
				g_armedBridge = bridge;
				g_seenDeath = false;
				g_armed.store(true, std::memory_order_release);
				g_probeTick = 0;
				g_kept.fetch_add(1, std::memory_order_relaxed);
				LogNoThrow("exu: death camera: keeping the first-person view for death1 (person=%p bridge=%p)",
					person, bridge);
				return 1;
			}
			if (static_cast<Mode>(g_mode.load(std::memory_order_acquire)) == Mode::First)
			{
				g_declined.fetch_add(1, std::memory_order_relaxed);
			}
			return reinterpret_cast<CameraPushFn>(Addr::CameraPush)();
		}

		void __cdecl SetViewStub(void* renderBridge)
		{
			if (g_skipSetView)
			{
				g_skipSetView = false;
				return;
			}
			reinterpret_cast<CameraSetFreeEyeFn>(Addr::CameraSetFreeEye)(renderBridge);
		}

		struct ProbeSample
		{
			std::int32_t index = -1;
			void* world = nullptr;
			void* firstPerson = nullptr;
			void* povBone = nullptr;
			std::int32_t fpDone = 0;
		};

		bool ReadProbeSeh(const void* person, const void* bridge, ProbeSample& out) noexcept
		{
			__try
			{
				const auto* p = reinterpret_cast<const std::uint8_t*>(person);
				const auto* b = reinterpret_cast<const std::uint8_t*>(bridge);
				out.index = *reinterpret_cast<const std::int32_t*>(p + kPersonAnimIndexOffset);
				out.world = *reinterpret_cast<void* const*>(b + kBridgeWorldEntity);
				out.firstPerson = *reinterpret_cast<void* const*>(b + kBridgeFirstPersonEntity);
				out.povBone = *reinterpret_cast<void* const*>(b + kBridgePovBone);
				out.fpDone = *reinterpret_cast<const std::int32_t*>(b + kBridgeFirstPersonDone);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool ReadIndexSeh(const void* person, std::int32_t& outIndex) noexcept
		{
			__try
			{
				outIndex = *reinterpret_cast<const std::int32_t*>(
					reinterpret_cast<const std::uint8_t*>(person) + kPersonAnimIndexOffset);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}
	}

	bool IsAvailable() noexcept
	{
		return RuntimeGate::IsSupported();
	}

	bool SetMode(Mode mode) noexcept
	{
		g_mode.store(static_cast<std::uint8_t>(mode), std::memory_order_release);
		try
		{
			const BasicPatch::Status status =
				mode == Mode::First ? BasicPatch::Status::ACTIVE : BasicPatch::Status::INACTIVE;
			// Install the second stub first so the pair is never half on in
			// the direction that would skip only the save.
			if (mode == Mode::First)
			{
				g_setViewPatch.SetStatus(status);
				g_pushPatch.SetStatus(status);
				if (!g_pushPatch.IsActive() || !g_setViewPatch.IsActive())
				{
					g_pushPatch.SetStatus(BasicPatch::Status::INACTIVE);
					g_setViewPatch.SetStatus(BasicPatch::Status::INACTIVE);
					g_mode.store(static_cast<std::uint8_t>(Mode::Stock), std::memory_order_release);
					LogNoThrow("exu: death camera: call-site preimages did not qualify; staying stock");
					return false;
				}
				return true;
			}
			g_pushPatch.SetStatus(status);
			g_setViewPatch.SetStatus(status);
		}
		catch (...)
		{
			g_mode.store(static_cast<std::uint8_t>(Mode::Stock), std::memory_order_release);
			return false;
		}
		return IsAvailable();
	}

	Mode GetMode() noexcept
	{
		return static_cast<Mode>(g_mode.load(std::memory_order_acquire));
	}

	void SetProbe(bool enabled) noexcept
	{
		g_probe.store(enabled, std::memory_order_release);
	}

	void GetStats(Stats& outStats) noexcept
	{
		outStats = Stats{};
		outStats.available = IsAvailable();
		outStats.patched = g_pushPatch.IsActive() && g_setViewPatch.IsActive();
		outStats.armed = g_armed.load(std::memory_order_acquire);
		outStats.probe = g_probe.load(std::memory_order_acquire);
		outStats.kept = g_kept.load(std::memory_order_relaxed);
		outStats.forced = g_forced.load(std::memory_order_relaxed);
		outStats.declined = g_declined.load(std::memory_order_relaxed);
	}

	void ResetMissionState() noexcept
	{
		SetMode(Mode::Stock);
		g_probe.store(false, std::memory_order_release);
		g_kept.store(0, std::memory_order_relaxed);
		g_forced.store(0, std::memory_order_relaxed);
		g_declined.store(0, std::memory_order_relaxed);
		g_skipSetView = false;
		Disarm();
	}

	void AfterSimulate(const void* person, bool removalPending) noexcept
	{
		if (!g_armed.load(std::memory_order_acquire) || person != g_armedPerson)
		{
			return;
		}
		if (removalPending)
		{
			// The stock removal branch switched the camera (save + free eye)
			// before removing the Person; never read it again.
			LogNoThrow("exu: death camera: death1 finished; stock camera switch ran at removal");
			Disarm();
			return;
		}

		CameraState camera{};
		void* user = nullptr;
		void* userBridge = nullptr;
		if (!ReadCameraSeh(camera) || !ReadUserSeh(user, userBridge))
		{
			Disarm();
			return;
		}
		if (camera.mode != kCameraAttached || camera.attached != g_armedBridge)
		{
			// Something else moved the camera: nothing left to hand back.
			Disarm();
			return;
		}

		std::int32_t index = -1;
		const bool readIndex = ReadIndexSeh(person, index);
		if (readIndex && index == kDeathIndex)
		{
			g_seenDeath = true;
		}
		if (user != person || !readIndex || (g_seenDeath && index != kDeathIndex))
		{
			// Guard: the dying pilot is no longer the user object (or left
			// death1) while the camera still follows its bridge. The Person is
			// alive (this is its own Simulate call), so hand the camera to
			// free eye exactly as the stock code would.
			reinterpret_cast<CameraPushFn>(Addr::CameraPush)();
			reinterpret_cast<CameraSetFreeEyeFn>(Addr::CameraSetFreeEye)(g_armedBridge);
			g_forced.fetch_add(1, std::memory_order_relaxed);
			LogNoThrow("exu: death camera: guard switched to free eye (user=%p person=%p index=%d)",
				user, person, static_cast<int>(index));
			Disarm();
			return;
		}

		if (g_probe.load(std::memory_order_acquire) && (g_probeTick++ % 10u) == 0u)
		{
			ProbeSample sample{};
			if (ReadProbeSeh(person, g_armedBridge, sample))
			{
				LogNoThrow(
					"exu: death camera probe: index=%d fpDone=%d cameraMode=%d attached=%p world=%p fp=%p povBone=%p",
					static_cast<int>(sample.index), static_cast<int>(sample.fpDone),
					static_cast<int>(camera.mode), camera.attached, sample.world, sample.firstPerson,
					sample.povBone);
			}
		}
	}
}
