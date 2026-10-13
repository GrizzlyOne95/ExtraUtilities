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

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Gibs/GibPayloads.h"

#include "Gibs/NativeChunks/gib_flesh_texture.h"
#include "Gibs/NativeChunks/native_chunk_cache.h"
#include "Gibs/NativeChunks/native_chunk_mesh.h"
#include "Gibs/NativeChunks/native_chunk_prune.h"
#include "Ogre/OgreGibRuntime.h"
#include "Util/Logging.h"
#include "Util/SehGuard.h"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace ExtraUtilities::Gibs::Payloads
{
	namespace
	{
		namespace NC = BZROpenShim::NativeChunks;
		namespace Rt = ExtraUtilities::OgreGibRuntime;

		constexpr const char* kGibFolder = OPENSHIM_CHUNK_CACHE_GIBS_DIR;   // "gibs/v6/"

		struct Entry
		{
			bool ready = false;
			std::shared_ptr<const ModelInfo> info;
		};

		std::map<std::string, Entry>& Models()
		{
			static std::map<std::string, Entry> models;
			return models;
		}

		bool g_groupReady = false;

		std::string Lower(std::string s)
		{
			for (char& c : s)
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return s;
		}

		// "Meshes/Foo.mesh" -> "foo"
		std::string BaseName(std::string s)
		{
			size_t p = s.find_last_of("/\\");
			if (p != std::string::npos)
			{
				s.erase(0, p + 1);
			}
			p = s.find_last_of('.');
			if (p != std::string::npos)
			{
				s.resize(p);
			}
			return Lower(s);
		}

		std::filesystem::path CacheRoot()
		{
			const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
			if (required == 0)
			{
				return {};
			}
			std::wstring buffer(static_cast<size_t>(required), L'\0');
			const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), required);
			if (written == 0 || written >= required)
			{
				return {};
			}
			buffer.resize(written);
			return std::filesystem::path(buffer) / L"ExtraUtilities" / L"cache";
		}

		// The pieces are keyed by what they were cut from: two mods reusing a
		// basename never share a folder.
		std::string ContentHash(const std::vector<uint8_t>& mesh, const std::vector<uint8_t>& skeleton)
		{
			uint64_t hash = 14695981039346656037ull;
			for (const std::vector<uint8_t>* source : { &mesh, &skeleton })
			{
				for (const uint8_t c : *source)
				{
					hash ^= c;
					hash *= 1099511628211ull;
				}
			}
			std::ostringstream hex;
			hex << std::hex << hash;
			return hex.str();
		}

		std::shared_ptr<const ModelInfo> PrepareImpl(const std::string& meshName, const std::string& group)
		{
			const auto started = std::chrono::steady_clock::now();
			std::vector<uint8_t> bytes;
			std::vector<uint8_t> skeleton;
			if (!Rt::ReadResource(meshName, group, bytes))
			{
				Logging::LogMessage("[EXU::Gibs] unsupported mesh=%s reason=mesh unreadable", meshName.c_str());
				return nullptr;
			}
			const std::string skeletonName = NC::SkeletonName(bytes);
			if (skeletonName.empty() || !Rt::ReadResource(skeletonName, group, skeleton))
			{
				Logging::LogMessage("[EXU::Gibs] unsupported mesh=%s reason=no skeleton", meshName.c_str());
				return nullptr;
			}

			const std::filesystem::path root = CacheRoot();
			if (root.empty())
			{
				Logging::LogMessage("[EXU::Gibs] unsupported mesh=%s reason=LOCALAPPDATA unavailable", meshName.c_str());
				return nullptr;
			}
			const std::string folder = std::string(kGibFolder) + ContentHash(bytes, skeleton);
			const std::filesystem::path directory = root / folder;

			std::vector<NC::CachedGib> cached;
			const bool reused = NC::ReadGibCache(directory, cached);
			if (!reused)
			{
				std::vector<NC::GibPiece> gibs;
				std::string error;
				if (!NC::ExtractGibs(bytes, skeleton, NC::GibOptions{}, gibs, error))
				{
					Logging::LogMessage("[EXU::Gibs] unsupported mesh=%s reason=%s", meshName.c_str(), error.c_str());
					return nullptr;
				}
				if (!NC::WriteGibCache(directory, gibs, cached))
				{
					Logging::LogMessage("[EXU::Gibs] cache write failed mesh=%s dir=%s", meshName.c_str(),
						directory.string().c_str());
					return nullptr;
				}
			}

			auto info = std::make_shared<ModelInfo>();
			info->source = directory.string();
			uint32_t triangles = 0;
			for (const NC::CachedGib& gib : cached)
			{
				PieceInfo piece;
				piece.resource = folder + "/" + gib.piece.name + ".mesh";
				piece.boneName = gib.boneName;
				piece.bone = gib.bone;
				std::copy(gib.piece.center, gib.piece.center + 3, piece.offset);
				piece.radius = gib.radius;
				piece.weapon = gib.weapon;
				triangles += gib.piece.triangles;
				info->pieces.push_back(std::move(piece));
			}
			if (info->pieces.empty())
			{
				return nullptr;
			}
			NC::TouchCacheFolder(directory);
			Logging::LogMessage("[EXU::Gibs] %s mesh=%s group=%s pieces=%u triangles=%u cache=%s prepareMs=%.3f",
				reused ? "reused" : "generated", meshName.c_str(), group.c_str(),
				static_cast<unsigned>(info->pieces.size()), triangles, info->source.c_str(),
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
			return info;
		}

		// ----- Default flesh materials -------------------------------------------
		// OpenShim's EnsureSkinnedGibFleshMaterial, with "a pack ships the same
		// file name" meaning a resource of that name already registered in
		// another Ogre group.
		void EnsureFleshMaterials(const std::filesystem::path& cacheRoot)
		{
			try
			{
				std::error_code ec;
				const auto readHead = [](const std::filesystem::path& path) {
					std::vector<uint8_t> head(64);
					std::ifstream in(path, std::ios::binary);
					in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
					head.resize(static_cast<size_t>(in.gcount()));
					return head;
				};
				// Generated material files, each with the same rules. A pack
				// overrides one by shipping the same file name; ours then steps
				// aside (and is removed if an earlier run wrote it), because both
				// would be a duplicate definition. A file in the cache root
				// without our marker is the user's: untouched. Ours is rewritten
				// whenever its version line is stale, so a changed default never
				// lingers behind an old "do not edit" copy. Returns true when the
				// file is overridden or foreign.
				const auto ensureFile = [&](const char* file, const char* marker, const std::string& header,
											const std::string& script, bool& wrote) {
					wrote = false;
					const std::filesystem::path ours = cacheRoot / file;
					if (Rt::ResourceExistsInAnyGroup(file))
					{
						std::ifstream existing(ours);
						std::string first;
						if (existing && std::getline(existing, first) && first.rfind(marker, 0) == 0)
						{
							existing.close();
							std::filesystem::remove(ours, ec);
						}
						Logging::LogMessage("[EXU::Gibs] flesh material %s overridden by another resource group", file);
						return true;
					}
					bool current = false;
					if (std::filesystem::exists(ours, ec))
					{
						std::ifstream existing(ours);
						std::string first;
						std::getline(existing, first);
						while (!first.empty() && (first.back() == '\r' || first.back() == '\n'))
						{
							first.pop_back();
						}
						if (first.rfind(marker, 0) != 0)
						{
							return true;
						}
						current = first == header;
					}
					ec.clear();
					if (current)
					{
						return false;
					}
					std::filesystem::create_directories(cacheRoot, ec);
					std::ofstream output(ours, std::ios::binary | std::ios::trunc);
					output << script;
					output.close();
					wrote = true;
					Logging::LogMessage("[EXU::Gibs] wrote default flesh material %s ok=%u", ours.string().c_str(),
						output ? 1u : 0u);
					return false;
				};
				bool wroteMain = false;
				bool wroteZones = false;
				const bool mainForeign = ensureFile(NC::kGibFleshMaterialFile, NC::kGibFleshMaterialMarker,
					NC::GibFleshMaterialHeader(), NC::GibFleshMaterialScript(), wroteMain);
				const bool zonesForeign = ensureFile(NC::kGibFleshZoneMaterialFile, NC::kGibFleshZoneMaterialMarker,
					NC::GibFleshZoneMaterialHeader(), NC::GibFleshZoneMaterialScript(), wroteZones);
				// The textures belong to our materials: written when missing or
				// stale, removed (if ours) when a pack or the user supplies the
				// material that uses them.
				for (const NC::GibFleshTexture& image : NC::GibFleshTextures())
				{
					const std::filesystem::path path = cacheRoot / image.file;
					const std::vector<uint8_t> head = readHead(path);
					if (image.zone ? zonesForeign : mainForeign)
					{
						if (NC::IsGeneratedGibFleshTga(head))
						{
							std::filesystem::remove(path, ec);
						}
					}
					else if (!NC::IsCurrentGibFleshTga(head))
					{
						std::ofstream out(path, std::ios::binary | std::ios::trunc);
						out.write(reinterpret_cast<const char*>(image.tga.data()), static_cast<std::streamsize>(image.tga.size()));
						out.close();
						Logging::LogMessage("[EXU::Gibs] wrote flesh texture %s ok=%u", path.string().c_str(), out ? 1u : 0u);
					}
				}
			}
			catch (...)
			{
				// Optional: a missing material renders as BaseWhite.
			}
		}
	}

	std::shared_ptr<const ModelInfo> Prepare(const char* meshName, const char* group) noexcept
	{
		if (meshName == nullptr || meshName[0] == '\0')
		{
			return nullptr;
		}
		const std::string key = BaseName(meshName);
		if (key.empty())
		{
			return nullptr;
		}
		auto& models = Models();
		const auto found = models.find(key);
		if (found != models.end())
		{
			return found->second.ready ? found->second.info : nullptr;
		}
		// Any failure below is remembered for the mission: no retry per death.
		Entry& entry = models[key];
		std::shared_ptr<const ModelInfo> info;
		const std::string mesh = meshName;
		const std::string groupName = group != nullptr ? group : "";
		Seh::Guard("Gibs::Prepare", [&] { info = PrepareImpl(mesh, groupName); },
			[&](unsigned long code) {
				Logging::LogMessage("[EXU::Gibs] preparing mesh=%s faulted code=0x%08lX", mesh.c_str(), code);
			});
		entry.ready = info != nullptr;
		entry.info = info;
		return info;
	}

	void ResetModels() noexcept
	{
		Models().clear();
	}

	size_t ModelCount() noexcept
	{
		size_t ready = 0;
		for (const auto& model : Models())
		{
			ready += model.second.ready ? 1 : 0;
		}
		return ready;
	}

	bool EnsureResourceGroup() noexcept
	{
		if (g_groupReady)
		{
			return true;
		}
		bool ok = false;
		Seh::Guard("Gibs::EnsureResourceGroup", [&] {
			if (Rt::ResourceGroupExists(kResourceGroup))
			{
				// Registered earlier in this process (the DLL is reloaded for
				// every mission; Ogre keeps the group).
				ok = true;
				return;
			}
			const std::filesystem::path root = CacheRoot();
			if (root.empty())
			{
				Logging::LogMessage("[EXU::Gibs] payload resources unavailable: LOCALAPPDATA is not set");
				return;
			}
			std::error_code ec;
			std::filesystem::create_directories(root, ec);
			if (ec)
			{
				Logging::LogMessage("[EXU::Gibs] payload resources unavailable: cannot create %s", root.string().c_str());
				return;
			}
			// Before initialiseResourceGroup, so the material scripts parse.
			EnsureFleshMaterials(root);
			bool created = false;
			ok = Rt::EnsureResourceGroup(kResourceGroup, root.string(), created);
			Logging::LogMessage("[EXU::Gibs] payload resources group=%s root=%s ready=%u", kResourceGroup,
				root.string().c_str(), ok ? 1u : 0u);
		});
		g_groupReady = ok;
		return ok;
	}
}
