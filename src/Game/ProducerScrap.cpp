// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

// Producer build control (exu.CancelBuild/FinishBuild/Get|SetBuildTimeLeft/
// GetBuildingOdf), Scavenger scrap hold (exu.Get|Set{Cur,Max}ScrapHold) and the
// rest of the current AI command (exu.GetCurrentParam/When/Where).
//
// Engine facts, all confirmed in the GOG 2.2.301 executable (Ghidra project
// /battlezone98redux.exe-007c64) unless marked 1.5:
//
// Producer (RTTI .?AVProducer@@; Recycler, Factory, Armory, ConstructionRig derive from it)
//   +0x300 buildClass (GameObjectClass*), +0x304 build time left (float seconds)
//     StartBuild 0x5AECB0: "this[0xc0] = cls; this[0xc1] = *(cls + 0x54)" (buildTime)
//     UpdateBuild 0x5AEE30: "+0x304 -= dt; if (+0x304 < 0) (*(vtable + 0xA8))(this)"
//     CancelBuild 0x5AED80: "if (this[0xc0] == 0) return false;" then refunds via team +0x174, clears +0x300
//     FinishBuild 0x5AF160 (vtable +0xA8 of Producer/Recycler/Factory/ConstructionRig,
//       Armory overrides with 0x4732C0): creates the object, clears this[0xc0]
//     IsBusy 0x4723D0: deployedBuilder byte +0x308 && !IsDeployed() -> false; else buildClass != 0
//     Simulate 0x5AC580 (slot +0x3C of Producer/Factory/Armory/ConstructionRig; Recycler's
//       0x5B2E50 tail-calls it): "switch (+0x228) ... case 2: if (IsBusy()) UpdateBuild();"
//       ConstructionRig ctor 0x49C940 sets +0x308 = 0, and BuildGoto ctor 0x49DFA0 calls StartBuild.
//   Primary vtable slot +0xA8 read from the PE: Producer 0x8860C4, Recycler 0x886454,
//   Factory 0x879388, ConstructionRig 0x877994 -> 0x5AF160; Armory 0x875CB0 -> 0x4732C0.
//
// Scavenger (RTTI .?AVScavenger@@, vtable 0x886FE8)
//   +0x300 maxScrap (int), +0x304 scrapHeld (int)
//     ctor 0x5C1D90: "this[0xc0] = *(cls + 0x5F0); this[0xc1] = 0"
//     GetAmmoGauge 0x5C1D60: "(float)*(int*)(+0x304) / (float)*(int*)(+0x300)"
//     Save 0x5C2C30 writes only +0x304 ("scrapHeld"); Load 0x5C2BA0 reads only +0x304,
//     so maxScrap returns to the ODF value on load.
//   1.5: Scavenger::LoadScrap clamps to [0, maxScrap] (0x45a051).
//
// GameObject current command AiCmdInfo at +0xA8 (size 0x20; nextCmd at +0xC8)
//   SetCommand(AiCmdInfo*) 0x4DBAF0: "if (*(param_1 + 0xd4)) FUN_00460600(...); copy 8 dwords to param_1 + 200"
//   GetCurrentCommand 0x504130 -> 0x5CB5C0 reads +0xAC; GetCurrentWho 0x504170 -> 0x5CB5F0 reads +0xB0
//   Lua Build 0x504A70 -> 0x5CC660: what = 0x15, who = 0, where = 0, param = GameObjectClass*
//     sign-extended, when left unset (stack garbage)
//   Lua BuildAt -> 0x5CC6E0: what = 0x15, where = new anonymous AiPath (0x460490), param = class
//   Lua SetCommand 0x504230: a string param goes through GameObjectClass::Find (FUN_004ff060)
//   AiPath { const char* label @0; int count @4; float* points @8 ({x, z} pairs) }

#include "Game/ProducerScrap.h"
#include "Game/ProducerScrapCore.h"
#include "LuaHelpers.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/FeatureQualification.h"
#include "Util/MsvcRtti.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ExtraUtilities::Lua::ProducerScrap
{
	namespace
	{
		namespace Core = ExtraUtilities::ProducerScrapCore;

		constexpr std::uintptr_t kBuildClassOffset = 0x300;     // Producer, see header comment
		constexpr std::uintptr_t kBuildTimeOffset = 0x304;      // Producer
		constexpr std::uintptr_t kMaxScrapOffset = 0x300;       // Scavenger
		constexpr std::uintptr_t kCurScrapOffset = 0x304;       // Scavenger
		constexpr std::uintptr_t kFinishBuildSlot = 0xA8;       // primary vtable
		constexpr std::uintptr_t kDeployStateOffset = 0x228;    // switch in Producer simulate 0x5AC580
		constexpr std::int32_t kDeployStateDeployed = 2;        // the only case that calls UpdateBuild
		constexpr std::uintptr_t kClassOdfOffset = 0x30;        // GameObjectClass, 8 chars
		constexpr std::uintptr_t kCurrentCommandOffset = 0xA8;  // GameObject AiCmdInfo
		constexpr std::int32_t kCmdBuild = 0x15;
		constexpr std::size_t kMaxLabel = 64;

		struct AiCmdInfoLayout
		{
			std::int32_t priority;  // 0x00
			std::int32_t what;      // 0x04 (GetCurrentCommand reads obj +0xAC)
			std::uint32_t who;      // 0x08 (GetCurrentWho reads obj +0xB0)
			const void* where;      // 0x0C AiPath*
			float when;             // 0x10
			std::uint32_t pad;      // 0x14
			std::int64_t param;     // 0x18 GameObjectClass* sign-extended for Build/BuildAt
		};
		static_assert(offsetof(AiCmdInfoLayout, what) == 0x04);
		static_assert(offsetof(AiCmdInfoLayout, who) == 0x08);
		static_assert(offsetof(AiCmdInfoLayout, when) == 0x10);
		static_assert(offsetof(AiCmdInfoLayout, param) == 0x18);
		static_assert(sizeof(AiCmdInfoLayout) == 0x20);

		struct AiPathView
		{
			const char* label;      // 0x00 0 for an anonymous path
			std::int32_t count;     // 0x04
			const float* points;    // 0x08 {x, z} pairs
		};
		static_assert(offsetof(AiPathView, count) == 0x04);
		static_assert(offsetof(AiPathView, points) == 0x08);

		using CancelBuildFn = bool(__thiscall*)(void*);
		using FinishBuildFn = void*(__thiscall*)(void*);
		using IsBusyFn = bool(__thiscall*)(void*);
		using TerrainHeightAtFn = double(__cdecl*)(double x, double z);

		unsigned char* Bytes(void* p) noexcept { return static_cast<unsigned char*>(p); }
		template <typename T> T& Field(void* obj, std::uintptr_t offset) noexcept
		{
			return *reinterpret_cast<T*>(Bytes(obj) + offset);
		}

		// The build-event anchors cover the three producer entry points called here;
		// the result cannot change within a process.
		bool ProducerCallsQualified() noexcept
		{
			static int qualified = -1;
			if (qualified < 0) qualified = QualifyFeature("Build events ", 4) && QualifyFeature("Producer scrap ", 1) ? 1 : 0;
			return qualified == 1;
		}

		// Remote replicas in a net game must not create or refund: the owner does that.
		bool IsLocalAuthorityUnsafe(void* obj) noexcept
		{
			return !*BZR::Multiplayer::isNetGame ||
				Bytes(obj)[BZR::Ownership::distributedObjectOffset + BZR::Ownership::stateOffset] == BZR::Ownership::stateLocal;
		}

		bool IsProducerUnsafe(void* obj) { return MsvcRtti::HasBaseUnsafe(obj, ".?AVProducer@@"); }
		bool IsScavengerUnsafe(void* obj) { return MsvcRtti::HasBaseUnsafe(obj, ".?AVScavenger@@"); }

		bool ReadClassOdfUnsafe(const void* cls, char (&out)[9])
		{
			if (!cls || !MsvcRtti::HasBaseUnsafe(cls, ".?AVGameObjectClass@@")) return false;
			return Core::DecodeOdf8(static_cast<const unsigned char*>(cls) + kClassOdfOffset, out);
		}

		bool ReadBuildSeh(void* obj, void*& cls, float& timeLeft) noexcept
		{
			__try
			{
				if (!IsProducerUnsafe(obj)) return false;
				cls = Field<void*>(obj, kBuildClassOffset);
				timeLeft = Field<float>(obj, kBuildTimeOffset);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool WriteBuildTimeSeh(void* obj, float seconds) noexcept
		{
			__try
			{
				if (!IsProducerUnsafe(obj) || !Field<void*>(obj, kBuildClassOffset)) return false;
				Field<float>(obj, kBuildTimeOffset) = seconds;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool ReadBuildingOdfSeh(void* obj, char (&out)[9]) noexcept
		{
			__try
			{
				if (!IsProducerUnsafe(obj)) return false;
				return ReadClassOdfUnsafe(Field<void*>(obj, kBuildClassOffset), out);
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool CancelBuildSeh(void* obj) noexcept
		{
			__try
			{
				if (!IsProducerUnsafe(obj) || !IsLocalAuthorityUnsafe(obj) || !Field<void*>(obj, kBuildClassOffset)) return false;
				return reinterpret_cast<CancelBuildFn>(EngineAddresses::ProducerEvents::CancelBuild)(obj);
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		// ok is false when nothing was finished; created is null for an Armory launch.
		bool FinishBuildSeh(void* obj, void*& created) noexcept
		{
			created = nullptr;
			__try
			{
				if (!IsProducerUnsafe(obj) || !IsLocalAuthorityUnsafe(obj)) return false;
				// Same gate as the engine's own completion: simulate 0x5AC580 calls UpdateBuild
				// (and so FinishBuild) only in deploy state 2 and when IsBusy. A rig is busy from
				// its BuildGoto ctor 0x49DFA0 while still driving, so state 2 is what keeps it from
				// building at a stale +0x378 construction matrix (0x49D0B0).
				if (Field<std::int32_t>(obj, kDeployStateOffset) != kDeployStateDeployed) return false;
				if (!reinterpret_cast<IsBusyFn>(EngineAddresses::ProducerEvents::IsBusy)(obj)) return false;
				const auto slot = reinterpret_cast<std::uintptr_t>((*reinterpret_cast<void***>(obj))[kFinishBuildSlot / sizeof(void*)]);
				if (slot != EngineAddresses::ProducerEvents::FinishBuild && slot != EngineAddresses::ProducerEvents::ArmoryFinishBuild) return false;
				created = reinterpret_cast<FinishBuildFn>(slot)(obj);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool ReadScrapSeh(void* obj, std::int32_t& cur, std::int32_t& max) noexcept
		{
			__try
			{
				if (!IsScavengerUnsafe(obj)) return false;
				cur = Field<std::int32_t>(obj, kCurScrapOffset);
				max = Field<std::int32_t>(obj, kMaxScrapOffset);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool WriteCurScrapSeh(void* obj, double amount) noexcept
		{
			__try
			{
				if (!IsScavengerUnsafe(obj)) return false;
				std::int32_t value = 0;
				if (!Core::TryClampScrap(amount, Field<std::int32_t>(obj, kMaxScrapOffset), value)) return false;
				Field<std::int32_t>(obj, kCurScrapOffset) = value;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool WriteMaxScrapSeh(void* obj, std::int32_t capacity) noexcept
		{
			__try
			{
				if (!IsScavengerUnsafe(obj)) return false;
				Field<std::int32_t>(obj, kMaxScrapOffset) = capacity;
				std::int32_t& cur = Field<std::int32_t>(obj, kCurScrapOffset);
				if (cur > capacity) cur = capacity;
				if (cur < 0) cur = 0;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool ReadCommandSeh(void* obj, AiCmdInfoLayout& out) noexcept
		{
			__try
			{
				out = Field<AiCmdInfoLayout>(obj, kCurrentCommandOffset);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool ReadParamOdfSeh(std::int64_t param, char (&out)[9]) noexcept
		{
			__try
			{
				// A class pointer is a 32-bit address sign-extended by the setter.
				if (param == 0 || param < INT32_MIN || param > INT32_MAX) return false;
				const auto* cls = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(param)));
				return ReadClassOdfUnsafe(cls, out);
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool ReadPathSeh(const void* path, char (&label)[kMaxLabel], bool& hasLabel, float& x, float& z) noexcept
		{
			__try
			{
				const auto* view = static_cast<const AiPathView*>(path);
				const std::int32_t last = Core::PathDestinationIndex(view->count);
				if (last < 0 || !view->points) return false;
				x = view->points[last * 2];
				z = view->points[last * 2 + 1];
				if (!std::isfinite(x) || !std::isfinite(z)) return false;
				hasLabel = false;
				label[0] = 0;
				if (view->label)
				{
					std::size_t n = 0;
					while (n < kMaxLabel - 1 && view->label[n]) { label[n] = view->label[n]; ++n; }
					label[n] = 0;
					hasLabel = n > 0;
				}
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		bool TryTerrainHeight(float x, float z, float& y) noexcept
		{
			__try
			{
				const double h = reinterpret_cast<TerrainHeightAtFn>(EngineAddresses::Terrain::HeightAt)(x, z);
				if (!std::isfinite(h) || std::fabs(h) >= 1.0e6) return false;
				y = static_cast<float>(h);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode())) { return false; }
		}

		void* ObjectArg(lua_State* L)
		{
			const BZR::handle h = CheckHandle(L, 1);
			if (!RuntimeGate::IsSupported()) return nullptr;
			return BZR::GameObject::GetObj(h);
		}

		int PushOdf(lua_State* L, bool ok, const char (&odf)[9])
		{
			if (ok) lua_pushstring(L, odf); else lua_pushnil(L);
			return 1;
		}
	}

	int CancelBuild(lua_State* L)
	{
		void* obj = ObjectArg(L);
		lua_pushboolean(L, obj && ProducerCallsQualified() && CancelBuildSeh(obj));
		return 1;
	}

	int FinishBuild(lua_State* L)
	{
		void* obj = ObjectArg(L);
		void* created = nullptr;
		if (obj && ProducerCallsQualified() && FinishBuildSeh(obj, created) &&
			created && BZR::GameObject::IsLiveArenaObject(created))
		{
			const BZR::handle h = BZR::GameObject::GetHandle(static_cast<BZR::GameObject*>(created));
			if (h)
			{
				lua_pushlightuserdata(L, reinterpret_cast<void*>(h));
				return 1;
			}
		}
		lua_pushnil(L);
		return 1;
	}

	int GetBuildTimeLeft(lua_State* L)
	{
		void* obj = ObjectArg(L);
		void* cls = nullptr;
		float left = 0.0f;
		if (obj && ReadBuildSeh(obj, cls, left) && cls && std::isfinite(left))
			lua_pushnumber(L, left < 0.0f ? 0.0f : left);
		else
			lua_pushnil(L);
		return 1;
	}

	int SetBuildTimeLeft(lua_State* L)
	{
		void* obj = ObjectArg(L);
		const double seconds = luaL_checknumber(L, 2);
		lua_pushboolean(L, obj && Core::IsValidBuildSeconds(seconds) && WriteBuildTimeSeh(obj, static_cast<float>(seconds)));
		return 1;
	}

	int GetBuildingOdf(lua_State* L)
	{
		void* obj = ObjectArg(L);
		char odf[9]{};
		return PushOdf(L, obj && ReadBuildingOdfSeh(obj, odf), odf);
	}

	int GetCurScrapHold(lua_State* L)
	{
		void* obj = ObjectArg(L);
		std::int32_t cur = 0, max = 0;
		if (obj && ReadScrapSeh(obj, cur, max)) lua_pushinteger(L, cur); else lua_pushnil(L);
		return 1;
	}

	int GetMaxScrapHold(lua_State* L)
	{
		void* obj = ObjectArg(L);
		std::int32_t cur = 0, max = 0;
		if (obj && ReadScrapSeh(obj, cur, max)) lua_pushinteger(L, max); else lua_pushnil(L);
		return 1;
	}

	int SetCurScrapHold(lua_State* L)
	{
		void* obj = ObjectArg(L);
		const double amount = luaL_checknumber(L, 2);
		lua_pushboolean(L, obj && WriteCurScrapSeh(obj, amount));
		return 1;
	}

	int SetMaxScrapHold(lua_State* L)
	{
		void* obj = ObjectArg(L);
		const double value = luaL_checknumber(L, 2);
		std::int32_t capacity = 0;
		lua_pushboolean(L, obj && Core::TryScrapCapacity(value, capacity) && WriteMaxScrapSeh(obj, capacity));
		return 1;
	}

	int GetCurrentParam(lua_State* L)
	{
		void* obj = ObjectArg(L);
		AiCmdInfoLayout cmd{};
		char odf[9]{};
		return PushOdf(L, obj && ReadCommandSeh(obj, cmd) && cmd.what == kCmdBuild && ReadParamOdfSeh(cmd.param, odf), odf);
	}

	int GetCurrentWhen(lua_State* L)
	{
		void* obj = ObjectArg(L);
		AiCmdInfoLayout cmd{};
		if (obj && ReadCommandSeh(obj, cmd) && std::isfinite(cmd.when)) lua_pushnumber(L, cmd.when); else lua_pushnil(L);
		return 1;
	}

	int GetCurrentWhere(lua_State* L)
	{
		void* obj = ObjectArg(L);
		AiCmdInfoLayout cmd{};
		char label[kMaxLabel]{};
		bool hasLabel = false;
		float x = 0.0f, z = 0.0f;
		if (!obj || !ReadCommandSeh(obj, cmd) || !cmd.where || !ReadPathSeh(cmd.where, label, hasLabel, x, z))
		{
			lua_pushnil(L);
			return 1;
		}
		float y = 0.0f;
		if (!TryTerrainHeight(x, z, y))
		{
			lua_pushnil(L);
			return 1;
		}
		if (hasLabel) lua_pushstring(L, label); else lua_pushnil(L);
		PushVector(L, BZR::VECTOR_3D(x, y, z));
		return 2;
	}
}
