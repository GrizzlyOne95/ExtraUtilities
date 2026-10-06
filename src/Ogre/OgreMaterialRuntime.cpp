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

#include "OgreMaterialRuntime.h"

#include "Util/Logging.h"
#include "Util/SehGuard.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <exception>
#include <unordered_map>

namespace ExtraUtilities::Lua::GameObject
{
	namespace
	{
		inline std::unordered_map<std::string, MaterialHandle> g_cachedMaterials;

		bool TryCloneMaterialCpp(
			const MaterialHandle& sourceMaterial,
			const std::string& cloneName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial,
			MaterialHandle* outShared = nullptr)
		{
			try
			{
				if (sourceMaterial.isNull())
				{
					outMaterial = nullptr;
					if (outShared != nullptr)
					{
						*outShared = {};
					}
					return false;
				}

				auto clone = ::Ogre::CloneMaterial(sourceMaterial.getPointer(), cloneName, false, resourceGroup);
				CacheMaterialHandle(cloneName, clone);
				outMaterial = clone.getPointer();
				if (outShared != nullptr)
				{
					*outShared = clone;
				}
				if (outMaterial == nullptr)
				{
					LogMaterialDebug("[EXU::Material] CloneMaterial returned null clone=%s group=%s", cloneName.c_str(), resourceGroup.c_str());
				}
				return outMaterial != nullptr;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug(
					"[EXU::Material] CloneMaterial threw clone=%s group=%s what=%s",
					cloneName.c_str(),
					resourceGroup.c_str(),
					ex.what());
				outMaterial = nullptr;
				if (outShared != nullptr)
				{
					*outShared = {};
				}
				return false;
			}
			catch (...)
			{
				LogMaterialDebug(
					"[EXU::Material] CloneMaterial threw clone=%s group=%s",
					cloneName.c_str(),
					resourceGroup.c_str());
				outMaterial = nullptr;
				if (outShared != nullptr)
				{
					*outShared = {};
				}
				return false;
			}
		}

		bool TryResolveMaterialPassCpp(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			MaterialPassHandle& outHandle)
		{
			try
			{
				outHandle = {};

				::Ogre::Material* material = nullptr;
				if (!TryResolveMaterialCpp(materialName, resourceGroup, material))
				{
					return false;
				}

				auto* technique = ::Ogre::GetMaterialTechnique(material, static_cast<unsigned short>(techniqueIndex));
				if (technique == nullptr)
				{
					LogMaterialDebug(
						"[EXU::Material] ResolveMaterialPass missing technique material=%s group=%s technique=%d",
						materialName.c_str(),
						resourceGroup.c_str(),
						techniqueIndex);
					return false;
				}

				if (passIndex < 0 || passIndex >= static_cast<int>(::Ogre::GetTechniqueNumPasses(technique)))
				{
					LogMaterialDebug(
						"[EXU::Material] ResolveMaterialPass bad pass material=%s group=%s technique=%d pass=%d",
						materialName.c_str(),
						resourceGroup.c_str(),
						techniqueIndex,
						passIndex);
					return false;
				}

				auto* pass = ::Ogre::GetTechniquePass(technique, static_cast<unsigned short>(passIndex));
				if (pass == nullptr)
				{
					LogMaterialDebug(
						"[EXU::Material] ResolveMaterialPass missing pass material=%s group=%s technique=%d pass=%d",
						materialName.c_str(),
						resourceGroup.c_str(),
						techniqueIndex,
						passIndex);
					return false;
				}

				outHandle.material = material;
				outHandle.technique = technique;
				outHandle.pass = pass;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterialPass threw material=%s technique=%d pass=%d what=%s",
					materialName.c_str(),
					techniqueIndex,
					passIndex,
					ex.what());
				outHandle = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterialPass threw material=%s technique=%d pass=%d",
					materialName.c_str(),
					techniqueIndex,
					passIndex);
				outHandle = {};
				return false;
			}
		}

		bool TryResolveMaterialTextureUnitCpp(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			int textureUnitIndex,
			MaterialTextureUnitHandle& outHandle)
		{
			try
			{
				outHandle = {};

				MaterialPassHandle passHandle{};
				if (!TryResolveMaterialPassCpp(materialName, resourceGroup, techniqueIndex, passIndex, passHandle))
				{
					return false;
				}

				if (textureUnitIndex < 0 ||
					textureUnitIndex >= static_cast<int>(::Ogre::GetPassNumTextureUnitStates(passHandle.pass)))
				{
					LogMaterialDebug(
						"[EXU::Material] ResolveMaterialTextureUnit bad texture unit material=%s group=%s technique=%d pass=%d unit=%d",
						materialName.c_str(),
						resourceGroup.c_str(),
						techniqueIndex,
						passIndex,
						textureUnitIndex);
					return false;
				}

				auto* textureUnit = ::Ogre::GetPassTextureUnitState(
					passHandle.pass,
					static_cast<unsigned short>(textureUnitIndex));
				if (textureUnit == nullptr)
				{
					LogMaterialDebug(
						"[EXU::Material] ResolveMaterialTextureUnit missing texture unit material=%s group=%s technique=%d pass=%d unit=%d",
						materialName.c_str(),
						resourceGroup.c_str(),
						techniqueIndex,
						passIndex,
						textureUnitIndex);
					return false;
				}

				outHandle.material = passHandle.material;
				outHandle.technique = passHandle.technique;
				outHandle.pass = passHandle.pass;
				outHandle.textureUnit = textureUnit;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterialTextureUnit threw material=%s technique=%d pass=%d unit=%d what=%s",
					materialName.c_str(),
					techniqueIndex,
					passIndex,
					textureUnitIndex,
					ex.what());
				outHandle = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterialTextureUnit threw material=%s technique=%d pass=%d unit=%d",
					materialName.c_str(),
					techniqueIndex,
					passIndex,
					textureUnitIndex);
				outHandle = {};
				return false;
			}
		}

		bool TryGetPassAmbientCpp(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			try
			{
				const auto* color = ::Ogre::GetPassAmbient(pass);
				if (color == nullptr)
				{
					outColor = {};
					return false;
				}

				outColor = *color;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] GetPassAmbient threw pass=%p what=%s", pass, ex.what());
				outColor = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] GetPassAmbient threw pass=%p", pass);
				outColor = {};
				return false;
			}
		}

		bool TryGetPassDiffuseCpp(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			try
			{
				const auto* color = ::Ogre::GetPassDiffuse(pass);
				if (color == nullptr)
				{
					outColor = {};
					return false;
				}

				outColor = *color;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] GetPassDiffuse threw pass=%p what=%s", pass, ex.what());
				outColor = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] GetPassDiffuse threw pass=%p", pass);
				outColor = {};
				return false;
			}
		}

		bool TryGetPassSpecularCpp(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			try
			{
				const auto* color = ::Ogre::GetPassSpecular(pass);
				if (color == nullptr)
				{
					outColor = {};
					return false;
				}

				outColor = *color;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] GetPassSpecular threw pass=%p what=%s", pass, ex.what());
				outColor = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] GetPassSpecular threw pass=%p", pass);
				outColor = {};
				return false;
			}
		}

		bool TryGetPassSelfIlluminationCpp(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			try
			{
				const auto* color = ::Ogre::GetPassSelfIllumination(pass);
				if (color == nullptr)
				{
					outColor = {};
					return false;
				}

				outColor = *color;
				return true;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] GetPassSelfIllumination threw pass=%p what=%s", pass, ex.what());
				outColor = {};
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] GetPassSelfIllumination threw pass=%p", pass);
				outColor = {};
				return false;
			}
		}

		bool TrySetPassAmbientCpp(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			try
			{
				return ::Ogre::SetPassAmbient(pass, color);
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] SetPassAmbient threw pass=%p what=%s", pass, ex.what());
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] SetPassAmbient threw pass=%p", pass);
				return false;
			}
		}

		bool TrySetPassDiffuseCpp(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			try
			{
				return ::Ogre::SetPassDiffuse(pass, color);
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] SetPassDiffuse threw pass=%p what=%s", pass, ex.what());
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] SetPassDiffuse threw pass=%p", pass);
				return false;
			}
		}

		bool TrySetPassSpecularCpp(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			try
			{
				return ::Ogre::SetPassSpecular(pass, color);
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] SetPassSpecular threw pass=%p what=%s", pass, ex.what());
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] SetPassSpecular threw pass=%p", pass);
				return false;
			}
		}

		bool TrySetPassSelfIlluminationCpp(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			try
			{
				return ::Ogre::SetPassSelfIllumination(pass, color);
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug("[EXU::Material] SetPassSelfIllumination threw pass=%p what=%s", pass, ex.what());
				return false;
			}
			catch (...)
			{
				LogMaterialDebug("[EXU::Material] SetPassSelfIllumination threw pass=%p", pass);
				return false;
			}
		}

		bool TryCollectMaterialTintPassesCpp(
			const std::string& materialName,
			const std::string& resourceGroup,
			int passIndex,
			bool includeGlowScheme,
			std::vector<::Ogre::Pass*>& outPasses)
		{
			try
			{
				outPasses.clear();

				::Ogre::Material* material = nullptr;
				if (!TryResolveMaterialCpp(materialName, resourceGroup, material))
				{
					return false;
				}

				const unsigned short techniqueCount = ::Ogre::GetMaterialNumTechniques(material);
				for (unsigned short t = 0; t < techniqueCount; ++t)
				{
					auto* technique = ::Ogre::GetMaterialTechnique(material, t);
					if (technique == nullptr)
					{
						continue;
					}

					// Retro ("og-") scheme techniques intentionally keep their
					// stock flat look; tints target only the default and
					// enhanced ("en-") scheme techniques. The "glow" scheme is the
					// bloom-mask pass (ambient/diffuse/emissive = $glow over the
					// emissive map); ordinary tints skip it so they cannot make the
					// whole mesh bleed into the glow buffer. Emissive-only callers
					// may opt in when they intentionally control the bloom mask.
					const ::Ogre::String* schemeName = ::Ogre::GetTechniqueSchemeName(technique);
					if (schemeName != nullptr &&
						(schemeName->rfind("og-", 0) == 0 ||
							(!includeGlowScheme && *schemeName == "glow")))
					{
						continue;
					}

					const unsigned short passCount = ::Ogre::GetTechniqueNumPasses(technique);
					if (passIndex >= 0)
					{
						if (passIndex < static_cast<int>(passCount))
						{
							auto* pass = ::Ogre::GetTechniquePass(technique, static_cast<unsigned short>(passIndex));
							if (pass != nullptr)
							{
								outPasses.push_back(pass);
							}
						}
						continue;
					}

					for (unsigned short p = 0; p < passCount; ++p)
					{
						auto* pass = ::Ogre::GetTechniquePass(technique, p);
						if (pass != nullptr)
						{
							outPasses.push_back(pass);
						}
					}
				}

				return !outPasses.empty();
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug(
					"[EXU::Material] CollectMaterialTintPasses threw material=%s what=%s",
					materialName.c_str(),
					ex.what());
				return false;
			}
		}
	}

	namespace Detail
	{
		// Verbose per-call tracing, opt-in through EXU_DEBUG_LOG=1: entity and
		// material bindings log on every call and each line costs a log-file
		// open/close.
		void LogMaterialDebug(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);
			ExtraUtilities::Logging::LogDebugToV("exu_material_debug.log", fmt, args);
			va_end(args);
		}

		// Fault paths (SEH handlers) always reach exu.log.
		void LogMaterialFault(const char* fmt, ...)
		{
			va_list args;
			va_start(args, fmt);
			ExtraUtilities::Logging::LogFaultToV("exu_material_debug.log", fmt, args);
			va_end(args);
		}

		void CacheMaterialHandle(const std::string& materialName, const MaterialHandle& material)
		{
			if (materialName.empty() || material.isNull())
			{
				return;
			}

			g_cachedMaterials[materialName] = material;
		}

		bool TryGetCachedMaterial(const std::string& materialName, MaterialHandle& outMaterial)
		{
			const auto it = g_cachedMaterials.find(materialName);
			if (it == g_cachedMaterials.end() || it->second.isNull())
			{
				outMaterial = {};
				return false;
			}

			outMaterial = it->second;
			return true;
		}

		bool TryResolveMaterialCpp(
			const std::string& materialName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial,
			MaterialHandle* outShared)
		{
			try
			{
				MaterialHandle cachedMaterial{};
				if (TryGetCachedMaterial(materialName, cachedMaterial))
				{
					outMaterial = cachedMaterial.getPointer();
					if (outShared != nullptr)
					{
						*outShared = cachedMaterial;
					}
					return outMaterial != nullptr;
				}

				auto* manager = ::Ogre::GetMaterialManagerSingletonPtr();
				if (manager == nullptr)
				{
					LogMaterialDebug("[EXU::Material] ResolveMaterial missing manager material=%s group=%s", materialName.c_str(), resourceGroup.c_str());
					outMaterial = nullptr;
					if (outShared != nullptr)
					{
						*outShared = {};
					}
					return false;
				}

				auto material = ::Ogre::GetMaterialByName(manager, materialName, resourceGroup);
				CacheMaterialHandle(materialName, material);
				outMaterial = material.getPointer();
				if (outShared != nullptr)
				{
					*outShared = material;
				}
				if (outMaterial == nullptr)
				{
					LogMaterialDebug("[EXU::Material] ResolveMaterial miss material=%s group=%s", materialName.c_str(), resourceGroup.c_str());
				}
				return outMaterial != nullptr;
			}
			catch (const std::exception& ex)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterial threw material=%s group=%s what=%s",
					materialName.c_str(),
					resourceGroup.c_str(),
					ex.what());
				outMaterial = nullptr;
				if (outShared != nullptr)
				{
					*outShared = {};
				}
				return false;
			}
			catch (...)
			{
				LogMaterialDebug(
					"[EXU::Material] ResolveMaterial threw material=%s group=%s",
					materialName.c_str(),
					resourceGroup.c_str());
				outMaterial = nullptr;
				if (outShared != nullptr)
				{
					*outShared = {};
				}
				return false;
			}
		}

		bool TryResolveMaterialSeh(
			const std::string& materialName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial)
		{
			__try
			{
				return TryResolveMaterialCpp(materialName, resourceGroup, outMaterial);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] ResolveMaterial crashed material=%s group=%s code=0x%08X",
					materialName.c_str(),
					resourceGroup.c_str(),
					GetExceptionCode());
				outMaterial = nullptr;
				return false;
			}
		}

		bool TryResolveMaterial(
			const std::string& materialName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial)
		{
			return Seh::CatchCpp("TryResolveMaterial", [&] { return TryResolveMaterialSeh(materialName, resourceGroup, outMaterial); }, [&] { outMaterial = nullptr; return false; });
		}

		bool TryCloneMaterialSeh(
			const MaterialHandle& sourceMaterial,
			const std::string& cloneName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial)
		{
			__try
			{
				return TryCloneMaterialCpp(sourceMaterial, cloneName, resourceGroup, outMaterial);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] CloneMaterial crashed clone=%s group=%s code=0x%08X",
					cloneName.c_str(),
					resourceGroup.c_str(),
					GetExceptionCode());
				outMaterial = nullptr;
				return false;
			}
		}

		bool TryCloneMaterial(
			const MaterialHandle& sourceMaterial,
			const std::string& cloneName,
			const std::string& resourceGroup,
			::Ogre::Material*& outMaterial)
		{
			return Seh::CatchCpp("TryCloneMaterial", [&] { return TryCloneMaterialSeh(sourceMaterial, cloneName, resourceGroup, outMaterial); }, [&] { outMaterial = nullptr; return false; });
		}

		bool TryResolveMaterialPassSeh(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			MaterialPassHandle& outHandle)
		{
			__try
			{
				return TryResolveMaterialPassCpp(materialName, resourceGroup, techniqueIndex, passIndex, outHandle);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] ResolveMaterialPass crashed material=%s technique=%d pass=%d code=0x%08X",
					materialName.c_str(),
					techniqueIndex,
					passIndex,
					GetExceptionCode());
				outHandle = {};
				return false;
			}
		}

		bool TryResolveMaterialPass(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			MaterialPassHandle& outHandle)
		{
			return Seh::CatchCpp("TryResolveMaterialPass", [&] { return TryResolveMaterialPassSeh(materialName, resourceGroup, techniqueIndex, passIndex, outHandle); }, [&] { outHandle = {}; return false; });
		}

		bool TryCollectMaterialTintPassesSeh(
			const std::string& materialName,
			const std::string& resourceGroup,
			int passIndex,
			bool includeGlowScheme,
			std::vector<::Ogre::Pass*>& outPasses)
		{
			__try
			{
				return TryCollectMaterialTintPassesCpp(
					materialName,
					resourceGroup,
					passIndex,
					includeGlowScheme,
					outPasses);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] CollectMaterialTintPasses crashed material=%s pass=%d code=0x%08X",
					materialName.c_str(),
					passIndex,
					GetExceptionCode());
				return false;
			}
		}

		bool TryCollectMaterialTintPasses(
			const std::string& materialName,
			const std::string& resourceGroup,
			int passIndex,
			bool includeGlowScheme,
			std::vector<::Ogre::Pass*>& outPasses)
		{
			return Seh::CatchCpp("TryCollectMaterialTintPasses", [&] { return TryCollectMaterialTintPassesSeh(materialName, resourceGroup, passIndex, includeGlowScheme, outPasses); }, false);
		}

		bool TryResolveMaterialTextureUnitSeh(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			int textureUnitIndex,
			MaterialTextureUnitHandle& outHandle)
		{
			__try
			{
				return TryResolveMaterialTextureUnitCpp(
					materialName,
					resourceGroup,
					techniqueIndex,
					passIndex,
					textureUnitIndex,
					outHandle);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] ResolveMaterialTextureUnit crashed material=%s technique=%d pass=%d unit=%d code=0x%08X",
					materialName.c_str(),
					techniqueIndex,
					passIndex,
					textureUnitIndex,
					GetExceptionCode());
				outHandle = {};
				return false;
			}
		}

		bool TryResolveMaterialTextureUnit(
			const std::string& materialName,
			const std::string& resourceGroup,
			int techniqueIndex,
			int passIndex,
			int textureUnitIndex,
			MaterialTextureUnitHandle& outHandle)
		{
			return Seh::CatchCpp("TryResolveMaterialTextureUnit", [&] { return TryResolveMaterialTextureUnitSeh(materialName, resourceGroup, techniqueIndex, passIndex, textureUnitIndex, outHandle); }, [&] { outHandle = {}; return false; });
		}

		bool TrySetMaterialTextureNameSeh(::Ogre::TextureUnitState* textureUnit, const std::string& textureName)
		{
			__try
			{
				return ::Ogre::SetTextureUnitStateTextureName(textureUnit, textureName);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] SetTextureUnitStateTextureName crashed textureUnit=%p texture=%s code=0x%08X",
					textureUnit,
					textureName.c_str(),
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetMaterialTextureName(::Ogre::TextureUnitState* textureUnit, const std::string& textureName)
		{
			return Seh::CatchCpp("TrySetMaterialTextureName", [&] { return TrySetMaterialTextureNameSeh(textureUnit, textureName); }, false);
		}

		bool TrySetMaterialTextureScrollSeh(::Ogre::TextureUnitState* textureUnit, float u, float v)
		{
			__try
			{
				return ::Ogre::SetTextureUnitStateTextureScroll(textureUnit, u, v);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] SetTextureScroll crashed textureUnit=%p u=%g v=%g code=0x%08X",
					textureUnit,
					u,
					v,
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetMaterialTextureScroll(::Ogre::TextureUnitState* textureUnit, float u, float v)
		{
			return Seh::CatchCpp("TrySetMaterialTextureScroll", [&] { return TrySetMaterialTextureScrollSeh(textureUnit, u, v); }, false);
		}

		bool TryGetMaterialTechniqueCountCpp(const std::string& materialName, const std::string& resourceGroup, int& outCount)
		{
			try
			{
				outCount = 0;
				::Ogre::Material* material = nullptr;
				if (!TryResolveMaterialCpp(materialName, resourceGroup, material) || material == nullptr)
				{
					return false;
				}
				outCount = static_cast<int>(::Ogre::GetMaterialNumTechniques(material));
				return true;
			}
			catch (...)
			{
				outCount = 0;
				return false;
			}
		}

		bool TryGetMaterialTechniqueCountSeh(const std::string& materialName, const std::string& resourceGroup, int& outCount)
		{
			__try
			{
				return TryGetMaterialTechniqueCountCpp(materialName, resourceGroup, outCount);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] technique count crashed material=%s code=0x%08X", materialName.c_str(), GetExceptionCode());
				outCount = 0;
				return false;
			}
		}

		bool TryGetMaterialTechniqueCount(const std::string& materialName, const std::string& resourceGroup, int& outCount)
		{
			return Seh::CatchCpp("TryGetMaterialTechniqueCount", [&] { return TryGetMaterialTechniqueCountSeh(materialName, resourceGroup, outCount); }, [&] { outCount = 0; return false; });
		}

		bool TrySetMaterialTextureScaleSeh(::Ogre::TextureUnitState* textureUnit, float uScale, float vScale)
		{
			__try
			{
				return ::Ogre::SetTextureUnitStateTextureScale(textureUnit, uScale, vScale);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] SetTextureScale crashed textureUnit=%p u=%g v=%g code=0x%08X",
					textureUnit,
					uScale,
					vScale,
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetMaterialTextureScale(::Ogre::TextureUnitState* textureUnit, float uScale, float vScale)
		{
			return Seh::CatchCpp("TrySetMaterialTextureScale", [&] { return TrySetMaterialTextureScaleSeh(textureUnit, uScale, vScale); }, false);
		}

		bool TrySetMaterialTextureRotateSeh(::Ogre::TextureUnitState* textureUnit, float radians)
		{
			__try
			{
				return ::Ogre::SetTextureUnitStateTextureRotate(textureUnit, radians);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] SetTextureRotate crashed textureUnit=%p radians=%g code=0x%08X",
					textureUnit,
					radians,
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetMaterialTextureRotate(::Ogre::TextureUnitState* textureUnit, float radians)
		{
			return Seh::CatchCpp("TrySetMaterialTextureRotate", [&] { return TrySetMaterialTextureRotateSeh(textureUnit, radians); }, false);
		}

		bool TrySetMaterialTextureScrollAnimationSeh(::Ogre::TextureUnitState* textureUnit, float uSpeed, float vSpeed)
		{
			__try
			{
				return ::Ogre::SetTextureUnitStateScrollAnimation(textureUnit, uSpeed, vSpeed);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] SetScrollAnimation crashed textureUnit=%p uSpeed=%g vSpeed=%g code=0x%08X",
					textureUnit,
					uSpeed,
					vSpeed,
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetMaterialTextureScrollAnimation(::Ogre::TextureUnitState* textureUnit, float uSpeed, float vSpeed)
		{
			return Seh::CatchCpp("TrySetMaterialTextureScrollAnimation", [&] { return TrySetMaterialTextureScrollAnimationSeh(textureUnit, uSpeed, vSpeed); }, false);
		}

		bool TrySetMaterialTextureRotateAnimationSeh(::Ogre::TextureUnitState* textureUnit, float speed)
		{
			__try
			{
				return ::Ogre::SetTextureUnitStateRotateAnimation(textureUnit, speed);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault(
					"[EXU::Material] SetRotateAnimation crashed textureUnit=%p speed=%g code=0x%08X",
					textureUnit,
					speed,
					GetExceptionCode());
				return false;
			}
		}

		bool TrySetMaterialTextureRotateAnimation(::Ogre::TextureUnitState* textureUnit, float speed)
		{
			return Seh::CatchCpp("TrySetMaterialTextureRotateAnimation", [&] { return TrySetMaterialTextureRotateAnimationSeh(textureUnit, speed); }, false);
		}

		ExtraUtilities::Ogre::Color ToExuColor(const ::Ogre::ColourValue& color)
		{
			return { color.r, color.g, color.b, color.a };
		}

		::Ogre::ColourValue ToMaterialColor(const ExtraUtilities::Ogre::Color& color)
		{
			return { color.r, color.g, color.b, color.a };
		}

		bool TryGetPassAmbientSeh(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			__try
			{
				return TryGetPassAmbientCpp(pass, outColor);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetPassAmbient crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				outColor = {};
				return false;
			}
		}

		bool TryGetPassAmbient(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			return Seh::CatchCpp("TryGetPassAmbient", [&] { return TryGetPassAmbientSeh(pass, outColor); }, [&] { outColor = {}; return false; });
		}

		bool TryGetPassDiffuseSeh(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			__try
			{
				return TryGetPassDiffuseCpp(pass, outColor);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetPassDiffuse crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				outColor = {};
				return false;
			}
		}

		bool TryGetPassDiffuse(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			return Seh::CatchCpp("TryGetPassDiffuse", [&] { return TryGetPassDiffuseSeh(pass, outColor); }, [&] { outColor = {}; return false; });
		}

		bool TryGetPassSpecularSeh(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			__try
			{
				return TryGetPassSpecularCpp(pass, outColor);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetPassSpecular crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				outColor = {};
				return false;
			}
		}

		bool TryGetPassSpecular(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			return Seh::CatchCpp("TryGetPassSpecular", [&] { return TryGetPassSpecularSeh(pass, outColor); }, [&] { outColor = {}; return false; });
		}

		bool TryGetPassSelfIlluminationSeh(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			__try
			{
				return TryGetPassSelfIlluminationCpp(pass, outColor);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] GetPassSelfIllumination crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				outColor = {};
				return false;
			}
		}

		bool TryGetPassSelfIllumination(::Ogre::Pass* pass, ::Ogre::ColourValue& outColor)
		{
			return Seh::CatchCpp("TryGetPassSelfIllumination", [&] { return TryGetPassSelfIlluminationSeh(pass, outColor); }, [&] { outColor = {}; return false; });
		}

		bool TrySetPassAmbientSeh(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			__try
			{
				return TrySetPassAmbientCpp(pass, color);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] SetPassAmbient crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				return false;
			}
		}

		bool TrySetPassAmbient(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			return Seh::CatchCpp("TrySetPassAmbient", [&] { return TrySetPassAmbientSeh(pass, color); }, false);
		}

		bool TrySetPassDiffuseSeh(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			__try
			{
				return TrySetPassDiffuseCpp(pass, color);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] SetPassDiffuse crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				return false;
			}
		}

		bool TrySetPassDiffuse(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			return Seh::CatchCpp("TrySetPassDiffuse", [&] { return TrySetPassDiffuseSeh(pass, color); }, false);
		}

		bool TrySetPassSpecularSeh(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			__try
			{
				return TrySetPassSpecularCpp(pass, color);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] SetPassSpecular crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				return false;
			}
		}

		bool TrySetPassSpecular(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			return Seh::CatchCpp("TrySetPassSpecular", [&] { return TrySetPassSpecularSeh(pass, color); }, false);
		}

		bool TrySetPassSelfIlluminationSeh(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			__try
			{
				return TrySetPassSelfIlluminationCpp(pass, color);
			}
			__except (Seh::Filter(GetExceptionCode()))
			{
				LogMaterialFault("[EXU::Material] SetPassSelfIllumination crashed pass=%p code=0x%08X", pass, GetExceptionCode());
				return false;
			}
		}

		bool TrySetPassSelfIllumination(::Ogre::Pass* pass, const ::Ogre::ColourValue& color)
		{
			return Seh::CatchCpp("TrySetPassSelfIllumination", [&] { return TrySetPassSelfIlluminationSeh(pass, color); }, false);
		}
	}

	void ClearMaterialCache() noexcept
	{
		g_cachedMaterials.clear();
	}
}
