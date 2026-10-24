// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Game/TerrainPlacement.h"
#include "LuaHelpers.h"
#include "Util/FeatureQualification.h"
#include "Util/SehGuard.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ExtraUtilities::TerrainPlacement
{
    namespace
    {
        namespace Addr = EngineAddresses::Placement;
        using FindFn = void*(__cdecl*)(const char*);
        using BoundsFn = const float*(__thiscall*)(void*);
        using FloorFn = double(__cdecl*)(BZR::VECTOR_3D_LONG);
        using TestFn = bool(__cdecl*)(const float*, const BZR::MAT_3D*);
        using TeamFn = void*(__cdecl*)(int);
        using AlliedFn = bool(__thiscall*)(void*, int);
        using ClassFn = const unsigned char*(__thiscall*)(void*);
        using PositionFn = const BZR::VECTOR_3D*(__thiscall*)(void*);

        bool Finite(const BZR::VECTOR_3D& v) noexcept
        { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

        Status QuerySeh(int team, const char* odf, const BZR::VECTOR_3D& position,
            const BZR::VECTOR_3D& facing, BZR::VECTOR_3D& adjusted)
        {
            __try
            {
                if (!*BZR::GameObject::user_entity_ptr) return Status::Unavailable;
                using namespace EngineAddresses::PathBlock;
                const double scale = *reinterpret_cast<const float*>(GridScale);
                const int minX = *reinterpret_cast<const int*>(GridMinX), maxX = *reinterpret_cast<const int*>(GridMaxX);
                const int minZ = *reinterpret_cast<const int*>(GridMinZ), maxZ = *reinterpret_cast<const int*>(GridMaxZ);
                if (!std::isfinite(scale) || scale <= 0 || minX >= maxX || minZ >= maxZ) return Status::Unavailable;
                // Bound untrusted coordinates before entering terrain's native
                // readers, which do not all tolerate positions beyond the map.
                if (position.x * scale < minX || position.x * scale >= maxX - 1 ||
                    position.z * scale < minZ || position.z * scale >= maxZ - 1) return Status::OutsideMap;
                void* cls = reinterpret_cast<FindFn>(Addr::FindClass)(odf);
                if (!cls) return Status::InvalidOdf;
                const auto* bytes = static_cast<const unsigned char*>(cls);
                const auto category = *reinterpret_cast<const int*>(bytes + 0x1c);
                const auto signature = *reinterpret_cast<const std::uint32_t*>(bytes + 0x14);
                if (category != 2 && signature != 0x54555252) return Status::UnsupportedClass;
                const float* bounds = reinterpret_cast<BoundsFn>(Addr::ClassBounds)(cls);
                if (!bounds) return Status::Unavailable;
                float box[6];
                for (int i = 0; i < 6; ++i) { box[i] = bounds[i]; if (!std::isfinite(box[i])) return Status::Unavailable; }
                if (box[0] > box[3] || box[1] > box[4] || box[2] > box[5]) return Status::Unavailable;
                // Exactly the rig preview's X/Z clearance, followed by the
                // engine's own blocked-cell, height-variance and collision test.
                box[0] -= 10; box[2] -= 10; box[3] += 10; box[5] += 10;
                const double length = std::hypot(static_cast<double>(facing.x), static_cast<double>(facing.z));
                const float fx = static_cast<float>(facing.x / length), fz = static_cast<float>(facing.z / length);
                BZR::MAT_3D matrix{};
                matrix.right_x = fz; matrix.right_z = -fx; matrix.up_y = 1;
                matrix.front_x = fx; matrix.front_z = fz;
                matrix.posit_x = position.x; matrix.posit_z = position.z;
                matrix.posit_y = reinterpret_cast<FloorFn>(Addr::FindFloor)(BZR::VECTOR_3D_LONG{position.x, position.y, position.z});
                if (!std::isfinite(matrix.posit_y)) return Status::Unavailable;
                adjusted = BZR::VECTOR_3D{matrix.posit_x, matrix.posit_y, matrix.posit_z};
                double loX = maxX, hiX = minX, loZ = maxZ, hiZ = minZ;
                for (int i = 0; i < 4; ++i)
                {
                    const double x = (matrix.posit_x + box[(i & 1) ? 3 : 0] * fz + box[(i & 2) ? 5 : 2] * fx) * scale;
                    const double z = (matrix.posit_z - box[(i & 1) ? 3 : 0] * fx + box[(i & 2) ? 5 : 2] * fz) * scale;
                    if (!std::isfinite(x) || !std::isfinite(z) || x < minX || x >= maxX - 1 || z < minZ || z >= maxZ - 1)
                        return Status::OutsideMap;
                    loX = (std::min)(loX, x); hiX = (std::max)(hiX, x);
                    loZ = (std::min)(loZ, z); hiZ = (std::max)(hiZ, z);
                }
                if ((hiX - loX + 2) * (hiZ - loZ + 2) > 65536) return Status::Unavailable;
                if (!reinterpret_cast<TestFn>(Addr::CanBuildHere)(box, &matrix)) return Status::Blocked;
                if (signature != 0x434f4d4d && signature != 0x54555252 && signature != 0x53484c44) return Status::Buildable;
                void* nativeTeam = reinterpret_cast<TeamFn>(Addr::TeamGet)(team);
                if (!nativeTeam) return Status::PowerRequired;
                const auto* vector = reinterpret_cast<const std::uintptr_t*>(Addr::PowerPlantList);
                const auto begin = vector[0], end = vector[1];
                if (end < begin || (end - begin) % sizeof(void*) || (end - begin) / sizeof(void*) > BZR::GameObject::kArenaSlotCount)
                    return Status::Unavailable;
                const auto* centre = reinterpret_cast<const float*>(bytes + 0x124);
                const double cx = matrix.posit_x + centre[0] * fz + centre[2] * fx;
                const double cy = matrix.posit_y + centre[1];
                const double cz = matrix.posit_z - centre[0] * fx + centre[2] * fz;
                if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz)) return Status::Unavailable;
                for (auto cursor = begin; cursor < end; cursor += sizeof(void*))
                {
                    auto* plant = *reinterpret_cast<BZR::GameObject**>(cursor);
                    if (!BZR::GameObject::IsLiveArenaObject(plant)) continue;
                    const auto* object = reinterpret_cast<const unsigned char*>(plant);
                    const int plantTeam = *reinterpret_cast<const int*>(object + 0x174);
                    if (plantTeam < 0 || plantTeam > 15 || !reinterpret_cast<AlliedFn>(Addr::TeamIsAllied)(nativeTeam, plantTeam)) continue;
                    auto* sub = reinterpret_cast<unsigned char*>(plant) + 0x18;
                    const auto* vtable = *reinterpret_cast<void***>(sub);
                    const auto* plantClass = reinterpret_cast<ClassFn>(vtable[0])(sub);
                    const double range = *reinterpret_cast<const float*>(plantClass + 0x160);
                    // Use the same native position accessor as the rig preview.
                    const auto* plantPosition = reinterpret_cast<PositionFn>(vtable[3])(sub);
                    if (!plantPosition || !Finite(*plantPosition)) continue;
                    const double dx = cx - plantPosition->x, dy = cy - plantPosition->y, dz = cz - plantPosition->z;
                    if (std::isfinite(range) && range > 0 && dx * dx + dy * dy + dz * dz < range * range) return Status::Buildable;
                }
                return Status::PowerRequired;
            }
            __except (Seh::Filter(GetExceptionCode())) { return Status::Unavailable; }
        }
    }

    Status Query(int team, const char* odf, const BZR::VECTOR_3D& position,
        const BZR::VECTOR_3D& facing, BZR::VECTOR_3D& adjusted) noexcept
    {
        adjusted = position;
        if (!RuntimeGate::IsSupported()) return Status::Unavailable;
        if (team < 0 || team > 15 || !odf || !Finite(position) || !Finite(facing) || std::hypot(facing.x, facing.z) < 1e-6f)
            return Status::Unavailable;
        const auto length = std::strlen(odf);
        if (!length || length > 12) return Status::InvalidOdf;
        char base[9]{};
        const auto count = length > 4 && _stricmp(odf + length - 4, ".odf") == 0 ? length - 4 : length;
        if (!count || count > 8) return Status::InvalidOdf;
        for (std::size_t i = 0; i < count; ++i)
        {
            const unsigned char ch = static_cast<unsigned char>(odf[i]);
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return Status::InvalidOdf;
            base[i] = odf[i];
        }
        static bool qualified = false;
        if (!qualified) qualified = QualifyFeature("Terrain placement ", 7);
        if (!qualified) return Status::Unavailable;
        return Seh::CatchCpp("IsTerrainBuildable", [&] { return QuerySeh(team, base, position, facing, adjusted); }, Status::Unavailable);
    }

    const char* Describe(Status status) noexcept
    {
        switch (status)
        {
        case Status::Buildable: return "buildable";
        case Status::Blocked: return "blocked";
        case Status::OutsideMap: return "outside-map";
        case Status::PowerRequired: return "power-required";
        case Status::InvalidOdf: return "invalid-odf";
        case Status::UnsupportedClass: return "unsupported-class";
        default: return "unavailable";
        }
    }
}

namespace ExtraUtilities::Lua::TerrainPlacementBindings
{
    int IsTerrainBuildable(lua_State* L)
    {
        const int team = luaL_checkint(L, 1);
        if (team < 0 || team > 15) return luaL_argerror(L, 1, "team must be in 0..15");
        const char* odf = luaL_checkstring(L, 2);
        const auto position = CheckVectorOrSingles(L, 3);
        const auto facing = CheckVectorOrSingles(L, 4);
        BZR::VECTOR_3D adjusted;
        const auto status = ExtraUtilities::TerrainPlacement::Query(team, odf, position, facing, adjusted);
        using Status = ExtraUtilities::TerrainPlacement::Status;
        if (status == Status::Unavailable || status == Status::InvalidOdf || status == Status::UnsupportedClass)
        { lua_pushnil(L); lua_pushnil(L); }
        else { lua_pushboolean(L, status == Status::Buildable); PushVector(L, adjusted); }
        lua_pushstring(L, ExtraUtilities::TerrainPlacement::Describe(status)); return 3;
    }
}
