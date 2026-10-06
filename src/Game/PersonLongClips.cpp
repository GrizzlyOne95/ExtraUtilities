/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "Game/PersonLongClips.h"

#include "Game/GameObject.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>

namespace ExtraUtilities::Lua::PersonLongClips
{
	namespace
	{
		// Same layout PersonAnimBlend reads (PERSON_ANIM_CROSSFADE_RE_20261005).
		constexpr std::size_t kPersonRenderBridgeOffset = 0x0F0;
		constexpr std::size_t kPersonAnimIndexOffset = 0x2A8;
		constexpr std::size_t kBridgeWorldEntity = 0x094;
		constexpr std::size_t kBridgeWorldName = 0x0B4;

		constexpr const char* kEntryNames[] = {
			"idle", "runForward", "runBackward", "runLeft", "runRight",
		};
		constexpr std::int32_t kEntryIndices[] = { kIdleIndex, 4, 5, 6, 7 };

		std::atomic<bool> g_runs{ false };
		std::atomic<bool> g_idle{ false };
		std::atomic<bool> g_qualified{ false };
		std::atomic<bool> g_faulted{ false };
		std::atomic<std::uint32_t> g_raisedCalls{ 0 };
		std::atomic<std::uint32_t> g_idleLoops{ 0 };

		// Hook thread only.
		LongIdleCache<64> g_longIdle;

		template <typename... Args>
		void LogNoThrow(const char* format, Args... args) noexcept
		{
			try
			{
				Logging::LogMessage(format, args...);
			}
			catch (...)
			{
				OutputDebugStringA("ExtraUtilities: long clips log line dropped\n");
			}
		}

		template <typename T>
		T ReadAt(const void* base, std::size_t offset) noexcept
		{
			return *reinterpret_cast<const T*>(reinterpret_cast<const std::uint8_t*>(base) + offset);
		}

		std::uint32_t FloatBits(float value) noexcept
		{
			std::uint32_t bits = 0;
			std::memcpy(&bits, &value, sizeof(bits));
			return bits;
		}

		float StockEnd() noexcept
		{
			float value = 0.0f;
			std::memcpy(&value, &kStockEndBits, sizeof(value));
			return value;
		}

		bool ReadIndexSeh(const void* person, std::int32_t& outIndex) noexcept
		{
			__try
			{
				outIndex = ReadAt<std::int32_t>(person, kPersonAnimIndexOffset);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool ReadWorldSeh(const void* person, std::int32_t& outIndex, void*& outEntity, const char*& outName) noexcept
		{
			__try
			{
				outIndex = ReadAt<std::int32_t>(person, kPersonAnimIndexOffset);
				const void* const bridge = ReadAt<const void*>(person, kPersonRenderBridgeOffset);
				if (bridge == nullptr)
				{
					return false;
				}
				outEntity = ReadAt<void*>(bridge, kBridgeWorldEntity);
				outName = ReadAt<const char*>(bridge, kBridgeWorldName);
				return outEntity != nullptr && outName != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool ReadEndSeh(std::int32_t index, std::uint32_t& outBits) noexcept
		{
			__try
			{
				outBits = FloatBits(BZR::PersonRuntime::animEndTime[index]);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool WriteEndSeh(std::int32_t index, float value) noexcept
		{
			__try
			{
				BZR::PersonRuntime::animEndTime[index] = value;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool NameIsSeh(std::int32_t index, const char* expected, bool& outSame) noexcept
		{
			__try
			{
				const char* const name = BZR::PersonRuntime::animName[index];
				outSame = name != nullptr && std::strcmp(name, expected) == 0;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		void Fault(const char* what) noexcept
		{
			if (!g_faulted.exchange(true, std::memory_order_acq_rel))
			{
				LogNoThrow("exu: person long clips disabled for this Lua state; %s faulted", what);
			}
		}

		// Ogre lookups allocate (std::string); C++ catch, the GameObject
		// helpers guard faults themselves.
		bool LoopLongIdle(void* entity, const char* name, bool& outIsLong) noexcept
		{
			outIsLong = false;
			try
			{
				const std::string clip(name);
				GameObject::EntityAnimationInfo info{};
				if (!GameObject::GetAnimationInfo(entity, clip, info))
				{
					return false;
				}
				outIsLong = IsLongClip(info.length, StockEnd());
				return !outIsLong || GameObject::SetAnimationLoop(entity, clip, true);
			}
			catch (...)
			{
				return false;
			}
		}
	}

	void SetSettings(const Settings& settings) noexcept
	{
		g_runs.store(settings.runs, std::memory_order_release);
		g_idle.store(settings.idle, std::memory_order_release);
	}

	Settings GetSettings() noexcept
	{
		Settings settings{};
		settings.runs = g_runs.load(std::memory_order_acquire);
		settings.idle = g_idle.load(std::memory_order_acquire);
		return settings;
	}

	bool IsAvailable() noexcept
	{
		return g_qualified.load(std::memory_order_acquire) && !g_faulted.load(std::memory_order_acquire);
	}

	void GetStats(Stats& outStats) noexcept
	{
		outStats.available = IsAvailable();
		outStats.faulted = g_faulted.load(std::memory_order_acquire);
		outStats.raisedCalls = g_raisedCalls.load(std::memory_order_relaxed);
		outStats.idleLoops = g_idleLoops.load(std::memory_order_relaxed);
	}

	void ResetMissionState() noexcept
	{
		g_runs.store(false, std::memory_order_release);
		g_idle.store(false, std::memory_order_release);
		g_faulted.store(false, std::memory_order_release);
		g_raisedCalls.store(0, std::memory_order_relaxed);
		g_idleLoops.store(0, std::memory_order_relaxed);
		g_longIdle.Clear();
	}

	bool Qualify() noexcept
	{
		if (g_qualified.load(std::memory_order_acquire))
		{
			return true;
		}
		if (!RuntimeGate::IsSupported())
		{
			return false;
		}
		for (std::size_t i = 0; i < sizeof(kEntryIndices) / sizeof(kEntryIndices[0]); ++i)
		{
			std::uint32_t bits = 0;
			bool same = false;
			if (!ReadEndSeh(kEntryIndices[i], bits) || bits != kStockEndBits ||
				!NameIsSeh(kEntryIndices[i], kEntryNames[i], same) || !same)
			{
				LogNoThrow("exu: person long clips unavailable; clip table entry %d (%s) did not qualify",
					kEntryIndices[i], kEntryNames[i]);
				return false;
			}
		}
		g_qualified.store(true, std::memory_order_release);
		return true;
	}

	void BeforeCall(const void* person, PreCall& out) noexcept
	{
		out = PreCall{};
		if (!IsAvailable())
		{
			return;
		}
		Settings settings{};
		settings.runs = g_runs.load(std::memory_order_acquire);
		settings.idle = g_idle.load(std::memory_order_acquire);
		if (!settings.runs && !settings.idle)
		{
			return;
		}

		std::int32_t index = -1;
		bool idleIsLong = false;
		if (settings.idle)
		{
			void* entity = nullptr;
			const char* name = nullptr;
			if (!ReadWorldSeh(person, index, entity, name))
			{
				// No WORLD entity: runs can still be raised.
				if (!ReadIndexSeh(person, index))
				{
					return;
				}
			}
			else if (index == kIdleIndex)
			{
				g_longIdle.Find(entity, idleIsLong);
			}
		}
		else if (!ReadIndexSeh(person, index))
		{
			return;
		}
		out.index = index;

		if (index != kIdleIndex && !IsRunIndex(index))
		{
			return;
		}
		std::uint32_t bits = 0;
		if (!ReadEndSeh(index, bits) || !ShouldRaise(settings, index, bits, idleIsLong))
		{
			return;
		}
		if (!WriteEndSeh(index, kRaisedEnd))
		{
			Fault("raise");
			return;
		}
		out.raised = true;
		g_raisedCalls.fetch_add(1, std::memory_order_relaxed);
	}

	void AfterCall(const void* person, const PreCall& pre, bool removalPending) noexcept
	{
		if (pre.raised)
		{
			// Put the stock value back unless something else rewrote it.
			std::uint32_t bits = 0;
			if (ReadEndSeh(pre.index, bits) && bits == FloatBits(kRaisedEnd))
			{
				float stock = 0.0f;
				std::memcpy(&stock, &kStockEndBits, sizeof(stock));
				if (!WriteEndSeh(pre.index, stock))
				{
					Fault("restore");
				}
			}
		}

		if (removalPending || pre.index < 0 || !IsAvailable() || !g_idle.load(std::memory_order_acquire))
		{
			return;
		}
		std::int32_t index = -1;
		void* entity = nullptr;
		const char* name = nullptr;
		if (!ReadWorldSeh(person, index, entity, name) || !EnteredIdle(pre.index, index))
		{
			return;
		}
		bool isLong = false;
		if (!LoopLongIdle(entity, name, isLong))
		{
			g_longIdle.Put(entity, false);
			return;
		}
		g_longIdle.Put(entity, isLong);
		if (isLong)
		{
			g_idleLoops.fetch_add(1, std::memory_order_relaxed);
		}
	}
}
