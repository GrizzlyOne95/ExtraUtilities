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

/* Vendored verbatim from BZR-OpenShim 72134279c: include/native_chunk_prune.h. See src/Gibs/NativeChunks/README.md. */
#pragma once
// Bounded pruning of the generated chunk cache (<game>/openshim/cache/chunks).
//
// The directory names below are the single source of truth for the cache
// layout: every writer builds its paths from them, and the pruner derives the
// "current" version of each tree from the same strings. Bumping a version here
// makes the old tree obsolete, and the next startup deletes it.
#define OPENSHIM_CHUNK_CACHE_NATIVE_DIR "native/v4/"
#define OPENSHIM_CHUNK_CACHE_GIBS_DIR "gibs/v6/"
#define OPENSHIM_CHUNK_CACHE_FALLBACK_DIR "fallback/v1/"
#define OPENSHIM_CHUNK_CACHE_CASINGS_DIR "casings/v1/"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace BZROpenShim::NativeChunks
{
enum class PruneKind
{
    ModelFolder,  // per-model cache folder: evictable by age and by size
    ObsoleteTree, // a version directory that is not the current one: always removed
    Protected,    // current-version shared shapes, loose .material files: never removed
    Unknown,      // anything else (user files, symlinks): never removed
};

struct PruneEntry
{
    std::string path; // relative to the cache root, '/' separated
    PruneKind kind = PruneKind::Unknown;
    int64_t lastUse = 0; // seconds, same clock as `now`
    uint64_t bytes = 0;
};

struct PrunePolicy
{
    uint64_t maxBytes = 0;     // 0 = no size cap
    int64_t maxAgeSeconds = 0; // 0 = no age limit
};

// Pure decision: the relative paths to delete. Obsolete trees always go; model
// folders go when older than the age limit, then least recently used first
// until everything kept fits the cap (Protected/Unknown bytes count toward the
// total but are never candidates).
std::vector<std::string> SelectPruneVictims(const std::vector<PruneEntry> &entries, int64_t now,
                                            const PrunePolicy &policy);

struct PruneResult
{
    size_t removed = 0;
    uint64_t freedBytes = 0;
    size_t keptModels = 0;
    uint64_t keptBytes = 0;
    std::vector<std::string> obsoleteTrees; // relative paths removed
    bool scanned = false;
};

// Scans `root`, decides with SelectPruneVictims and deletes. Every deletion is
// re-validated to lie strictly under `root` and not to be a symlink/junction.
// Never throws; errors leave the entry in place.
PruneResult PruneChunkCache(const std::filesystem::path &root, const PrunePolicy &policy);

// Records "used now" on a per-model folder by setting its last_write_time,
// at most once per folder per process. Errors are ignored.
void TouchCacheFolder(const std::filesystem::path &folder);
} // namespace BZROpenShim::NativeChunks
