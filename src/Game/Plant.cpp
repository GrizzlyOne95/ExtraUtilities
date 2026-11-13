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
#include "Hook.h"
#include "InlinePatch.h"
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
		using ExplodeFn = void(__thiscall*)(void* self);
		using SetDamageFlagsFn = void(__thiscall*)(void* self, void* damage);
		using GetObjFlagsFn = std::uint32_t(__thiscall*)(void* self);
		using SetCommandObjectFn = void(__thiscall*)(void* panel, int command, void* object);
		using SetCommandPositionFn = void(__thiscall*)(void* panel, int command, void* position);

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
		constexpr std::size_t kGameObjectPlayerShot = 0x1D8; // float, SetDamageFlags / SelectionDisplay
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
		// GameObjectClass* -> its g_entries node (null: no ODF name). The HUD
		// hooks classify every object every frame, so the name read and the
		// string key are paid once per class. Entries are node-stable and
		// both maps are cleared together.
		std::unordered_map<const void*, Entry*> g_classEntries;

		// Hit scope state: set while a plant is being hit.
		bool g_suppressChunklets = false;

		struct Hook
		{
			EntryDetour32* detour = nullptr;
		};
		Hook g_clsnSounds, g_ordnanceHit, g_explode, g_setDamageFlags;
		::ExtraUtilities::Hook* g_chunkletGate = nullptr;
		ClsnSoundsFn g_originalClsnSounds = nullptr;
		OrdnanceHitFn g_originalOrdnanceHit = nullptr;
		ExplodeFn g_originalExplode = nullptr;
		SetDamageFlagsFn g_originalSetDamageFlags = nullptr;

		// Object-loop stores in CockpitRadar::Render, MapRadar::Render and
		// OverView::ClickedOn, and four retargeted calls: the candidate tests of
		// Targeting::FindTarget/ValidateTarget and InfoDisplay::FindSightObject,
		// and Reticle::Simulate's ControlPanel::SetCommand(cmd, obj).
		::ExtraUtilities::Hook* g_radarGather = nullptr;
		::ExtraUtilities::Hook* g_mapRadarGather = nullptr;
		::ExtraUtilities::Hook* g_clickedOn = nullptr;
		InlinePatch* g_callSites[4] = {};

		// Loop-continue labels the stubs return to for a plant (read by asm).
		std::uintptr_t g_radarGatherNext = Addr::RadarGatherNext;
		std::uintptr_t g_mapRadarGatherNext = Addr::MapRadarGatherNext;
		std::uintptr_t g_clickedOnNext = Addr::ClickedOnNext;

		// Stolen bytes: push ebp; mov ebp,esp; sub esp,imm. Whole, reloc-free
		// instructions; identity is catalogued as Plant.* in exu.json.
		constexpr std::array<std::uint8_t, 6> kClsnSoundsEntry = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C };
		constexpr std::array<std::uint8_t, 9> kOrdnanceHitEntry = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00 };
		// cmp dword ptr [eax+0x8028], 0x400
		constexpr std::array<std::uint8_t, 10> kChunkletPoolCheck = { 0x81, 0xB8, 0x28, 0x80, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00 };
		constexpr std::array<std::uint8_t, 9> kExplodeEntry = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x24, 0x01, 0x00, 0x00 };
		constexpr std::array<std::uint8_t, 6> kSetDamageFlagsEntry = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C };
		// mov [ebp-0x154], edx / mov [ebp-0x150], edx / mov [ebp-0x14], edx; mov ecx, [ebp-0x14]
		constexpr std::array<std::uint8_t, 6> kRadarGatherStore = { 0x89, 0x95, 0xAC, 0xFE, 0xFF, 0xFF };
		constexpr std::array<std::uint8_t, 6> kMapRadarGatherStore = { 0x89, 0x95, 0xB0, 0xFE, 0xFF, 0xFF };
		constexpr std::array<std::uint8_t, 6> kClickedOnStore = { 0x89, 0x55, 0xEC, 0x8B, 0x4D, 0xEC };
		// The stock call rel32 of each retargeted call (GetObjFlags, SetCommand).
		constexpr std::array<std::uint8_t, 5> kFindTargetFlagsCall = { 0xE8, 0xD7, 0xD6, 0xE7, 0xFF };
		constexpr std::array<std::uint8_t, 5> kValidateTargetFlagsCall = { 0xE8, 0xB2, 0xD3, 0xE7, 0xFF };
		constexpr std::array<std::uint8_t, 5> kReticleSetCommandCall = { 0xE8, 0xAE, 0xC7, 0xEE, 0xFF };
		constexpr std::array<std::uint8_t, 5> kSightObjectFlagsCall = { 0xE8, 0x3D, 0x83, 0xF6, 0xFF };

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

		bool ReadU32Seh(const void* base, std::size_t offset, std::uint32_t& out) noexcept
		{
			out = 0;
			if (base == nullptr)
				return false;
			__try
			{
				out = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(base) + offset);
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool WriteU32Seh(void* base, std::size_t offset, std::uint32_t value) noexcept
		{
			if (base == nullptr)
				return false;
			__try
			{
				*reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(base) + offset) = value;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// GameObjectClass* of a GameObject (secondary vtable slot 0).
		bool ReadClassSeh(void* gameObject, const void*& out) noexcept
		{
			out = nullptr;
			if (gameObject == nullptr)
				return false;
			__try
			{
				auto* sub = reinterpret_cast<std::uint8_t*>(gameObject) + kGameObjectClassSub;
				void** vtable = *reinterpret_cast<void***>(sub);
				const auto getClass = reinterpret_cast<VirtualGetFn>(vtable[0]);
				out = getClass(sub);
				return out != nullptr;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				out = nullptr;
				return false;
			}
		}

		// Class ODF base name (as Lua GetOdf reads it), copied into a
		// terminated buffer. false when the class is unreadable.
		bool ReadClassNameSeh(const void* objClass, char (&out)[Core::kOdfNameMax + 1]) noexcept
		{
			out[0] = '\0';
			if (objClass == nullptr)
				return false;
			__try
			{
				const auto* cls = reinterpret_cast<const std::uint8_t*>(objClass);
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
		// unreadable). Never throws. After the first object of a class this
		// is one virtual call and one pointer-keyed lookup; the verdict itself
		// is re-read from the entry, so exu.SetPlant applies at once.
		Entry* ClassifyGameObject(void* gameObject) noexcept
		{
			try
			{
				const void* objClass = nullptr;
				if (!ReadClassSeh(gameObject, objClass))
					return nullptr;
				Entry* entry = nullptr;
				const auto cached = g_classEntries.find(objClass);
				if (cached != g_classEntries.end())
				{
					entry = cached->second;
				}
				else
				{
					char name[Core::kOdfNameMax + 1];
					if (!ReadClassNameSeh(objClass, name))
						return nullptr; // not cached: may be readable later
					char key[Core::kOdfNameMax + 1];
					if (Core::NormalizeOdfNameInto(std::string_view(name, Core::kOdfNameMax), key) != 0)
						entry = &GetEntry(key);
					g_classEntries.emplace(objClass, entry);
				}
				return entry != nullptr && EntryIsPlant(*entry) ? entry : nullptr;
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

		// Replaces CreateChunklet's "pool full" compare, past the entry that
		// OpenShim's chunk trace detours, so either order of the two hooks works.
		// The next instruction is jge to the function's own exit, and equal
		// operands take it. EAX holds this; ret leaves the flags alone.
		__declspec(naked) void ChunkletPoolCheckStub()
		{
			__asm
			{
				cmp byte ptr [g_suppressChunklets], 0
				jne suppress
				cmp dword ptr [eax + 0x8028], 0x400
				ret
			suppress:
				cmp eax, eax
				ret
			}
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

		// ----- HUD and targeting ----------------------------------------------

		// GameObject::SetDamageFlags is __thiscall(this = GameObject, Damage*)
		// with ret 4. It stamps playerShot (+0x1D8), which SelectionDisplay
		// compares with now + 2 s to draw the "recently shot" sprite. For a
		// plant the stamp is undone; everything else the call does stays.
		void __fastcall SetDamageFlagsHook(void* self, void* /*edx*/, void* damage)
		{
			const SetDamageFlagsFn original = g_originalSetDamageFlags;
			if (original == nullptr)
				return;
			std::uint32_t before = 0;
			const bool haveBefore = ReadU32Seh(self, kGameObjectPlayerShot, before);
			original(self, damage);
			if (!haveBefore)
				return;
			std::uint32_t after = 0;
			if (ReadU32Seh(self, kGameObjectPlayerShot, after) && after != before &&
				ClassifyGameObject(self) != nullptr)
			{
				WriteU32Seh(self, kGameObjectPlayerShot, before);
			}
		}

		bool __cdecl IsPlantForStub(void* gameObject) noexcept
		{
			return ClassifyGameObject(gameObject) != nullptr;
		}

		// The three object-loop stubs replace a 6-byte store of the current
		// GameObject* (EDX) into the loop's local. They run the stolen bytes,
		// then for a plant swap the return address for the loop's own continue
		// label, so the object is skipped exactly as the stock filters skip
		// one. No stack is pending at either point (ebp-relative frames).
		// EAX/ECX/EDX are kept; the flags at the return point are dead.
		__declspec(naked) void RadarGatherStub()
		{
			__asm
			{
				mov dword ptr [ebp - 0x154], edx
				push eax
				push ecx
				push edx
				push edx
				call IsPlantForStub
				add esp, 4
				test al, al
				pop edx
				pop ecx
				pop eax
				jz keep
				push eax
				mov eax, dword ptr [g_radarGatherNext]
				mov dword ptr [esp + 4], eax
				pop eax
			keep:
				ret
			}
		}

		__declspec(naked) void MapRadarGatherStub()
		{
			__asm
			{
				mov dword ptr [ebp - 0x150], edx
				push eax
				push ecx
				push edx
				push edx
				call IsPlantForStub
				add esp, 4
				test al, al
				pop edx
				pop ecx
				pop eax
				jz keep
				push eax
				mov eax, dword ptr [g_mapRadarGatherNext]
				mov dword ptr [esp + 4], eax
				pop eax
			keep:
				ret
			}
		}

		__declspec(naked) void ClickedOnStub()
		{
			__asm
			{
				mov dword ptr [ebp - 0x14], edx
				push eax
				push ecx
				push edx
				push edx
				call IsPlantForStub
				add esp, 4
				test al, al
				pop edx
				pop ecx
				pop eax
				mov ecx, dword ptr [ebp - 0x14]
				jz keep
				push eax
				mov eax, dword ptr [g_clickedOnNext]
				mov dword ptr [esp + 4], eax
				pop eax
			keep:
				ret
			}
		}

		// Replaces GameObject::GetObjFlags at the candidate tests of
		// Targeting::FindTarget/ValidateTarget (and InfoDisplay::FindSightObject,
		// whose mask is 0x600), which reject an object
		// whose flags & 0x201 is set. A plant reads as dying (0x200) there only.
		std::uint32_t __fastcall TargetFlagsFilter(void* self)
		{
			std::uint32_t flags = reinterpret_cast<GetObjFlagsFn>(Addr::GameObjectGetObjFlags)(self);
			if (ClassifyGameObject(self) != nullptr)
				flags |= kObjDying;
			return flags;
		}

		// Replaces ControlPanel::SetCommand(cmd, obj) where Reticle::Simulate
		// hands the object under the reticle to the command button. For a plant
		// the button is filled as when aiming at the sky (CMD_NONE,
		// no position): no name/health readout and no order on it. The reticle
		// still selects the plant, so lobbers and convergence aim at it.
		void __fastcall ReticleSetCommandFilter(void* panel, void* /*edx*/, int command, void* object)
		{
			if (object != nullptr && ClassifyGameObject(object) != nullptr)
			{
				reinterpret_cast<SetCommandPositionFn>(Addr::ControlPanelSetCommandPosition)(panel, 0, nullptr);
				return;
			}
			reinterpret_cast<SetCommandObjectFn>(Addr::ControlPanelSetCommandObject)(panel, command, object);
		}

		std::vector<std::uint8_t> CallTo(std::uintptr_t site, const void* target)
		{
			const std::int32_t rel = static_cast<std::int32_t>(
				reinterpret_cast<std::uintptr_t>(target) - (site + 5u));
			std::vector<std::uint8_t> bytes(5);
			bytes[0] = 0xE8;
			std::memcpy(bytes.data() + 1, &rel, sizeof(rel));
			return bytes;
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
				!PrepareHook(g_explode, g_originalExplode, Addr::BuildingExplode,
					reinterpret_cast<const void*>(&ExplodeHook), kExplodeEntry, "Building::Explode") ||
				!PrepareHook(g_setDamageFlags, g_originalSetDamageFlags, Addr::SetDamageFlags,
					reinterpret_cast<const void*>(&SetDamageFlagsHook), kSetDamageFlagsEntry, "GameObject::SetDamageFlags"))
			{
				Shutdown();
				return false;
			}

			// The HUD and targeting sites: three object-loop stores and four
			// retargeted calls. All seven must qualify, or none stays.
			const auto makeLoopHook = [](std::uintptr_t address, void (*stub)(), const std::array<std::uint8_t, 6>& preimage) {
				return new (std::nothrow) ::ExtraUtilities::Hook(
					address, reinterpret_cast<const void*>(stub), preimage.size(), BasicPatch::Status::INACTIVE,
					std::vector<std::uint8_t>(preimage.begin(), preimage.end()));
			};
			g_radarGather = makeLoopHook(Addr::RadarGatherObject, &RadarGatherStub, kRadarGatherStore);
			g_mapRadarGather = makeLoopHook(Addr::MapRadarGatherObject, &MapRadarGatherStub, kMapRadarGatherStore);
			g_clickedOn = makeLoopHook(Addr::ClickedOnObject, &ClickedOnStub, kClickedOnStore);

			const auto makeCall = [](std::uintptr_t site, const void* target, const std::array<std::uint8_t, 5>& preimage) {
				return new (std::nothrow) InlinePatch(site, CallTo(site, target), BasicPatch::Status::INACTIVE,
					std::vector<std::uint8_t>(preimage.begin(), preimage.end()));
			};
			const void* flagsFilter = reinterpret_cast<const void*>(&TargetFlagsFilter);
			const void* commandFilter = reinterpret_cast<const void*>(&ReticleSetCommandFilter);
			g_callSites[0] = makeCall(Addr::FindTargetFlagsCall, flagsFilter, kFindTargetFlagsCall);
			g_callSites[1] = makeCall(Addr::ValidateTargetFlagsCall, flagsFilter, kValidateTargetFlagsCall);
			g_callSites[2] = makeCall(Addr::SightObjectFlagsCall, flagsFilter, kSightObjectFlagsCall);
			g_callSites[3] = makeCall(Addr::ReticleSetCommandCall, commandFilter, kReticleSetCommandCall);

			std::vector<BasicPatch*> sitePatches = { g_radarGather, g_mapRadarGather, g_clickedOn };
			for (InlinePatch* site : g_callSites)
				sitePatches.push_back(site);
			for (BasicPatch* patch : sitePatches)
			{
				if (patch == nullptr || (patch->SetStatus(true), !patch->IsActive()))
				{
					Logging::LogMessage("exu: plant unavailable; a radar/target/reticle site did not qualify");
					Shutdown();
					return false;
				}
			}

			g_chunkletGate = new (std::nothrow) ::ExtraUtilities::Hook(
				Addr::CreateChunkletPoolCheck, reinterpret_cast<const void*>(&ChunkletPoolCheckStub),
				kChunkletPoolCheck.size(), BasicPatch::Status::INACTIVE,
				std::vector<std::uint8_t>(kChunkletPoolCheck.begin(), kChunkletPoolCheck.end()));
			if (g_chunkletGate == nullptr || (g_chunkletGate->SetStatus(true), !g_chunkletGate->IsActive()))
			{
				Logging::LogMessage("exu: plant unavailable; ChunkEffect::CreateChunklet pool check did not qualify");
				Shutdown();
				return false;
			}

			for (Hook* hook : { &g_clsnSounds, &g_ordnanceHit, &g_explode, &g_setDamageFlags })
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
		delete g_chunkletGate;
		g_chunkletGate = nullptr;
		DestroyHook(g_explode);
		DestroyHook(g_setDamageFlags);
		delete g_radarGather;
		g_radarGather = nullptr;
		delete g_mapRadarGather;
		g_mapRadarGather = nullptr;
		delete g_clickedOn;
		g_clickedOn = nullptr;
		for (InlinePatch*& site : g_callSites)
		{
			delete site;
			site = nullptr;
		}
		g_originalClsnSounds = nullptr;
		g_originalOrdnanceHit = nullptr;
		g_originalExplode = nullptr;
		g_originalSetDamageFlags = nullptr;
		g_suppressChunklets = false;
		ResetMissionState();
	}

	bool IsActive() noexcept
	{
		if (g_clsnSounds.detour == nullptr || g_ordnanceHit.detour == nullptr ||
			g_chunkletGate == nullptr || g_explode.detour == nullptr || g_setDamageFlags.detour == nullptr ||
			g_radarGather == nullptr || g_mapRadarGather == nullptr || g_clickedOn == nullptr)
			return false;
		for (const InlinePatch* site : g_callSites)
		{
			if (site == nullptr || !site->IsActive())
				return false;
		}
		return g_clsnSounds.detour->IsActive() && g_ordnanceHit.detour->IsActive() &&
			g_chunkletGate->IsActive() && g_explode.detour->IsActive() && g_setDamageFlags.detour->IsActive() &&
			g_radarGather->IsActive() && g_mapRadarGather->IsActive() && g_clickedOn->IsActive();
	}

	void ResetMissionState() noexcept
	{
		g_classEntries.clear();
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
