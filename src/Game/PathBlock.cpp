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

#include "Game/PathBlock.h"

#include "Game/PathBlockMath.h"
#include "InlinePatch.h"
#include "Util/EngineAddresses.generated.h"
#include "Util/Logging.h"
#include "Util/RuntimeGate.h"
#include "Util/SehGuard.h"
#include "bzr.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ExtraUtilities::PathBlock
{
	namespace
	{
		namespace Addr = EngineAddresses::PathBlock;
		namespace M = PathBlockMath;

		using BlockCellsFn = void(__cdecl*)(void* gameObject, bool block);
		using CellTestFn = bool(__cdecl*)(int gridX, int gridZ);
		using InvalidateStripsFn = void(__cdecl*)(float x0, float z0, float x1, float z1);
		using ObjBoundingBoxFn = void(__cdecl*)(void* obj76, float* outMin, float* outMax);
		using SelectLodFn = int(__cdecl*)(void* obj76, int lod);
		using FileSizeFn = int(__cdecl*)(const char* fileName);
		using HashFn = std::uint32_t(__cdecl*)(const char* text, std::uint32_t seed);
		using PdbOpenFn = void*(__thiscall*)(void* self, const char* fileName);
		using PdbCloseFn = void(__thiscall*)(void* self);
		using PdbGetStringFn = const char*(__thiscall*)(void* self, std::uint32_t section, std::uint32_t key);
		using VirtualGetFn = void*(__thiscall*)(void* self);

		// GameObject layout (bzr.h; GOG BlockCells 0x00468A70).
		constexpr std::size_t kGameObjectClassSub = 0x18;     // vtable[0] -> class, vtable[12] -> OBJ76
		constexpr std::size_t kVtableGetClass = 0;
		constexpr std::size_t kVtableGetObj76 = 0x30 / sizeof(void*);
		constexpr std::size_t kGameObjectObj76 = 0xF4;        // transform source (GOG 0x0045C4D0)
		constexpr std::size_t kClassCategory = 0x1C;          // 5 -> perimeter branch
		constexpr std::size_t kClassOdfName = 0x30;           // inline name, read 8 chars like GetOdf
		constexpr std::size_t kOdfNameMax = 8;
		// OBJ76 (GOG get_obj_bounding_box 0x0062E3F0, SetObjCollision 0x00443A80).
		constexpr std::size_t kObjFlags = 0x14;               // bit 0: skipped by the bbox
		constexpr std::size_t kObjTransform = 0x20;           // 9 floats, pad, 3 doubles
		constexpr std::size_t kObjTransformPosit = 0x48;
		constexpr std::size_t kObjGeo = 0x64;
		constexpr std::size_t kObjSibling = 0x7C;
		constexpr std::size_t kObjChild = 0x80;
		// GEO and FACE (Cgeom_Create, GOG 0x00444220).
		constexpr std::size_t kGeoVertexCount = 0x04;
		constexpr std::size_t kGeoVertices = 0x0C;
		constexpr std::size_t kGeoFaces = 0x14;
		constexpr std::size_t kFaceVertexCount = 0x04;
		constexpr std::size_t kFaceNext = 0x30;
		constexpr std::size_t kFaceVertexList = 0x40;
		constexpr std::size_t kFaceVertexStride = 0x10;
		// std::map<GameObject*, Area> node (MSVC): left, parent, right, color, isnil.
		constexpr std::size_t kNodeLeft = 0x00;
		constexpr std::size_t kNodeParent = 0x04;
		constexpr std::size_t kNodeRight = 0x08;
		constexpr std::size_t kNodeIsNil = 0x0D;
		constexpr std::size_t kNodeKey = 0x10;
		constexpr std::size_t kNodeArea = 0x14;

		constexpr int kMaxMapEntries = 8192;
		constexpr int kMaxHierarchyNodes = 512;
		constexpr int kMaxFaceVertices = 64;
		constexpr std::size_t kMaxTriangles = 200000;
		constexpr std::uint32_t kHashSeed = 0x811C9DC5u;
		constexpr std::int32_t kPerimeterCategory = 5;

		void __cdecl BlockCellsStub(void* gameObject, bool block);

		std::vector<std::uint8_t> CallTo(std::uintptr_t site)
		{
			const std::int32_t rel = static_cast<std::int32_t>(
				reinterpret_cast<std::uintptr_t>(&BlockCellsStub) - (site + 5u));
			std::vector<std::uint8_t> bytes(5);
			bytes[0] = 0xE8;
			std::memcpy(bytes.data() + 1, &rel, sizeof(rel));
			return bytes;
		}

		// Every call of BlockCells in .text. Preimages are the stock call rel32,
		// so a site another module already patched is left alone.
		InlinePatch g_sites[] = {
			InlinePatch(Addr::ProcessBuildingsCall, CallTo(Addr::ProcessBuildingsCall), BasicPatch::Status::ACTIVE, { 0xE8, 0xF4, 0xEC, 0xFF, 0xFF }),
			InlinePatch(Addr::AddObjectCall, CallTo(Addr::AddObjectCall), BasicPatch::Status::ACTIVE, { 0xE8, 0xE1, 0xD8, 0xFF, 0xFF }),
			InlinePatch(Addr::DeleteObjectCall, CallTo(Addr::DeleteObjectCall), BasicPatch::Status::ACTIVE, { 0xE8, 0x41, 0xD8, 0xFF, 0xFF }),
			InlinePatch(Addr::DeployUnblockCallA, CallTo(Addr::DeployUnblockCallA), BasicPatch::Status::ACTIVE, { 0xE8, 0x24, 0xCA, 0xEB, 0xFF }),
			InlinePatch(Addr::DeployUnblockCallB, CallTo(Addr::DeployUnblockCallB), BasicPatch::Status::ACTIVE, { 0xE8, 0xC3, 0xC9, 0xEB, 0xFF }),
			InlinePatch(Addr::DeployBlockCallA, CallTo(Addr::DeployBlockCallA), BasicPatch::Status::ACTIVE, { 0xE8, 0x10, 0xC1, 0xEB, 0xFF }),
			InlinePatch(Addr::DeployBlockCallB, CallTo(Addr::DeployBlockCallB), BasicPatch::Status::ACTIVE, { 0xE8, 0xFE, 0xB5, 0xEB, 0xFF }),
			InlinePatch(Addr::DeployBlockCallC, CallTo(Addr::DeployBlockCallC), BasicPatch::Status::ACTIVE, { 0xE8, 0x33, 0x8B, 0xEB, 0xFF }),
		};
		constexpr int kSiteCount = static_cast<int>(sizeof(g_sites) / sizeof(g_sites[0]));

		struct OdfInfo
		{
			M::Mode configured = M::Mode::Box;
			float height1 = M::kDefaultHeight1;
			float height2 = M::kDefaultHeight2;
			bool trianglesTried = false;
			std::vector<M::Triangle> triangles;
		};

		bool g_enabled = true;
		std::unordered_map<std::string, OdfInfo> g_odf;
		// Objects already reported in exu.log (pointer + serial).
		std::unordered_set<std::uint64_t> g_logged;
		bool g_inStub = false;

		template <typename... Args>
		void Log(const char* format, Args... args) noexcept
		{
			try
			{
				Logging::LogMessage(format, args...);
			}
			catch (...)
			{
				OutputDebugStringA("ExtraUtilities: pathing log line dropped\n");
			}
		}

		// --- Raw engine reads (SEH shells: no C++ objects inside) -------------

		bool ReadGridSeh(M::Grid& grid, std::uint8_t*& cells) noexcept
		{
			__try
			{
				cells = *reinterpret_cast<std::uint8_t* const*>(Addr::CellTypeArray);
				grid.minX = *reinterpret_cast<const int*>(Addr::GridMinX);
				grid.maxX = *reinterpret_cast<const int*>(Addr::GridMaxX);
				grid.minZ = *reinterpret_cast<const int*>(Addr::GridMinZ);
				grid.maxZ = *reinterpret_cast<const int*>(Addr::GridMaxZ);
				grid.size = *reinterpret_cast<const float*>(Addr::GridSize);
				grid.scale = *reinterpret_cast<const float*>(Addr::GridScale);
				return cells != nullptr && grid.Valid();
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		bool ReadGrid(M::Grid& grid, std::uint8_t*& cells) noexcept
		{
			cells = nullptr;
			return RuntimeGate::IsSupported() && ReadGridSeh(grid, cells);
		}

		struct AreaEntry
		{
			void* key = nullptr;
			float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
		};

		// In-order walk of an MSVC std::map<GameObject*, Area>.
		int ReadAreaMapSeh(std::uintptr_t mapAddress, AreaEntry* out, int capacity) noexcept
		{
			__try
			{
				const std::uintptr_t head = *reinterpret_cast<const std::uintptr_t*>(mapAddress);
				if (head == 0)
					return 0;
				const auto isNil = [](std::uintptr_t node) {
					return *reinterpret_cast<const std::uint8_t*>(node + kNodeIsNil) != 0;
				};
				std::uintptr_t node = *reinterpret_cast<const std::uintptr_t*>(head + kNodeLeft);
				int count = 0;
				int guard = 0;
				while (node != 0 && node != head && !isNil(node))
				{
					if (++guard > kMaxMapEntries)
						return -1;
					if (count < capacity)
					{
						AreaEntry& e = out[count];
						e.key = *reinterpret_cast<void* const*>(node + kNodeKey);
						const float* area = reinterpret_cast<const float*>(node + kNodeArea);
						e.x0 = area[0];
						e.z0 = area[1];
						e.x1 = area[2];
						e.z1 = area[3];
					}
					++count;
					// Successor.
					std::uintptr_t right = *reinterpret_cast<const std::uintptr_t*>(node + kNodeRight);
					if (!isNil(right))
					{
						node = right;
						for (int depth = 0; depth < 64; ++depth)
						{
							const std::uintptr_t left = *reinterpret_cast<const std::uintptr_t*>(node + kNodeLeft);
							if (isNil(left))
								break;
							node = left;
						}
					}
					else
					{
						std::uintptr_t parent = *reinterpret_cast<const std::uintptr_t*>(node + kNodeParent);
						for (int depth = 0; depth < 64 && !isNil(parent) &&
							node == *reinterpret_cast<const std::uintptr_t*>(parent + kNodeRight); ++depth)
						{
							node = parent;
							parent = *reinterpret_cast<const std::uintptr_t*>(node + kNodeParent);
						}
						node = parent;
					}
				}
				return count;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return -1;
			}
		}

		std::vector<AreaEntry> ReadAreaMap(std::uintptr_t mapAddress)
		{
			std::vector<AreaEntry> entries(256);
			for (int attempt = 0; attempt < 3; ++attempt)
			{
				const int count = ReadAreaMapSeh(mapAddress, entries.data(), static_cast<int>(entries.size()));
				if (count < 0)
					return {};
				if (count <= static_cast<int>(entries.size()))
				{
					entries.resize(static_cast<std::size_t>(count));
					return entries;
				}
				entries.resize(static_cast<std::size_t>(count) + 64);
			}
			return {};
		}

		struct ObjectView
		{
			void* obj76 = nullptr;        // vtable OBJ76 (bbox and hierarchy root)
			char odf[kOdfNameMax + 1] = {};
			std::int32_t category = 0;
			M::Placement placement;
			float bmin[3] = {};
			float bmax[3] = {};
			bool bboxValid = false;
		};

		bool ReadObjectSeh(void* gameObject, ObjectView& out, bool full) noexcept
		{
			__try
			{
				auto* sub = reinterpret_cast<std::uint8_t*>(gameObject) + kGameObjectClassSub;
				void** vtable = *reinterpret_cast<void***>(sub);
				const auto getClass = reinterpret_cast<VirtualGetFn>(vtable[kVtableGetClass]);
				const auto getObj = reinterpret_cast<VirtualGetFn>(vtable[kVtableGetObj76]);
				const auto* cls = reinterpret_cast<const std::uint8_t*>(getClass(sub));
				if (cls == nullptr)
					return false;
				out.category = *reinterpret_cast<const std::int32_t*>(cls + kClassCategory);
				const char* name = reinterpret_cast<const char*>(cls + kClassOdfName);
				std::size_t n = 0;
				for (; n < kOdfNameMax && name[n] != '\0'; ++n)
					out.odf[n] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[n])));
				out.odf[n] = '\0';
				if (!full)
					return true;

				const auto* xformObj = *reinterpret_cast<const std::uint8_t* const*>(
					reinterpret_cast<const std::uint8_t*>(gameObject) + kGameObjectObj76);
				if (xformObj == nullptr)
					return false;
				const float* m = reinterpret_cast<const float*>(xformObj + kObjTransform);
				const double* posit = reinterpret_cast<const double*>(xformObj + kObjTransformPosit);
				out.placement.rightX = m[0];
				out.placement.rightZ = m[2];
				out.placement.frontX = m[6];
				out.placement.frontZ = m[8];
				out.placement.positX = posit[0];
				out.placement.positZ = posit[2];

				out.obj76 = getObj(sub);
				if (out.obj76 == nullptr)
					return false;
				reinterpret_cast<ObjBoundingBoxFn>(Addr::ObjBoundingBox)(out.obj76, out.bmin, out.bmax);
				out.bboxValid = out.bmin[0] <= out.bmax[0] && out.bmin[1] <= out.bmax[1] && out.bmin[2] <= out.bmax[2];
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		// full: also the placement and the engine bbox (get_obj_bounding_box
		// walks every LOD0 vertex, so the cheap form is used for filtering).
		bool ReadObject(void* gameObject, ObjectView& out, bool full = true) noexcept
		{
			if (gameObject == nullptr || !BZR::GameObject::IsLiveArenaObject(gameObject))
				return false;
			return ReadObjectSeh(gameObject, out, full);
		}

		// LOD0 GEO triangles of the hierarchy in the root's local frame (the
		// root's own matrix is the identity, as in get_obj_bounding_box).
		M::Mat ReadLocalMatrix(const std::uint8_t* obj) noexcept
		{
			const float* m = reinterpret_cast<const float*>(obj + kObjTransform);
			const double* posit = reinterpret_cast<const double*>(obj + kObjTransformPosit);
			M::Mat out;
			for (int i = 0; i < 3; ++i)
			{
				out.right[i] = m[i];
				out.up[i] = m[3 + i];
				out.front[i] = m[6 + i];
				out.posit[i] = posit[i];
			}
			return out;
		}

		struct WalkItem
		{
			const std::uint8_t* obj;
			M::Mat parent;
			bool root;
		};

		// Returns the triangle count (may exceed capacity: call again larger),
		// or -1 on a fault or malformed data.
		int CollectTrianglesSeh(void* rootObj, M::Triangle* out, int capacity, WalkItem* stack) noexcept
		{
			__try
			{
				int count = 0;
				int top = 0;
				int visited = 0;
				stack[top++] = WalkItem{ reinterpret_cast<const std::uint8_t*>(rootObj), M::Mat{}, true };
				while (top > 0)
				{
					const WalkItem item = stack[--top];
					if (++visited > kMaxHierarchyNodes)
						return -1;
					const std::uint8_t* obj = item.obj;
					const M::Mat world = item.root ? M::Mat{} : M::Then(ReadLocalMatrix(obj), item.parent);

					const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(obj + kObjFlags);
					if ((flags & 1u) == 0 &&
						reinterpret_cast<SelectLodFn>(Addr::GeoSelectLod)(const_cast<std::uint8_t*>(obj), 0) == 1)
					{
						const std::uint8_t* geo = *reinterpret_cast<const std::uint8_t* const*>(obj + kObjGeo);
						if (geo != nullptr)
						{
							const std::uint32_t vertexCount = *reinterpret_cast<const std::uint32_t*>(geo + kGeoVertexCount);
							const float* vertices = *reinterpret_cast<const float* const*>(geo + kGeoVertices);
							const std::uint8_t* face = *reinterpret_cast<const std::uint8_t* const*>(geo + kGeoFaces);
							int faces = 0;
							while (face != nullptr)
							{
								if (++faces > 65536 || vertices == nullptr)
									return -1;
								const std::int32_t n = *reinterpret_cast<const std::int32_t*>(face + kFaceVertexCount);
								if (n >= 3 && n <= kMaxFaceVertices)
								{
									double world0[3] = {};
									double prev[3] = {};
									for (std::int32_t k = 0; k < n; ++k)
									{
										const std::uint32_t index = *reinterpret_cast<const std::uint32_t*>(
											face + kFaceVertexList + static_cast<std::size_t>(k) * kFaceVertexStride);
										if (index >= vertexCount)
											return -1;
										const double local[3] = { vertices[index * 3], vertices[index * 3 + 1], vertices[index * 3 + 2] };
										double point[3];
										M::Apply(world, local, point);
										if (k == 0)
										{
											std::memcpy(world0, point, sizeof(world0));
										}
										else if (k >= 2)
										{
											if (count < capacity)
											{
												M::Triangle& t = out[count];
												t.a = { static_cast<float>(world0[0]), static_cast<float>(world0[1]), static_cast<float>(world0[2]) };
												t.b = { static_cast<float>(prev[0]), static_cast<float>(prev[1]), static_cast<float>(prev[2]) };
												t.c = { static_cast<float>(point[0]), static_cast<float>(point[1]), static_cast<float>(point[2]) };
											}
											++count;
										}
										std::memcpy(prev, point, sizeof(prev));
									}
								}
								face = *reinterpret_cast<const std::uint8_t* const*>(face + kFaceNext);
							}
						}
					}

					const std::uint8_t* child = *reinterpret_cast<const std::uint8_t* const*>(obj + kObjChild);
					// The root's siblings are not part of this object.
					const std::uint8_t* sibling = item.root ? nullptr
						: *reinterpret_cast<const std::uint8_t* const*>(obj + kObjSibling);
					if (sibling != nullptr)
					{
						if (top >= kMaxHierarchyNodes)
							return -1;
						stack[top++] = WalkItem{ sibling, item.parent, false };
					}
					if (child != nullptr)
					{
						if (top >= kMaxHierarchyNodes)
							return -1;
						stack[top++] = WalkItem{ child, world, false };
					}
				}
				return count;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return -1;
			}
		}

		bool CollectTriangles(void* rootObj, std::vector<M::Triangle>& out)
		{
			std::vector<WalkItem> stack(kMaxHierarchyNodes + 1);
			out.assign(2048, M::Triangle{});
			for (int attempt = 0; attempt < 3; ++attempt)
			{
				const int count = CollectTrianglesSeh(rootObj, out.data(), static_cast<int>(out.size()), stack.data());
				if (count < 0 || static_cast<std::size_t>(count) > kMaxTriangles)
				{
					out.clear();
					return false;
				}
				if (static_cast<std::size_t>(count) <= out.size())
				{
					out.resize(static_cast<std::size_t>(count));
					return count > 0;
				}
				out.assign(static_cast<std::size_t>(count), M::Triangle{});
			}
			out.clear();
			return false;
		}

		struct OdfValues
		{
			bool found = false;
			char mode[32] = {};
			char height1[32] = {};
			char height2[32] = {};
		};

		void CopyValue(char* out, std::size_t size, const char* value) noexcept
		{
			if (value == nullptr)
				return;
			std::size_t i = 0;
			for (; i + 1 < size && value[i] != '\0'; ++i)
				out[i] = value[i];
			out[i] = '\0';
		}

		// Reads [GameObjectClass] pathBlock / pathBlockHeight / pathBlockHeight2
		// through the engine's ParameterDB (Lua OpenODF/GetODFString).
		bool ReadOdfValuesSeh(const char* fileName, OdfValues& out) noexcept
		{
			__try
			{
				if (reinterpret_cast<FileSizeFn>(Addr::ResourceFileSize)(fileName) <= 0)
					return true; // no such file: stock box
				const auto hash = reinterpret_cast<HashFn>(Addr::ParameterHash);
				const std::uint32_t section = hash("GameObjectClass", kHashSeed);
				const std::uint32_t keyMode = hash("pathBlock", kHashSeed);
				const std::uint32_t keyH1 = hash("pathBlockHeight", kHashSeed);
				const std::uint32_t keyH2 = hash("pathBlockHeight2", kHashSeed);

				void* handle = nullptr;
				reinterpret_cast<PdbOpenFn>(Addr::ParameterDbOpen)(&handle, fileName);
				if (handle == nullptr)
					return true;
				const auto getString = reinterpret_cast<PdbGetStringFn>(Addr::ParameterDbGetString);
				CopyValue(out.mode, sizeof(out.mode), getString(&handle, section, keyMode));
				CopyValue(out.height1, sizeof(out.height1), getString(&handle, section, keyH1));
				CopyValue(out.height2, sizeof(out.height2), getString(&handle, section, keyH2));
				reinterpret_cast<PdbCloseFn>(Addr::ParameterDbClose)(&handle);
				out.found = true;
				return true;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				return false;
			}
		}

		OdfInfo& LookupOdf(const char* odf)
		{
			const std::string key(odf);
			auto it = g_odf.find(key);
			if (it != g_odf.end())
				return it->second;

			OdfInfo info;
			OdfValues values;
			char fileName[kOdfNameMax + 8] = {};
			std::snprintf(fileName, sizeof(fileName), "%s.odf", odf);
			if (odf[0] != '\0' && ReadOdfValuesSeh(fileName, values) && values.found)
			{
				info.configured = M::ParseMode(values.mode[0] ? values.mode : nullptr);
				info.height1 = M::ParseHeight(values.height1[0] ? values.height1 : nullptr, M::kDefaultHeight1);
				info.height2 = M::ParseHeight(values.height2[0] ? values.height2 : nullptr, M::kDefaultHeight2);
			}
			if (info.configured != M::Mode::Box)
			{
				Log("exu: pathing: %s.odf pathBlock=\"%s\" heights %.2f/%.2f",
					odf, M::ModeName(info.configured), info.height1, info.height2);
			}
			return g_odf.emplace(key, std::move(info)).first->second;
		}

		// The faces of a flagged ODF, read once from the first live instance.
		bool EnsureTriangles(OdfInfo& info, const ObjectView& view, const char* odf)
		{
			if (!info.trianglesTried)
			{
				info.trianglesTried = true;
				if (!CollectTriangles(view.obj76, info.triangles))
				{
					Log("exu: pathing: %s: no readable LOD0 collision faces; using the stock box", odf);
				}
			}
			return !info.triangles.empty();
		}

		M::Mode EffectiveMode(OdfInfo& info, const ObjectView& view, const char* odf)
		{
			if (!g_enabled || info.configured == M::Mode::Box)
				return M::Mode::Box;
			if (info.configured == M::Mode::Faces && !EnsureTriangles(info, view, odf))
				return M::Mode::Box;
			return info.configured;
		}

		// --- Region recompute -------------------------------------------------

		struct Blocker
		{
			void* key = nullptr;
			M::CellRect rect;
			M::Mode mode = M::Mode::Box;
			M::BoxPolygon poly;
			M::Placement placement;
			float bmin[3] = {};
			float bmax[3] = {};
			const OdfInfo* info = nullptr;
		};

		struct Snapshot
		{
			M::Grid grid;
			std::uint8_t* cells = nullptr;
			std::vector<Blocker> blockers;
			std::vector<M::CellRect> perimeters;
		};

		// Blockers whose stored area overlaps the filter (all when null).
		bool BuildSnapshot(Snapshot& snap, const M::CellRect* filter = nullptr)
		{
			if (!ReadGrid(snap.grid, snap.cells))
				return false;
			for (const AreaEntry& e : ReadAreaMap(Addr::BuildingAreaMap))
			{
				const M::CellRect rect = M::RectFromArea(snap.grid, e.x0, e.z0, e.x1, e.z1);
				if (filter != nullptr && !rect.Overlaps(*filter))
					continue;
				ObjectView view;
				if (!ReadObject(e.key, view))
					continue;
				Blocker b;
				b.key = e.key;
				b.rect = rect;
				OdfInfo& info = LookupOdf(view.odf);
				b.mode = EffectiveMode(info, view, view.odf);
				b.info = &info;
				b.placement = view.placement;
				std::memcpy(b.bmin, view.bmin, sizeof(b.bmin));
				std::memcpy(b.bmax, view.bmax, sizeof(b.bmax));
				if (b.mode == M::Mode::Box)
				{
					if (!view.bboxValid)
						continue; // the stock pass skips an empty bbox too
					b.poly = M::MakeBoxPolygon(view.placement, view.bmin, view.bmax);
				}
				snap.blockers.push_back(b);
			}
			for (const AreaEntry& e : ReadAreaMap(Addr::PerimeterAreaMap))
			{
				if (!BZR::GameObject::IsLiveArenaObject(e.key))
					continue;
				snap.perimeters.push_back(M::RectFromArea(snap.grid, e.x0, e.z0, e.x1, e.z1));
			}
			return true;
		}

		bool FaceCellSolid(const Blocker& b, float cx, float cz)
		{
			double lx = 0.0, lz = 0.0;
			if (!M::WorldToLocal(b.placement, cx, cz, lx, lz) || !M::InsideBox(lx, lz, b.bmin, b.bmax))
				return false;
			const auto& tris = b.info->triangles;
			return M::SolidAt(tris.data(), tris.size(), lx, lz, b.info->height1, b.info->height2);
		}

		bool CellBlockedBy(const Blocker& b, float cx, float cz, float half)
		{
			switch (b.mode)
			{
			case M::Mode::Box: return M::BoxCellBlocked(b.poly, cx, cz, half);
			case M::Mode::Faces: return FaceCellSolid(b, cx, cz);
			default: return false;
			}
		}

		std::uint8_t TerrainBitsSeh(int gridX, int gridZ, bool& cliff) noexcept
		{
			__try
			{
				cliff = reinterpret_cast<CellTestFn>(Addr::CellIsCliff)(gridX, gridZ);
				if (cliff)
					return M::kCliffBits;
				if (reinterpret_cast<CellTestFn>(Addr::CellIsSteep)(gridX, gridZ))
					return M::kSteepBit;
				if (reinterpret_cast<CellTestFn>(Addr::CellIsSlope)(gridX, gridZ))
					return M::kSlopeBit;
				return 0;
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				cliff = false;
				return 0xFF;
			}
		}

		void InvalidateSeh(float x0, float z0, float x1, float z1) noexcept
		{
			__try
			{
				reinterpret_cast<InvalidateStripsFn>(Addr::InvalidateStrips)(x0, z0, x1, z1);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
			}
		}

		// Recomputes every cell of the rectangle from scratch: building bits
		// cleared, terrain bits re-derived as ProcessCliffs does, perimeter
		// bits re-added, then every blocker that covers the cell (stock box
		// for unflagged objects, face mask for flagged ones).
		void ApplyRegion(const Snapshot& snap, M::CellRect rect)
		{
			const M::Grid& g = snap.grid;
			M::CellRect all;
			all.x0 = 0;
			all.z0 = 0;
			all.x1 = g.Width() - 1;
			all.z1 = g.Depth() - 1;
			rect = rect.Intersect(all);
			if (rect.Empty())
				return;

			const float half = g.size * 0.5f;
			std::vector<const Blocker*> local;
			for (const Blocker& b : snap.blockers)
			{
				if (b.rect.Overlaps(rect))
					local.push_back(&b);
			}

			for (int z = rect.z0; z <= rect.z1; ++z)
			{
				for (int x = rect.x0; x <= rect.x1; ++x)
				{
					const std::size_t index = static_cast<std::size_t>(z) * static_cast<std::size_t>(g.Width()) + static_cast<std::size_t>(x);
					const int gridX = x + g.minX;
					const int gridZ = z + g.minZ;
					bool cliff = false;
					const std::uint8_t terrain = TerrainBitsSeh(gridX, gridZ, cliff);
					if (terrain == 0xFF)
						return; // engine fault: leave the rest as it is
					std::uint8_t value = static_cast<std::uint8_t>((snap.cells[index] & ~M::kBuildingBits) | terrain);
					if (!cliff)
					{
						for (const M::CellRect& p : snap.perimeters)
						{
							if (p.Contains(x, z))
							{
								value |= M::kPerimeterBit;
								break;
							}
						}
					}
					const float cx = M::CellCentre(x, g.minX, g.size);
					const float cz = M::CellCentre(z, g.minZ, g.size);
					for (const Blocker* b : local)
					{
						if (b->rect.Contains(x, z) && CellBlockedBy(*b, cx, cz, half))
						{
							value |= M::kBuildingBits;
							break;
						}
					}
					snap.cells[index] = value;
				}
			}
			InvalidateSeh(
				static_cast<float>(rect.x0 + g.minX) * g.size,
				static_cast<float>(rect.z0 + g.minZ) * g.size,
				static_cast<float>(rect.x1 + 1 + g.minX) * g.size,
				static_cast<float>(rect.z1 + 1 + g.minZ) * g.size);
		}

		void CountMask(const Snapshot& snap, const Blocker& b, int& inBox, int& blocked)
		{
			inBox = 0;
			blocked = 0;
			const M::Grid& g = snap.grid;
			const float half = g.size * 0.5f;
			for (int z = b.rect.z0; z <= b.rect.z1; ++z)
			{
				for (int x = b.rect.x0; x <= b.rect.x1; ++x)
				{
					const float cx = M::CellCentre(x, g.minX, g.size);
					const float cz = M::CellCentre(z, g.minZ, g.size);
					double lx = 0.0, lz = 0.0;
					if (M::WorldToLocal(b.placement, cx, cz, lx, lz) && M::InsideBox(lx, lz, b.bmin, b.bmax))
						++inBox;
					if (CellBlockedBy(b, cx, cz, half))
						++blocked;
				}
			}
		}

		std::uint64_t LogKey(void* gameObject)
		{
			std::uint32_t serial = 0;
			if (BZR::GameObject::IsLiveArenaObject(gameObject))
			{
				serial = *reinterpret_cast<const std::uint32_t*>(
					reinterpret_cast<const std::uint8_t*>(gameObject) + BZR::GameObject::kSerialOffset);
			}
			return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(gameObject)) << 32) | serial;
		}

		void LogObjectOnce(const Snapshot& snap, const Blocker& b, bool force)
		{
			const std::uint64_t key = LogKey(b.key);
			if (!force && g_logged.count(key) != 0)
				return;
			g_logged.insert(key);
			int inBox = 0, blocked = 0;
			CountMask(snap, b, inBox, blocked);
			ObjectView view;
			const char* odf = ReadObject(b.key, view, false) ? view.odf : "?";
			Log("exu: pathing: odf=%s mode=%s cells_in_bbox=%d cells_blocked=%d tris=%u",
				odf, M::ModeName(b.mode), inBox, blocked,
				static_cast<unsigned>(b.info != nullptr ? b.info->triangles.size() : 0u));
		}

		const Blocker* FindBlocker(const Snapshot& snap, void* key)
		{
			for (const Blocker& b : snap.blockers)
			{
				if (b.key == key)
					return &b;
			}
			return nullptr;
		}

		bool IsFlagged(const Blocker& b)
		{
			return b.info != nullptr && b.info->configured != M::Mode::Box;
		}

		struct CheapEntry
		{
			void* key = nullptr;
			M::CellRect rect;
			M::Mode configured = M::Mode::Box;
		};

		// The stored areas with each object's configured mode, without the
		// engine bbox: cheap enough to run on every BlockCells call.
		bool ReadCheapEntries(M::Grid& grid, std::vector<CheapEntry>& out)
		{
			std::uint8_t* cells = nullptr;
			if (!ReadGrid(grid, cells))
				return false;
			for (const AreaEntry& e : ReadAreaMap(Addr::BuildingAreaMap))
			{
				ObjectView view;
				if (!ReadObject(e.key, view, false))
					continue;
				CheapEntry c;
				c.key = e.key;
				c.rect = M::RectFromArea(grid, e.x0, e.z0, e.x1, e.z1);
				c.configured = LookupOdf(view.odf).configured;
				out.push_back(c);
			}
			return true;
		}

		// After a stock BlockCells(go, block): recompute the touched region when
		// the object or anything overlapping it is flagged.
		void AfterBlockCells(void* gameObject, bool block, const M::CellRect* removedRect)
		{
			M::Grid grid;
			std::vector<CheapEntry> entries;
			if (!ReadCheapEntries(grid, entries))
				return;
			M::CellRect rect;
			bool selfFlagged = false;
			if (block)
			{
				const CheapEntry* self = nullptr;
				for (const CheapEntry& c : entries)
				{
					if (c.key == gameObject)
						self = &c;
				}
				if (self == nullptr)
					return; // perimeter object or empty bbox: stock only
				rect = self->rect;
				selfFlagged = self->configured != M::Mode::Box;
			}
			else
			{
				if (removedRect == nullptr)
					return;
				rect = *removedRect;
				ObjectView view;
				if (ReadObject(gameObject, view, false))
					selfFlagged = LookupOdf(view.odf).configured != M::Mode::Box;
			}

			bool relevant = selfFlagged;
			for (const CheapEntry& c : entries)
			{
				if (relevant)
					break;
				if (c.key != gameObject && c.configured != M::Mode::Box && c.rect.Overlaps(rect))
					relevant = true;
			}
			if (!relevant)
				return;

			Snapshot snap;
			if (!BuildSnapshot(snap, &rect))
				return;
			ApplyRegion(snap, rect);
			if (block && selfFlagged)
			{
				if (const Blocker* self = FindBlocker(snap, gameObject))
					LogObjectOnce(snap, *self, false);
			}
		}

		bool FindAreaRect(void* gameObject, M::CellRect& out)
		{
			M::Grid grid;
			std::uint8_t* cells = nullptr;
			if (!ReadGrid(grid, cells))
				return false;
			for (std::uintptr_t map : { static_cast<std::uintptr_t>(Addr::BuildingAreaMap), static_cast<std::uintptr_t>(Addr::PerimeterAreaMap) })
			{
				for (const AreaEntry& e : ReadAreaMap(map))
				{
					if (e.key == gameObject)
					{
						out = M::RectFromArea(grid, e.x0, e.z0, e.x1, e.z1);
						return true;
					}
				}
			}
			return false;
		}

		void __cdecl BlockCellsStub(void* gameObject, bool block)
		{
			const auto original = reinterpret_cast<BlockCellsFn>(Addr::BlockCells);
			if (g_inStub || !RuntimeGate::IsSupported())
			{
				original(gameObject, block);
				return;
			}
			g_inStub = true;
			M::CellRect removed;
			bool haveRemoved = false;
			try
			{
				if (!block)
					haveRemoved = FindAreaRect(gameObject, removed);
			}
			catch (...)
			{
				haveRemoved = false;
			}

			original(gameObject, block);

			try
			{
				AfterBlockCells(gameObject, block, haveRemoved ? &removed : nullptr);
			}
			catch (...)
			{
				Seh::LogCurrentCppException("exu.pathing BlockCells stub");
			}
			g_inStub = false;
		}

		int HookedSiteCount() noexcept
		{
			int n = 0;
			for (const InlinePatch& site : g_sites)
			{
				if (site.IsActive())
					++n;
			}
			return n;
		}

		int RefreshInternal(bool logAll)
		{
			M::Grid grid;
			std::vector<CheapEntry> entries;
			if (!ReadCheapEntries(grid, entries))
				return -1;
			int count = 0;
			for (const CheapEntry& c : entries)
			{
				if (c.configured == M::Mode::Box)
					continue;
				Snapshot snap;
				if (!BuildSnapshot(snap, &c.rect))
					continue;
				ApplyRegion(snap, c.rect);
				if (const Blocker* self = FindBlocker(snap, c.key))
					LogObjectOnce(snap, *self, logAll);
				++count;
			}
			return count;
		}
	}

	void ResetMissionState() noexcept
	{
		g_enabled = true;
		g_inStub = false;
		try
		{
			g_odf.clear();
			g_logged.clear();
		}
		catch (...)
		{
		}
	}

	void OnInit() noexcept
	{
		try
		{
			ResetMissionState();
			const int hooked = HookedSiteCount();
			Log("exu: pathing: %d/%d BlockCells call sites redirected", hooked, kSiteCount);
			if (!RuntimeGate::IsSupported())
				return;
			const int applied = RefreshInternal(false);
			if (applied < 0)
				Log("exu: pathing: no path grid yet; the map load pass will go through the hooks");
			else
				Log("exu: pathing: init refresh re-applied %d flagged object(s)", applied);
		}
		catch (...)
		{
			Seh::LogCurrentCppException("exu.pathing init");
		}
	}

	int Refresh() noexcept
	{
		if (!RuntimeGate::IsSupported())
			return -1;
		try
		{
			const int applied = RefreshInternal(true);
			Log("exu: pathing: Refresh re-applied %d flagged object(s)", applied);
			return applied;
		}
		catch (...)
		{
			Seh::LogCurrentCppException("exu.pathing Refresh");
			return -1;
		}
	}

	int SetEnabled(bool enabled) noexcept
	{
		if (g_enabled == enabled)
			return Refresh();
		g_enabled = enabled;
		Log("exu: pathing: %s", enabled ? "enabled" : "disabled (flagged objects use the stock box)");
		return Refresh();
	}

	bool IsEnabled() noexcept
	{
		return g_enabled;
	}

	bool GetObjectReport(void* gameObject, ObjectReport& out) noexcept
	{
		if (!RuntimeGate::IsSupported())
			return false;
		try
		{
			ObjectView view;
			if (!ReadObject(gameObject, view))
				return false;
			std::snprintf(out.odf, sizeof(out.odf), "%s", view.odf);
			OdfInfo& info = LookupOdf(view.odf);
			const M::Mode effective = EffectiveMode(info, view, view.odf);
			out.configured = M::ModeName(info.configured);
			out.effective = M::ModeName(effective);
			out.triangles = static_cast<int>(info.triangles.size());

			M::CellRect rect;
			bool found = false;
			{
				M::Grid grid;
				std::vector<CheapEntry> entries;
				if (!ReadCheapEntries(grid, entries))
					return true;
				for (const CheapEntry& c : entries)
				{
					if (c.key == gameObject)
					{
						rect = c.rect;
						found = true;
					}
				}
			}
			Snapshot snap;
			if (!found || !BuildSnapshot(snap, &rect))
				return true;
			if (const Blocker* b = FindBlocker(snap, gameObject))
			{
				out.inGrid = true;
				CountMask(snap, *b, out.cellsInBox, out.cellsBlocked);
			}
			return true;
		}
		catch (...)
		{
			Seh::LogCurrentCppException("exu.pathing GetMode");
			return false;
		}
	}

	std::string DumpGrid(float worldX, float worldZ, float radius)
	{
		M::Grid probe;
		std::uint8_t* probeCells = nullptr;
		if (!ReadGrid(probe, probeCells) || !std::isfinite(worldX) || !std::isfinite(worldZ) || !std::isfinite(radius))
			return std::string();
		int reach = static_cast<int>(std::ceil((std::max)(radius, 0.0f) / probe.size));
		reach = (std::min)(reach, 64);
		const int cx = M::CellIndex(worldX, probe.scale, probe.minX, probe.Width());
		const int cz = M::CellIndex(worldZ, probe.scale, probe.minZ, probe.Depth());
		M::CellRect view;
		view.x0 = cx - reach;
		view.x1 = cx + reach;
		view.z0 = cz - reach;
		view.z1 = cz + reach;
		Snapshot snap;
		if (!BuildSnapshot(snap, &view))
			return std::string();
		const M::Grid& g = snap.grid;

		char line[256];
		std::string out;
		std::snprintf(line, sizeof(line),
			"path grid: cell %.2f m, grid x %d..%d z %d..%d, centre cell (%d, %d) = world (%.1f, %.1f)\n",
			g.size, g.minX, g.maxX - 1, g.minZ, g.maxZ - 1, cx + g.minX, cz + g.minZ,
			M::CellCentre(cx, g.minX, g.size), M::CellCentre(cz, g.minZ, g.size));
		out += line;
		out += "legend: # building/blocked (bits 0x0B)  c cliff  s steep  / slope  p perimeter  ~ lava  . open  "
			"O centre; rows run +Z (north) at the top, +X to the right\n";
		for (int z = (std::min)(cz + reach, g.Depth() - 1); z >= (std::max)(cz - reach, 0); --z)
		{
			std::snprintf(line, sizeof(line), "%6d |", z + g.minZ);
			out += line;
			for (int x2 = (std::max)(cx - reach, 0); x2 <= (std::min)(cx + reach, g.Width() - 1); ++x2)
			{
				const std::uint8_t v = snap.cells[static_cast<std::size_t>(z) * static_cast<std::size_t>(g.Width()) + static_cast<std::size_t>(x2)];
				char ch = '.';
				if ((v & M::kBuildingBits) == M::kBuildingBits) ch = '#';
				else if ((v & M::kCliffBits) == M::kCliffBits) ch = 'c';
				else if (v & M::kPerimeterBit) ch = 'p';
				else if (v & 0x04) ch = '~';
				else if (v & M::kSteepBit) ch = 's';
				else if (v & M::kSlopeBit) ch = '/';
				if (x2 == cx && z == cz)
					ch = (ch == '#') ? '@' : 'O';
				out += ch;
			}
			out += "|\n";
		}
		// Flagged objects near the point.
		for (const Blocker& b : snap.blockers)
		{
			if (!IsFlagged(b) || !b.rect.Overlaps(view))
				continue;
			int inBox = 0, blocked = 0;
			CountMask(snap, b, inBox, blocked);
			ObjectView ov;
			std::snprintf(line, sizeof(line), "flagged %s: %s, %d cells in bbox, %d blocked, cells x %d..%d z %d..%d\n",
				ReadObject(b.key, ov, false) ? ov.odf : "?", M::ModeName(b.mode), inBox, blocked,
				b.rect.x0 + g.minX, b.rect.x1 + g.minX, b.rect.z0 + g.minZ, b.rect.z1 + g.minZ);
			out += line;
		}
		return out;
	}

	void GetCapabilities(Capabilities& out) noexcept
	{
		out.available = RuntimeGate::IsSupported();
		out.hookSites = kSiteCount;
		out.hookedSites = HookedSiteCount();
		out.enabled = g_enabled;
		M::Grid grid;
		std::uint8_t* cells = nullptr;
		out.gridReady = ReadGrid(grid, cells);
		out.cellSize = out.gridReady ? grid.size : 0.0f;
	}
}
