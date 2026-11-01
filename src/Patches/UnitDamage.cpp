/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#include "UnitDamage.h"

#include "EntryDetour32.h"
#include "Game/GameObjectHandle.h"
#include "Game/UnitDamagePolicy.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

// Craft::DamageAlloc stores the effective damage to [ebp-0xFC], then
// "MOVSS xmm0,[0]; COMISS xmm0,[ebp-0xFC]; jb ..." decides whether anything is
// dealt. The detour sits on that MOVSS: the thunk scales the local in place and
// the trampoline replays MOVSS and resumes at COMISS. The shared DAMAGE record
// is never written, so one shield cannot reduce a blast's damage to the next
// victim.

namespace ExtraUtilities::Patch::UnitDamage
{
	namespace
	{
		// MOVSS xmm0,[imm32] (absolute operand, so the trampoline copy is
		// position-independent), then COMISS xmm0,[ebp-0xFC]; jb +7. Only the
		// MOVSS is stolen; the rest is part of the identity check.
		constexpr std::array<std::uint8_t, 24> kSite = {
			0xF3, 0x0F, 0x10, 0x05, 0x98, 0x24, 0x8A, 0x00,
			0x0F, 0x2F, 0x85, 0x04, 0xFF, 0xFF, 0xFF, 0x72, 0x07, 0x32, 0xC0, 0xE9, 0xD1, 0x07, 0x00, 0x00 };
		constexpr std::size_t kStolen = 8;

		// Craft::DamageAlloc frame, proven on the qualified build: [ebp-0xF8] is
		// the distributedObject subobject (GameObject+0x18), not the object.
		constexpr std::ptrdiff_t kReceiverLocal = -0xF8;
		constexpr std::ptrdiff_t kDamageLocal = -0xFC;
		constexpr std::ptrdiff_t kDistributedObjectOffset = 0x18;

		ExtraUtilities::UnitDamage::Policy g_policy;
		EntryDetour32* g_detour = nullptr;
		void* g_resume = nullptr; // read by the thunk
		bool g_siteChecked = false;
		bool g_siteQualified = false;

		bool SiteMatchesSeh() noexcept
		{
			__try
			{
				const auto* code = reinterpret_cast<const std::uint8_t*>(EngineAddresses::Craft::EffectiveDamageResistanceSite);
				for (std::size_t i = 0; i < kSite.size(); ++i)
				{
					if (code[i] != kSite[i])
						return false;
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		void __cdecl ApplyDamage(void* frame) noexcept
		{
			if (g_policy.Empty())
				return;
			auto* bytes = static_cast<std::uint8_t*>(frame);
			auto* object = *reinterpret_cast<std::uint8_t**>(bytes + kReceiverLocal);
			if (object == nullptr)
				return;
			object -= kDistributedObjectOffset;
			BZR::handle handle = 0;
			if (!Lua::GameObject::Detail::TryGetHandleFromObject(reinterpret_cast<BZR::GameObject*>(object), handle))
				return;
			auto* damage = reinterpret_cast<float*>(bytes + kDamageLocal);
			*damage = g_policy.Apply(reinterpret_cast<std::uintptr_t>(object), handle, *damage);
		}

		// Runs mid-function with the engine's registers live, so it saves
		// flags, integer registers, x87 and XMM state (fxsave, 16-aligned)
		// around the C++ call.
		void __declspec(naked) DamageThunk()
		{
			__asm
			{
				pushfd
				pushad
				mov eax, esp
				sub esp, 528
				and esp, -16
				mov [esp+512], eax
				fxsave [esp]
				push ebp
				call ApplyDamage
				add esp, 4
				fxrstor [esp]
				mov esp, [esp+512]
				popad
				popfd
				jmp dword ptr [g_resume]
			}
		}

		void RemoveDetour() noexcept
		{
			// Restores the engine bytes before the trampoline is freed.
			delete g_detour;
			g_detour = nullptr;
			g_resume = nullptr;
		}

		bool EnsureDetour() noexcept
		{
			try
			{
				if (g_detour != nullptr && g_detour->IsActive())
					return true;
				RemoveDetour();
				if (!g_siteChecked)
				{
					g_siteChecked = true;
					g_siteQualified = RuntimeGate::IsSupported() && SiteMatchesSeh();
					if (!g_siteQualified)
						Logging::LogMessage("exu: native damage resistance unavailable; Craft damage site did not qualify");
				}
				if (!g_siteQualified)
					return false;

				g_detour = new (std::nothrow) EntryDetour32(
					EngineAddresses::Craft::EffectiveDamageResistanceSite, reinterpret_cast<const void*>(&DamageThunk), kStolen,
					BasicPatch::Status::INACTIVE,
					std::vector<std::uint8_t>(kSite.begin(), kSite.begin() + kStolen));
				if (g_detour == nullptr || !g_detour->PrepareTrampoline())
				{
					Logging::LogMessage("exu: native damage resistance unavailable; detour could not be prepared");
					RemoveDetour();
					return false;
				}
				g_resume = g_detour->GetTrampoline();
				g_detour->SetStatus(true);
				if (g_resume == nullptr || !g_detour->IsActive())
				{
					Logging::LogMessage("exu: native damage resistance detour could not activate");
					RemoveDetour();
					return false;
				}
				Logging::LogMessage("exu: native damage resistance detour active");
				return true;
			}
			catch (...)
			{
				RemoveDetour();
				return false;
			}
		}
	}

	bool Available() noexcept
	{
		return EnsureDetour();
	}

	bool Set(BZR::GameObject* object, uint32_t handle, float multiplier) noexcept
	{
		if (!EnsureDetour())
			return false;
		try
		{
			return g_policy.Set(reinterpret_cast<std::uintptr_t>(object), handle, multiplier);
		}
		catch (...)
		{
			return false;
		}
	}

	void Clear(uint32_t handle) noexcept
	{
		try { g_policy.Clear(handle); }
		catch (...) {}
	}

	void Reset() noexcept
	{
		g_policy.Reset();
	}

	void Shutdown() noexcept
	{
		RemoveDetour();
		g_policy.Reset();
		g_siteChecked = false;
		g_siteQualified = false;
	}
}
