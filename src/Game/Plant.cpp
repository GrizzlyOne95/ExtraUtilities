/* Copyright (C) 2026 GrizzlyOne95
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

#include "Game/Plant.h"

#include "EntryDetour32.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ExtraUtilities::Plant
{
	namespace
	{
		namespace Addr = EngineAddresses::Plant;
		// The ODF plumbing (file probe, ParameterDB open/close/string, hash) is
		// the one PathBlock uses; only GetBool is Plant's own catalog entry.
		namespace OdfAddr = EngineAddresses::PathBlock;
		namespace Core = PlantCore;

		using FileSizeFn = int(__cdecl*)(const char* fileName);
		using HashFn = std::uint32_t(__cdecl*)(const char* text, std::uint32_t seed);
		using PdbOpenFn = void*(__thiscall*)(void* self, const char* fileName);
		using PdbCloseFn = void(__thiscall*)(void* self);
		using PdbGetStringFn = const char*(__thiscall*)(void* self, std::uint32_t section, std::uint32_t key);
		using PdbGetBoolFn = bool(__thiscall*)(void* self, std::uint32_t section, std::uint32_t key, bool* out, bool defaultValue);
		using VirtualGetFn = void*(__thiscall*)(void* self);
		using ObjSetFlagFn = void(__cdecl*)(void* obj76, std::uint32_t flags);
		using RemoveLightFn = void(__cdecl*)(void* obj76);
		using ExplosionFindFn = void*(__cdecl*)(std::uint32_t nameLo, std::uint32_t nameHi);
		using ExplosionBuildFn = void(__thiscall*)(void* explosionClass, BZR::MAT_3D* transform, void* owner);
		using MakeScrapFieldFn = void(__cdecl*)(const float* position, int count);

		using ClsnSoundsFn = void(__cdecl*)(void* a, void* b);
		using OrdnanceHitFn = void(__thiscall*)(void* self, void* hit, float* normal);
		using CreateChunkletFn = void(__thiscall*)(void* self, void* a, void* b, bool c);
		using ExplodeFn = void(__thiscall*)(void* self);

		// Layouts (Docs/Research/PLANT_COLLISION_RE_20261013.md, GOG 2.2.301).
		constexpr std::size_t kClsnInfoObj = 0x04;          // CLSN_INFO -> OBJ76*
		constexpr std::size_t kObjFlags = 0x14;             // OBJ76 flags
		constexpr std::size_t kObjTransform = 0x20;         // OBJ76 MAT_3D
		constexpr std::size_t kObjOwner = 0x8C;             // OBJ76 -> GameObject*
		constexpr std::uint32_t kObjDying = 0x200;
		constexpr std::uint32_t kObjHasLight = 0x800;
		constexpr std::uint32_t kObjNoScrap = 0x80;
		constexpr std::size_t kGameObjectClassSub = 0x18;   // secondary vtable; slot 0 -> class
		constexpr std::size_t kClassOdfName = 0x30;         // inline, <= 8 chars, may be unterminated
		constexpr std::size_t kClassScrap = 0x4C;           // int
		constexpr std::size_t kClassExplosion = 0x144;      // ExplosionClass*
		constexpr std::size_t kExplodeObj = 0xDC;           // Building::Explode this -> OBJ76*
		constexpr std::size_t kExplodeClass = 0xE0;         // Building::Explode this -> GameObjectClass*
		constexpr std::size_t kExplodeVtableCleanup = 0x10; // vtable slot 4: release and remove
		constexpr std::size_t kOrdnanceClass = 0x0C;        // Ordnance -> OrdnanceClass*
		constexpr std::size_t kOrdnanceXplGround = 0x3C;
		constexpr std::size_t kOrdnanceXplBuilding = 0x44;
		constexpr std::size_t kMatrixPosit = 0x28;
		constexpr std::uint32_t kHashSeed = 0x811C9DC5u;

		static_assert(sizeof(BZR::MAT_3D) == 0x40, "MAT_3D must match the engine transform");
		static_assert(offsetof(BZR::MAT_3D, posit_x) == kMatrixPosit, "MAT_3D position offset");

		// ----- Per-ODF state (mission lifetime) -------------------------------

		struct Entry
		{
			Core::PlantParams params;
			bool odfRead = false;
			int overrideState = -1; // exu.SetPlant: -1 none, 0 off, 1 on
			bool hitResolved = false;
			void* hitClass = nullptr;   // ExplosionClass* of hitExplosion
			bool deathResolved = false;
			void* deathClass = nullptr; // ExplosionClass* of explosionName
		};

		std::unordered_map<std::string, Entry> g_entries;

		// Hit scope state: set while a plant is being hit.
		bool g_suppressChunklets = false;

		struct Hook
		{
			EntryDetour32* detour = nullptr;
		};
		Hook g_clsnSounds, g_ordnanceHit, g_createChunklet, g_explode;
		ClsnSoundsFn g_originalClsnSounds = nullptr;
		OrdnanceHitFn g_originalOrdnanceHit = nullptr;
		CreateChunkletFn g_originalCreateChunklet = nullptr;
		ExplodeFn g_originalExplode = nullptr;

		// Stolen bytes: push ebp; mov ebp,esp; sub esp,imm. Whole, reloc-free
		// instructions; identity is catalogued as Plant.* in exu.json.
		constexpr std::array<std::uint8_t, 6> kClsnSoundsEntry = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C };
		constexpr std::array<std::uint8_t, 9> kOrdnanceHitEntry = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00 };
		constexpr std::array<std::uint8_t, 9> kCreateChunkletEntry = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00 };
		constexpr std::array<std::uint8_t, 9> kExplodeEntry = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x24, 0x01, 0x00, 0x00 };

		// ----- Raw engine reads (POD-only SEH shells) -------------------------

		bool ReadPtrSeh(const void* base, std::size_t offset, void*& out) noexcept
		{
			out = nullptr;
			if (base == nullptr)
				return false;
			__try
			{
				out = *reinterpret_cast<void* const*>(reinterpret_cast<const std::uint8_t*>(base) + offset);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out = nullptr;
				return false;
			}
		}

		bool WritePtrSeh(void* base, std::size_t offset, void* value) noexcept
		{
			if (base == nullptr)
				return false;
			__try
			{
				*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(base) + offset) = value;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// Class ODF base name of a GameObject (as Lua GetOdf reads it), copied
		// into a terminated buffer. false when the class is unreadable.
		bool ReadClassNameSeh(void* gameObject, char (&out)[Core::kOdfNameMax + 1]) noexcept
		{
			out[0] = '\0';
			if (gameObject == nullptr)
				return false;
			__try
			{
				auto* sub = reinterpret_cast<std::uint8_t*>(gameObject) + kGameObjectClassSub;
				void** vtable = *reinterpret_cast<void***>(sub);
				const auto getClass = reinterpret_cast<VirtualGetFn>(vtable[0]);
				const auto* cls = reinterpret_cast<const std::uint8_t*>(getClass(sub));
				if (cls == nullptr)
					return false;
				const char* name = reinterpret_cast<const char*>(cls + kClassOdfName);
				std::size_t n = 0;
				for (; n < Core::kOdfNameMax && name[n] != '\0'; ++n)
					out[n] = name[n];
				out[n] = '\0';
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out[0] = '\0';
				return false;
			}
		}

		// Everything Building::Explode reads before the object is released.
		struct DeathSnapshot
		{
			BZR::MAT_3D transform;
			std::uint32_t flags = 0;
			void* stockExplosion = nullptr;
			int scrap = 0;
		};

		bool ReadDeathSnapshotSeh(const void* obj76, const void* objClass, DeathSnapshot& out) noexcept
		{
			__try
			{
				const auto* o = reinterpret_cast<const std::uint8_t*>(obj76);
				const auto* c = reinterpret_cast<const std::uint8_t*>(objClass);
				std::memcpy(&out.transform, o + kObjTransform, sizeof(out.transform));
				out.flags = *reinterpret_cast<const std::uint32_t*>(o + kObjFlags);
				out.stockExplosion = *reinterpret_cast<void* const*>(c + kClassExplosion);
				out.scrap = *reinterpret_cast<const int*>(c + kClassScrap);
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
				return *reinterpret_cast<const bool*>(EngineAddresses::Multiplayer::isNetGame);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return true; // unreadable: do not add scrap
			}
		}

		// Reads [PlantClass] through the engine's ParameterDB. A missing file or
		// key leaves the defaults.
		bool ReadOdfSeh(const char* fileName, Core::PlantParams& out) noexcept
		{
			__try
			{
				if (reinterpret_cast<FileSizeFn>(OdfAddr::ResourceFileSize)(fileName) <= 0)
					return true;
				const auto hash = reinterpret_cast<HashFn>(OdfAddr::ParameterHash);
				const std::uint32_t section = hash("PlantClass", kHashSeed);
				const std::uint32_t keyPlant = hash("isPlant", kHashSeed);
				const std::uint32_t keyHit = hash("hitExplosion", kHashSeed);
				const std::uint32_t keyDeath = hash("explosionName", kHashSeed);
				const std::uint32_t keyScrap = hash("dropScrap", kHashSeed);

				void* handle = nullptr;
				reinterpret_cast<PdbOpenFn>(OdfAddr::ParameterDbOpen)(&handle, fileName);
				if (handle == nullptr)
					return true;
				const auto getBool = reinterpret_cast<PdbGetBoolFn>(Addr::ParameterDbGetBool);
				const auto getString = reinterpret_cast<PdbGetStringFn>(OdfAddr::ParameterDbGetString);
				bool isPlant = false;
				bool dropScrap = false;
				getBool(&handle, section, keyPlant, &isPlant, false);
				getBool(&handle, section, keyScrap, &dropScrap, false);
				char hit[32];
				Core::CleanOdfString(getString(&handle, section, keyHit), hit, sizeof(hit));
				Core::CleanOdfString(getString(&handle, section, keyDeath), out.explosionName, sizeof(out.explosionName));
				reinterpret_cast<PdbCloseFn>(OdfAddr::ParameterDbClose)(&handle);

				out.isPlant = isPlant;
				out.dropScrap = dropScrap;
				out.hitNone = Core::IsNoneName(hit);
				if (!out.hitNone)
					Core::CleanOdfString(hit, out.hitExplosion, sizeof(out.hitExplosion));
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// ----- Lookup ---------------------------------------------------------

		Entry& GetEntry(const std::string& key)
		{
			Entry& entry = g_entries[key];
			if (!entry.odfRead)
			{
				entry.odfRead = true;
				if (RuntimeGate::IsSupported() && !key.empty())
				{
					const std::string fileName = key + ".odf";
					Core::PlantParams params;
					if (ReadOdfSeh(fileName.c_str(), params))
						entry.params = params;
					if (entry.params.isPlant)
					{
						Logging::LogMessage("exu: plant: %s.odf isPlant hitExplosion=%s explosionName=%s dropScrap=%d",
							key.c_str(),
							entry.params.hitNone ? "none" : (entry.params.hitExplosion[0] ? entry.params.hitExplosion : "(ordnance ground hit)"),
							entry.params.explosionName[0] ? entry.params.explosionName : "(ODF explosion)",
							entry.params.dropScrap ? 1 : 0);
					}
				}
			}
			return entry;
		}

		bool EntryIsPlant(const Entry& entry) noexcept
		{
			return Core::ResolveIsPlant(entry.params.isPlant, entry.overrideState);
		}

		// The plant entry of a GameObject, or null when it is not a plant (or
		// unreadable). Never throws.
		Entry* ClassifyGameObject(void* gameObject) noexcept
		{
			try
			{
				char name[Core::kOdfNameMax + 1];
				if (!ReadClassNameSeh(gameObject, name))
					return nullptr;
				char key[Core::kOdfNameMax + 1];
				if (Core::NormalizeOdfNameInto(std::string_view(name, Core::kOdfNameMax), key) == 0)
					return nullptr;
				Entry& entry = GetEntry(key);
				return EntryIsPlant(entry) ? &entry : nullptr;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		Entry* ClassifyObj76(const void* obj76) noexcept
		{
			void* gameObject = nullptr;
			if (!ReadPtrSeh(obj76, kObjOwner, gameObject) || gameObject == nullptr)
				return nullptr;
			return ClassifyGameObject(gameObject);
		}

		void* FindExplosion(const char* name) noexcept
		{
			std::uint32_t packed[2];
			if (!Core::PackExplosionName(name, packed))
				return nullptr;
			return reinterpret_cast<ExplosionFindFn>(Addr::ExplosionFind)(packed[0], packed[1]);
		}

		void* ResolveHitExplosion(Entry& entry) noexcept
		{
			if (!entry.hitResolved)
			{
				entry.hitResolved = true;
				entry.hitClass = entry.params.hitExplosion[0] ? FindExplosion(entry.params.hitExplosion) : nullptr;
			}
			return entry.hitClass;
		}

		void* ResolveDeathExplosion(Entry& entry) noexcept
		{
			if (!entry.deathResolved)
			{
				entry.deathResolved = true;
				entry.deathClass = entry.params.explosionName[0] ? FindExplosion(entry.params.explosionName) : nullptr;
			}
			return entry.deathClass;
		}

		// ----- Hooks ----------------------------------------------------------

		// ClsnSounds: the bump sound. Skipped when either side is a plant.
		void __cdecl ClsnSoundsHook(void* a, void* b)
		{
			const ClsnSoundsFn original = g_originalClsnSounds;
			if (original == nullptr)
				return;
			void* objA = nullptr;
			void* objB = nullptr;
			ReadPtrSeh(a, kClsnInfoObj, objA);
			ReadPtrSeh(b, kClsnInfoObj, objB);
			if (ClassifyObj76(objA) != nullptr || ClassifyObj76(objB) != nullptr)
				return;
			original(a, b);
		}

		// While a plant is hit: the building hit explosion of the ordnance
		// class is replaced by the plant's, and chips are suppressed. Ordnance
		// classes are shared, so the restore must run on every path.
		class HitScope
		{
		public:
			HitScope(void* ordnance, Entry& entry) noexcept
			{
				m_previousSuppress = g_suppressChunklets;
				g_suppressChunklets = true;
				ReadPtrSeh(ordnance, kOrdnanceClass, m_class);
				if (m_class == nullptr)
					return;
				void* replacement = nullptr;
				if (!entry.params.hitNone)
				{
					replacement = ResolveHitExplosion(entry);
					if (replacement == nullptr)
						ReadPtrSeh(m_class, kOrdnanceXplGround, replacement);
				}
				if (ReadPtrSeh(m_class, kOrdnanceXplBuilding, m_saved) &&
					WritePtrSeh(m_class, kOrdnanceXplBuilding, replacement))
				{
					m_swapped = true;
				}
			}

			~HitScope()
			{
				if (m_swapped)
					WritePtrSeh(m_class, kOrdnanceXplBuilding, m_saved);
				g_suppressChunklets = m_previousSuppress;
			}

			HitScope(const HitScope&) = delete;
			HitScope& operator=(const HitScope&) = delete;

		private:
			void* m_class = nullptr;
			void* m_saved = nullptr;
			bool m_swapped = false;
			bool m_previousSuppress = false;
		};

		// Ordnance::Hit is __thiscall; an x86 __fastcall wrapper takes ECX/EDX
		// as the first two parameters and leaves the two stack arguments in the
		// same slots with the same callee pop (ret 8), as in PilotFsmIntercept.
		void __fastcall OrdnanceHitHook(void* self, void* /*edx*/, void* hit, float* normal)
		{
			const OrdnanceHitFn original = g_originalOrdnanceHit;
			if (original == nullptr)
				return;
			Entry* plant = hit != nullptr ? ClassifyGameObject(hit) : nullptr;
			if (plant == nullptr)
			{
				original(self, hit, normal);
				return;
			}
			HitScope scope(self, *plant);
			original(self, hit, normal);
		}

		// CreateChunklet is __thiscall with three stack arguments (ret 0x0C).
		void __fastcall CreateChunkletHook(void* self, void* /*edx*/, void* a, void* b, bool c)
		{
			const CreateChunkletFn original = g_originalCreateChunklet;
			if (original == nullptr || g_suppressChunklets)
				return;
			original(self, a, b, c);
		}

		// The reduced death of a plant. false: nothing was changed, run the
		// stock sequence instead.
		bool PlantDeath(void* self, Entry& plant) noexcept
		{
			void* obj = nullptr;
			void* objClass = nullptr;
			if (!ReadPtrSeh(self, kExplodeObj, obj) || !ReadPtrSeh(self, kExplodeClass, objClass) ||
				obj == nullptr || objClass == nullptr)
				return false;
			DeathSnapshot snap;
			if (!ReadDeathSnapshotSeh(obj, objClass, snap))
				return false;

			reinterpret_cast<ObjSetFlagFn>(Addr::ObjSetFlag)(obj, kObjDying);
			snap.flags |= kObjDying;

			void* explosion = ResolveDeathExplosion(plant);
			if (explosion == nullptr)
				explosion = snap.stockExplosion;
			if (explosion != nullptr)
				reinterpret_cast<ExplosionBuildFn>(Addr::ExplosionBuild)(explosion, &snap.transform, nullptr);

			if ((snap.flags & kObjHasLight) != 0)
				reinterpret_cast<RemoveLightFn>(Addr::RemoveLightSource)(obj);

			// Mandatory: releases the Ogre entities and removes the object.
			void** vtable = *reinterpret_cast<void***>(self);
			reinterpret_cast<ExplodeFn>(vtable[kExplodeVtableCleanup / sizeof(void*)])(self);

			// Single player only, like the stock path (which also skips objects
			// flagged 0x80 and has its own multiplayer rules).
			if (plant.params.dropScrap && snap.scrap > 0 && (snap.flags & kObjNoScrap) == 0 && !IsNetGameSeh())
			{
				const float position[3] = {
					static_cast<float>(snap.transform.posit_x),
					static_cast<float>(snap.transform.posit_y),
					static_cast<float>(snap.transform.posit_z),
				};
				reinterpret_cast<MakeScrapFieldFn>(Addr::MakeScrapField)(position, snap.scrap);
			}
			return true;
		}

		// Building::Explode is __thiscall(this = GameObject + 0x18).
		void __fastcall ExplodeHook(void* self, void* /*edx*/)
		{
			const ExplodeFn original = g_originalExplode;
			if (original == nullptr)
				return;
			Entry* plant = ClassifyGameObject(reinterpret_cast<std::uint8_t*>(self) - kGameObjectClassSub);
			if (plant == nullptr || !PlantDeath(self, *plant))
				original(self);
		}

		// ----- Install --------------------------------------------------------

		void DestroyHook(Hook& hook) noexcept
		{
			// Restores the engine entry before the trampoline is freed.
			delete hook.detour;
			hook.detour = nullptr;
		}

		template <typename Fn, std::size_t N>
		bool PrepareHook(Hook& hook, Fn& original, std::uintptr_t address, const void* handler,
			const std::array<std::uint8_t, N>& preimage, const char* name)
		{
			auto* raw = new (std::nothrow) EntryDetour32(
				address, handler, N, BasicPatch::Status::INACTIVE,
				std::vector<std::uint8_t>(preimage.begin(), preimage.end()));
			if (raw == nullptr)
				return false;
			hook.detour = raw;
			if (!hook.detour->PrepareTrampoline())
			{
				Logging::LogMessage("exu: plant unavailable; %s identity/preimage did not qualify", name);
				DestroyHook(hook);
				return false;
			}
			original = reinterpret_cast<Fn>(hook.detour->GetTrampoline());
			return original != nullptr;
		}
	}

	bool Install() noexcept
	{
		try
		{
			if (!RuntimeGate::IsSupported())
				return false;
			if (IsActive())
				return true;
			Shutdown();

			if (!PrepareHook(g_clsnSounds, g_originalClsnSounds, Addr::ClsnSounds,
					reinterpret_cast<const void*>(&ClsnSoundsHook), kClsnSoundsEntry, "ClsnSounds") ||
				!PrepareHook(g_ordnanceHit, g_originalOrdnanceHit, Addr::OrdnanceHit,
					reinterpret_cast<const void*>(&OrdnanceHitHook), kOrdnanceHitEntry, "Ordnance::Hit") ||
				!PrepareHook(g_createChunklet, g_originalCreateChunklet, Addr::CreateChunklet,
					reinterpret_cast<const void*>(&CreateChunkletHook), kCreateChunkletEntry, "ChunkEffect::CreateChunklet") ||
				!PrepareHook(g_explode, g_originalExplode, Addr::BuildingExplode,
					reinterpret_cast<const void*>(&ExplodeHook), kExplodeEntry, "Building::Explode"))
			{
				Shutdown();
				return false;
			}

			for (Hook* hook : { &g_clsnSounds, &g_ordnanceHit, &g_createChunklet, &g_explode })
			{
				hook->detour->SetStatus(true);
				if (!hook->detour->IsActive())
				{
					Logging::LogMessage("exu: plant prepared but an entry patch could not activate");
					Shutdown();
					return false;
				}
			}
			Logging::LogMessage("exu: plant hooks active");
			return true;
		}
		catch (...)
		{
			OutputDebugStringA("ExtraUtilities: exception while installing plant hooks\n");
			Shutdown();
			return false;
		}
	}

	void Shutdown() noexcept
	{
		DestroyHook(g_clsnSounds);
		DestroyHook(g_ordnanceHit);
		DestroyHook(g_createChunklet);
		DestroyHook(g_explode);
		g_originalClsnSounds = nullptr;
		g_originalOrdnanceHit = nullptr;
		g_originalCreateChunklet = nullptr;
		g_originalExplode = nullptr;
		g_suppressChunklets = false;
		ResetMissionState();
	}

	bool IsActive() noexcept
	{
		return g_clsnSounds.detour != nullptr && g_ordnanceHit.detour != nullptr &&
			g_createChunklet.detour != nullptr && g_explode.detour != nullptr &&
			g_clsnSounds.detour->IsActive() && g_ordnanceHit.detour->IsActive() &&
			g_createChunklet.detour->IsActive() && g_explode.detour->IsActive();
	}

	void ResetMissionState() noexcept
	{
		g_entries.clear();
	}

	bool QueryPlant(const char* odf)
	{
		const std::string key = Core::NormalizeOdfName(odf != nullptr ? odf : "");
		if (key.empty())
			return false;
		return EntryIsPlant(GetEntry(key));
	}

	void SetOverride(const char* odf, int overrideState)
	{
		const std::string key = Core::NormalizeOdfName(odf != nullptr ? odf : "");
		if (key.empty())
			return;
		if (overrideState < 0)
		{
			const auto it = g_entries.find(key);
			if (it != g_entries.end())
				it->second.overrideState = -1;
			return;
		}
		g_entries[key].overrideState = overrideState != 0 ? 1 : 0;
	}
}
