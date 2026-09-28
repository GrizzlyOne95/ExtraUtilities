/* Copyright (C) 2023-2026 VTrider
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

#include "OverlayInternal.h"

// Runtime overlay resources: EXU's resource groups and the runtime font (font
// script, then TrueType, then the sprite-table image fallback), and the
// filesystem search that finds their assets next to the game and in mods.

namespace ExtraUtilities::Lua::Overlay
{
	namespace
	{
		constexpr const char* kOverlayRuntimeResourceGroup = "EXUOverlayRuntime";
		constexpr const char* kOverlayRuntimeFontResourceGroup = "EXUOverlayFontRuntime";
		constexpr const char* kOverlayRuntimeFontName = "CRBZoneOverlayFont";
		constexpr const char* kOverlayRuntimeFontScript = "CRBZoneOverlay.fontdef";
		constexpr const char* kOverlayRuntimeTrueTypeSource = "BZONE.ttf";
		constexpr float kOverlayRuntimeTrueTypeSize = 32.0f;
		constexpr unsigned int kOverlayRuntimeTrueTypeResolution = 96u;
		constexpr unsigned int kOverlayRuntimeFirstCodePoint = 32u;
		constexpr unsigned int kOverlayRuntimeLastCodePoint = 126u;
		constexpr const char* kOverlayRuntimeFontSource = "bzfont.dds";
		constexpr const char* kOverlayRuntimeFontSpriteTable = "Edit\\stock\\bzfont.st";
		constexpr const char* kBattlezoneWorkshopAppId = "301650";

		std::string GetDirectoryForModule(HMODULE module)
		{
			if (module == nullptr)
			{
				return {};
			}

			std::array<char, MAX_PATH> path{};
			const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
			if (length == 0 || length >= path.size())
			{
				return {};
			}

			std::string result(path.data(), length);
			const auto slash = result.find_last_of("\\/");
			if (slash == std::string::npos)
			{
				return {};
			}

			result.resize(slash);
			return result;
		}

		std::string GetCurrentModuleDirectory()
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExA(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(&GetCurrentModuleDirectory),
				&module) || module == nullptr)
			{
				return {};
			}

			return GetDirectoryForModule(module);
		}

		std::string GetCurrentGameRootDirectory()
		{
			return GetDirectoryForModule(GetModuleHandleA(nullptr));
		}

		bool IsRegularFile(const std::filesystem::path& path)
		{
			std::error_code error;
			return std::filesystem::is_regular_file(path, error);
		}

		bool IsDirectory(const std::filesystem::path& path)
		{
			std::error_code error;
			return std::filesystem::is_directory(path, error);
		}

		void AppendUniquePath(std::vector<std::filesystem::path>& paths, std::unordered_set<std::string>& seen, const std::filesystem::path& path)
		{
			if (path.empty())
			{
				return;
			}

			std::error_code error;
			const std::filesystem::path normalized = path.lexically_normal();
			const std::string key = normalized.string();
			if (key.empty() || !seen.insert(key).second)
			{
				return;
			}

			paths.push_back(normalized);
		}

		bool ContainsOverlayRuntimeFontAsset(const std::filesystem::path& directory)
		{
			if (!IsDirectory(directory))
			{
				return false;
			}

			return IsRegularFile(directory / kOverlayRuntimeFontScript)
				|| IsRegularFile(directory / kOverlayRuntimeTrueTypeSource);
		}

		void AppendOverlayFontCandidatesForBase(
			const std::filesystem::path& base,
			std::vector<std::filesystem::path>& candidates,
			std::unordered_set<std::string>& seen)
		{
			if (base.empty())
			{
				return;
			}

			AppendUniquePath(candidates, seen, base / "OverlayFont");
			AppendUniquePath(candidates, seen, base);
		}

		void AppendNearbyOverlayFontCandidates(
			const std::filesystem::path& start,
			size_t maxAncestorDepth,
			std::vector<std::filesystem::path>& candidates,
			std::unordered_set<std::string>& seen)
		{
			std::filesystem::path current = start;
			for (size_t depth = 0; !current.empty() && depth <= maxAncestorDepth; ++depth)
			{
				AppendOverlayFontCandidatesForBase(current, candidates, seen);

				const std::filesystem::path parent = current.parent_path();
				if (parent.empty() || parent == current)
				{
					break;
				}

				current = parent;
			}
		}

		void AppendOverlayFontCandidatesUnder(
			const std::filesystem::path& root,
			size_t maxDepth,
			std::vector<std::filesystem::path>& candidates,
			std::unordered_set<std::string>& seen)
		{
			if (!IsDirectory(root))
			{
				return;
			}

			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, error), end;
				it != end;
				it.increment(error))
			{
				if (error)
				{
					error.clear();
					continue;
				}

				if (it.depth() > static_cast<int>(maxDepth))
				{
					it.disable_recursion_pending();
					continue;
				}

				if (!it->is_directory(error))
				{
					error.clear();
					continue;
				}

				AppendUniquePath(candidates, seen, it->path());
			}
		}

		std::filesystem::path GetWorkshopContentDirectory(const std::filesystem::path& gameRootDirectory)
		{
			if (gameRootDirectory.empty())
			{
				return {};
			}

			const std::filesystem::path commonDirectory = gameRootDirectory.parent_path();
			if (commonDirectory.empty())
			{
				return {};
			}

			const std::filesystem::path steamappsDirectory = commonDirectory.parent_path();
			if (steamappsDirectory.empty())
			{
				return {};
			}

			return steamappsDirectory / "workshop" / "content" / kBattlezoneWorkshopAppId;
		}

		void EnsureOverlayRuntimeResources()
		{
			if (overlayRuntimeResourcesReady || overlayRuntimeResourcesAttempted)
			{
				return;
			}

			overlayRuntimeResourcesAttempted = true;
			EnsureOverlaySupport();

			const std::string moduleDirectory = GetCurrentModuleDirectory();
			if (moduleDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime resources failed to resolve EXU module directory");
				return;
			}

			const std::string gameRootDirectory = GetCurrentGameRootDirectory();
			if (gameRootDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime resources failed to resolve game root directory");
				return;
			}

			const std::filesystem::path moduleDirectoryPath(moduleDirectory);
			const std::filesystem::path gameRootDirectoryPath(gameRootDirectory);
			std::vector<std::filesystem::path> fontDirectories;
			std::unordered_set<std::string> seenFontDirectories;
			std::vector<std::string> addedFontDirectories;

			AppendNearbyOverlayFontCandidates(moduleDirectoryPath, 5, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesForBase(gameRootDirectoryPath, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesUnder(gameRootDirectoryPath / "addon", 2, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesUnder(gameRootDirectoryPath / "mods", 2, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesUnder(gameRootDirectoryPath / "packaged_mods", 2, fontDirectories, seenFontDirectories);
			AppendOverlayFontCandidatesUnder(GetWorkshopContentDirectory(gameRootDirectoryPath), 3, fontDirectories, seenFontDirectories);

			bool addedAnyFontLocation = false;
			for (const std::filesystem::path& fontDirectory : fontDirectories)
			{
				if (!ContainsOverlayRuntimeFontAsset(fontDirectory))
				{
					continue;
				}

				const std::string fontDirectoryString = fontDirectory.string();
				const std::filesystem::path fontScriptPath = fontDirectory / kOverlayRuntimeFontScript;
				if (IsRegularFile(fontScriptPath))
				{
					const bool currentIsOverlayFont = !overlayRuntimeFontScriptPath.empty()
						&& std::filesystem::path(overlayRuntimeFontScriptPath).parent_path().filename() == "OverlayFont";
					const bool candidateIsOverlayFont = fontDirectory.filename() == "OverlayFont";
					if (overlayRuntimeFontScriptPath.empty() || (candidateIsOverlayFont && !currentIsOverlayFont))
					{
						overlayRuntimeFontScriptPath = fontScriptPath.string();
					}
				}
				if (Native::TryAddResourceLocation(fontDirectoryString.c_str(), kOverlayRuntimeResourceGroup))
				{
					addedAnyFontLocation = true;
					addedFontDirectories.push_back(fontDirectoryString);
				}
			}

			if (!addedAnyFontLocation)
			{
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime resources failed to locate any font directory module=%s gameRoot=%s",
					moduleDirectory.c_str(),
					gameRootDirectory.c_str());
				return;
			}

			const std::string stockTextureDirectory = gameRootDirectory + "\\BZ_ASSETS\\pc\\textures\\MISC_DDS";
			if (!Native::TryAddResourceLocation(stockTextureDirectory.c_str(), kOverlayRuntimeResourceGroup))
			{
				return;
			}

			overlayRuntimeResourcesReady = true;
			Logging::LogMessage(
				"[EXU::Overlay] overlay runtime resources ready group=%s moduleDir=%s gameRoot=%s stockTextures=%s fontLocationCount=%zu",
				kOverlayRuntimeResourceGroup,
				moduleDirectory.c_str(),
				gameRootDirectory.c_str(),
				stockTextureDirectory.c_str(),
				addedFontDirectories.size());
			for (const std::string& fontDirectory : addedFontDirectories)
			{
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font location group=%s path=%s",
					kOverlayRuntimeResourceGroup,
					fontDirectory.c_str());
			}
		}
	}

	namespace Detail
	{
		bool overlayRuntimeResourcesReady = false;
		bool overlayRuntimeResourcesAttempted = false;
		bool overlayRuntimeFontReady = false;
		bool overlayRuntimeFontAttempted = false;
		std::string overlayRuntimeFontScriptPath;

		void EnsureOverlayRuntimeFont()
		{
			if (overlayRuntimeFontReady || overlayRuntimeFontAttempted)
			{
				return;
			}

			overlayRuntimeFontAttempted = true;
			EnsureOverlayRuntimeResources();
			if (!overlayRuntimeResourcesReady)
			{
				return;
			}

			const std::string gameRootDirectory = GetCurrentGameRootDirectory();
			if (gameRootDirectory.empty())
			{
				Logging::LogMessage("[EXU::Overlay] overlay runtime font failed to resolve game root directory");
				return;
			}

			if (Native::TryHasFontResource(kOverlayRuntimeFontName, kOverlayRuntimeResourceGroup))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s mode=existing",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup);
				return;
			}

			if (!Native::TryResetFontResourceGroupIfStale(kOverlayRuntimeFontName, kOverlayRuntimeFontResourceGroup))
			{
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font failed to prepare resource group name=%s group=%s",
					kOverlayRuntimeFontName,
					kOverlayRuntimeFontResourceGroup);
				return;
			}

			if (!overlayRuntimeFontScriptPath.empty())
			{
				const std::filesystem::path fontScriptPath(overlayRuntimeFontScriptPath);
				const std::string fontScriptDirectory = fontScriptPath.parent_path().string();
				const std::string stockTextureDirectory = gameRootDirectory + "\\BZ_ASSETS\\pc\\textures\\MISC_DDS";
				if (!fontScriptDirectory.empty()
					&& Native::TryAddResourceLocation(fontScriptDirectory.c_str(), kOverlayRuntimeFontResourceGroup)
					&& Native::TryAddResourceLocation(stockTextureDirectory.c_str(), kOverlayRuntimeFontResourceGroup)
					&& Native::TryParseFontScript(kOverlayRuntimeFontScript, kOverlayRuntimeFontResourceGroup)
					&& Native::TryHasFontResource(kOverlayRuntimeFontName, kOverlayRuntimeFontResourceGroup))
				{
					overlayRuntimeFontReady = true;
					Logging::LogMessage(
						"[EXU::Overlay] overlay runtime font ready name=%s group=%s script=%s mode=script",
						kOverlayRuntimeFontName,
						kOverlayRuntimeFontResourceGroup,
						overlayRuntimeFontScriptPath.c_str());
					return;
				}
			}

			if (Native::TryEnsureTrueTypeFont(
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeTrueTypeSource,
				kOverlayRuntimeTrueTypeSize,
				kOverlayRuntimeTrueTypeResolution,
				kOverlayRuntimeFirstCodePoint,
				kOverlayRuntimeLastCodePoint))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s source=%s mode=truetype size=%.1f resolution=%u range=%u-%u",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup,
					kOverlayRuntimeTrueTypeSource,
					kOverlayRuntimeTrueTypeSize,
					kOverlayRuntimeTrueTypeResolution,
					kOverlayRuntimeFirstCodePoint,
					kOverlayRuntimeLastCodePoint);
				return;
			}

			const std::string spriteTablePath = gameRootDirectory + "\\" + kOverlayRuntimeFontSpriteTable;
			if (Native::TryEnsureImageFontFromSpriteTable(
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeFontSource,
				spriteTablePath.c_str()))
			{
				overlayRuntimeFontReady = true;
				Logging::LogMessage(
					"[EXU::Overlay] overlay runtime font ready name=%s group=%s source=%s spriteTable=%s mode=image-fallback",
					kOverlayRuntimeFontName,
					kOverlayRuntimeResourceGroup,
					kOverlayRuntimeFontSource,
					spriteTablePath.c_str());
				return;
			}

			Logging::LogMessage(
				"[EXU::Overlay] overlay runtime font unavailable name=%s group=%s truetype=%s image=%s spriteTable=%s",
				kOverlayRuntimeFontName,
				kOverlayRuntimeResourceGroup,
				kOverlayRuntimeTrueTypeSource,
				kOverlayRuntimeFontSource,
				spriteTablePath.c_str());
		}
	}
}
