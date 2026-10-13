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

/* Vendored verbatim from BZR-OpenShim 72134279c: include/native_chunk_cache.h. See src/Gibs/NativeChunks/README.md. */
#pragma once
#include "native_chunk_mesh.h"
#include <filesystem>

namespace BZROpenShim::NativeChunks
{
struct CachedPiece
{
    std::string name;
    uint32_t triangles = 0;
    float center[3] = {0, 0, 0};
};
uint64_t Fingerprint(const std::vector<uint8_t> &bytes);
// Only complete, bounded manifests with matching payload hashes are usable.
// The caller supplies a directory keyed by both source resources and emitter
// version. Corrupt, partial or stale caches fail closed and can be regenerated.
bool ReadCache(const std::filesystem::path &directory, std::vector<CachedPiece> &pieces,
               uintmax_t byteBudget = 128 * 1024 * 1024, uintmax_t *validatedBytes = nullptr);
// WriteCache split in two: SerializeCache validates and builds the manifest and
// per-piece content hashes in memory (the piece .mesh bytes already exist);
// WriteCache then writes them. Used by tests/native_chunk_bench.cpp.
struct CacheImage
{
    std::string manifest;
    std::vector<CachedPiece> cached;
    std::vector<uint64_t> hashes;
};
bool SerializeCache(const std::vector<Piece> &pieces, CacheImage &image);
bool WriteCache(const std::filesystem::path &directory, const std::vector<Piece> &pieces,
                std::vector<CachedPiece> &cached);
// Skinned gib caches: the same content-validated piece cache plus a
// gibs.cache sidecar carrying each piece's driving bone and bound radius.
// A missing, stale or mismatched sidecar fails the whole cache closed.
struct CachedGib
{
    CachedPiece piece;
    uint16_t bone = 0;
    std::string boneName;
    float radius = 0;
    uint32_t capTriangles = 0;
    bool weapon = false;
};
bool ReadGibCache(const std::filesystem::path &directory, std::vector<CachedGib> &gibs);
bool WriteGibCache(const std::filesystem::path &directory, const std::vector<GibPiece> &gibs,
                   std::vector<CachedGib> &cached);
} // namespace BZROpenShim::NativeChunks
