// Copyright (C) 2026 GrizzlyOne95. LGPL-3.0-or-later.

// Lightning, lava and glare bindings (exu.*Lightning*, exu.*Lava, exu.SetGlare).
//
// Engine facts, GOG 2.2.301 (Ghidra /battlezone98redux.exe-007c64), with the
// BZ 1.5 PDB-matched names from Battlezone_Source\BZ1\1.5\functions\004d:
//   LightningFeature singleton 0x9b7cd8, vftable 0x87b934 (ctor FUN_004fa310,
//   0x4fa322 "mov dword ptr [eax], 0x87b934").
//   PostLoad FUN_004fa380 reads TRN [LightningBolt] into the BoltSet globals at
//   0x9b7ca4, points activeList[i] at emitterList[i] for all 16 (0x4fa897..
//   0x4fa8b7), sets activeNum = 0 (0x4fa8c3) and calls AddBoltEmitter(0)
//   bolts times (0x4fa8fb). Cleanup FUN_004fa920 zeroes the BoltSet and the
//   next PostLoad reloads it from the TRN, so nothing here needs restoring.
//   Simulate FUN_004fa9a0, per active emitter: delay -= dt (0x4fa9e7); when
//   delay <= 0 and flash == 0 (0x4faa11) it strikes and plays BoltSet.sound
//   (0x4faa82); when flash >= flashes (0x4faa9d) it calls BoltInit
//   (0x4faab3), else flash++ and delay += duration (0x4faabd..0x4faadc).
//   lavaMat 0x8f05c8: written by the TRN loader FUN_00780160 at 0x780458
//   (NormalView "Lava", default -1) on every terrain load; craft lava damage
//   FUN_004aaea0 reads it per call and scales by the 40.0 at 0x8a295c
//   (0x4ab171 "mulss xmm0, dword ptr [0x8a295c]").
//   ColorFade 0x97838c, SetGlare FUN_0049b4c0 "glare(+0x28) += amount"; the
//   render FUN_0049b520 draws alpha min(glare, 1) * 224 and zeroes glare.

#include "Environment.h"
#include "EnvironmentHazardsCore.h"

#include "InlinePatch.h"
#include "LuaHelpers.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"

#include <Windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ExtraUtilities::Patch
{
	// The damage scale is the disp32 of "mulss xmm0, [0x8a295c]" at
	// LavaDamageScaleSite + 4 (F3 0F 59 05 <disp32>). The 40.0 constant itself
	// is shared by 71 instructions, so the operand is retargeted to EXU's own
	// float instead of overwriting it. INACTIVE by default; BasicPatch puts the
	// stock operand back when the Lua state closes.
	float lavaDamageScale = 40.0f;
	InlinePatch lavaDamageScalePatch(
		EngineAddresses::Environment::LavaDamageScaleSite + 4,
		reinterpret_cast<uintptr_t>(&lavaDamageScale),
		BasicPatch::Status::INACTIVE,
		{ 0x5C, 0x29, 0x8A, 0x00 });
}

namespace ExtraUtilities::Lua::Environment
{
	namespace
	{
		using namespace ExtraUtilities::EnvironmentHazards;

		// LightningFeature, 1.5 lightning.h names; offsets confirmed in Redux
		// AddBoltEmitter FUN_004fab60 (0x4fab6c "cmp [eax+0x68], 0x10",
		// 0x4fab7b "mov ecx, [eax+edx*4+0x28]", 0x4fab97 "[edx+0x1038]") and
		// PostLoad (0x4fa897 "imul ecx, ..., 0x103c", 0x4fa8a7 "+0x6c").
		constexpr std::size_t kActiveListOffset = 0x28;
		constexpr std::size_t kActiveNumOffset = 0x68;
		constexpr std::size_t kEmitterListOffset = 0x6C;
		constexpr std::size_t kEmitterStride = 0x103C;

		// BoltEmitter fields used here (Simulate 0x4faa11 flash, 0x4fa9e7 delay).
		struct BoltEmitterLayout
		{
			uint8_t _pad0[0x20];
			int32_t flash;
			float duration;
			float delay;
		};
		static_assert(offsetof(BoltEmitterLayout, flash) == 0x20);
		static_assert(offsetof(BoltEmitterLayout, delay) == 0x28);

		// BoltSet globals from 0x9b7ca4, offsets from Cleanup FUN_004fa920
		// (0x4fa927 bolts, 0x4fa931 name object, 0x4fa93b color, 0x4fa94d
		// minDelay, 0x4fa95d maxDelay, 0x4fa96d duration, 0x4fa97d distance,
		// 0x4fa987..0x4fa996 sound). Times are float seconds (1.5 PostLoad
		// 004d0f98 multiplies the TRN ms by 0.001).
		struct BoltSetLayout
		{
			int32_t bolts;
			uint8_t name[8];
			uint32_t color;
			float minDelay;
			float maxDelay;
			float duration;
			float distance;
			char sound[kSoundCapacity];
		};
		static_assert(offsetof(BoltSetLayout, color) == 0x0C);
		static_assert(offsetof(BoltSetLayout, minDelay) == 0x10);
		static_assert(offsetof(BoltSetLayout, maxDelay) == 0x14);
		static_assert(offsetof(BoltSetLayout, duration) == 0x18);
		static_assert(offsetof(BoltSetLayout, distance) == 0x1C);
		static_assert(offsetof(BoltSetLayout, sound) == 0x20);
		static_assert(sizeof(BoltSetLayout) == 0x30);

		using AddBoltEmitterFn = void(__thiscall*)(void* feature, void* target);
		using BoltInitFn = void(__thiscall*)(void* feature, void* emitter);
		using SetGlareFn = void(__thiscall*)(void* colorFade, float amount);

		uint8_t* Feature() noexcept
		{
			return reinterpret_cast<uint8_t*>(EngineAddresses::Environment::LightningFeature);
		}

		BoltSetLayout* BoltSet() noexcept
		{
			return reinterpret_cast<BoltSetLayout*>(EngineAddresses::Environment::LightningBoltSet);
		}

		int32_t& ActiveNum() noexcept
		{
			return *reinterpret_cast<int32_t*>(Feature() + kActiveNumOffset);
		}

		BoltEmitterLayout* ActiveEmitter(int32_t index) noexcept
		{
			return *reinterpret_cast<BoltEmitterLayout**>(Feature() + kActiveListOffset + index * sizeof(void*));
		}

		// The constructed singleton, with an activeNum in range.
		bool FeatureLooksValidUnsafe() noexcept
		{
			if (*reinterpret_cast<uintptr_t*>(Feature()) != EngineAddresses::Environment::LightningFeatureVftable)
			{
				return false;
			}
			const int32_t active = ActiveNum();
			return active >= 0 && active <= kMaxBolts;
		}

		// activeList slots are pointed at emitterList by PostLoad; refuse a slot
		// that does not hold one of those.
		bool SlotLooksValidUnsafe(int32_t index) noexcept
		{
			const auto slot = reinterpret_cast<uintptr_t>(ActiveEmitter(index));
			const uintptr_t first = reinterpret_cast<uintptr_t>(Feature()) + kEmitterListOffset;
			if (slot < first)
			{
				return false;
			}
			const uintptr_t offset = slot - first;
			return offset % kEmitterStride == 0 && offset / kEmitterStride < static_cast<uintptr_t>(kMaxBolts);
		}

		bool ReadActiveBolts(int32_t& out) noexcept
		{
			__try
			{
				if (!FeatureLooksValidUnsafe())
				{
					return false;
				}
				out = ActiveNum();
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool ReadBoltSet(BoltSetLayout& out) noexcept
		{
			__try
			{
				if (!FeatureLooksValidUnsafe())
				{
					return false;
				}
				std::memcpy(&out, BoltSet(), sizeof(out));
				out.sound[kSoundCapacity - 1] = '\0';
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		enum class BoltField
		{
			MinDelay,
			MaxDelay,
			Duration,
			Distance,
		};

		bool WriteBoltFloat(BoltField field, float value) noexcept
		{
			__try
			{
				if (!FeatureLooksValidUnsafe())
				{
					return false;
				}
				BoltSetLayout* set = BoltSet();
				switch (field)
				{
				case BoltField::MinDelay: set->minDelay = value; break;
				case BoltField::MaxDelay: set->maxDelay = value; break;
				case BoltField::Duration: set->duration = value; break;
				case BoltField::Distance: set->distance = value; break;
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool WriteBoltColor(uint32_t argb) noexcept
		{
			__try
			{
				if (!FeatureLooksValidUnsafe())
				{
					return false;
				}
				BoltSet()->color = argb;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool WriteBoltSound(const char* text, std::size_t length) noexcept
		{
			__try
			{
				if (!FeatureLooksValidUnsafe())
				{
					return false;
				}
				char buffer[kSoundCapacity]{};
				std::memcpy(buffer, text, length);
				std::memcpy(BoltSet()->sound, buffer, sizeof(buffer));
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Grows the active list the way PostLoad does (AddBoltEmitter(0)), then
		// BoltInit gives each new bolt a fresh delay from min/max; shrinking just
		// lowers activeNum, as the list slots stay pointed at their emitters.
		// storedCount goes to BoltSet.bolts, the count StartLightning() resumes.
		bool SetActiveBolts(int32_t wanted, int32_t storedCount) noexcept
		{
			__try
			{
				if (!FeatureLooksValidUnsafe())
				{
					return false;
				}
				const auto addBolt = reinterpret_cast<AddBoltEmitterFn>(EngineAddresses::Environment::LightningAddBoltEmitter);
				const auto boltInit = reinterpret_cast<BoltInitFn>(EngineAddresses::Environment::LightningBoltInit);
				while (ActiveNum() < wanted)
				{
					const int32_t slot = ActiveNum();
					if (!SlotLooksValidUnsafe(slot))
					{
						return false;
					}
					addBolt(Feature(), nullptr);
					if (ActiveNum() != slot + 1)
					{
						return false;
					}
					boltInit(Feature(), ActiveEmitter(slot));
				}
				if (ActiveNum() > wanted)
				{
					ActiveNum() = wanted;
				}
				BoltSet()->bolts = storedCount;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Zero delay on idle bolts: the next Simulate strikes them.
		bool FireIdleBolts(int32_t& fired) noexcept
		{
			fired = 0;
			__try
			{
				if (!FeatureLooksValidUnsafe())
				{
					return false;
				}
				const int32_t active = ActiveNum();
				for (int32_t i = 0; i < active; ++i)
				{
					if (!SlotLooksValidUnsafe(i))
					{
						continue;
					}
					BoltEmitterLayout* emitter = ActiveEmitter(i);
					if (emitter->flash == 0)
					{
						emitter->delay = 0.0f;
						++fired;
					}
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		int32_t* LavaMaterial() noexcept
		{
			return reinterpret_cast<int32_t*>(EngineAddresses::Environment::LavaMaterial);
		}

		bool ReadLavaMaterial(int32_t& out) noexcept
		{
			__try
			{
				out = *LavaMaterial();
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool WriteLavaMaterial(int32_t value) noexcept
		{
			__try
			{
				*LavaMaterial() = value;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool CallSetGlare(float amount) noexcept
		{
			__try
			{
				reinterpret_cast<SetGlareFn>(EngineAddresses::Environment::ColorFadeSetGlare)(
					reinterpret_cast<void*>(EngineAddresses::Environment::ColorFade), amount);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// The lava material StopLava turned off, for StartLava() to bring back.
		// -1 means none. Per mission: reset in ResetHazardState.
		int32_t g_stoppedLavaMaterial = -1;

		int PushBoltSetFloat(lua_State* L, float BoltSetLayout::* field, bool milliseconds)
		{
			if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
			BoltSetLayout set{};
			if (!ReadBoltSet(set))
			{
				lua_pushnil(L);
				return 1;
			}
			const float value = set.*field;
			lua_pushnumber(L, milliseconds ? SecondsToMilliseconds(value) : static_cast<double>(value));
			return 1;
		}

		int SetBoltSetTime(lua_State* L, BoltField field)
		{
			const double ms = luaL_checknumber(L, 1);
			if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
			float seconds = 0.0f;
			lua_pushboolean(L, MillisecondsToSeconds(ms, seconds) && WriteBoltFloat(field, seconds));
			return 1;
		}
	}

	void ResetHazardState() noexcept
	{
		g_stoppedLavaMaterial = -1;
		Patch::lavaDamageScale = 40.0f;
	}

	int GetLightningBolts(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		int32_t active = 0;
		if (!ReadActiveBolts(active))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushinteger(L, active);
		return 1;
	}

	int GetLightningColor(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		BoltSetLayout set{};
		if (!ReadBoltSet(set))
		{
			lua_pushnil(L);
			return 1;
		}
		const Rgba c = UnpackArgb(set.color);
		lua_pushinteger(L, c.r);
		lua_pushinteger(L, c.g);
		lua_pushinteger(L, c.b);
		lua_pushinteger(L, c.a);
		return 4;
	}

	int SetLightningColor(lua_State* L)
	{
		const double r = luaL_checknumber(L, 1);
		const double g = luaL_checknumber(L, 2);
		const double b = luaL_checknumber(L, 3);
		const double a = lua_isnoneornil(L, 4) ? 255.0 : luaL_checknumber(L, 4);
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		uint32_t cr = 0, cg = 0, cb = 0, ca = 0;
		const bool ok = ChannelFromNumber(r, cr) && ChannelFromNumber(g, cg) && ChannelFromNumber(b, cb) &&
			ChannelFromNumber(a, ca) && WriteBoltColor(PackArgb(cr, cg, cb, ca));
		lua_pushboolean(L, ok);
		return 1;
	}

	int GetLightningDistance(lua_State* L)
	{
		return PushBoltSetFloat(L, &BoltSetLayout::distance, false);
	}

	int SetLightningDistance(lua_State* L)
	{
		const double distance = luaL_checknumber(L, 1);
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		lua_pushboolean(L, IsNonNegativeFinite(distance) && WriteBoltFloat(BoltField::Distance, static_cast<float>(distance)));
		return 1;
	}

	int GetLightningDuration(lua_State* L)
	{
		return PushBoltSetFloat(L, &BoltSetLayout::duration, true);
	}

	int SetLightningDuration(lua_State* L)
	{
		return SetBoltSetTime(L, BoltField::Duration);
	}

	int GetLightningMaxDelay(lua_State* L)
	{
		return PushBoltSetFloat(L, &BoltSetLayout::maxDelay, true);
	}

	int SetLightningMaxDelay(lua_State* L)
	{
		return SetBoltSetTime(L, BoltField::MaxDelay);
	}

	int GetLightningMinDelay(lua_State* L)
	{
		return PushBoltSetFloat(L, &BoltSetLayout::minDelay, true);
	}

	int SetLightningMinDelay(lua_State* L)
	{
		return SetBoltSetTime(L, BoltField::MinDelay);
	}

	int GetLightningSound(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		BoltSetLayout set{};
		if (!ReadBoltSet(set))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushstring(L, set.sound);
		return 1;
	}

	int SetLightningSound(lua_State* L)
	{
		std::size_t length = 0;
		const char* text = luaL_checklstring(L, 1, &length);
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		lua_pushboolean(L, IsValidSoundName(text, length) && WriteBoltSound(text, length));
		return 1;
	}

	int StartLightning(lua_State* L)
	{
		const bool hasArgument = !lua_isnoneornil(L, 1);
		const double requested = hasArgument ? luaL_checknumber(L, 1) : 0.0;
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		if (hasArgument && (!std::isfinite(requested) || std::floor(requested) != requested))
		{
			lua_pushboolean(L, false);
			return 1;
		}
		BoltSetLayout set{};
		if (!ReadBoltSet(set))
		{
			lua_pushboolean(L, false);
			return 1;
		}
		const long long asInteger = hasArgument && std::fabs(requested) < 1.0e9 ? static_cast<long long>(requested) : 0;
		const int wanted = ResolveBoltCount(hasArgument, asInteger, set.bolts);
		lua_pushboolean(L, wanted > 0 && SetActiveBolts(wanted, wanted));
		return 1;
	}

	int StopLightning(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		BoltSetLayout set{};
		if (!ReadBoltSet(set))
		{
			lua_pushboolean(L, false);
			return 1;
		}
		// Keep BoltSet.bolts so StartLightning() resumes the same count.
		lua_pushboolean(L, SetActiveBolts(0, set.bolts));
		return 1;
	}

	int FireLightning(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		int32_t fired = 0;
		if (!FireIdleBolts(fired))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushinteger(L, fired);
		return 1;
	}

	int StartLava(lua_State* L)
	{
		const bool hasDamage = !lua_isnoneornil(L, 1);
		const double damage = hasDamage ? luaL_checknumber(L, 1) : 0.0;
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		if (hasDamage && !IsNonNegativeFinite(damage))
		{
			lua_pushboolean(L, false);
			return 1;
		}
		int32_t current = -1;
		if (!ReadLavaMaterial(current))
		{
			lua_pushboolean(L, false);
			return 1;
		}
		const bool restore = !IsValidTerrainMaterial(current);
		if (restore && !IsValidTerrainMaterial(g_stoppedLavaMaterial))
		{
			lua_pushboolean(L, false);
			return 1;
		}
		if (hasDamage)
		{
			Patch::lavaDamageScale = static_cast<float>(damage);
			Patch::lavaDamageScalePatch.SetStatus(BasicPatch::Status::ACTIVE);
			// Preimage mismatch or patching disabled: the scale did not apply.
			if (!Patch::lavaDamageScalePatch.IsActive())
			{
				Patch::lavaDamageScale = 40.0f;
				lua_pushboolean(L, false);
				return 1;
			}
		}
		if (restore)
		{
			if (!WriteLavaMaterial(g_stoppedLavaMaterial))
			{
				lua_pushboolean(L, false);
				return 1;
			}
			g_stoppedLavaMaterial = -1;
		}
		lua_pushboolean(L, true);
		return 1;
	}

	int StopLava(lua_State* L)
	{
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		int32_t current = -1;
		if (!ReadLavaMaterial(current))
		{
			lua_pushboolean(L, false);
			return 1;
		}
		if (IsValidTerrainMaterial(current))
		{
			if (!WriteLavaMaterial(-1))
			{
				lua_pushboolean(L, false);
				return 1;
			}
			g_stoppedLavaMaterial = current;
		}
		lua_pushboolean(L, true);
		return 1;
	}

	int SetLava(lua_State* L)
	{
		const double material = luaL_checknumber(L, 1);
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		const bool integral = std::isfinite(material) && std::floor(material) == material && std::fabs(material) < 1.0e9;
		const bool ok = integral && IsValidTerrainMaterial(static_cast<long long>(material)) &&
			WriteLavaMaterial(static_cast<int32_t>(material));
		if (ok)
		{
			g_stoppedLavaMaterial = -1;
		}
		lua_pushboolean(L, ok);
		return 1;
	}

	int SetGlare(lua_State* L)
	{
		const double amount = luaL_checknumber(L, 1);
		if (!RuntimeGate::IsSupported()) return PushUnsupportedBuild(L);
		// The render clamps glare to 1 per frame; the cap keeps repeated calls in
		// frames that skip the render from summing to infinity.
		lua_pushboolean(L, IsNonNegativeFinite(amount) && amount <= kMaxGlare && CallSetGlare(static_cast<float>(amount)));
		return 1;
	}
}
