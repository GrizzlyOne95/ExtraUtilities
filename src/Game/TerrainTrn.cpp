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

#include "GameObjectInternal.h"

#include "Util/AsciiString.h"

// Terrain TRN discovery and the [Atlases] MaterialName parser behind
// GetTerrainMaterialName and SetTerrainTextureSet.

namespace ExtraUtilities::Lua::GameObject
{
	namespace
	{
		struct TerrainTextureSlotBinding
		{
			const char* canonicalName = nullptr;
			int textureUnitIndex = 0;
			const char* aliasA = nullptr;
			const char* aliasB = nullptr;
			const char* aliasC = nullptr;
		};

		struct TerrainTextureSlotUpdate
		{
			const TerrainTextureSlotBinding* binding = nullptr;
			std::string textureName;
		};
		constexpr const char* kTerrainAtlasSectionName = "Atlases";
		constexpr const char* kTerrainAtlasMaterialKey = "MaterialName";

		const TerrainTextureSlotBinding kTerrainTextureSlots[] = {
			{ "diffuse", 0, "diffuseMap", "DiffuseMap", "base" },
			{ "detail", 1, "detailMap", "DetailMap", nullptr },
			{ "normal", 2, "normalMap", "NormalMap", nullptr },
			{ "specular", 3, "specularMap", "SpecularMap", nullptr },
			{ "emissive", 4, "emissiveMap", "EmissiveMap", nullptr },
		};

		bool IsSpaceChar(unsigned char ch)
		{
			return std::isspace(ch) != 0;
		}

		std::string TrimAscii(std::string value)
		{
			const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return IsSpaceChar(ch); });
			if (begin == value.end())
			{
				return {};
			}

			const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return IsSpaceChar(ch); }).base();
			return std::string(begin, end);
		}

		using AsciiString::EqualsIgnoreCase;
		using AsciiString::ToLowerAscii;

		std::string StripMatchingQuotes(std::string value)
		{
			if (value.size() < 2)
			{
				return value;
			}

			const char first = value.front();
			const char last = value.back();
			if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
			{
				return value.substr(1, value.size() - 2);
			}

			return value;
		}

		std::string NormalizePathKey(const std::filesystem::path& path)
		{
			return ToLowerAscii(path.lexically_normal().generic_string());
		}

		bool IsRegularFile(const std::filesystem::path& path)
		{
			std::error_code error;
			return std::filesystem::is_regular_file(path, error);
		}

		void AppendUniquePath(
			std::vector<std::filesystem::path>& paths,
			std::unordered_set<std::string>& seen,
			const std::filesystem::path& path)
		{
			if (path.empty())
			{
				return;
			}

			const std::string normalized = NormalizePathKey(path);
			if (!seen.insert(normalized).second)
			{
				return;
			}

			paths.push_back(path);
		}

		std::filesystem::path GetMainModuleDirectory()
		{
			char modulePath[MAX_PATH] = {};
			const DWORD length = GetModuleFileNameA(nullptr, modulePath, static_cast<DWORD>(std::size(modulePath)));
			if (length == 0 || length >= std::size(modulePath))
			{
				return {};
			}

			return std::filesystem::path(modulePath).parent_path();
		}

		bool TryCallLuaStringFunction(lua_State* L, const char* functionName, std::string& outValue)
		{
			StackGuard guard(L);
			outValue.clear();

			lua_getglobal(L, functionName);
			if (!lua_isfunction(L, -1))
			{
				lua_pop(L, 1);
				return false;
			}

			if (lua_pcall(L, 0, 1, 0) != 0)
			{
				const char* error = lua_tostring(L, -1);
				LogMaterialDebug(
					"[EXU::Terrain] %s() failed: %s",
					functionName,
					error == nullptr ? "unknown lua error" : error);
				return false;
			}

			if (!lua_isstring(L, -1))
			{
				return false;
			}

			outValue = lua_tostring(L, -1);
			return !outValue.empty();
		}

		bool TryGetCurrentTerrainFilename(lua_State* L, std::string& outFilename)
		{
			return TryCallLuaStringFunction(L, "GetMapTRNFilename", outFilename);
		}

		bool TryFindFileRecursive(
			const std::filesystem::path& root,
			const std::filesystem::path& filename,
			std::filesystem::path& outPath)
		{
			outPath.clear();
			if (root.empty() || filename.empty() || !std::filesystem::exists(root))
			{
				return false;
			}

			std::error_code error;
			for (std::filesystem::recursive_directory_iterator it(
					root,
					std::filesystem::directory_options::skip_permission_denied,
					error), end;
				it != end;
				it.increment(error))
			{
				if (error)
				{
					error.clear();
					continue;
				}

				if (!it->is_regular_file(error))
				{
					error.clear();
					continue;
				}

				if (EqualsIgnoreCase(it->path().filename().string(), filename.string()))
				{
					outPath = it->path();
					return true;
				}
			}

			return false;
		}

		bool TryResolveTerrainDefinitionPath(const std::string& trnFilename, std::filesystem::path& outPath)
		{
			outPath.clear();
			if (trnFilename.empty())
			{
				return false;
			}

			std::filesystem::path requestedPath = std::filesystem::path(trnFilename);
			if (requestedPath.is_absolute() && IsRegularFile(requestedPath))
			{
				outPath = requestedPath;
				return true;
			}

			if (!requestedPath.has_extension())
			{
				requestedPath += ".trn";
			}

			const std::filesystem::path gameRoot = GetMainModuleDirectory();
			std::vector<std::filesystem::path> candidates;
			std::unordered_set<std::string> seen;

			AppendUniquePath(candidates, seen, gameRoot / requestedPath);
			AppendUniquePath(candidates, seen, gameRoot / "Missions" / requestedPath);
			AppendUniquePath(candidates, seen, gameRoot / "addon" / requestedPath);
			AppendUniquePath(candidates, seen, gameRoot / "addon" / "Missions" / requestedPath);
			AppendUniquePath(candidates, seen, gameRoot / "Workshop" / requestedPath);

			const std::filesystem::path filenameOnly = requestedPath.filename();
			if (!filenameOnly.empty())
			{
				AppendUniquePath(candidates, seen, gameRoot / "Missions" / filenameOnly);
				AppendUniquePath(candidates, seen, gameRoot / "addon" / "Missions" / filenameOnly);
			}

			for (const std::filesystem::path& candidate : candidates)
			{
				if (IsRegularFile(candidate))
				{
					outPath = candidate;
					return true;
				}
			}

			if (!filenameOnly.empty())
			{
				if (TryFindFileRecursive(gameRoot / "Missions", filenameOnly, outPath)
					|| TryFindFileRecursive(gameRoot / "addon", filenameOnly, outPath)
					|| TryFindFileRecursive(gameRoot / "Workshop", filenameOnly, outPath))
				{
					return true;
				}
			}

			return false;
		}

		bool TryReadTerrainMaterialNameFromFile(const std::filesystem::path& trnPath, std::string& outMaterialName)
		{
			outMaterialName.clear();

			std::ifstream input(trnPath);
			if (!input)
			{
				return false;
			}

			std::string line;
			std::string currentSection;
			while (std::getline(input, line))
			{
				if (!line.empty() && line.back() == '\r')
				{
					line.pop_back();
				}

				const size_t commentPos = line.find(';');
				if (commentPos != std::string::npos)
				{
					line.erase(commentPos);
				}

				std::string trimmed = TrimAscii(line);
				if (trimmed.empty())
				{
					continue;
				}

				if (trimmed.front() == '[' && trimmed.back() == ']')
				{
					currentSection = TrimAscii(trimmed.substr(1, trimmed.size() - 2));
					continue;
				}

				if (!EqualsIgnoreCase(currentSection, kTerrainAtlasSectionName))
				{
					continue;
				}

				const size_t equalsPos = trimmed.find('=');
				if (equalsPos == std::string::npos)
				{
					continue;
				}

				const std::string key = TrimAscii(trimmed.substr(0, equalsPos));
				if (!EqualsIgnoreCase(key, kTerrainAtlasMaterialKey))
				{
					continue;
				}

				outMaterialName = StripMatchingQuotes(TrimAscii(trimmed.substr(equalsPos + 1)));
				return !outMaterialName.empty();
			}

			return false;
		}

		bool TryResolveTerrainMaterialName(
			lua_State* L,
			const std::string& explicitTrnFilename,
			std::string& outMaterialName,
			std::filesystem::path* outTrnPath = nullptr)
		{
			outMaterialName.clear();
			if (outTrnPath != nullptr)
			{
				outTrnPath->clear();
			}

			std::string trnFilename = explicitTrnFilename;
			if (trnFilename.empty() && !TryGetCurrentTerrainFilename(L, trnFilename))
			{
				LogMaterialDebug("[EXU::Terrain] Failed to resolve current map TRN filename");
				return false;
			}

			std::filesystem::path trnPath;
			if (!TryResolveTerrainDefinitionPath(trnFilename, trnPath))
			{
				LogMaterialDebug("[EXU::Terrain] Failed to resolve TRN path for %s", trnFilename.c_str());
				return false;
			}

			if (!TryReadTerrainMaterialNameFromFile(trnPath, outMaterialName))
			{
				LogMaterialDebug(
					"[EXU::Terrain] Failed to read [%s] %s from %s",
					kTerrainAtlasSectionName,
					kTerrainAtlasMaterialKey,
					trnPath.string().c_str());
				return false;
			}

			if (outTrnPath != nullptr)
			{
				*outTrnPath = trnPath;
			}

			return true;
		}

		bool TryReadOptionalStringField(lua_State* L, int tableIndex, const char* fieldName, std::string& outValue)
		{
			const int absIndex = AbsoluteStackIndex(L, tableIndex);
			lua_getfield(L, absIndex, fieldName);
			const int valueType = lua_type(L, -1);
			if (valueType == LUA_TNIL)
			{
				lua_pop(L, 1);
				return false;
			}

			if (valueType != LUA_TSTRING)
			{
				luaL_error(L, "Extra Utilities: field '%s' must be a string", fieldName);
			}

			outValue = lua_tostring(L, -1);
			lua_pop(L, 1);
			return true;
		}

		bool TryReadOptionalIntegerField(lua_State* L, int tableIndex, const char* fieldName, int& outValue)
		{
			const int absIndex = AbsoluteStackIndex(L, tableIndex);
			lua_getfield(L, absIndex, fieldName);
			const int valueType = lua_type(L, -1);
			if (valueType == LUA_TNIL)
			{
				lua_pop(L, 1);
				return false;
			}

			if (valueType != LUA_TNUMBER)
			{
				luaL_error(L, "Extra Utilities: field '%s' must be a number", fieldName);
			}

			outValue = static_cast<int>(lua_tointeger(L, -1));
			lua_pop(L, 1);
			return true;
		}

		bool TryReadTerrainTextureField(
			lua_State* L,
			int tableIndex,
			const TerrainTextureSlotBinding& binding,
			std::string& outTextureName)
		{
			if (TryReadOptionalStringField(L, tableIndex, binding.canonicalName, outTextureName))
			{
				return true;
			}

			if (binding.aliasA != nullptr && TryReadOptionalStringField(L, tableIndex, binding.aliasA, outTextureName))
			{
				return true;
			}

			if (binding.aliasB != nullptr && TryReadOptionalStringField(L, tableIndex, binding.aliasB, outTextureName))
			{
				return true;
			}

			if (binding.aliasC != nullptr && TryReadOptionalStringField(L, tableIndex, binding.aliasC, outTextureName))
			{
				return true;
			}

			return false;
		}
	}

	int GetTerrainMaterialName(lua_State* L)
	{
		std::string trnFilename;
		if (!lua_isnoneornil(L, 1))
		{
			trnFilename = luaL_checkstring(L, 1);
		}

		std::string materialName;
		if (!TryResolveTerrainMaterialName(L, trnFilename, materialName))
		{
			lua_pushnil(L);
			return 1;
		}

		lua_pushstring(L, materialName.c_str());
		return 1;
	}

	int SetTerrainTextureSet(lua_State* L)
	{
		luaL_checktype(L, 1, LUA_TTABLE);

		std::string materialName;
		TryReadOptionalStringField(L, 1, "material", materialName)
			|| TryReadOptionalStringField(L, 1, "materialName", materialName);

		if (materialName.empty())
		{
			std::string trnFilename;
			TryReadOptionalStringField(L, 1, "trn", trnFilename)
				|| TryReadOptionalStringField(L, 1, "trnFilename", trnFilename);

			if (!TryResolveTerrainMaterialName(L, trnFilename, materialName))
			{
				lua_pushboolean(L, 0);
				lua_pushnil(L);
				return 2;
			}
		}

		std::string resourceGroup = "General";
		TryReadOptionalStringField(L, 1, "resourceGroup", resourceGroup);

		int techniqueIndex = 0;
		if (!TryReadOptionalIntegerField(L, 1, "techniqueIndex", techniqueIndex))
		{
			TryReadOptionalIntegerField(L, 1, "technique", techniqueIndex);
		}

		int passIndex = 0;
		if (!TryReadOptionalIntegerField(L, 1, "passIndex", passIndex))
		{
			TryReadOptionalIntegerField(L, 1, "pass", passIndex);
		}

		std::vector<TerrainTextureSlotUpdate> updates;
		updates.reserve(std::size(kTerrainTextureSlots));

		for (const TerrainTextureSlotBinding& binding : kTerrainTextureSlots)
		{
			std::string textureName;
			if (!TryReadTerrainTextureField(L, 1, binding, textureName))
			{
				continue;
			}

			updates.push_back({ &binding, textureName });
		}

		if (updates.empty())
		{
			LogMaterialDebug("[EXU::Terrain] SetTerrainTextureSet had no texture fields material=%s", materialName.c_str());
			lua_pushboolean(L, 0);
			lua_pushstring(L, materialName.c_str());
			return 2;
		}

		bool success = true;
		for (const TerrainTextureSlotUpdate& update : updates)
		{
			MaterialTextureUnitHandle handle;
			if (!TryResolveMaterialTextureUnit(
				materialName,
				resourceGroup,
				techniqueIndex,
				passIndex,
				update.binding->textureUnitIndex,
				handle))
			{
				LogMaterialDebug(
					"[EXU::Terrain] Failed to resolve terrain texture slot material=%s slot=%s group=%s technique=%d pass=%d unit=%d",
					materialName.c_str(),
					update.binding->canonicalName,
					resourceGroup.c_str(),
					techniqueIndex,
					passIndex,
					update.binding->textureUnitIndex);
				success = false;
				continue;
			}

			if (!TrySetMaterialTextureName(handle.textureUnit, update.textureName))
			{
				LogMaterialDebug(
					"[EXU::Terrain] Failed to set terrain texture slot material=%s slot=%s texture=%s",
					materialName.c_str(),
					update.binding->canonicalName,
					update.textureName.c_str());
				success = false;
			}
		}

		lua_pushboolean(L, success ? 1 : 0);
		lua_pushstring(L, materialName.c_str());
		return 2;
	}
}
